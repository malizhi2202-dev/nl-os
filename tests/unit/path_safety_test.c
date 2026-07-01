/*
 * path_safety 单元测试
 * 覆盖: 路径规范化/白名单/黑名单/符号链接/../遍历/Unicode 净化
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>

/* 简易测试框架 */
static int tests_run = 0, tests_passed = 0, tests_failed = 0;
#define TEST(n)  do { tests_run++; printf("  TEST %s ... ", n); } while(0)
#define PASS()   do { tests_passed++; printf("PASS\n"); } while(0)
#define FAIL(m)  do { tests_failed++; printf("FAIL: %s\n", m); return; } while(0)
#define ASSERT(c,m) do { if (!(c)) { FAIL(m); return; } } while(0)

#include "../../src/libnlc/path_safety.c"

/* ============================================================
 * 路径规范化
 * ============================================================ */
static void test_normalize_simple(void) {
    TEST("normalize /tmp");
    NlPathResult r = nl_path_check("/tmp", 0);
    ASSERT(r.status == NL_PATH_PASS, "should pass");
    ASSERT(strcmp(r.normalized_path, "/tmp") == 0, "should be /tmp");
    nl_path_result_free(&r);
    PASS();
}

static void test_normalize_home(void) {
    TEST("normalize ~/");
    NlPathResult r = nl_path_check("~/", 0);
    ASSERT(r.status == NL_PATH_PASS, "should pass for home");
    /* normalized should start with /home/ or /root */
    ASSERT(strncmp(r.normalized_path, "/", 1) == 0, "should be absolute");
    nl_path_result_free(&r);
    PASS();
}

static void test_normalize_dotdot(void) {
    TEST("normalize /tmp/../etc → /etc → L4 reject");
    NlPathResult r = nl_path_check("/tmp/../etc", 1);
    ASSERT(r.status == NL_PATH_REJECT_L4, "should reject /etc via ../ (write)");
    ASSERT(strcmp(r.normalized_path, "/etc") == 0, "should normalize to /etc");
    nl_path_result_free(&r);
    PASS();
}

/* ============================================================
 * 白名单
 * ============================================================ */
static void test_whitelist_home_write(void) {
    TEST("whitelist /home/user write");
    NlPathResult r = nl_path_check("/home/user/projects", 1);
    ASSERT(r.status == NL_PATH_PASS, "should allow /home write");
    nl_path_result_free(&r);
    PASS();
}

static void test_whitelist_tmp_write(void) {
    TEST("whitelist /tmp write");
    NlPathResult r = nl_path_check("/tmp/test", 1);
    ASSERT(r.status == NL_PATH_PASS, "should allow /tmp write");
    nl_path_result_free(&r);
    PASS();
}

static void test_whitelist_var_write(void) {
    TEST("whitelist /var/tmp write");
    NlPathResult r = nl_path_check("/var/tmp/cache", 1);
    ASSERT(r.status == NL_PATH_PASS, "should allow /var/tmp write");
    nl_path_result_free(&r);
    PASS();
}

static void test_whitelist_etc_read(void) {
    TEST("read /etc/hosts (allowed even w/o whitelist)");
    NlPathResult r = nl_path_check("/etc/hosts", 0);
    ASSERT(r.status == NL_PATH_PASS, "should allow read of /etc/hosts");
    nl_path_result_free(&r);
    PASS();
}

static void test_whitelist_etc_write_rejected(void) {
    TEST("reject write to /etc (blacklist L4)");
    NlPathResult r = nl_path_check("/etc/hosts", 1);
    ASSERT(r.status == NL_PATH_REJECT_L4, "write to /etc should be L4 blacklist");
    nl_path_result_free(&r);
    PASS();
}

static void test_whitelist_unknown_write_rejected(void) {
    TEST("reject write to /unknown");
    NlPathResult r = nl_path_check("/unknown/dir", 1);
    ASSERT(r.status == NL_PATH_REJECT_WHITE, "should reject /unknown write");
    nl_path_result_free(&r);
    PASS();
}

/* ============================================================
 * 黑名单
 * ============================================================ */
static void test_blacklist_root(void) {
    TEST("blacklist / write → L4");
    NlPathResult r = nl_path_check("/", 1);
    ASSERT(r.status == NL_PATH_REJECT_L4, "write to / should be L4");
    nl_path_result_free(&r);
    PASS();
}

static void test_blacklist_etc(void) {
    TEST("blacklist /etc write → L4, read → pass");
    NlPathResult r = nl_path_check("/etc", 1);
    ASSERT(r.status == NL_PATH_REJECT_L4, "write to /etc should be L4");
    nl_path_result_free(&r);
    /* 读操作应通过 */
    r = nl_path_check("/etc/hosts", 0);
    ASSERT(r.status == NL_PATH_PASS, "read /etc/hosts should pass");
    nl_path_result_free(&r);
    PASS();
}

static void test_blacklist_boot(void) {
    TEST("blacklist /boot write");
    NlPathResult r = nl_path_check("/boot", 1);
    ASSERT(r.status == NL_PATH_REJECT_L4, "write to /boot should be L4");
    nl_path_result_free(&r);
    PASS();
}

static void test_blacklist_passwd(void) {
    TEST("blacklist /etc/passwd write");
    NlPathResult r = nl_path_check("/etc/passwd", 1);
    ASSERT(r.status == NL_PATH_REJECT_L4, "write to /etc/passwd should be L4");
    nl_path_result_free(&r);
    PASS();
}

static void test_blacklist_shadow(void) {
    TEST("blacklist /etc/shadow write");
    NlPathResult r = nl_path_check("/etc/shadow", 1);
    ASSERT(r.status == NL_PATH_REJECT_L4, "write to /etc/shadow should be L4");
    nl_path_result_free(&r);
    PASS();
}

static void test_blacklist_dev_sda(void) {
    TEST("blacklist /dev/sda write");
    NlPathResult r = nl_path_check("/dev/sda", 1);
    ASSERT(r.status == NL_PATH_REJECT_L4, "write to /dev/sda should be L4");
    nl_path_result_free(&r);
    PASS();
}

static void test_blacklist_dev_nvme(void) {
    TEST("blacklist /dev/nvme0n1 write");
    NlPathResult r = nl_path_check("/dev/nvme0n1", 1);
    ASSERT(r.status == NL_PATH_REJECT_L4, "write to /dev/nvme should be L4");
    nl_path_result_free(&r);
    PASS();
}

/* ============================================================
 * 符号链接
 * ============================================================ */
static void test_symlink_to_etc(void) {
    TEST("symlink /tmp/link → /etc");
    /* 创建临时符号链接 */
    unlink("/tmp/nl-test-link");
    int rc = symlink("/etc", "/tmp/nl-test-link");
    if (rc != 0) { printf("SKIP (cannot create symlink)\n"); tests_run--; return; }

    NlPathResult r = nl_path_check("/tmp/nl-test-link", 1);
    ASSERT(r.status == NL_PATH_REJECT_L4, "should reject write to symlink→/etc");
    unlink("/tmp/nl-test-link");
    nl_path_result_free(&r);
    PASS();
}

/* ============================================================
 * 路径提取
 * ============================================================ */
static void test_extract_paths(void) {
    TEST("extract paths from NL input");
    char **paths = nl_extract_paths("删除 /tmp/test 和 ~/backup 目录");
    ASSERT(paths != NULL, "should extract paths");
    ASSERT(paths[0] != NULL, "should have at least 1 path");
    int found_tmp = 0, found_home = 0;
    for (int i = 0; paths[i]; i++) {
        if (strcmp(paths[i], "/tmp/test") == 0) found_tmp = 1;
        if (strcmp(paths[i], "~/backup") == 0) found_home = 1;
    }
    ASSERT(found_tmp, "should find /tmp/test");
    ASSERT(found_home, "should find ~/backup");
    nl_free_paths(paths);
    PASS();
}

/* ============================================================
 * 输入净化
 * ============================================================ */
static void test_sanitize_clean_input(void) {
    TEST("sanitize clean input");
    NlSanitizeResult r = nl_input_sanitize("hello world");
    ASSERT(r.cleaned_input != NULL, "should have output");
    ASSERT(strcmp(r.cleaned_input, "hello world") == 0, "should be unchanged");
    ASSERT(r.chars_stripped == 0, "should strip 0 chars");
    nl_sanitize_result_free(&r);
    PASS();
}

static void test_sanitize_null_byte(void) {
    TEST("sanitize null byte");
    char input[] = "test\0hidden";
    NlSanitizeResult r = nl_input_sanitize(input);
    ASSERT(r.cleaned_input != NULL, "should have output");
    ASSERT(strcmp(r.cleaned_input, "test") == 0, "should stop at null");
    nl_sanitize_result_free(&r);
    PASS();
}

static void test_sanitize_control_chars(void) {
    TEST("sanitize control chars");
    /* 使用 BEL(0x07) BS(0x08) FF(0x0c) 三个控制字符，避免 \x01\x02\x03 的歧义 */
    char input[16];
    memcpy(input, "test\x07\x08\x0C""end", 10);
    input[10] = '\0';
    NlSanitizeResult r = nl_input_sanitize(input);
    ASSERT(r.chars_stripped == 3, "should strip 3 chars");
    ASSERT(strcmp(r.cleaned_input, "testend") == 0, "should remove control chars");
    nl_sanitize_result_free(&r);
    PASS();
}

static void test_sanitize_empty(void) {
    TEST("sanitize empty string");
    NlSanitizeResult r = nl_input_sanitize("");
    ASSERT(r.cleaned_input != NULL, "should have output");
    ASSERT(strlen(r.cleaned_input) == 0, "should be empty");
    nl_sanitize_result_free(&r);
    PASS();
}

static void test_sanitize_null_input(void) {
    TEST("sanitize NULL");
    NlSanitizeResult r = nl_input_sanitize(NULL);
    ASSERT(r.cleaned_input == NULL, "should return NULL output");
    ASSERT(strlen(r.warning) > 0, "should have warning");
    nl_sanitize_result_free(&r);
    PASS();
}

/* ============================================================
 * 边界条件
 * ============================================================ */
static void test_empty_path(void) {
    TEST("empty path");
    NlPathResult r = nl_path_check("", 0);
    ASSERT(r.status == NL_PATH_ERROR, "empty path should error");
    nl_path_result_free(&r);
    PASS();
}

static void test_null_path(void) {
    TEST("NULL path");
    NlPathResult r = nl_path_check(NULL, 0);
    ASSERT(r.status == NL_PATH_ERROR, "NULL path should error");
    nl_path_result_free(&r);
    PASS();
}

/* ============================================================
 * 入口
 * ============================================================ */
int main(void) {
    printf("path_safety 单元测试\n====================\n\n");

    printf("--- 路径规范化 ---\n");
    test_normalize_simple();
    test_normalize_home();
    test_normalize_dotdot();

    printf("\n--- 白名单 ---\n");
    test_whitelist_home_write();
    test_whitelist_tmp_write();
    test_whitelist_var_write();
    test_whitelist_etc_read();
    test_whitelist_etc_write_rejected();
    test_whitelist_unknown_write_rejected();

    printf("\n--- 黑名单 ---\n");
    test_blacklist_root();
    test_blacklist_etc();
    test_blacklist_boot();
    test_blacklist_passwd();
    test_blacklist_shadow();
    test_blacklist_dev_sda();
    test_blacklist_dev_nvme();

    printf("\n--- 符号链接 ---\n");
    test_symlink_to_etc();

    printf("\n--- 路径提取 ---\n");
    test_extract_paths();

    printf("\n--- 输入净化 ---\n");
    test_sanitize_clean_input();
    test_sanitize_null_byte();
    test_sanitize_control_chars();
    test_sanitize_empty();
    test_sanitize_null_input();

    printf("\n--- 边界条件 ---\n");
    test_empty_path();
    test_null_path();

    printf("\n====================\n");
    printf("结果: %d 通过 / %d 失败 / %d 总计\n", tests_passed, tests_failed, tests_run);
    return tests_failed > 0 ? 1 : 0;
}
