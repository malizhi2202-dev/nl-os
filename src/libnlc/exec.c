#include "exec.h"
#include "path_safety.h"   /* nl_domain_check：只读形态白名单里校验 dig 的参数 */

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

/* ============================================================
 * 只读形态白名单（ADR-024）
 *
 * 有些二进制既能只读也能改系统：`ip`（link set ... down）、`dig`（-f 读文件）。
 * 权限表是按"二进制名"分级，无法表达"只允许只读形态"，于是这两者作为
 * 网络诊断能力登记为 L1 时，必须再补一道**按命令形态**的闸门：
 * 生成的命令只有在登记形态里才允许执行，其它形态一律拒绝（fail-closed）。
 *
 * 注意：这道闸门只管"NL 生成的命令"（nl_exec_execute 这条路径）。
 * 用户在 shell 里手打 `ip link set ...` 属于原始命令快路径，不归这里管。
 * ============================================================ */
typedef enum { FORM_EXACT = 0, FORM_DOMAIN_ARG = 1 } NlFormKind;

static const struct {
    const char *argv0;
    const char *form;
    NlFormKind  kind;
} RESTRICTED_FORMS[] = {
    { "ip",        "ip -br addr", FORM_EXACT      },
    { "dig",       "dig +short",  FORM_DOMAIN_ARG },
    /* systemctl 在权限表里是 L2（能 stop/restart/enable）。只放行服务列表这一个
     * 只读形态，别的形态一律拒绝 —— 这样"服务列表"才敢按 L1 无人值守执行。 */
    { "systemctl", "systemctl list-units --type=service --no-pager", FORM_EXACT },
    { NULL,  NULL,          FORM_EXACT      }
};

int nl_exec_form_allowed(const char *cmd)
{
    if (!cmd || !*cmd) return 0;

    char argv0[64];
    size_t i = 0;
    while (cmd[i] && cmd[i] != ' ' && cmd[i] != '\t' && i < sizeof(argv0) - 1) {
        argv0[i] = cmd[i];
        i++;
    }
    argv0[i] = '\0';

    for (int k = 0; RESTRICTED_FORMS[k].argv0; k++) {
        if (strcmp(argv0, RESTRICTED_FORMS[k].argv0) != 0) continue;

        const char *form = RESTRICTED_FORMS[k].form;
        size_t flen = strlen(form);

        /* 形态匹配刻意做成两种、不做宽松前缀：
         *   EXACT      —— 整条必须一模一样。`ip` 必须走这条：`ip -br addr add …`
         *                 这种"前缀对、后面改配置"的命令只有全串比对才拦得住。
         *   DOMAIN_ARG —— 固定前缀 + 一个参数，参数必须过 nl_domain_check：
         *                 `dig +short -f /etc/passwd` 这类"选项当域名"的注入由域名校验拦。 */
        if (RESTRICTED_FORMS[k].kind == FORM_EXACT)
            return strcmp(cmd, form) == 0 ? 1 : 0;

        if (strncmp(cmd, form, flen) != 0 || cmd[flen] != ' ') return 0;
        const char *arg = cmd + flen + 1;
        if (*arg == '\0') return 0;
        for (const char *p = arg; *p; p++)
            if (*p == ' ' || *p == '\t') return 0;      /* 只允许一个参数 */
        return nl_domain_check(arg) == 0 ? 1 : 0;
    }
    return 1;   /* 未受控的二进制：沿用原有的权限分级 */
}

int nl_exec_execute(const char *cmd, int timeout_ms)
{
    if (!cmd || !*cmd) return -1;
    if (nl_exec_has_metachars(cmd)) {
        fprintf(stderr, "❌ 拒绝执行：命令含 shell 元字符\n");
        return -1;
    }
    if (!nl_exec_form_allowed(cmd)) {
        fprintf(stderr, "❌ 拒绝执行：`%s` 不是已登记的只读形态\n", cmd);
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
