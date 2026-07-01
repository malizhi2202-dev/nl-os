/*
 * rule_engine 单元测试
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int tests_run = 0, tests_passed = 0, tests_failed = 0;
#define TEST(n)  do { tests_run++; printf("  TEST %s ... ", n); } while(0)
#define PASS()   do { tests_passed++; printf("PASS\n"); } while(0)
#define FAIL(m)  do { tests_failed++; printf("FAIL: %s\n", m); return; } while(0)
#define ASSERT(c,m) do { if (!(c)) { FAIL(m); return; } } while(0)

#include "../../src/libnlc/rule_engine.c"

/* ============================================================
 * create_dir
 * ============================================================ */
static void test_create_dir_1(void) {
    TEST("创建文件夹"); NlRuleResult r;
    ASSERT(nl_rule_match("创建一个叫 test 的文件夹", &r) == 0, "should match");
    ASSERT(r.intent == NL_INTENT_CREATE_DIR, "intent=create_dir");
    ASSERT(r.confidence == 1.0f, "confidence=1.0");
    nl_rule_result_free(&r); PASS();
}

static void test_create_dir_2(void) {
    TEST("新建目录"); NlRuleResult r;
    ASSERT(nl_rule_match("新建一个叫 src 的目录", &r) == 0, "should match");
    ASSERT(r.intent == NL_INTENT_CREATE_DIR, "intent=create_dir");
    nl_rule_result_free(&r); PASS();
}

static void test_create_dir_mkdir(void) {
    TEST("mkdir test"); NlRuleResult r;
    ASSERT(nl_rule_match("mkdir test", &r) == 0, "should match");
    ASSERT(r.intent == NL_INTENT_CREATE_DIR, "intent=create_dir");
    nl_rule_result_free(&r); PASS();
}

/* ============================================================
 * create_file
 * ============================================================ */
static void test_create_file(void) {
    TEST("创建文件"); NlRuleResult r;
    ASSERT(nl_rule_match("创建一个叫 README.md 的文件", &r) == 0, "should match");
    ASSERT(r.intent == NL_INTENT_CREATE_FILE, "intent=create_file");
    nl_rule_result_free(&r); PASS();
}

static void test_touch(void) {
    TEST("touch file"); NlRuleResult r;
    ASSERT(nl_rule_match("touch config.json", &r) == 0, "should match");
    ASSERT(r.intent == NL_INTENT_CREATE_FILE, "intent=create_file");
    nl_rule_result_free(&r); PASS();
}

/* ============================================================
 * delete
 * ============================================================ */
static void test_delete(void) {
    TEST("删除目录"); NlRuleResult r;
    ASSERT(nl_rule_match("删除 test 目录", &r) == 0, "should match");
    ASSERT(r.intent == NL_INTENT_DELETE, "intent=delete");
    nl_rule_result_free(&r); PASS();
}

static void test_rm(void) {
    TEST("rm file"); NlRuleResult r;
    ASSERT(nl_rule_match("rm file.txt", &r) == 0, "should match");
    ASSERT(r.intent == NL_INTENT_DELETE, "intent=delete");
    nl_rule_result_free(&r); PASS();
}

static void test_delete_clean(void) {
    TEST("清理目录"); NlRuleResult r;
    ASSERT(nl_rule_match("清理 /tmp 目录", &r) == 0, "should match");
    ASSERT(r.intent == NL_INTENT_DELETE, "intent=delete");
    nl_rule_result_free(&r); PASS();
}

/* ============================================================
 * move / copy
 * ============================================================ */
static void test_move(void) {
    TEST("移动文件"); NlRuleResult r;
    ASSERT(nl_rule_match("移动 file.txt 到 /tmp", &r) == 0, "should match");
    ASSERT(r.intent == NL_INTENT_MOVE, "intent=move");
    nl_rule_result_free(&r); PASS();
}

static void test_copy(void) {
    TEST("复制文件"); NlRuleResult r;
    ASSERT(nl_rule_match("复制 file.txt 到 /backup", &r) == 0, "should match");
    ASSERT(r.intent == NL_INTENT_COPY, "intent=copy");
    nl_rule_result_free(&r); PASS();
}

/* ============================================================
 * list_dir
 * ============================================================ */
static void test_list_dir(void) {
    TEST("列出文件"); NlRuleResult r;
    ASSERT(nl_rule_match("列出当前目录的文件", &r) == 0, "should match");
    ASSERT(r.intent == NL_INTENT_LIST_DIR, "intent=list_dir");
    nl_rule_result_free(&r); PASS();
}

static void test_ls(void) {
    TEST("ls"); NlRuleResult r;
    ASSERT(nl_rule_match("ls", &r) == 0, "should match");
    ASSERT(r.intent == NL_INTENT_LIST_DIR, "intent=list_dir");
    nl_rule_result_free(&r); PASS();
}

/* ============================================================
 * view_file
 * ============================================================ */
static void test_view_file(void) {
    TEST("查看文件"); NlRuleResult r;
    ASSERT(nl_rule_match("查看 README.md 文件", &r) == 0, "should match");
    ASSERT(r.intent == NL_INTENT_VIEW_FILE, "intent=view_file");
    nl_rule_result_free(&r); PASS();
}

/* ============================================================
 * find_files
 * ============================================================ */
static void test_find_files(void) {
    TEST("查找文件"); NlRuleResult r;
    ASSERT(nl_rule_match("查找所有 python 文件", &r) == 0, "should match");
    ASSERT(r.intent == NL_INTENT_FIND_FILES, "intent=find_files");
    nl_rule_result_free(&r); PASS();
}

/* ============================================================
 * change_dir
 * ============================================================ */
static void test_change_dir(void) {
    TEST("进入目录"); NlRuleResult r;
    ASSERT(nl_rule_match("进入 /home/user 目录", &r) == 0, "should match");
    ASSERT(r.intent == NL_INTENT_CHANGE_DIR, "intent=change_dir");
    nl_rule_result_free(&r); PASS();
}

static void test_cd(void) {
    TEST("cd"); NlRuleResult r;
    ASSERT(nl_rule_match("cd /tmp", &r) == 0, "should match");
    ASSERT(r.intent == NL_INTENT_CHANGE_DIR, "intent=change_dir");
    nl_rule_result_free(&r); PASS();
}

/* ============================================================
 * show_process
 * ============================================================ */
static void test_show_process(void) {
    TEST("查看进程"); NlRuleResult r;
    ASSERT(nl_rule_match("查看进程列表", &r) == 0, "should match");
    ASSERT(r.intent == NL_INTENT_SHOW_PROCESS, "intent=show_process");
    nl_rule_result_free(&r); PASS();
}

static void test_system_load(void) {
    TEST("系统负载"); NlRuleResult r;
    ASSERT(nl_rule_match("查看系统负载", &r) == 0, "should match");
    ASSERT(r.intent == NL_INTENT_SHOW_PROCESS, "intent=show_process");
    nl_rule_result_free(&r); PASS();
}

/* ============================================================
 * search_content
 * ============================================================ */
static void test_grep(void) {
    TEST("grep"); NlRuleResult r;
    ASSERT(nl_rule_match("grep error *.log", &r) == 0, "should match");
    ASSERT(r.intent == NL_INTENT_SEARCH_CONTENT, "intent=search_content");
    nl_rule_result_free(&r); PASS();
}

/* ============================================================
 * run_cmd
 * ============================================================ */
static void test_make(void) {
    TEST("make"); NlRuleResult r;
    ASSERT(nl_rule_match("make", &r) == 0, "should match");
    ASSERT(r.intent == NL_INTENT_RUN_CMD, "intent=run_cmd");
    nl_rule_result_free(&r); PASS();
}

static void test_restart(void) {
    TEST("重启服务"); NlRuleResult r;
    ASSERT(nl_rule_match("重启 nginx 服务", &r) == 0, "should match");
    ASSERT(r.intent == NL_INTENT_RUN_CMD, "intent=run_cmd");
    nl_rule_result_free(&r); PASS();
}

/* ============================================================
 * 未命中
 * ============================================================ */
static void test_no_match(void) {
    TEST("未命中 → 返回 -1"); NlRuleResult r;
    ASSERT(nl_rule_match("把那个东西弄一下", &r) == -1, "should not match");
    ASSERT(r.matched == 0, "matched=0");
    PASS();
}

static void test_empty_input(void) {
    TEST("空输入"); NlRuleResult r;
    ASSERT(nl_rule_match("", &r) == -1, "empty should not match");
    PASS();
}

static void test_null_input(void) {
    TEST("NULL 输入"); NlRuleResult r;
    ASSERT(nl_rule_match(NULL, &r) == -1, "NULL should not match");
    PASS();
}

/* ============================================================
 * 入口
 * ============================================================ */
int main(void) {
    printf("rule_engine 单元测试\n====================\n\n");

    printf("--- create_dir ---\n");
    test_create_dir_1(); test_create_dir_2(); test_create_dir_mkdir();
    printf("\n--- create_file ---\n");
    test_create_file(); test_touch();
    printf("\n--- delete ---\n");
    test_delete(); test_rm(); test_delete_clean();
    printf("\n--- move/copy ---\n");
    test_move(); test_copy();
    printf("\n--- list_dir ---\n");
    test_list_dir(); test_ls();
    printf("\n--- view_file ---\n");
    test_view_file();
    printf("\n--- find_files ---\n");
    test_find_files();
    printf("\n--- change_dir ---\n");
    test_change_dir(); test_cd();
    printf("\n--- show_process ---\n");
    test_show_process(); test_system_load();
    printf("\n--- search_content ---\n");
    test_grep();
    printf("\n--- run_cmd ---\n");
    test_make(); test_restart();
    printf("\n--- 边界条件 ---\n");
    test_no_match(); test_empty_input(); test_null_input();

    printf("\n====================\n");
    printf("结果: %d 通过 / %d 失败 / %d 总计\n", tests_passed, tests_failed, tests_run);
    return tests_failed > 0 ? 1 : 0;
}
