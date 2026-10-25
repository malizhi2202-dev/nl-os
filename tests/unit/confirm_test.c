/*
 * 统一确认门单元测试（ADR-019）
 * 覆盖：触发条件矩阵 / 回答词表（含 nl-run 曾不认的 y）/ 绕过开关 / EOF 取消
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

#include "confirm.h"

static void test_required(void)
{
    TEST("trigger matrix: write or L2+ asks, read does not");
    ASSERT(nl_confirm_required(0, 0) == 0, "read L0 no ask");
    ASSERT(nl_confirm_required(0, 1) == 0, "read L1 no ask");
    ASSERT(nl_confirm_required(1, 0) == 1, "needs_confirm asks");
    ASSERT(nl_confirm_required(0, 2) == 1, "L2 asks");
    ASSERT(nl_confirm_required(1, 3) == 1, "L3 asks");
    PASS();
}

static void test_answer_vocabulary(void)
{
    TEST("answer vocabulary identical everywhere (y/yes/YES/是)");
    const char *yes[] = { "y", "y\n", "Y", "yes", "yes\n", "YES", "是", "是\n", "  y\n" };
    for (size_t i = 0; i < sizeof(yes) / sizeof(yes[0]); i++)
        ASSERT(nl_confirm_yes(yes[i]) == 1, yes[i]);

    const char *no[] = { "", "\n", "n", "no", "N\n", "取消", "确认", NULL, " ", "1" };
    for (size_t i = 0; i < sizeof(no) / sizeof(no[0]); i++)
        ASSERT(nl_confirm_yes(no[i]) == 0, no[i] ? no[i] : "(null)");
    PASS();
}

static void test_bypass(void)
{
    TEST("NL_ASSUME_YES is the single bypass switch");
    unsetenv("NL_ASSUME_YES");
    ASSERT(nl_confirm_assume_yes() == 0, "default off");
    setenv("NL_ASSUME_YES", "1", 1);
    ASSERT(nl_confirm_assume_yes() == 1, "1 on");
    setenv("NL_ASSUME_YES", "y", 1);
    ASSERT(nl_confirm_assume_yes() == 1, "y on");
    setenv("NL_ASSUME_YES", "0", 1);
    ASSERT(nl_confirm_assume_yes() == 0, "0 off");
    unsetenv("NL_ASSUME_YES");
    PASS();
}

static void test_ask_eof_is_cancel(void)
{
    TEST("EOF on stdin = cancel (fail-closed), not execute");
    unsetenv("NL_ASSUME_YES");
    FILE *saved = stdin;
    stdin = freopen("/dev/null", "r", stdin);
    ASSERT(stdin != NULL, "redirect stdin");
    int r = nl_confirm_ask("rm -rf /tmp/should-not-run", 3);
    ASSERT(r == 0, "EOF must cancel");
    (void)saved;
    PASS();
}

int main(void)
{
    printf("confirm tests\n");
    test_required();
    test_answer_vocabulary();
    test_bypass();
    test_ask_eof_is_cancel();
    printf("\n%d/%d passed, %d failed\n", tests_passed, tests_run, tests_failed);
    return tests_failed == 0 ? 0 : 1;
}
