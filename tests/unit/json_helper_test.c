/*
 * json_helper 单元测试
 *
 * 测试 json_helper.h 的所有公开 API。
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* 简易测试宏（不依赖外部框架） */
static int tests_run = 0;
static int tests_passed = 0;
static int tests_failed = 0;

#define TEST(name) do { tests_run++; printf("  TEST %s ... ", name); } while(0)
#define PASS()     do { tests_passed++; printf("PASS\n"); } while(0)
#define FAIL(msg)  do { tests_failed++; printf("FAIL: %s\n", msg); } while(0)
#define ASSERT(cond, msg) do { if (!(cond)) { FAIL(msg); return; } } while(0)

/* 包含待测模块（直接编译源文件） */
#include "../../src/libnlc/json_helper.c"

/* ============================================================
 * 测试用例
 * ============================================================ */

static void test_parse_valid_json(void)
{
    TEST("parse valid JSON");
    NlJsonDoc doc;
    const char *json = "{\"name\":\"test\",\"count\":42,\"pi\":3.14,\"active\":true}";
    int rc = nl_json_parse(json, &doc);
    ASSERT(rc == 0, "parse should succeed");
    ASSERT(doc.root != NULL, "root should not be NULL");

    const char *name;
    ASSERT(nl_json_get_string(doc.root, "name", &name) == 0, "name should be string");
    ASSERT(strcmp(name, "test") == 0, "name should be 'test'");

    int count;
    ASSERT(nl_json_get_int(doc.root, "count", &count) == 0, "count should be int");
    ASSERT(count == 42, "count should be 42");

    double pi;
    ASSERT(nl_json_get_float(doc.root, "pi", &pi) == 0, "pi should be float");
    ASSERT(pi > 3.13 && pi < 3.15, "pi should be ~3.14");

    int active;
    ASSERT(nl_json_get_bool(doc.root, "active", &active) == 0, "active should be bool");
    ASSERT(active == 1, "active should be true");

    nl_json_free(&doc);
    PASS();
}

static void test_parse_invalid_json(void)
{
    TEST("parse invalid JSON");
    NlJsonDoc doc;
    const char *json = "{bad json";
    int rc = nl_json_parse(json, &doc);
    ASSERT(rc == -1, "parse should fail");
    ASSERT(strlen(doc.error_msg) > 0, "error_msg should not be empty");
    nl_json_free(&doc);
    PASS();
}

static void test_parse_empty(void)
{
    TEST("parse empty string");
    NlJsonDoc doc;
    int rc = nl_json_parse("", &doc);
    ASSERT(rc == -1, "parse empty should fail");
    PASS();
}

static void test_parse_null(void)
{
    TEST("parse NULL pointer");
    NlJsonDoc doc;
    int rc = nl_json_parse(NULL, &doc);
    ASSERT(rc == -1, "parse NULL should fail");
    PASS();
}

static void test_get_missing_field(void)
{
    TEST("get missing field");
    NlJsonDoc doc;
    nl_json_parse("{\"a\":1}", &doc);
    const char *s;
    ASSERT(nl_json_get_string(doc.root, "nonexistent", &s) == -1,
           "missing field should return -1");
    ASSERT(nl_json_get_int(doc.root, "nonexistent", NULL) == -1,
           "missing field with NULL out should return -1");
    nl_json_free(&doc);
    PASS();
}

static void test_get_wrong_type(void)
{
    TEST("get wrong type");
    NlJsonDoc doc;
    nl_json_parse("{\"val\":\"hello\"}", &doc);
    int i;
    ASSERT(nl_json_get_int(doc.root, "val", &i) == -1,
           "string as int should return -1");
    nl_json_free(&doc);
    PASS();
}

static void test_build_json(void)
{
    TEST("build JSON object");
    cJSON *obj = nl_json_new_object();
    ASSERT(obj != NULL, "new object should not be NULL");
    nl_json_add_string(obj, "name", "nl-os");
    nl_json_add_int(obj, "version", 1);
    nl_json_add_bool(obj, "ready", 0);

    char *printed = nl_json_print(obj);
    ASSERT(printed != NULL, "print should not be NULL");
    ASSERT(strstr(printed, "nl-os") != NULL, "should contain 'nl-os'");
    ASSERT(strstr(printed, "version") != NULL, "should contain 'version'");

    /* 验证可以重新解析 */
    NlJsonDoc doc;
    ASSERT(nl_json_parse(printed, &doc) == 0, "should re-parse printed JSON");
    const char *name;
    ASSERT(nl_json_get_string(doc.root, "name", &name) == 0, "name should exist");
    ASSERT(strcmp(name, "nl-os") == 0, "name should be 'nl-os'");

    free(printed);
    nl_json_free(&doc);
    cJSON_Delete(obj);
    PASS();
}

static void test_array_operations(void)
{
    TEST("array operations");
    cJSON *arr = nl_json_new_array();
    ASSERT(arr != NULL, "new array should not be NULL");

    cJSON *item1 = cJSON_CreateString("item1");
    cJSON *item2 = cJSON_CreateString("item2");
    cJSON_AddItemToArray(arr, item1);
    cJSON_AddItemToArray(arr, item2);

    ASSERT(nl_json_array_size(arr) == 2, "array size should be 2");

    cJSON *got = nl_json_array_get(arr, 0);
    ASSERT(got != NULL, "array[0] should exist");
    ASSERT(strcmp(cJSON_GetStringValue(got), "item1") == 0, "array[0] should be 'item1'");

    got = nl_json_array_get(arr, 1);
    ASSERT(got != NULL, "array[1] should exist");

    ASSERT(nl_json_array_get(arr, -1) == NULL, "array[-1] should be NULL");
    ASSERT(nl_json_array_get(arr, 99) == NULL, "array[99] should be NULL");
    ASSERT(nl_json_array_size(NULL) == 0, "NULL array size should be 0");

    cJSON_Delete(arr);
    PASS();
}

/* ============================================================
 * 入口
 * ============================================================ */

int main(void)
{
    printf("json_helper 单元测试\n");
    printf("====================\n\n");

    test_parse_valid_json();
    test_parse_invalid_json();
    test_parse_empty();
    test_parse_null();
    test_get_missing_field();
    test_get_wrong_type();
    test_build_json();
    test_array_operations();

    printf("\n====================\n");
    printf("结果: %d 通过 / %d 失败 / %d 总计\n",
           tests_passed, tests_failed, tests_run);

    return tests_failed > 0 ? 1 : 0;
}
