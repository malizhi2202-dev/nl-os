#include "exec.h"

#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

/* 生成命令里不允许出现的 shell 元字符：出现即拒绝，绝不交给 shell */
static const char *const BANNED = "|;&$`><()[]{}*?!\\'\"";

int nl_exec_has_metachars(const char *cmd)
{
    if (!cmd) return 1;
    return strpbrk(cmd, BANNED) != NULL ? 1 : 0;
}

int nl_exec_execute(const char *cmd, int timeout_ms)
{
    if (!cmd || !*cmd) return -1;
    if (nl_exec_has_metachars(cmd)) {
        fprintf(stderr, "❌ 拒绝执行：命令含 shell 元字符\n");
        return -1;
    }

    char buf[2048];
    snprintf(buf, sizeof(buf), "%s", cmd);
    char *argv[64];
    int n = 0;
    for (char *tok = strtok(buf, " \t"); tok && n < 63; tok = strtok(NULL, " \t"))
        argv[n++] = tok;
    argv[n] = NULL;
    if (n == 0) return -1;

    pid_t pid = fork();
    if (pid < 0) return -1;
    if (pid == 0) { execvp(argv[0], argv); _exit(127); }

    int waited = 0, status = 0;
    for (;;) {
        pid_t w = waitpid(pid, &status, WNOHANG);
        if (w == pid) break;
        if (w < 0) return -1;
        if (waited >= timeout_ms) { kill(pid, SIGKILL); (void)waitpid(pid, &status, 0); return -1; }
        usleep(10 * 1000);
        waited += 10;
    }
    if (!WIFEXITED(status)) return -1;
    return WEXITSTATUS(status);
}

int nl_exec_collect_paths(const char *cmd, char paths[][NL_EXEC_PATH_MAX], int max)
{
    if (!cmd || !max) return 0;
    char buf[2048];
    snprintf(buf, sizeof(buf), "%s", cmd);
    int n = 0, idx = 0;
    for (char *tok = strtok(buf, " \t"); tok && n < max; tok = strtok(NULL, " \t"), idx++) {
        if (idx == 0 || tok[0] == '-') continue;
        snprintf(paths[n], NL_EXEC_PATH_MAX, "%.500s", tok);
        n++;
    }
    return n;
}
