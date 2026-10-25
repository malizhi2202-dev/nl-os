/*
 * 决策层（模型无关部分）单元测试（T10 / T11 / T12）
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static int tests_run = 0, tests_passed = 0, tests_failed = 0;
#define TEST(n) do { tests_run++; printf("  TEST %s ... ", n); fflush(stdout); } while (0)
#define PASS()  do { tests_passed++; printf("PASS\n"); } while (0)
#define FAIL(m) do { tests_failed++; printf("FAIL: %s\n", m); return; } while (0)
#define ASSERT(c,m) do { if (!(c)) { FAIL(m); } } while (0)

#include "decision.h"

static NlCapRegistry *g_reg;

static void add(NlCapRegistry *r, const char *id, int perm)
{
    NlCapability c;
    memset(&c, 0, sizeof(c));
    snprintf(c.id, sizeof(c.id), "%s", id);
    snprintf(c.name, sizeof(c.name), "%s", id);
    c.path = (char *)"/bin/true";
    c.source = (char *)"scan";
    c.trusted = 1;
    c.perm_level = perm;
    c.effect = (perm >= 3) ? NL_CAP_EFFECT_DELETE : NL_CAP_EFFECT_READ;
    c.reversible = 1;
    snprintf(c.namespace, sizeof(c.namespace), "cli");
    (void)nl_cap_add(r, &c);
}

static void seed(void)
{
    g_reg = nl_cap_create();
    const char *ids[] = { "cli:ls", "cli:cat", "cli:mkdir", "cli:rm", "cli:mv",
                          "cli:cp", "cli:find", "cli:ps", "cli:tar", "cli:curl",
                          "cli:chmod", "cli:df", "cli:man", "cli:touch", "cli:sudo" };
    for (size_t i = 0; i < sizeof(ids) / sizeof(ids[0]); i++) {
        int perm = 1;
        if (strcmp(ids[i], "cli:mkdir") == 0 || strcmp(ids[i], "cli:touch") == 0 ||
            strcmp(ids[i], "cli:cp") == 0 || strcmp(ids[i], "cli:mv") == 0 ||
            strcmp(ids[i], "cli:chmod") == 0) perm = 2;
        else if (strcmp(ids[i], "cli:rm") == 0) perm = 3;
        else if (strcmp(ids[i], "cli:sudo") == 0) perm = 4;
        add(g_reg, ids[i], perm);
    }
}

static void test_candidates_dictionary(void)
{
    TEST("dictionary narrows to expected capability");
    struct { const char *input; const char *expect; } cases[] = {
        { "列出当前目录", "cli:ls" },
        { "查看 README 内容", "cli:cat" },
        { "创建一个文件夹", "cli:mkdir" },
        { "把日志压缩打包", "cli:tar" },
    };
    for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
        NlCandidates c;
        ASSERT(nl_decide_candidates(cases[i].input, g_reg, &c) == 0, "decide failed");
        ASSERT(c.count >= 1, cases[i].input);
        ASSERT(strcmp(c.items[0].id, cases[i].expect) == 0, cases[i].input);
    }
    PASS();
}

static void test_candidates_cap_and_l4(void)
{
    TEST("top-K cap and L4 excluded");
    NlCandidates c;
    ASSERT(nl_decide_candidates("列出 查看 创建 删除 移动 复制 查找 进程 压缩 下载 权限 磁盘", g_reg, &c) == 0,
           "decide failed");
    ASSERT(c.count <= NL_DECIDE_K, "must respect K");
    for (int i = 0; i < c.count; i++)
        ASSERT(strcmp(c.items[i].id, "cli:sudo") != 0, "L4 must never be a candidate");
    PASS();
}

static void test_candidates_empty(void)
{
    TEST("no match yields empty candidate set");
    NlCandidates c;
    ASSERT(nl_decide_candidates("zzz 完全无关的一句话", g_reg, &c) == 0, "decide failed");
    ASSERT(c.count == 0, "should be empty");
    PASS();
}

static void test_grammar(void)
{
    TEST("GBNF enumerates only candidates (+refuse)");
    NlCandidates c;
    ASSERT(nl_decide_candidates("列出当前目录", g_reg, &c) == 0, "decide failed");
    char buf[1024];
    int n = nl_decide_grammar(&c, buf, sizeof(buf));
    ASSERT(n > 0, "grammar build failed");
    ASSERT(strstr(buf, "root ::= choice") != NULL, "must define root");
    for (int i = 0; i < c.count; i++)
        ASSERT(strstr(buf, c.items[i].id) != NULL, "each candidate must be a literal");
    ASSERT(strstr(buf, "<refuse>") != NULL, "must allow refusal");
    ASSERT(strstr(buf, "cli:sudo") == NULL, "must not enumerate non-candidates");
    PASS();
}

static void test_grammar_build_cost(void)
{
    TEST("grammar build cost well under 5ms");
    NlCandidates c;
    (void)nl_decide_candidates("列出 查看 创建 删除 移动 复制 查找 进程", g_reg, &c);
    char buf[2048];
    struct timespec t0, t1;
    clock_gettime(CLOCK_MONOTONIC, &t0);
    for (int i = 0; i < 1000; i++) (void)nl_decide_grammar(&c, buf, sizeof(buf));
    clock_gettime(CLOCK_MONOTONIC, &t1);
    double ms = (double)(t1.tv_sec - t0.tv_sec) * 1000.0 +
                (double)(t1.tv_nsec - t0.tv_nsec) / 1e6;
    printf("[1000 次构造 %.3fms，单次 %.4fms] ", ms, ms / 1000.0);
    ASSERT(ms / 1000.0 < 5.0, "single build must be < 5ms");
    PASS();
}

static void test_noul_monotonicity(void)
{
    TEST("Noul: model can only tighten");
    ASSERT(nl_noul_combine(NL_NOUL_ALLOW, NL_NOUL_ALLOW) == NL_NOUL_ALLOW, "allow+allow");
    ASSERT(nl_noul_combine(NL_NOUL_ALLOW, NL_NOUL_CONFIRM) == NL_NOUL_CONFIRM, "model may tighten");
    ASSERT(nl_noul_combine(NL_NOUL_ALLOW, NL_NOUL_DENY) == NL_NOUL_DENY, "model may deny");
    ASSERT(nl_noul_combine(NL_NOUL_CONFIRM, NL_NOUL_ALLOW) == NL_NOUL_CONFIRM, "model must NOT relax");
    ASSERT(nl_noul_combine(NL_NOUL_DENY, NL_NOUL_ALLOW) == NL_NOUL_DENY, "model must NOT relax deny");
    PASS();
}

int main(void)
{
    seed();
    printf("decision tests\n");
    test_candidates_dictionary();
    test_candidates_cap_and_l4();
    test_candidates_empty();
    test_grammar();
    test_grammar_build_cost();
    test_noul_monotonicity();
    nl_cap_free(g_reg);
    printf("\n%d/%d passed, %d failed\n", tests_passed, tests_run, tests_failed);
    return tests_failed == 0 ? 0 : 1;
}
