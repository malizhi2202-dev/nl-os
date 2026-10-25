/*
 * 复杂度预算单元测试（T05）
 * 覆盖：5 条超范围 refused + 维度 + 替代 / 3 条简单指令 pass / 替代必须存在于注册表
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int tests_run = 0, tests_passed = 0, tests_failed = 0;
#define TEST(n) do { tests_run++; printf("  TEST %s ... ", n); fflush(stdout); } while (0)
#define PASS()  do { tests_passed++; printf("PASS\n"); } while (0)
#define FAIL(m) do { tests_failed++; printf("FAIL: %s\n", m); return; } while (0)
#define ASSERT(c,m) do { if (!(c)) { FAIL(m); } } while (0)

#include "budget.h"

static NlCapRegistry *g_reg;

static void seed_registry(void)
{
    const char *ids[] = { "cli:ls", "cli:stat", "cli:cat", "cli:mkdir" };
    const int perms[] = { 1, 1, 1, 2 };
    g_reg = nl_cap_create();
    for (int i = 0; i < 4; i++) {
        NlCapability c;
        memset(&c, 0, sizeof(c));
        snprintf(c.id, sizeof(c.id), "%s", ids[i]);
        snprintf(c.name, sizeof(c.name), "%s", ids[i] + 4);
        c.path = (char *)"/bin/true";
        c.source = (char *)"scan";
        c.trusted = 1;
        c.perm_level = perms[i];
        c.effect = (i == 3) ? NL_CAP_EFFECT_WRITE : NL_CAP_EFFECT_READ;
        snprintf(c.namespace, sizeof(c.namespace), "cli");
        ASSERT(nl_cap_add(g_reg, &c) == NL_CAP_OK, "seed add");
    }
}

static void test_refuse_complex(void)
{
    TEST("5 complex inputs refused with dims + alternatives");
    const char *inputs[] = {
        "每天凌晨三点把日志压缩后上传到备份服务器",
        "循环检查磁盘空间，低于 10% 就清理",
        "如果磁盘满了就删除旧日志",
        "用 sudo 安装并重启服务",
        "同时并发下载十个文件然后逐个解压并且校验再备份",
    };
    for (int i = 0; i < 5; i++) {
        NlBudgetDetail d;
        NlBudgetVerdict v = nl_budget_check(inputs[i], g_reg, &d);
        ASSERT(v == NL_BUDGET_REFUSE, inputs[i]);
        ASSERT(d.dim_count >= 1, "must report dimension");
        ASSERT(d.alternative_count >= 1, "must give alternatives");
        for (int k = 0; k < d.alternative_count; k++)
            ASSERT(nl_cap_find(g_reg, d.alternative_ids[k]) != NULL,
                   "alternative must exist in registry");
    }
    PASS();
}

static void test_pass_simple(void)
{
    TEST("3 simple inputs pass");
    const char *inputs[] = { "创建一个叫 build 的文件夹", "列出当前目录", "查看 README 内容" };
    for (int i = 0; i < 3; i++) {
        NlBudgetDetail d;
        ASSERT(nl_budget_check(inputs[i], g_reg, &d) == NL_BUDGET_PASS, inputs[i]);
        ASSERT(d.dim_count == 0, "no dims on pass");
    }
    PASS();
}

static void test_empty_input(void)
{
    TEST("empty input passes (no false refuse)");
    NlBudgetDetail d;
    ASSERT(nl_budget_check("", g_reg, &d) == NL_BUDGET_PASS, "empty");
    ASSERT(nl_budget_check(NULL, g_reg, &d) == NL_BUDGET_PASS, "null");
    PASS();
}

int main(void)
{
    seed_registry();
    printf("budget tests\n");
    test_refuse_complex();
    test_pass_simple();
    test_empty_input();
    nl_cap_free(g_reg);
    printf("\n%d/%d passed, %d failed\n", tests_passed, tests_run, tests_failed);
    return tests_failed == 0 ? 0 : 1;
}
