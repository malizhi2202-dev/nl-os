/*
 * nlc_api 单元测试
 * 测试四层管线组装 + Agent 扩展点
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

#include "../../src/libnlc/nlc_api.h"

static char *create_test_gguf(void) {
    const char *path = "/tmp/nl-test-api.gguf";
    FILE *f = fopen(path, "wb");
    if (!f) return NULL;
    fwrite("GGUF",1,4,f); unsigned char v[4]={3,0,0,0}; fwrite(v,1,4,f);
    unsigned char d[64]={0}; fwrite(d,1,64,f); fclose(f);
    return strdup(path);
}

static void test_init_free(void) {
    TEST("init + free");
    char *p = create_test_gguf();
    ASSERT(nl_init(p) == 0, "init");
    nl_free();
    unlink(p); free(p);
    PASS();
}

static void test_parse_rule_hit(void) {
    TEST("parse → rule engine hit");
    char *p = create_test_gguf();
    ASSERT(nl_init(p) == 0, "init");
    NlParseResult r;
    ASSERT(nl_parse("创建一个叫 test 的文件夹", &r) == 0, "parse");
    ASSERT(r.intent == NL_INTENT_CREATE_DIR, "create_dir");
    ASSERT(r.source == NL_SOURCE_RULE, "from rule");
    ASSERT(r.confidence == 1.0f, "conf=1.0");
    ASSERT(r.command != NULL, "has command");
    ASSERT(strstr(r.command, "mkdir") != NULL, "mkdir");
    ASSERT(strstr(r.debug_info, "规则引擎") != NULL, "debug has rule info");
    nl_parse_result_free(&r); nl_free();
    unlink(p); free(p);
    PASS();
}

static void test_parse_model_fallback(void) {
    TEST("parse → model fallback (no rule hit)");
    char *p = create_test_gguf();
    ASSERT(nl_init(p) == 0, "init");
    NlParseResult r;
    ASSERT(nl_parse("把那个东西处理一下", &r) == 0, "parse");
    ASSERT(r.source == NL_SOURCE_MODEL, "from model");
    ASSERT(r.command != NULL, "has command");
    ASSERT(strstr(r.debug_info, "模型推理") != NULL, "debug mentions model");
    nl_parse_result_free(&r); nl_free();
    unlink(p); free(p);
    PASS();
}

static void test_agent_extensions(void) {
    TEST("agent plan/execute/observe");
    NlPlan plan;
    ASSERT(nl_plan("测试目标", &plan) == 0, "plan");
    ASSERT(plan.count == 1, "MVP returns 1 step");

    NlExecResult er;
    ASSERT(nl_execute_step(&plan.steps[0], &er) == 0, "execute");
    ASSERT(er.done == 1, "done");

    NlObserveResult obs;
    ASSERT(nl_observe(&er, &obs) == 0, "observe");
    ASSERT(obs.status == 0, "status=done");

    nl_observe_result_free(&obs);
    nl_exec_result_free(&er);
    nl_plan_free(&plan);
    PASS();
}

static void test_no_model_error(void) {
    TEST("parse without model → error");
    nl_free();
    NlParseResult r;
    int rc = nl_parse("把那个东西处理一下", &r);
    ASSERT(rc == -1, "should fail without model");
    ASSERT(strstr(r.debug_info, "模型未加载") != NULL, "error message");
    nl_parse_result_free(&r);
    PASS();
}

int main(void) {
    printf("nlc_api 单元测试\n================\n\n");
    test_init_free();
    test_parse_rule_hit();
    test_parse_model_fallback();
    test_agent_extensions();
    test_no_model_error();
    printf("\n结果: %d 通过 / %d 失败 / %d 总计\n", tests_passed, tests_failed, tests_run);
    return tests_failed > 0 ? 1 : 0;
}
