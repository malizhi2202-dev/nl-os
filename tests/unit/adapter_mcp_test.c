/*
 * MCP 客户端适配器单元测试（T11）
 * 覆盖：initialize / tools/list / tools/call / 登记进注册表
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

#include "../../src/nlbroker/adapter_mcp.c"

#ifndef FIXTURE_DIR
#define FIXTURE_DIR "tests/fixtures"
#endif

static NlMcpClient *start_fake(void)
{
    char script[512];
    snprintf(script, sizeof(script), "%s/fake_mcp_server.py", FIXTURE_DIR);
    if (access(script, R_OK) != 0) return NULL;
    char *args[1] = { script };
    return nl_mcp_start("fake", "/usr/bin/python3", args, 1);
}

static void test_list_tools(void)
{
    TEST("tools/list returns 2 tools");
    NlMcpClient *c = start_fake();
    if (!c) SKIP("python3 or fixture missing");
    NlMcpTool tools[NL_MCP_MAX_TOOLS];
    int n = 0;
    ASSERT(nl_mcp_list_tools(c, tools, NL_MCP_MAX_TOOLS, &n) == 0, "list failed");
    ASSERT(n == 2, "expected 2 tools");
    ASSERT(strcmp(tools[0].name, "echo_low") == 0, "tools[0].name");
    ASSERT(strcmp(tools[1].name, "danger_high") == 0, "tools[1].name");
    ASSERT(strstr(tools[0].schema, "object") != NULL, "schema captured");
    nl_mcp_stop(c);
    PASS();
}

static void test_call_tool(void)
{
    TEST("tools/call echoes args");
    NlMcpClient *c = start_fake();
    if (!c) SKIP("python3 or fixture missing");
    char out[2048];
    ASSERT(nl_mcp_call_tool(c, "echo_low", "{\"msg\":\"hi\"}", out, sizeof(out)) == 0, "call failed");
    ASSERT(strstr(out, "called echo_low") != NULL, "must echo tool name");
    ASSERT(strstr(out, "hi") != NULL, "must echo args");
    nl_mcp_stop(c);
    PASS();
}

static void test_register(void)
{
    TEST("register mcp tools into registry");
    NlMcpClient *c = start_fake();
    if (!c) SKIP("python3 or fixture missing");
    NlCapRegistry *reg = nl_cap_create();
    ASSERT(reg != NULL, "create failed");
    int added = nl_mcp_add_to_registry(c, reg);
    ASSERT(added == 2, "should add 2 capabilities");
    const NlCapability *cap = nl_cap_find(reg, "mcp:fake/echo_low");
    ASSERT(cap != NULL, "id must be mcp:<server>/<tool>");
    ASSERT(cap->effect == NL_CAP_EFFECT_WRITE, "effect must be write");
    ASSERT(cap->effect_defaulted == 1, "must be defaulted");
    ASSERT(strcmp(cap->namespace, "mcp") == 0, "namespace must be mcp");
    nl_cap_free(reg);
    nl_mcp_stop(c);
    PASS();
}

int main(void)
{
    printf("adapter_mcp tests\n");
    test_list_tools();
    test_call_tool();
    test_register();
    printf("\n%d/%d passed, %d failed\n", tests_passed, tests_run, tests_failed);
    return tests_failed == 0 ? 0 : 1;
}
