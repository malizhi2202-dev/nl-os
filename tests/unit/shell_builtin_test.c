/*
 * shell_builtin_test.c — shell 内建（cd）指令通道
 *
 * 这条通道是 `cd` 唯一能改变你所在目录的方式（nlsh 在子进程执行），
 * 所以它必须：只在目标真是目录时成功、只写路径不写命令、没有通道时诚实报错。
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static int failures = 0;
#define ASSERT(cond, msg) do { \
    if (!(cond)) { fprintf(stderr, "FAIL: %s (%s:%d)\n", msg, __FILE__, __LINE__); failures++; } \
} while (0)

#include "../../src/libnlc/shell_builtin.c"

static char g_tmp[512];
static char g_directive[640];

static void reset_directive(void)
{
    FILE *f = fopen(g_directive, "w");
    if (f) fclose(f);
    setenv("NL_SHELL_DIRECTIVE", g_directive, 1);
}

static long directive_size(void)
{
    FILE *f = fopen(g_directive, "r");
    if (!f) return -1;
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fclose(f);
    return n;
}

static void test_is_builtin(void)
{
    ASSERT(nl_shell_builtin_is("cd") == 1, "cd 是内建");
    ASSERT(nl_shell_builtin_is("ls") == 0, "ls 不是内建");
    ASSERT(nl_shell_builtin_is("") == 0, "空串不是内建");
    ASSERT(nl_shell_builtin_is(NULL) == 0, "NULL 不是内建");
}

static void test_emit_cd(void)
{
    reset_directive();
    char err[512];
    ASSERT(nl_shell_builtin_emit("cd /tmp", err, sizeof(err)) == 0, "cd /tmp 成功");
    ASSERT(directive_size() > 0, "写了指令");
    FILE *f = fopen(g_directive, "r");
    char line[512] = "";
    if (f) { if (!fgets(line, sizeof(line), f)) line[0] = 0; fclose(f); }
    ASSERT(strcmp(line, "__NL_CD__ /tmp\n") == 0, "指令格式是 __NL_CD__ <路径>");

    /* 只写路径：指令里不能出现其他命令 */
    reset_directive();
    ASSERT(nl_shell_builtin_emit("cd /tmp; rm -rf /", err, sizeof(err)) != 0,
           "带分号的目标按目录判断 → 不存在，拒绝");
}

static void test_emit_dotdot(void)
{
    reset_directive();
    char err[512];
    ASSERT(nl_shell_builtin_emit("cd ..", err, sizeof(err)) == 0, "cd .. 成功");
}

static void test_not_a_dir(void)
{
    char err[512];
    reset_directive();
    ASSERT(nl_shell_builtin_emit("cd /no_such_dir_zzz", err, sizeof(err)) != 0, "不存在 → 拒绝");
    ASSERT(strstr(err, "不是目录或不存在") != NULL, "原因说清楚");
    ASSERT(directive_size() == 0, "拒绝时不写指令");

    /* 普通文件也不是目录 */
    char file[600];
    snprintf(file, sizeof(file), "%s/file", g_tmp);
    FILE *f = fopen(file, "w");
    if (f) fclose(f);
    char cmd[700];
    snprintf(cmd, sizeof(cmd), "cd %s", file);
    ASSERT(nl_shell_builtin_emit(cmd, err, sizeof(err)) != 0, "普通文件 → 拒绝");
}

static void test_no_channel(void)
{
    unsetenv("NL_SHELL_DIRECTIVE");
    char err[512];
    ASSERT(nl_shell_builtin_emit("cd /tmp", err, sizeof(err)) != 0,
           "没有指令通道 → 不假装成功");
    ASSERT(strstr(err, "需要在终端钩子下运行") != NULL, "解释为什么");
}

static void test_home_expansion(void)
{
    reset_directive();
    setenv("HOME", "/tmp", 1);
    char err[512];
    ASSERT(nl_shell_builtin_emit("cd ~", err, sizeof(err)) == 0, "cd ~ 展开到 HOME");
    FILE *f = fopen(g_directive, "r");
    char line[512] = "";
    if (f) { if (!fgets(line, sizeof(line), f)) line[0] = 0; fclose(f); }
    ASSERT(strcmp(line, "__NL_CD__ /tmp\n") == 0, "写入展开后的绝对路径");
}

int main(void)
{
    snprintf(g_tmp, sizeof(g_tmp), "/tmp/nl-cd-test-XXXXXX");
    if (!mkdtemp(g_tmp)) { perror("mkdtemp"); return 1; }
    snprintf(g_directive, sizeof(g_directive), "%s/directive", g_tmp);

    test_is_builtin();
    test_emit_cd();
    test_emit_dotdot();
    test_not_a_dir();
    test_no_channel();
    test_home_expansion();

    char cmd[700];
    snprintf(cmd, sizeof(cmd), "rm -rf %s", g_tmp);
    if (system(cmd) != 0) { /* 清理失败不影响结论 */ }

    if (failures == 0) printf("shell_builtin: 全部通过\n");
    else fprintf(stderr, "shell_builtin: %d 项失败\n", failures);
    return failures == 0 ? 0 : 1;
}
