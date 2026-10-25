/*
 * dbus 适配器单元测试（T09 / T10）
 * 覆盖：2 段/4 段 ID 解析、非法字符拒绝、非空参数拒绝、真实调用、能力发现
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int tests_run = 0, tests_passed = 0, tests_failed = 0;
#define TEST(n) do { tests_run++; printf("  TEST %s ... ", n); fflush(stdout); } while (0)
#define PASS()  do { tests_passed++; printf("PASS\n"); } while (0)
#define FAIL(m) do { tests_failed++; printf("FAIL: %s\n", m); return; } while (0)
#define ASSERT(c,m) do { if (!(c)) { FAIL(m); } } while (0)
#define SKIP(m) do { tests_passed++; printf("SKIP (%s)\n", m); return; } while (0)

#include "../../src/nlbroker/adapter_cli.c"
#include "../../src/nlbroker/adapter_dbus.c"

static void make_cap(NlCapability *c, const char *id)
{
    memset(c, 0, sizeof(*c));
    snprintf(c->id, sizeof(c->id), "%s", id);
    c->path = (char *)"/usr/bin/busctl";
    c->source = (char *)"bus";
    c->trusted = 1;
    c->perm_level = 2;
    c->effect = NL_CAP_EFFECT_NETWORK;
    snprintf(c->namespace, sizeof(c->namespace), "dbus");
}

static void test_two_part(void)
{
    TEST("two-part id -> busctl argv");
    char tool[256];
    if (nl_dbus_tool_path(tool, sizeof(tool)) != 0) SKIP("busctl not installed");
    NlCapability c;
    NlPlan plan;
    make_cap(&c, "dbus:org.freedesktop.DBus|ListNames");
    ASSERT(nl_dbus_build_plan(&c, "{}", &plan) == 0, "plan failed");
    ASSERT(plan.argv[0] && strcmp(plan.argv[0], "busctl") == 0, "argv0");
    ASSERT(plan.argv[1] && strcmp(plan.argv[1], "call") == 0, "argv1");
    ASSERT(plan.argv[2] && strcmp(plan.argv[2], "org.freedesktop.DBus") == 0, "service");
    ASSERT(plan.argv[3] && strcmp(plan.argv[3], "/") == 0, "default path");
    ASSERT(plan.argv[4] && strcmp(plan.argv[4], "org.freedesktop.DBus") == 0, "default iface");
    ASSERT(plan.argv[5] && strcmp(plan.argv[5], "ListNames") == 0, "method");
    nl_cli_plan_free(&plan);
    PASS();
}

static void test_four_part(void)
{
    TEST("four-part id accepted");
    char tool[256];
    if (nl_dbus_tool_path(tool, sizeof(tool)) != 0) SKIP("busctl not installed");
    NlCapability c;
    NlPlan plan;
    make_cap(&c, "dbus:org.freedesktop.DBus|/org/freedesktop/DBus|org.freedesktop.DBus|GetId");
    ASSERT(nl_dbus_build_plan(&c, "{}", &plan) == 0, "plan failed");
    ASSERT(plan.argv[5] && strcmp(plan.argv[5], "GetId") == 0, "method");
    ASSERT(plan.argv[3] && strcmp(plan.argv[3], "/org/freedesktop/DBus") == 0, "path");
    nl_cli_plan_free(&plan);
    PASS();
}

static void test_invalid_chars(void)
{
    TEST("shell metacharacters rejected");
    NlCapability c;
    NlPlan plan;
    make_cap(&c, "dbus:org.freedesktop.DBus|List;Names");
    ASSERT(nl_dbus_build_plan(&c, "{}", &plan) != 0, "must reject ';'");
    make_cap(&c, "dbus:org.freedesktop.DBus|/|org.freedesktop.DBus|$(id)");
    ASSERT(nl_dbus_build_plan(&c, "{}", &plan) != 0, "must reject '$()'");
    PASS();
}

static void test_args_rejected(void)
{
    TEST("non-empty args rejected (v1 no-arg only)");
    NlCapability c;
    NlPlan plan;
    make_cap(&c, "dbus:org.freedesktop.DBus|ListNames");
    ASSERT(nl_dbus_build_plan(&c, "{\"args\":[\"x\"]}", &plan) != 0, "must reject args");
    PASS();
}

static void test_live_call(void)
{
    TEST("live call org.freedesktop.DBus.ListNames");
    char tool[256];
    if (nl_dbus_tool_path(tool, sizeof(tool)) != 0) SKIP("busctl not installed");
    if (access("/run/dbus/system_bus_socket", F_OK) != 0) SKIP("no system bus");

    NlCapability c;
    NlPlan plan;
    NlCliResult r;
    make_cap(&c, "dbus:org.freedesktop.DBus|ListNames");
    ASSERT(nl_dbus_build_plan(&c, "{}", &plan) == 0, "plan failed");
    ASSERT(nl_cli_execute(&plan, 3000, &r) == 0, "exec failed");
    ASSERT(r.status == NL_CLI_OK, "status not ok");
    ASSERT(strstr(r.output, "org.freedesktop.DBus") != NULL, "output must list names");
    nl_cli_plan_free(&plan);
    PASS();
}

static void test_discover(void)
{
    TEST("discover registers invocable dbus methods");
    char tool[256];
    if (nl_dbus_tool_path(tool, sizeof(tool)) != 0) SKIP("busctl not installed");
    if (access("/run/dbus/system_bus_socket", F_OK) != 0) SKIP("no system bus");

    NlCapRegistry *reg = nl_cap_create();
    ASSERT(reg != NULL, "create failed");
    int added = nl_dbus_discover(reg);
    ASSERT(added >= 1, "must add at least one method");
    ASSERT(nl_cap_count(reg) >= 1, "registry must be non-empty");
    const NlCapability *c = nl_cap_at(reg, 0);
    ASSERT(c != NULL, "cap missing");
    ASSERT(c->effect == NL_CAP_EFFECT_NETWORK, "effect must be network");
    ASSERT(c->effect_defaulted == 1, "must be flagged defaulted");
    nl_cap_free(reg);
    PASS();
}

int main(void)
{
    printf("adapter_dbus tests\n");
    test_two_part();
    test_four_part();
    test_invalid_chars();
    test_args_rejected();
    test_live_call();
    test_discover();
    printf("\n%d/%d passed, %d failed\n", tests_passed, tests_run, tests_failed);
    return tests_failed == 0 ? 0 : 1;
}
