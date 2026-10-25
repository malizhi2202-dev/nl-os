/*
 * .tools 固化与校验（T14）
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int tests_run = 0, tests_passed = 0, tests_failed = 0;
#define TEST(n) do { tests_run++; printf("  TEST %s ... ", n); fflush(stdout); } while (0)
#define PASS()  do { tests_passed++; printf("PASS\n"); } while (0)
#define FAIL(m) do { tests_failed++; printf("FAIL: %s\n", m); return; } while (0)
#define ASSERT(c,m) do { if (!(c)) { FAIL(m); } } while (0)

#include "freeze.h"

static char *read_all(const char *p)
{
    FILE *f = fopen(p, "r");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END); long n = ftell(f); rewind(f);
    char *b = malloc((size_t)n + 1);
    size_t got = fread(b, 1, (size_t)n, f);
    b[got] = '\0';
    fclose(f);
    return b;
}

static void test_write_and_verify(void)
{
    TEST("write then verify OK");
    char *body[] = { "mkdir build", "cd build", "touch README.md" };
    ASSERT(nl_freeze_write("/tmp/nlfreeze-a.tools", body, 3, NULL) == 0, "write failed");
    ASSERT(nl_freeze_verify("/tmp/nlfreeze-a.tools") == NL_FREEZE_OK, "verify should pass");
    ASSERT(nl_freeze_is_frozen("/tmp/nlfreeze-a.tools") == 1, "must be frozen");
    PASS();
}

static void test_idempotent(void)
{
    TEST("freeze is idempotent");
    char *body[] = { "mkdir x", "ls x" };
    ASSERT(nl_freeze_write("/tmp/nlfreeze-b.tools", body, 2, NULL) == 0, "write1");
    char *first = read_all("/tmp/nlfreeze-b.tools");
    ASSERT(nl_freeze_write("/tmp/nlfreeze-b.tools", body, 2, NULL) == 0, "write2");
    char *second = read_all("/tmp/nlfreeze-b.tools");
    ASSERT(first && second && strcmp(first, second) == 0, "content must be identical");
    free(first); free(second);
    PASS();
}

static void test_tamper_body(void)
{
    TEST("tampered body rejected");
    char *body[] = { "mkdir safe", "rm -rf /tmp/target" };
    ASSERT(nl_freeze_write("/tmp/nlfreeze-c.tools", body, 2, NULL) == 0, "write");
    FILE *f = fopen("/tmp/nlfreeze-c.tools", "r");
    ASSERT(f != NULL, "open");
    char buf[8192]; size_t n = fread(buf, 1, sizeof(buf) - 1, f); buf[n] = '\0'; fclose(f);
    char *pos = strstr(buf, "rm -rf /tmp/target");
    ASSERT(pos != NULL, "target line");
    memcpy(pos, "rm -rf /etc/passwd", 18);
    f = fopen("/tmp/nlfreeze-c.tools", "w");
    ASSERT(f != NULL, "rewrite");
    fputs(buf, f); fclose(f);
    ASSERT(nl_freeze_verify("/tmp/nlfreeze-c.tools") == NL_FREEZE_TAMPERED, "must detect tamper");
    PASS();
}

static void test_tamper_header(void)
{
    TEST("tampered checksum header rejected");
    char *body[] = { "echo hi" };
    ASSERT(nl_freeze_write("/tmp/nlfreeze-d.tools", body, 1, NULL) == 0, "write");
    char *buf = read_all("/tmp/nlfreeze-d.tools");
    ASSERT(buf != NULL, "read");
    char *p = strstr(buf, "# @checksum: sha256:");
    ASSERT(p != NULL, "header");
    p[20] = (p[20] == 'a') ? 'b' : 'a';   /* 改一个 hex 字符 */
    FILE *f = fopen("/tmp/nlfreeze-d.tools", "w");
    fputs(buf, f); fclose(f); free(buf);
    ASSERT(nl_freeze_verify("/tmp/nlfreeze-d.tools") == NL_FREEZE_TAMPERED, "must detect");
    PASS();
}

static void test_plain_file(void)
{
    TEST("plain file reported as PLAIN");
    FILE *f = fopen("/tmp/nlfreeze-e.tools", "w");
    ASSERT(f != NULL, "open");
    fputs("mkdir x\n", f);
    fclose(f);
    ASSERT(nl_freeze_verify("/tmp/nlfreeze-e.tools") == NL_FREEZE_PLAIN, "must be plain");
    ASSERT(nl_freeze_is_frozen("/tmp/nlfreeze-e.tools") == 0, "not frozen");
    PASS();
}

static void test_missing_file(void)
{
    TEST("missing file → IO error");
    ASSERT(nl_freeze_verify("/tmp/nlfreeze-does-not-exist.tools") == NL_FREEZE_IO_ERR, "io err");
    PASS();
}

/* ADR-019 P-3：`# @perm: allow-l2` 必须随固化保留，且被校验覆盖。
 * 曾经的缺陷：freeze 只写 format+checksum+正文，策略头在 --freeze 后静默消失。 */
static void test_policy_header_preserved(void)
{
    TEST("policy header survives freeze and is hashed");
    char *body[] = { "mkdir x" };
    ASSERT(nl_freeze_write("/tmp/nlfreeze-f.tools", body, 1, "# @perm: allow-l2") == 0, "write");
    char *buf = read_all("/tmp/nlfreeze-f.tools");
    ASSERT(buf != NULL, "read");
    ASSERT(strstr(buf, "# @perm: allow-l2\n") != NULL, "policy line must be kept");
    ASSERT(nl_freeze_verify("/tmp/nlfreeze-f.tools") == NL_FREEZE_OK, "verify with policy");
    free(buf);

    /* 改策略头 = 篡改（否则可以从外部"加回"允许绕过） */
    buf = read_all("/tmp/nlfreeze-f.tools");
    char *p = strstr(buf, "allow-l2");
    ASSERT(p != NULL, "policy token");
    memcpy(p, "allow-l9", 8);
    FILE *f = fopen("/tmp/nlfreeze-f.tools", "w");
    ASSERT(f != NULL, "rewrite");
    fputs(buf, f); fclose(f); free(buf);
    ASSERT(nl_freeze_verify("/tmp/nlfreeze-f.tools") == NL_FREEZE_TAMPERED, "policy tamper");

    /* 只接受注释行 */
    ASSERT(nl_freeze_write("/tmp/nlfreeze-g.tools", body, 1, "rm -rf /") != 0, "reject non-comment");
    PASS();
}

int main(void)
{
    printf("freeze tests\n");
    test_write_and_verify();
    test_idempotent();
    test_tamper_body();
    test_tamper_header();
    test_plain_file();
    test_missing_file();
    test_policy_header_preserved();
    printf("\n%d/%d passed, %d failed\n", tests_passed, tests_run, tests_failed);
    return tests_failed == 0 ? 0 : 1;
}
