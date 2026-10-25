/*
 * adapter_cli.c — cli: 适配器（ADR-018 / T06）
 */
#include "adapter_cli.h"
#include "cJSON.h"

#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#define NL_ARG_MAX_LEN 1024

const char *nl_cli_status_str(NlCliStatus s)
{
    switch (s) {
    case NL_CLI_OK:        return "ok";
    case NL_CLI_E_BAD_PLAN:return "E_BAD_REQ";
    case NL_CLI_E_EXEC:    return "E_EXEC_FAIL";
    case NL_CLI_E_TIMEOUT: return "E_EXEC_FAIL";
    default:               return "E_EXEC_FAIL";
    }
}

static int push_arg(NlPlan *plan, int *n, const char *s)
{
    if (!s || strlen(s) >= NL_ARG_MAX_LEN) return -1;
    if (*n >= NL_CLI_MAX_ARGV) return -1;
    plan->argv[*n] = strdup(s);
    if (!plan->argv[*n]) return -1;
    (*n)++;
    plan->argv[*n] = NULL;
    return 0;
}

static int push_array(NlPlan *plan, int *n, const cJSON *arr)
{
    /* vendor cJSON 子集：对 object 的 GetArraySize 恒 0；array 可用 */
    int size = cJSON_GetArraySize(arr);
    for (int i = 0; i < size; i++) {
        const char *v = cJSON_GetStringValue(cJSON_GetArrayItem(arr, i));
        if (!v) return -1;
        if (push_arg(plan, n, v) != 0) return -1;
    }
    return 0;
}

int nl_cli_build_plan(const NlCapability *cap, const char *args_json, NlPlan *plan)
{
    if (!cap || !plan) return -1;
    memset(plan, 0, sizeof(*plan));

    if (strncmp(cap->id, "cli:", 4) != 0) return -1;
    if (!cap->path || cap->path[0] == '\0' || !cap->trusted) return -1;

    char *exe = strdup(cap->path);
    if (!exe) return -1;
    plan->exe_path = exe;

    int n = 0;
    if (push_arg(plan, &n, cap->id + 4) != 0) { nl_cli_plan_free(plan); return -1; }

    if (args_json && args_json[0] != '\0') {
        cJSON *root = cJSON_Parse(args_json);
        if (root) {
            for (const cJSON *it = root->child; it != NULL; it = it->next) {
                if (!it->string) continue;
                if (strcmp(it->string, "paths") == 0 || strcmp(it->string, "args") == 0) {
                    if (cJSON_IsArray(it) && push_array(plan, &n, it) != 0) {
                        cJSON_Delete(root);
                        nl_cli_plan_free(plan);
                        return -1;
                    }
                }
            }
            cJSON_Delete(root);
        }
    }
    return 0;
}

void nl_cli_plan_free(NlPlan *plan)
{
    if (!plan) return;
    for (int i = 0; i <= NL_CLI_MAX_ARGV; i++) {
        free(plan->argv[i]);
        plan->argv[i] = NULL;
    }
    free(plan->exe_path);
    plan->exe_path = NULL;
}

static long now_ms(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (long)ts.tv_sec * 1000L + ts.tv_nsec / 1000000L;
}

int nl_cli_execute(const NlPlan *plan, int timeout_ms, NlCliResult *res)
{
    if (!plan || !res || !plan->exe_path || !plan->argv[0]) return -1;
    memset(res, 0, sizeof(*res));

    int pfd[2];
    if (pipe(pfd) != 0) { res->status = NL_CLI_E_EXEC; return -1; }

    pid_t pid = fork();
    if (pid < 0) {
        close(pfd[0]);
        close(pfd[1]);
        res->status = NL_CLI_E_EXEC;
        return -1;
    }
    if (pid == 0) {
        close(pfd[0]);
        (void)dup2(pfd[1], STDOUT_FILENO);
        (void)dup2(pfd[1], STDERR_FILENO);
        close(pfd[1]);
        execv(plan->exe_path, plan->argv);
        _exit(127);
    }

    close(pfd[1]);
    long deadline = now_ms() + (timeout_ms > 0 ? timeout_ms : 5000);
    size_t used = 0;
    int timed_out = 0;

    for (;;) {
        long remain = deadline - now_ms();
        if (remain <= 0) { timed_out = 1; break; }
        struct pollfd p = { .fd = pfd[0], .events = POLLIN, .revents = 0 };
        int pr = poll(&p, 1, (int)remain);
        if (pr < 0) {
            if (errno == EINTR) continue;
            break;
        }
        if (pr == 0) { timed_out = 1; break; }
        if (p.revents & (POLLIN | POLLHUP)) {
            char buf[512];
            ssize_t r = read(pfd[0], buf, sizeof(buf));
            if (r <= 0) break;
            size_t take = (size_t)r;
            if (used + take > NL_CLI_MAX_OUT - 1) take = NL_CLI_MAX_OUT - 1 - used;
            memcpy(res->output + used, buf, take);
            used += take;
        }
    }
    close(pfd[0]);

    int status = 0;
    if (timed_out) {
        kill(pid, SIGKILL);
        waitpid(pid, &status, 0);
        res->status = NL_CLI_E_TIMEOUT;
    } else {
        waitpid(pid, &status, 0);
        if (WIFEXITED(status)) {
            res->exit_code = WEXITSTATUS(status);
            res->status = NL_CLI_OK;
        } else {
            res->status = NL_CLI_E_EXEC;
            res->exit_code = 128 + (WIFSIGNALED(status) ? WTERMSIG(status) : 0);
        }
    }
    res->output[used] = '\0';
    return 0;
}
