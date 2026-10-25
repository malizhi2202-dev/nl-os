/*
 * 共享执行器单元测试（nlsh-exec / AC-8）
 * 覆盖：元字符拒绝 / 正常命令退出码 / 失败退出码 / 超时回收 / 路径提取
 * 不在测试里跑 shell：exec 走 execvp，元字符序列必须被拒绝而不是执行。
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static int tests_run = 0, tests_passed = 0, tests_failed = 0;
#define TEST(n) do { tests_run++; printf("  TEST %s ... ", n); fflush(stdout); } while (0)
#define PASS()  do { tests_passed++; printf("PASS\n"); } while (0)
#define FAIL(m) do { tests_failed++; printf("FAIL: %s\n", m); return; } while (0)
#define ASSERT(c,m) do { if (!(c)) { FAIL(m); } } while (0)

#include "exec.h"

static void test_metachars(void)
{
    TEST("shell metachars rejected before exec");
    const char *bad[] = {
        "rm -rf a; rm -rf b",
        "echo hi | wc -l",
        "cat x && rm y",
        "echo $(whoami)",
        "cat `id`",
        "ls > out.txt",
        "echo \"quoted\"",
        "rm *",
        "cat /etc/passwd &",
    };
    for (size_t i = 0; i < sizeof(bad) / sizeof(bad[0]); i++) {
        ASSERT(nl_exec_has_metachars(bad[i]) == 1, bad[i]);
        ASSERT(nl_exec_execute(bad[i], 2000) == -1, "must not execute");
    }
    PASS();
}

static void test_clean_commands_pass_check(void)
{
    TEST("plain commands are not flagged");
    const char *ok[] = { "ls -la", "mkdir -p uat", "echo hello", "cat README.md", NULL };
    for (int i = 0; ok[i]; i++)
        ASSERT(nl_exec_has_metachars(ok[i]) == 0, ok[i]);
    PASS();
}

static void test_execute_ok(void)
{
    TEST("clean command executes with exit code 0");
    ASSERT(nl_exec_execute("true", 2000) == 0, "true");
    PASS();
}

static void test_execute_failure_code(void)
{
    TEST("failing command returns its exit code");
    int rc = nl_exec_execute("false", 2000);
    ASSERT(rc == 1, "false → 1");
    PASS();
}

static void test_execute_timeout(void)
{
    TEST("timeout kills the child and reports failure");
    ASSERT(nl_exec_execute("sleep 5", 100) == -1, "must time out");
    PASS();
}

static void test_collect_paths(void)
{
    TEST("paths extracted from command, flags skipped");
    char ps[8][NL_EXEC_PATH_MAX];
    const char *arr[8];
    int n = nl_exec_collect_paths("mkdir -p uat-x", ps, 8);
    ASSERT(n == 1, "one path");
    ASSERT(strcmp(ps[0], "uat-x") == 0, "path value");
    for (int k = 0; k < n; k++) arr[k] = ps[k];
    ASSERT(arr[0] != NULL, "array usable");

    n = nl_exec_collect_paths("rm -rf a b", ps, 8);
    ASSERT(n == 2, "two paths");
    ASSERT(strcmp(ps[0], "a") == 0 && strcmp(ps[1], "b") == 0, "both paths");
    PASS();
}

int main(void)
{
    printf("exec tests\n");
    test_metachars();
    test_clean_commands_pass_check();
    test_execute_ok();
    test_execute_failure_code();
    test_execute_timeout();
    test_collect_paths();
    printf("\n%d/%d passed, %d failed\n", tests_passed, tests_run, tests_failed);
    return tests_failed == 0 ? 0 : 1;
}
