/*
 * shell_builtin.c — shell 内建命令（cd）的"父进程执行"通道
 *
 * 为什么需要：`cd` 是 shell 内建，不是 PATH 里的二进制。nlsh 用 fork/execvp 执行，
 * 子进程 chdir 对**父 shell 没有任何影响**——所以 `进入 /tmp 目录` 在 nlsh 里能"执行成功"
 * 却不改变你所在的目录，这是骗人的。
 *
 * 做法（ADR-020）：nlsh 仍然负责 解析 → 确认 → 审计，但**不自己执行**；它把结果写成一行
 * 指令 `__NL_CD__ <dir>` 追加到 $NL_SHELL_DIRECTIVE 指向的文件，由 shell 钩子在父 shell 里
 * 执行 `cd -- "$dir"`。钩子拿到的是**路径字符串**而不是可执行命令，全程不 eval。
 */

#include "shell_builtin.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#define NL_CD_PREFIX "__NL_CD__ "

int nl_shell_builtin_is(const char *argv0)
{
    /* 只放 cd。export/alias/source 都要父 shell 且有各自的信任问题，暂不做。 */
    return argv0 && strcmp(argv0, "cd") == 0;
}

/* 把参数里的 ~ 展开成 $HOME；返回 0 表示有结果 */
static int expand_tilde(const char *in, char *out, size_t outsz)
{
    if (in[0] != '~') {
        if (strlen(in) >= outsz) return -1;
        snprintf(out, outsz, "%s", in);
        return 0;
    }
    const char *home = getenv("HOME");
    if (!home || !*home) return -1;
    if (in[1] == '\0')
        return snprintf(out, outsz, "%s", home) < (int)outsz ? 0 : -1;
    if (in[1] != '/') return -1;              /* ~user 不支持 */
    return snprintf(out, outsz, "%s%s", home, in + 1) < (int)outsz ? 0 : -1;
}

int nl_shell_builtin_emit(const char *command, char *err, size_t errsz)
{
    if (err && errsz) err[0] = '\0';
    if (!command) return -1;

    const char *p = command;
    while (*p == ' ') p++;
    if (strncmp(p, "cd", 2) != 0) return -1;
    p += 2;
    if (*p != ' ' && *p != '\0') return -1;   /* cdxxx 不是 cd */
    while (*p == ' ') p++;

    /* 参数：允许为空（cd = 回家）、含空格时必须是单引号形式（模板产出不带引号，稳妥起见只取到行尾并去尾空格） */
    char target[1024];
    size_t n = strlen(p);
    while (n > 0 && (p[n - 1] == ' ' || p[n - 1] == '\n')) n--;
    if (n == 0) {
        const char *home = getenv("HOME");
        if (!home || !*home) {
            if (err && errsz) snprintf(err, errsz, "无法确定主目录（HOME 未设置）");
            return -1;
        }
        snprintf(target, sizeof(target), "%s", home);
    } else {
        if (n >= sizeof(target)) {
            if (err && errsz) snprintf(err, errsz, "目标路径过长");
            return -1;
        }
        memcpy(target, p, n);
        target[n] = '\0';
        char expanded[1024];
        if (expand_tilde(target, expanded, sizeof(expanded)) != 0) {
            if (err && errsz) snprintf(err, errsz, "不支持的目标路径：%s", target);
            return -1;
        }
        snprintf(target, sizeof(target), "%s", expanded);
    }

    /* 必须真的存在且是目录：宁可报错，也不要让 shell 静默失败 */
    struct stat st;
    if (stat(target, &st) != 0 || !S_ISDIR(st.st_mode)) {
        if (err && errsz) snprintf(err, errsz, "不是目录或不存在：%s", target);
        return -1;
    }

    const char *sink = getenv("NL_SHELL_DIRECTIVE");
    if (!sink || !*sink) {
        if (err && errsz)
            snprintf(err, errsz,
                     "cd 需要在终端钩子下运行（独立进程改不了你的 shell 目录）：目前目标 %s",
                     target);
        return -1;
    }

    FILE *fp = fopen(sink, "a");
    if (!fp) {
        if (err && errsz) snprintf(err, errsz, "无法写入指令通道：%s", sink);
        return -1;
    }
    int ok = fprintf(fp, NL_CD_PREFIX "%s\n", target) > 0;
    if (fclose(fp) != 0) ok = 0;
    if (!ok && err && errsz) snprintf(err, errsz, "写入指令通道失败：%s", sink);
    return ok ? 0 : -1;
}
