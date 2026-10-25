/*
 * 裁决器单元测试（T04）
 *
 * 覆盖：L1 allow / L2 confirm / L3 confirm / L4 deny / 保护路径 deny 且给替代 /
 *       模型伪造 effect=read 仍 deny 且打标记
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int tests_run = 0, tests_passed = 0, tests_failed = 0;
#define TEST(n) do { tests_run++; printf("  TEST %s ... ", n); fflush(stdout); } while (0)
#define PASS()  do { tests_passed++; printf("PASS\n"); } while (0)
#define FAIL(m) do { tests_failed++; printf("FAIL: %s\n", m); return; } while (0)
#define ASSERT(c,m) do { if (!(c)) { FAIL(m); } } while (0)

#include "../../src/nlbroker/verdict.c"

static void make_cap(NlCapability *c, const char *id, int perm, NlCapEffect eff)
{
    memset(c, 0, sizeof(*c));
    snprintf(c->id, sizeof(c->id), "%s", id);
    snprintf(c->name, sizeof(c->name), "%s", id);
    c->path = (char *)"/usr/bin/x";
    c->source = (char *)"scan";
    c->perm_level = perm;
    c->effect = eff;
    c->reversible = (eff == NL_CAP_EFFECT_READ) ? 1 : 0;
    c->trusted = 1;
    snprintf(c->namespace, sizeof(c->namespace), "cli");
}

static void test_allow_read_l1(void)
{
    TEST("L1 read -> allow");
    NlCapability c;
    NlVerdictResult r;
    make_cap(&c, "cli:ls", 1, NL_CAP_EFFECT_READ);
    ASSERT(nl_verdict_eval(&c, "{\"paths\":[\"/tmp\"]}", &r) == 0, "eval failed");
    ASSERT(r.verdict == NL_VERDICT_ALLOW, "must allow");
    ASSERT(r.untrusted_effect_attempt == 0, "no fake effect");
    PASS();
}

static void test_confirm_write_l2(void)
{
    TEST("L2 write -> confirm");
    NlCapability c;
    NlVerdictResult r;
    make_cap(&c, "cli:mkdir", 2, NL_CAP_EFFECT_WRITE);
    ASSERT(nl_verdict_eval(&c, "{\"paths\":[\"/tmp/nlcap-x\"]}", &r) == 0, "eval failed");
    ASSERT(r.verdict == NL_VERDICT_CONFIRM, "must confirm");
    PASS();
}

static void test_confirm_delete_l3(void)
{
    TEST("L3 delete -> confirm");
    NlCapability c;
    NlVerdictResult r;
    make_cap(&c, "cli:rm", 3, NL_CAP_EFFECT_DELETE);
    ASSERT(nl_verdict_eval(&c, "{\"paths\":[\"/tmp/nlcap-y\"]}", &r) == 0, "eval failed");
    ASSERT(r.verdict == NL_VERDICT_CONFIRM, "must confirm");
    PASS();
}

static void test_deny_l4(void)
{
    TEST("L4 -> deny + alternatives");
    NlCapability c;
    NlVerdictResult r;
    make_cap(&c, "cli:sudo", 4, NL_CAP_EFFECT_PRIVILEGE);
    ASSERT(nl_verdict_eval(&c, "{}", &r) == 0, "eval failed");
    ASSERT(r.verdict == NL_VERDICT_DENY, "must deny");
    ASSERT(r.alt_count >= 1, "deny must carry alternatives");
    PASS();
}

static void test_deny_protected_path(void)
{
    TEST("protected path -> deny");
    NlCapability c;
    NlVerdictResult r;
    make_cap(&c, "cli:rm", 3, NL_CAP_EFFECT_DELETE);
    ASSERT(nl_verdict_eval(&c, "{\"paths\":[\"/etc/passwd\"]}", &r) == 0, "eval failed");
    ASSERT(r.verdict == NL_VERDICT_DENY, "must deny");
    ASSERT(r.alt_count >= 1, "must give alternatives");
    PASS();
}

static void test_fake_effect_denied(void)
{
    TEST("model self-reported effect=read still denied");
    NlCapability c;
    NlVerdictResult r;
    make_cap(&c, "cli:rm", 3, NL_CAP_EFFECT_DELETE);
    ASSERT(nl_verdict_eval(&c,
        "{\"paths\":[\"/etc/passwd\"],\"effect\":\"read\"}", &r) == 0, "eval failed");
    ASSERT(r.verdict == NL_VERDICT_DENY, "fake effect must not downgrade");
    ASSERT(r.untrusted_effect_attempt == 1, "must flag untrusted effect attempt");
    PASS();
}

int main(void)
{
    printf("verdict tests\n");
    test_allow_read_l1();
    test_confirm_write_l2();
    test_confirm_delete_l3();
    test_deny_l4();
    test_deny_protected_path();
    test_fake_effect_denied();
    printf("\n%d/%d passed, %d failed\n", tests_passed, tests_run, tests_failed);
    return tests_failed == 0 ? 0 : 1;
}
