/*
 * ticket 单元测试（T05）
 *
 * 覆盖：SHA-256 NIST 向量 / 正常 / 伪造 / 过期 / 参数不符 / 重放 / 表满淘汰 / 限速
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int tests_run = 0, tests_passed = 0, tests_failed = 0;
#define TEST(n) do { tests_run++; printf("  TEST %s ... ", n); fflush(stdout); } while (0)
#define PASS()  do { tests_passed++; printf("PASS\n"); } while (0)
#define FAIL(m) do { tests_failed++; printf("FAIL: %s\n", m); return; } while (0)
#define ASSERT(c,m) do { if (!(c)) { FAIL(m); } } while (0)

#include "../../src/nlbroker/ticket.c"

static void test_sha256_vectors(void)
{
    TEST("SHA-256 NIST vectors");
    char hex[65];
    nl_sha256_hex("", 0, hex);
    ASSERT(strcmp(hex, "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855") == 0,
           "empty vector mismatch");
    nl_sha256_hex("abc", 3, hex);
    ASSERT(strcmp(hex, "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad") == 0,
           "abc vector mismatch");
    PASS();
}

static void test_normal_check(void)
{
    TEST("issue then check -> OK");
    nl_ticket_reset();
    char tok[NL_TICKET_TOKEN_LEN];
    ASSERT(nl_ticket_issue("cli:mkdir", "{\"paths\":[\"/tmp/a\"]}", 2, 1000, tok) == 0, "issue failed");
    ASSERT(strlen(tok) == 32, "token must be 32 hex chars");
    ASSERT(nl_ticket_check(tok, "cli:mkdir", "{\"paths\":[\"/tmp/a\"]}", 1001) == NL_TICKET_OK, "check failed");
    PASS();
}

static void test_forged(void)
{
    TEST("forged token -> INVALID");
    nl_ticket_reset();
    ASSERT(nl_ticket_check("00112233445566778899aabbccddeeff", "cli:ls", "{}", 1000) == NL_TICKET_INVALID,
           "forged must be invalid");
    PASS();
}

static void test_expired(void)
{
    TEST("expired -> EXPIRED");
    nl_ticket_reset();
    char tok[NL_TICKET_TOKEN_LEN];
    ASSERT(nl_ticket_issue("cli:ls", "{}", 1, 1000, tok) == 0, "issue failed");
    ASSERT(nl_ticket_check(tok, "cli:ls", "{}", 1000 + NL_TICKET_TTL_SEC + 1) == NL_TICKET_EXPIRED,
           "must expire");
    PASS();
}

static void test_args_mismatch(void)
{
    TEST("args mismatch -> MISMATCH");
    nl_ticket_reset();
    char tok[NL_TICKET_TOKEN_LEN];
    ASSERT(nl_ticket_issue("cli:mkdir", "{\"paths\":[\"/tmp/a\"]}", 2, 1000, tok) == 0, "issue failed");
    ASSERT(nl_ticket_check(tok, "cli:mkdir", "{\"paths\":[\"/tmp/b\"]}", 1001) == NL_TICKET_MISMATCH,
           "must mismatch");
    PASS();
}

static void test_replay(void)
{
    TEST("replay -> MISMATCH");
    nl_ticket_reset();
    char tok[NL_TICKET_TOKEN_LEN];
    ASSERT(nl_ticket_issue("cli:ls", "{}", 1, 1000, tok) == 0, "issue failed");
    ASSERT(nl_ticket_check(tok, "cli:ls", "{}", 1001) == NL_TICKET_OK, "first use ok");
    ASSERT(nl_ticket_check(tok, "cli:ls", "{}", 1002) == NL_TICKET_MISMATCH, "second use must be rejected");
    PASS();
}

static void test_overflow_eviction(void)
{
    TEST("table full -> oldest evicted, size capped");
    nl_ticket_reset();
    char first[NL_TICKET_TOKEN_LEN];
    char tok[NL_TICKET_TOKEN_LEN];
    char args[64];
    for (int i = 0; i < NL_TICKET_MAX + 1; i++) {
        snprintf(args, sizeof(args), "{\"i\":%d}", i);
        ASSERT(nl_ticket_issue("cli:ls", args, 1, 1000, tok) == 0, "issue failed");
        if (i == 0) snprintf(first, sizeof(first), "%s", tok);
    }
    ASSERT(nl_ticket_count() == NL_TICKET_MAX, "count must be capped at 1024");
    ASSERT(nl_ticket_evictions() >= 1, "must record eviction");
    ASSERT(nl_ticket_check(first, "cli:ls", "{\"i\":0}", 1001) == NL_TICKET_INVALID,
           "evicted token must be invalid");
    PASS();
}

static void test_rate_limit(void)
{
    TEST("rate limit per second");
    nl_ticket_reset();
    long now = 2000;
    for (int i = 0; i < NL_TICKET_RATE_LIMIT; i++)
        ASSERT(nl_ticket_rate_ok(now) == 1, "under limit must pass");
    ASSERT(nl_ticket_rate_ok(now) == 0, "over limit must fail");
    ASSERT(nl_ticket_rate_ok(now + 1) == 1, "next second resets");
    PASS();
}

int main(void)
{
    printf("ticket tests\n");
    test_sha256_vectors();
    test_normal_check();
    test_forged();
    test_expired();
    test_args_mismatch();
    test_replay();
    test_overflow_eviction();
    test_rate_limit();
    printf("\n%d/%d passed, %d failed\n", tests_passed, tests_run, tests_failed);
    return tests_failed == 0 ? 0 : 1;
}
