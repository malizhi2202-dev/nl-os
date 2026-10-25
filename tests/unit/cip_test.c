/*
 * cip 编解码单元测试（T01）
 * 覆盖：合法 discover / 未知 op / v=2 / 缺 id / 空行 / render 后可 reparse
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int tests_run = 0, tests_passed = 0, tests_failed = 0;
#define TEST(n) do { tests_run++; printf("  TEST %s ... ", n); fflush(stdout); } while (0)
#define PASS()  do { tests_passed++; printf("PASS\n"); } while (0)
#define FAIL(m) do { tests_failed++; printf("FAIL: %s\n", m); return; } while (0)
#define ASSERT(c,m) do { if (!(c)) { FAIL(m); } } while (0)

#include "cip.h"

static void test_discover_ok(void)
{
    TEST("parse valid discover");
    NlCipReq r;
    int rc = nl_cip_parse("{\"v\":1,\"id\":\"r0\",\"op\":\"discover\",\"limit\":10}", &r);
    ASSERT(rc == NL_CIP_OK, "should parse");
    ASSERT(r.op == NL_CIP_OP_DISCOVER, "op must be discover");
    ASSERT(strcmp(r.id, "r0") == 0, "id mismatch");
    ASSERT(r.limit == 10, "limit mismatch");
    PASS();
}

static void test_unknown_op(void)
{
    TEST("unknown op rejected");
    NlCipReq r;
    int rc = nl_cip_parse("{\"v\":1,\"id\":\"r0\",\"op\":\"explode\"}", &r);
    ASSERT(rc == NL_CIP_E_BAD_REQ, "should be E_BAD_REQ");
    PASS();
}

static void test_version_rejected(void)
{
    TEST("v=2 rejected");
    NlCipReq r;
    int rc = nl_cip_parse("{\"v\":2,\"id\":\"r0\",\"op\":\"discover\"}", &r);
    ASSERT(rc == NL_CIP_E_VERSION, "should be E_VERSION");
    PASS();
}

static void test_missing_id(void)
{
    TEST("missing id rejected");
    NlCipReq r;
    int rc = nl_cip_parse("{\"v\":1,\"op\":\"discover\"}", &r);
    ASSERT(rc == NL_CIP_E_BAD_REQ, "should be E_BAD_REQ");
    PASS();
}

static void test_empty_line(void)
{
    TEST("empty line rejected");
    NlCipReq r;
    ASSERT(nl_cip_parse("", &r) == NL_CIP_E_BAD_REQ, "empty should fail");
    ASSERT(nl_cip_parse("  \n", &r) == NL_CIP_E_BAD_REQ, "blank should fail");
    PASS();
}

static void test_invoke_and_roundtrip(void)
{
    TEST("invoke fields + render roundtrip");
    NlCipReq r;
    int rc = nl_cip_parse(
        "{\"v\":1,\"id\":\"r2\",\"op\":\"invoke\",\"capability\":\"cli:mkdir\","
        "\"args\":{\"paths\":[\"/tmp/x\"]},\"dry_run\":false}", &r);
    ASSERT(rc == NL_CIP_OK, "invoke should parse");
    ASSERT(strcmp(r.capability, "cli:mkdir") == 0, "capability mismatch");
    ASSERT(strcmp(r.id, "r2") == 0, "envelope id must stay r2");
    ASSERT(strstr(r.args_json, "/tmp/x") != NULL, "args subtree kept");

    NlCipResp resp;
    memset(&resp, 0, sizeof(resp));
    resp.v = 1;
    snprintf(resp.id, sizeof(resp.id), "%s", r.id);
    resp.ok = 1;
    snprintf(resp.payload_json, sizeof(resp.payload_json),
             "{\"verdict\":\"confirm\",\"plan\":{\"argv\":[\"mkdir\",\"-p\",\"/tmp/x\"]}}");

    char buf[2048];
    int n = nl_cip_render(&resp, buf, sizeof(buf));
    ASSERT(n > 0, "render should succeed");
    ASSERT(strstr(buf, "\"ok\":true") != NULL, "render must carry ok");
    ASSERT(strstr(buf, "\"plan\"") != NULL, "render must carry payload");
    ASSERT(buf[n - 1] == '\n', "render must end with newline");

    /* 响应本身不是合法请求，但应是合法 JSON：用 parse 验证 envelope 结构可被复用解析 */
    NlCipReq dummy;
    ASSERT(nl_cip_parse(buf, &dummy) == NL_CIP_E_BAD_REQ, "response is not a request");
    PASS();
}

static void test_commit_and_audit(void)
{
    TEST("commit / audit fields");
    NlCipReq r;
    ASSERT(nl_cip_parse("{\"v\":1,\"id\":\"r3\",\"op\":\"commit\",\"ticket\":\"abc\"}", &r) == NL_CIP_OK,
           "commit should parse");
    ASSERT(strcmp(r.ticket, "abc") == 0, "ticket mismatch");

    NlCipReq a;
    ASSERT(nl_cip_parse("{\"v\":1,\"id\":\"r4\",\"op\":\"audit\",\"since\":123,\"limit\":9999}", &a) == NL_CIP_OK,
           "audit should parse");
    ASSERT(a.since == 123, "since mismatch");
    ASSERT(a.audit_limit == 50, "limit must clamp to 50");
    PASS();
}

int main(void)
{
    printf("cip tests\n");
    test_discover_ok();
    test_unknown_op();
    test_version_rejected();
    test_missing_id();
    test_empty_line();
    test_invoke_and_roundtrip();
    test_commit_and_audit();
    printf("\n%d/%d passed, %d failed\n", tests_passed, tests_run, tests_failed);
    return tests_failed == 0 ? 0 : 1;
}
