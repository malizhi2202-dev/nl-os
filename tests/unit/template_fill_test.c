/*
 * template_fill 单元测试
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int tests_run = 0, tests_passed = 0, tests_failed = 0;
#define TEST(n)  do { tests_run++; printf("  TEST %s ... ", n); } while(0)
#define PASS()   do { tests_passed++; printf("PASS\n"); } while(0)
#define FAIL(m)  do { tests_failed++; printf("FAIL: %s\n", m); return; } while(0)
#define ASSERT(c,m) do { if (!(c)) { FAIL(m); return; } } while(0)

#include "../../src/libnlc/template_fill.c"

/* 辅助：创建带一个槽位的数组 */
static NlSlot make_slot(const char *name, const char *value) {
    NlSlot s = {strdup(name), strdup(value)};
    return s;
}

static void free_slots(NlSlot *slots, int n) {
    for (int i = 0; i < n; i++) { free(slots[i].name); free(slots[i].value); }
}

/* ============================================================
 * create_dir
 * ============================================================ */
static void test_create_dir(void) {
    TEST("mkdir"); NlCommand cmd;
    NlSlot s[] = {make_slot("name", "test")};
    ASSERT(nl_template_fill(NL_INTENT_CREATE_DIR, 1, s, 1, &cmd) == 0, "fill");
    ASSERT(strcmp(cmd.command, "mkdir -p test") == 0, "command");
    ASSERT(cmd.perm_level == NL_PERM_L2, "L2");
    ASSERT(cmd.needs_confirm == 1, "needs confirm");
    nl_command_free(&cmd); free_slots(s, 1);
    PASS();
}

/* ============================================================
 * create_file
 * ============================================================ */
static void test_create_file(void) {
    TEST("touch"); NlCommand cmd;
    NlSlot s[] = {make_slot("name", "main.c")};
    ASSERT(nl_template_fill(NL_INTENT_CREATE_FILE, 2, s, 1, &cmd) == 0, "fill");
    ASSERT(strcmp(cmd.command, "touch main.c") == 0, "command");
    ASSERT(cmd.perm_level == NL_PERM_L2, "L2");
    nl_command_free(&cmd); free_slots(s, 1);
    PASS();
}

/* ============================================================
 * delete → L3
 * ============================================================ */
static void test_delete(void) {
    TEST("rm → L3"); NlCommand cmd;
    NlSlot s[] = {make_slot("target", "/tmp/test")};
    ASSERT(nl_template_fill(NL_INTENT_DELETE, 3, s, 1, &cmd) == 0, "fill");
    ASSERT(strstr(cmd.command, "rm -rf") != NULL, "rm -rf");
    ASSERT(cmd.perm_level == NL_PERM_L3, "L3");
    ASSERT(cmd.path_count > 0, "has paths");
    nl_command_free(&cmd); free_slots(s, 1);
    PASS();
}

/* ============================================================
 * move / copy
 * ============================================================ */
static void test_move(void) {
    TEST("mv"); NlCommand cmd;
    NlSlot s[] = {make_slot("source", "a.txt"), make_slot("dest", "/tmp/b.txt")};
    ASSERT(nl_template_fill(NL_INTENT_MOVE, 4, s, 2, &cmd) == 0, "fill");
    ASSERT(strstr(cmd.command, "mv") != NULL, "mv");
    ASSERT(cmd.path_count >= 2, "2 paths");
    nl_command_free(&cmd); free_slots(s, 2);
    PASS();
}

static void test_copy(void) {
    TEST("cp"); NlCommand cmd;
    NlSlot s[] = {make_slot("source", "src/"), make_slot("dest", "/backup/")};
    ASSERT(nl_template_fill(NL_INTENT_COPY, 5, s, 2, &cmd) == 0, "fill");
    ASSERT(strstr(cmd.command, "cp") != NULL, "cp");
    nl_command_free(&cmd); free_slots(s, 2);
    PASS();
}

/* ============================================================
 * list_dir → L1
 * ============================================================ */
static void test_list_dir(void) {
    TEST("ls → L1"); NlCommand cmd;
    ASSERT(nl_template_fill(NL_INTENT_LIST_DIR, 6, NULL, 0, &cmd) == 0, "fill");
    ASSERT(strstr(cmd.command, "ls") != NULL, "ls");
    ASSERT(cmd.perm_level == NL_PERM_L1, "L1");
    ASSERT(cmd.needs_confirm == 0, "no confirm");
    nl_command_free(&cmd);
    PASS();
}

/* ============================================================
 * show_process / search_content
 * ============================================================ */
static void test_show_process(void) {
    TEST("ps aux → L1"); NlCommand cmd;
    ASSERT(nl_template_fill(NL_INTENT_SHOW_PROCESS, 10, NULL, 0, &cmd) == 0, "fill");
    ASSERT(strstr(cmd.command, "ps") != NULL, "ps");
    ASSERT(cmd.perm_level == NL_PERM_L1, "L1");
    nl_command_free(&cmd);
    PASS();
}

static void test_search_content(void) {
    TEST("grep"); NlCommand cmd;
    NlSlot s[] = {make_slot("pattern", "TODO")};
    ASSERT(nl_template_fill(NL_INTENT_SEARCH_CONTENT, 11, s, 1, &cmd) == 0, "fill");
    ASSERT(strstr(cmd.command, "grep") != NULL, "grep");
    ASSERT(strstr(cmd.command, "TODO") != NULL, "contains pattern");
    nl_command_free(&cmd); free_slots(s, 1);
    PASS();
}

/* ============================================================
 * change_dir
 * ============================================================ */
static void test_change_dir(void) {
    TEST("cd"); NlCommand cmd;
    NlSlot s[] = {make_slot("location", "/tmp")};
    ASSERT(nl_template_fill(NL_INTENT_CHANGE_DIR, 9, s, 1, &cmd) == 0, "fill");
    ASSERT(strcmp(cmd.command, "cd /tmp") == 0, "command");
    nl_command_free(&cmd); free_slots(s, 1);
    PASS();
}

/* ============================================================
 * run_cmd（透传）
 * ============================================================ */
static void test_run_cmd(void) {
    TEST("透传命令"); NlCommand cmd;
    NlSlot s[] = {make_slot("command", "./configure --prefix=/usr")};
    ASSERT(nl_template_fill(NL_INTENT_RUN_CMD, 12, s, 1, &cmd) == 0, "fill");
    ASSERT(strstr(cmd.command, "./configure") != NULL, "configure");
    nl_command_free(&cmd); free_slots(s, 1);
    PASS();
}

/* ============================================================
 * 边界条件
 * ============================================================ */
static void test_null_result(void) {
    TEST("NULL result → -1");
    ASSERT(nl_template_fill(NL_INTENT_CREATE_DIR, 1, NULL, 0, NULL) == -1, "no crash");
    PASS();
}

static void test_bad_intent(void) {
    TEST("bad intent → -1"); NlCommand cmd;
    ASSERT(nl_template_fill(999, 0, NULL, 0, &cmd) == -1, "not found");
    nl_command_free(&cmd);
    PASS();
}

static void test_empty_slot(void) {
    TEST("empty slot → {slot} removed"); NlCommand cmd;
    ASSERT(nl_template_fill(NL_INTENT_CREATE_DIR, 1, NULL, 0, &cmd) == 0, "fill");
    /* 槽位缺失时 {name} 被移除，留下 "mkdir -p " */
    ASSERT(strstr(cmd.command, "mkdir") != NULL, "still mkdir");
    nl_command_free(&cmd);
    PASS();
}

static void test_perm_level_lookup(void) {
    TEST("perm level lookup");
    ASSERT(nl_intent_perm_level(NL_INTENT_LIST_DIR) == NL_PERM_L1, "list_dir=L1");
    ASSERT(nl_intent_perm_level(NL_INTENT_DELETE) == NL_PERM_L3, "delete=L3");
    ASSERT(nl_intent_perm_level(NL_INTENT_CREATE_DIR) == NL_PERM_L2, "create_dir=L2");
    PASS();
}


/* ============================================================
 * 槽位驱动选模板（ADR-024）
 *
 * 旧语义 template_id==0 = 该 intent 的第一张模板，会把模型提取到的实体丢掉
 * （说 list_dir + location=/var/log，结果还是裸 ls -la）。这里固定新语义。
 * ============================================================ */
static void test_pick_by_slots(void) {
    TEST("模型层：实体能选中更具体的模板");
    NlSlot s[2];
    NlCommand cmd;

    /* 给 location 就该选 601（ls -la {location}），而不是 6（ls -la） */
    s[0].name = strdup("location"); s[0].value = strdup("/var/log");
    ASSERT(nl_template_fill(NL_INTENT_LIST_DIR, 0, s, 1, &cmd) == 0, "fill 601");
    ASSERT(strcmp(cmd.command, "ls -la /var/log") == 0, "路径进了命令");
    nl_command_free(&cmd);

    /* 不给槽位 → 退到不需要槽位的那张 */
    ASSERT(nl_template_fill(NL_INTENT_LIST_DIR, 0, NULL, 0, &cmd) == 0, "fill 6");
    ASSERT(strcmp(cmd.command, "ls -la") == 0, "无槽位用通用模板");
    nl_command_free(&cmd);

    /* view_file：给了 target 才可能生成 cat */
    free(s[0].name); free(s[0].value);
    s[0].name = strdup("target"); s[0].value = strdup("a.log");
    ASSERT(nl_template_fill(NL_INTENT_VIEW_FILE, 0, s, 1, &cmd) == 0, "fill cat");
    ASSERT(strcmp(cmd.command, "cat a.log") == 0, "文件名进了命令");
    nl_command_free(&cmd);
    free(s[0].name); free(s[0].value);
    PASS();
}

static void test_pick_network(void) {
    TEST("网络模板：域名槽位是唯一自由参数");
    NlCommand cmd;
    NlSlot s[1];

    /* 无域名 → dig 模板不可满足，退到不参数的 ip */
    ASSERT(nl_template_fill(NL_INTENT_SHOW_NETWORK, 0, NULL, 0, &cmd) == 0, "fill net");
    ASSERT(strcmp(cmd.command, "ip -br addr") == 0, "无参数模板是 ip");
    ASSERT(cmd.perm_level == NL_PERM_L1, "L1");
    nl_command_free(&cmd);

    s[0].name = strdup("domain"); s[0].value = strdup("www.baidu.com");
    ASSERT(nl_template_fill(NL_INTENT_SHOW_NETWORK, 0, s, 1, &cmd) == 0, "fill dig");
    ASSERT(strcmp(cmd.command, "dig +short www.baidu.com") == 0, "域名进了命令");
    nl_command_free(&cmd);

    /* 非法域名：整条失败，绝不生成 `dig +short -f /etc/passwd` 这种东西 */
    free(s[0].value);
    s[0].value = strdup("-f");
    ASSERT(nl_template_fill(NL_INTENT_SHOW_NETWORK, 0, s, 1, &cmd) == -1, "非法域名拒绝");
    free(s[0].value);
    s[0].value = strdup("example.com;rm -rf /");
    ASSERT(nl_template_fill(NL_INTENT_SHOW_NETWORK, 0, s, 1, &cmd) == -1, "元字符域名拒绝");
    free(s[0].name); free(s[0].value);
    PASS();
}

static void test_other_not_auto_picked(void) {
    TEST("other 不参与自动选模板（否则泛规则会静默拿到命令）");
    NlCommand cmd;
    ASSERT(nl_template_fill(NL_INTENT_OTHER, 0, NULL, 0, &cmd) == -1, "other 无自动模板");
    PASS();
}

/* ============================================================
 * 入口
 * ============================================================ */
int main(void) {
    printf("template_fill 单元测试\n======================\n\n");
    test_create_dir();
    test_create_file();
    test_delete();
    test_move();
    test_copy();
    test_list_dir();
    test_show_process();
    test_search_content();
    test_change_dir();
    test_run_cmd();
    printf("\n--- 槽位驱动选模板 ---\n");
    test_pick_by_slots();
    test_pick_network();
    test_other_not_auto_picked();
    printf("\n--- 边界条件 ---\n");
    test_null_result();
    test_bad_intent();
    test_empty_slot();
    test_perm_level_lookup();
    printf("\n====================\n");
    printf("结果: %d 通过 / %d 失败 / %d 总计\n", tests_passed, tests_failed, tests_run);
    return tests_failed > 0 ? 1 : 0;
}
