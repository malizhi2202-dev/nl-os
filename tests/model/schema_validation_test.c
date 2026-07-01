/*
 * schema 验证测试
 * 验证 JSON Schema 校验逻辑 + 各种边界情况
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int tests_run = 0, tests_passed = 0, tests_failed = 0;
#define TEST(n)  do { tests_run++; printf("  TEST %s ... ", n); } while(0)
#define PASS()   do { tests_passed++; printf("PASS\n"); } while(0)
#define FAIL(m)  do { tests_failed++; printf("FAIL: %s\n", m); return; } while(0)
#define ASSERT(c,m) do { if (!(c)) { FAIL(m); return; } } while(0)

/* 链接 nlc 库 */
#include "../../src/libnlc/intent_extract.h"
#include "../../src/libnlc/json_helper.h"
static void test_valid_json(void) {
    TEST("valid JSON passes schema");
    NlJsonDoc doc;
    const char *j = "{\"intent\":\"create_dir\",\"entities\":{\"target\":\"x\"},\"confidence\":0.95}";
    ASSERT(nl_json_parse(j, &doc) == 0, "parse");
    char err[256];
    ASSERT(validate_schema(doc.root, err, sizeof(err)) == 0, "schema OK");
    nl_json_free(&doc);
    PASS();
}

static void test_missing_intent(void) {
    TEST("missing intent → fail");
    NlJsonDoc doc;
    nl_json_parse("{\"confidence\":0.9}", &doc);
    char err[256];
    ASSERT(validate_schema(doc.root, err, sizeof(err)) != 0, "should fail");
    nl_json_free(&doc);
    PASS();
}

static void test_missing_confidence(void) {
    TEST("missing confidence → fail");
    NlJsonDoc doc;
    nl_json_parse("{\"intent\":\"delete\"}", &doc);
    char err[256];
    ASSERT(validate_schema(doc.root, err, sizeof(err)) != 0, "should fail");
    nl_json_free(&doc);
    PASS();
}

static void test_invalid_intent(void) {
    TEST("unknown intent → fail");
    NlJsonDoc doc;
    nl_json_parse("{\"intent\":\"fly_to_moon\",\"confidence\":0.9}", &doc);
    char err[256];
    ASSERT(validate_schema(doc.root, err, sizeof(err)) != 0, "should fail");
    ASSERT(strstr(err, "unknown") != NULL, "error mentions unknown");
    nl_json_free(&doc);
    PASS();
}

static void test_all_12_intents(void) {
    TEST("all 12 intents pass schema");
    const char *intents[] = {"create_file","create_dir","delete","move","copy",
        "list_dir","view_file","find_files","change_dir",
        "show_process","search_content","run_cmd","other"};
    for (int i = 0; i < 13; i++) {
        char json[256];
        snprintf(json, sizeof(json), "{\"intent\":\"%s\",\"confidence\":0.8}", intents[i]);
        NlJsonDoc doc;
        ASSERT(nl_json_parse(json, &doc) == 0, intents[i]);
        char err[256];
        ASSERT(validate_schema(doc.root, err, sizeof(err)) == 0, intents[i]);
        nl_json_free(&doc);
    }
    PASS();
}

static void test_not_object(void) {
    TEST("root not object → fail");
    NlJsonDoc doc;
    nl_json_parse("[1,2,3]", &doc);
    char err[256];
    ASSERT(validate_schema(doc.root, err, sizeof(err)) != 0, "array should fail");
    nl_json_free(&doc);
    PASS();
}

int main(void) {
    printf("schema 验证测试\n===============\n\n");
    test_valid_json();
    test_missing_intent();
    test_missing_confidence();
    test_invalid_intent();
    test_all_12_intents();
    test_not_object();
    printf("\n结果: %d 通过 / %d 失败 / %d 总计\n", tests_passed, tests_failed, tests_run);
    return tests_failed > 0 ? 1 : 0;
}
