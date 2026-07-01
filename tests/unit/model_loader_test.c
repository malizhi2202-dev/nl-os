/*
 * model_loader 单元测试
 * 测试 stub 版本的模型加载 API
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

#include "../../src/libnlc/model_loader.c"

/* 创建一个临时的 GGUF 文件用于测试 */
static char *create_test_gguf(void) {
    const char *path = "/tmp/nl-test-model.gguf";
    FILE *f = fopen(path, "wb");
    if (!f) return NULL;
    /* 写入 GGUF 魔数 + 最小头 */
    fwrite("GGUF", 1, 4, f);
    /* 版本 (v3 = 3) */
    unsigned char version[4] = {3, 0, 0, 0};
    fwrite(version, 1, 4, f);
    /* 填充一些数据让文件有意义 */
    unsigned char dummy[64] = {0};
    fwrite(dummy, 1, sizeof(dummy), f);
    fclose(f);
    return strdup(path);
}

/* ============================================================
 * 测试用例
 * ============================================================ */

static void test_init_valid(void) {
    TEST("init valid GGUF file");
    char *path = create_test_gguf();
    ASSERT(path != NULL, "create test file");
    NlModel *m = nl_model_init(path);
    ASSERT(m != NULL, "should load valid GGUF");
    ASSERT(m->is_stub == 1, "should be stub mode");
    nl_model_free(m);
    unlink(path);
    free(path);
    PASS();
}

static void test_init_null(void) {
    TEST("init NULL path");
    NlModel *m = nl_model_init(NULL);
    ASSERT(m == NULL, "should return NULL for NULL path");
    PASS();
}

static void test_init_not_found(void) {
    TEST("init nonexistent file");
    NlModel *m = nl_model_init("/tmp/nl-nonexistent-model-12345.gguf");
    ASSERT(m == NULL, "should return NULL for missing file");
    PASS();
}

static void test_get_info(void) {
    TEST("get model info");
    char *path = create_test_gguf();
    NlModel *m = nl_model_init(path);
    ASSERT(m != NULL, "should load");

    const NlModelInfo *info = nl_model_get_info(m);
    ASSERT(info != NULL, "should return info");
    ASSERT(strcmp(info->arch, "stub") == 0, "arch should be stub");
    ASSERT(info->vocab_size == 32000, "vocab_size default");
    ASSERT(info->hidden_size == 2048, "hidden_size default");
    ASSERT(info->vocab_size > 0, "vocab_size > 0");
    ASSERT(strcmp(info->gguf_version, "v3") == 0, "gguf_version v3");
    ASSERT(info->model_size > 0, "model_size > 0");

    nl_model_free(m);
    unlink(path);
    free(path);
    PASS();
}

static void test_get_info_null(void) {
    TEST("get info NULL model");
    const NlModelInfo *info = nl_model_get_info(NULL);
    ASSERT(info == NULL, "should return NULL");
    PASS();
}

static void test_infer_stub(void) {
    TEST("stub inference");
    char *path = create_test_gguf();
    NlModel *m = nl_model_init(path);
    ASSERT(m != NULL, "should load");

    char output[256];
    int len = nl_model_infer(m, "hello", output, sizeof(output));
    ASSERT(len > 0, "should return positive length");
    ASSERT(strstr(output, "intent") != NULL, "should contain 'intent'");
    ASSERT(strstr(output, "confidence") != NULL, "should contain 'confidence'");

    nl_model_free(m);
    unlink(path);
    free(path);
    PASS();
}

static void test_infer_null(void) {
    TEST("infer NULL model");
    char buf[64];
    ASSERT(nl_model_infer(NULL, "test", buf, 64) == -1, "should return -1");
    PASS();
}

static void test_infer_small_buffer(void) {
    TEST("infer with small buffer");
    char *path = create_test_gguf();
    NlModel *m = nl_model_init(path);
    ASSERT(m != NULL, "should load");

    char tiny[8];
    int len = nl_model_infer(m, "test", tiny, sizeof(tiny));
    ASSERT(len > 0 && len < 8, "should truncate to buffer size");

    nl_model_free(m);
    unlink(path);
    free(path);
    PASS();
}

static void test_free_null(void) {
    TEST("free NULL model (no crash)");
    nl_model_free(NULL);
    PASS();
}

/* ============================================================
 * 入口
 * ============================================================ */
int main(void) {
    printf("model_loader 单元测试\n====================\n\n");

    test_init_valid();
    test_init_null();
    test_init_not_found();
    test_get_info();
    test_get_info_null();
    test_infer_stub();
    test_infer_null();
    test_infer_small_buffer();
    test_free_null();

    printf("\n====================\n");
    printf("结果: %d 通过 / %d 失败 / %d 总计\n", tests_passed, tests_failed, tests_run);
    return tests_failed > 0 ? 1 : 0;
}
