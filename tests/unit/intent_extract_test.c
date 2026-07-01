/*
 * intent_extract 单元测试
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static int tests_run = 0, tests_passed = 0, tests_failed = 0;
#define TEST(n)  do { tests_run++; printf("  TEST %s ... ", n); } while(0)
#define PASS()   do { tests_passed++; printf("PASS\n"); } while(0)
#define FAIL(m)  do { tests_failed++; printf("FAIL: %s\n", m); return; } while(0)
#define ASSERT(c,m) do { if (!(c)) { FAIL(m); return; } } while(0)

/* 链接 nlc 库，使用头文件 */
#include "../../src/libnlc/intent_extract.h"
#include "../../src/libnlc/model_loader.h"
#include "../../src/libnlc/few_shot.h"

/* 创建临时 GGUF 文件 */
static char *create_test_gguf(void) {
    const char *path = "/tmp/nl-test-model-intent.gguf";
    FILE *f = fopen(path, "wb");
    if (!f) return NULL;
    fwrite("GGUF", 1, 4, f);
    unsigned char v[4] = {3,0,0,0}; fwrite(v,1,4,f);
    unsigned char d[64] = {0}; fwrite(d,1,64,f);
    fclose(f);
    return strdup(path);
}

static void test_extract_stub_model(void) {
    TEST("extract from stub model");
    char *path = create_test_gguf();
    NlModel *m = nl_model_init(path); if(!m) { printf("SKIP (no model)\n"); tests_run--; unlink(path); free(path); return; }
    ASSERT(m != NULL, "init model");

    NlIntentResult r;
    ASSERT(nl_intent_extract(m, "创建一个 test 文件夹", &r) == 0, "extract");
    ASSERT(r.status == NL_INFER_OK, "OK status");
    ASSERT(r.intent == NL_INTENT_CREATE_DIR, "create_dir");
    ASSERT(r.confidence > 0.6f, "confidence OK");
    ASSERT(r.slot_count >= 1, "has slots");

    nl_intent_result_free(&r);
    nl_model_free(m);
    unlink(path); free(path);
    PASS();
}

static void test_extract_null(void) {
    TEST("NULL input → -1"); NlIntentResult r;
    ASSERT(nl_intent_extract(NULL, "test", &r) == -1, "null model");
    ASSERT(nl_intent_extract((NlModel*)0x1, NULL, &r) == -1, "null input");
    PASS();
}

static void test_intent_names(void) {
    TEST("intent names");
    ASSERT(strcmp(nl_intent_name(NL_INTENT_CREATE_DIR), "create_dir") == 0, "create_dir");
    ASSERT(strcmp(nl_intent_name(NL_INTENT_DELETE), "delete") == 0, "delete");
    ASSERT(strcmp(nl_intent_name(NL_INTENT_OTHER), "other") == 0, "other");
    PASS();
}

static void test_few_shot_not_empty(void) {
    TEST("few-shot not empty");
    ASSERT(strlen(nl_few_shot_examples()) > 50, "has examples");
    ASSERT(strlen(nl_system_prompt()) > 100, "system prompt");
    PASS();
}

int main(void) {
    printf("intent_extract 单元测试\n=======================\n\n");
    test_extract_stub_model();
    test_extract_null();
    test_intent_names();
    test_few_shot_not_empty();
    printf("\n结果: %d 通过 / %d 失败 / %d 总计\n", tests_passed, tests_failed, tests_run);
    return tests_failed > 0 ? 1 : 0;
}
