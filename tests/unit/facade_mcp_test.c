/*
 * MCP 门面单元测试（T12）
 * 覆盖：initialize / tools/list 只含 perm≤L2 / L3 调用被拒 / allow 执行 / confirm 被拒
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int tests_run = 0, tests_passed = 0, tests_failed = 0;
#define TEST(n) do { tests_run++; printf("  TEST %s ... ", n); fflush(stdout); } while (0)
#define PASS()  do { tests_passed++; printf("PASS\n"); } while (0)
#define FAIL(m) do { tests_failed++; printf("FAIL: %s\n", m); return; } while (0)
#define ASSERT(c,m) do { if (!(c)) { FAIL(m); } } while (0)

#include "../../src/nlbroker/adapter_cli.c"
#include "../../src/nlbroker/adapter_dbus.c"
#include "../../src/nlbroker/adapter_mcp.c"
#include "../../src/nlbroker/verdict.c"
#include "../../src/nlbroker/ticket.c"
#include "../../src/nlbroker/dispatch.c"
#include "../../src/nlbroker/facade_mcp.c"

static char g_reg[512];

static int write_registry(void)
{
    FILE *f = fopen(g_reg, "w");
    if (!f) return -1;
    fputs("{\"format_version\":2,\"scanner\":\"t12\"}\n", f);
    fputs("{\"id\":\"cli:echo\",\"name\":\"echo\",\"path\":\"/bin/echo\",\"perm\":\"L1\","
          "\"effect\":\"read\",\"reversible\":true,\"source\":\"scan\",\"trusted\":true,"
          "\"namespace\":\"cli\"}\n", f);
    fputs("{\"id\":\"cli:mkdir\",\"name\":\"mkdir\",\"path\":\"/bin/mkdir\",\"perm\":\"L2\","
          "\"effect\":\"write\",\"reversible\":true,\"source\":\"scan\",\"trusted\":true,"
          "\"namespace\":\"cli\"}\n", f);
    fputs("{\"id\":\"cli:rm\",\"name\":\"rm\",\"path\":\"/bin/rm\",\"perm\":\"L3\","
          "\"effect\":\"delete\",\"reversible\":false,\"source\":\"scan\",\"trusted\":true,"
          "\"namespace\":\"cli\"}\n", f);
    fclose(f);
    return 0;
}

static void test_initialize(void)
{
    TEST("initialize returns protocolVersion");
    NlDispatch d;
    ASSERT(nl_dispatch_init(&d, g_reg) == 0, "dispatch init");
    char out[8192];
    int n = nl_facade_handle_line(&d, "{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"initialize\"}",
                                  out, sizeof(out));
    nl_dispatch_free(&d);
    ASSERT(n > 0, "no response");
    ASSERT(strstr(out, "protocolVersion") != NULL, "must return protocolVersion");
    PASS();
}

static void test_tools_list_filtering(void)
{
    TEST("tools/list excludes L3+");
    NlDispatch d;
    ASSERT(nl_dispatch_init(&d, g_reg) == 0, "dispatch init");
    char out[8192];
    int n = nl_facade_handle_line(&d, "{\"jsonrpc\":\"2.0\",\"id\":2,\"method\":\"tools/list\"}",
                                  out, sizeof(out));
    nl_dispatch_free(&d);
    ASSERT(n > 0, "no response");
    ASSERT(strstr(out, "nl_cli_echo") != NULL, "L1 must be listed");
    ASSERT(strstr(out, "nl_cli_mkdir") != NULL, "L2 must be listed");
    ASSERT(strstr(out, "nl_cli_rm") == NULL, "L3 must NOT be listed");
    PASS();
}

static void test_deny_l3_call(void)
{
    TEST("direct call to L3 tool denied");
    NlDispatch d;
    ASSERT(nl_dispatch_init(&d, g_reg) == 0, "dispatch init");
    char out[8192];
    int n = nl_facade_handle_line(&d,
        "{\"jsonrpc\":\"2.0\",\"id\":3,\"method\":\"tools/call\","
        "\"params\":{\"name\":\"nl_cli_rm\",\"arguments\":{\"paths\":[\"/tmp/x\"]}}}",
        out, sizeof(out));
    nl_dispatch_free(&d);
    ASSERT(n > 0, "no response");
    ASSERT(strstr(out, "\"error\"") != NULL, "must be an error");
    ASSERT(strstr(out, "L3") != NULL || strstr(out, "level") != NULL, "must explain level");
    PASS();
}

static void test_allow_executes(void)
{
    TEST("L1 tool executes and returns content");
    NlDispatch d;
    ASSERT(nl_dispatch_init(&d, g_reg) == 0, "dispatch init");
    char out[8192];
    int n = nl_facade_handle_line(&d,
        "{\"jsonrpc\":\"2.0\",\"id\":4,\"method\":\"tools/call\","
        "\"params\":{\"name\":\"nl_cli_echo\",\"arguments\":{\"args\":[\"hello-t12\"]}}}",
        out, sizeof(out));
    nl_dispatch_free(&d);
    ASSERT(n > 0, "no response");
    ASSERT(strstr(out, "\"content\"") != NULL, "must return content");
    ASSERT(strstr(out, "hello-t12") != NULL, "must carry command output");
    PASS();
}

static void test_confirm_rejected(void)
{
    TEST("L2 (confirm) rejected without trusted channel");
    NlDispatch d;
    ASSERT(nl_dispatch_init(&d, g_reg) == 0, "dispatch init");
    char out[8192];
    int n = nl_facade_handle_line(&d,
        "{\"jsonrpc\":\"2.0\",\"id\":5,\"method\":\"tools/call\","
        "\"params\":{\"name\":\"nl_cli_mkdir\",\"arguments\":{\"paths\":[\"/tmp/t12-x\"]}}}",
        out, sizeof(out));
    nl_dispatch_free(&d);
    ASSERT(n > 0, "no response");
    ASSERT(strstr(out, "\"error\"") != NULL, "must be error");
    ASSERT(strstr(out, "confirmation") != NULL, "must ask for human confirmation");
    PASS();
}

int main(void)
{
    char tmpl[] = "/tmp/nlfacade-XXXXXX";
    char *dir = mkdtemp(tmpl);
    if (!dir) { printf("mkdtemp failed\n"); return 1; }
    setenv("XDG_STATE_HOME", dir, 1);
    snprintf(g_reg, sizeof(g_reg), "%s/reg.jsonl", dir);
    if (write_registry() != 0) { printf("write registry failed\n"); return 1; }

    printf("facade_mcp tests (%s)\n", dir);
    test_initialize();
    test_tools_list_filtering();
    test_deny_l3_call();
    test_allow_executes();
    test_confirm_rejected();
    printf("\n%d/%d passed, %d failed\n", tests_passed, tests_run, tests_failed);
    return tests_failed == 0 ? 0 : 1;
}
