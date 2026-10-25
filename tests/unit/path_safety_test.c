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
 * 域名校验（ADR-024）
 * ============================================================ */
static void test_domain_valid(void) {
    TEST("合法域名");
    const char *ok[] = {
        "example.com", "www.baidu.com", "wwww.baidu.com",
        "a-b.example.co", "1.2.3.com", "xn--fiq228c.cn",
        "a.b.c.d.example.org",
    };
    for (size_t i = 0; i < sizeof(ok) / sizeof(ok[0]); i++)
        ASSERT(nl_domain_check(ok[i]) == 0, ok[i]);
    PASS();
}

static void test_domain_invalid(void) {
    TEST("非法域名一律拒绝（fail-closed）");
    const char *bad[] = {
        "",                    /* 空 */
        "localhost",           /* 无点 */
        ".example.com",        /* 点开头 */
        "example.com.",        /* 点结尾 */
        "example..com",        /* 连续点 */
        "-example.com",        /* '-'开头 → 选项注入面 */
        "example-.com",        /* 标签以 '-' 结尾 */
        "a.b",                 /* TLD 单字符（真实 TLD 没有 1 字母的） */
        "8.8.8.8",             /* 全数字，不是主机名 */
        "1.2.3",               /* TLD 不是字母 */
        "exa mple.com",        /* 空格 */
        "example.com; rm -rf /",/* 元字符 */
        "example.com/x",       /* 斜杠 */
        "user@example.com",    /* '@' */
        "example.c",           /* TLD 太短 */
        "_dmarc.example.com",  /* 下划线 */
        "-f",                  /* dig 的选项注入试探 */
        "/etc/passwd",
    };
    for (size_t i = 0; i < sizeof(bad) / sizeof(bad[0]); i++)
        ASSERT(nl_domain_check(bad[i]) == -1, bad[i]);
    ASSERT(nl_domain_check(NULL) == -1, "NULL");
    PASS();
}

static void test_domain_length_limits(void) {
    TEST("域名长度边界（标签 63 / 总长 253）");
    char label63[64];
    memset(label63, 'a', 63); label63[63] = '\0';
    char ok[80];
    snprintf(ok, sizeof(ok), "%s.com", label63);
    ASSERT(nl_domain_check(ok) == 0, "63 字符标签合法");

    char label64[65];
    memset(label64, 'a', 64); label64[64] = '\0';
    char too_long[80];
    snprintf(too_long, sizeof(too_long), "%s.com", label64);
    ASSERT(nl_domain_check(too_long) == -1, "64 字符标签拒绝");

    /* 总长 253 = 前缀 250 字符（以 '.' 结尾）+ "com" */
    char big[258];
    size_t pos = 0;
    while (pos + 4 <= 247) { memcpy(big + pos, "aaa.", 4); pos += 4; }  /* 244 */
    while (pos < 249) big[pos++] = 'a';                                 /* 249 */
    big[pos++] = '.';                                                   /* 250 */
    big[pos++] = 'c'; big[pos++] = 'o'; big[pos++] = 'm';               /* 253 */
    big[pos] = '\0';
    ASSERT(strlen(big) == 253, "构造出 253 字符");
    ASSERT(nl_domain_check(big) == 0, "253 字符合法");

    big[pos] = 'm'; big[pos + 1] = '\0';      /* 254 */
    ASSERT(nl_domain_check(big) == -1, "254 字符拒绝");
    PASS();
}

/* ============================================================
 * 服务名校验（ADR-024 续）
 * ============================================================ */
static void test_service_name(void) {
    TEST("服务名只接受 unit 形状");
    const char *ok[] = { "nginx", "docker", "nginx.service", "getty@tty1",
                         "systemd-networkd", "a_b:c", "sshd" };
    for (size_t i = 0; i < sizeof(ok) / sizeof(ok[0]); i++)
        ASSERT(nl_service_name_check(ok[i]) == 0, ok[i]);

    const char *bad[] = { "", "-f", "--all", "ngi nx", "a/b", "a;b", "x.php?",
                          "systemctl stop", "@@@", NULL };
    for (size_t i = 0; bad[i]; i++)
        ASSERT(nl_service_name_check(bad[i]) == -1, bad[i]);
    ASSERT(nl_service_name_check(NULL) == -1, "NULL");
    PASS();
}

/* 白名单根目录本身也要放行：`cp a /tmp` 不该被"不在 /tmp/ 之下"拒掉 */
static void test_whitelist_root_itself(void) {
    TEST("白名单根目录本身可写");
    const char *ok[] = { "/tmp", "/home", "/opt", "/mnt", "/var/tmp" };
    for (size_t i = 0; i < sizeof(ok) / sizeof(ok[0]); i++) {
        NlPathResult r = nl_path_check(ok[i], 1);
        ASSERT(r.status == NL_PATH_PASS, ok[i]);
        nl_path_result_free(&r);
    }
    /* 但 /tmpfoo 这种"只是前缀相同"的不行 */
    NlPathResult r = nl_path_check("/tmpfoo", 1);
    ASSERT(r.status != NL_PATH_PASS, "/tmpfoo 不在白名单");
    nl_path_result_free(&r);
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
    test_sanitize_empty();    test_sanitize_null_input();

    printf("\n--- 域名校验 ---\n");
    test_domain_valid();
    test_domain_invalid();
    test_domain_length_limits();

    printf("\n--- 服务名 / 白名单根 ---\n");
    test_service_name();
    test_whitelist_root_itself();

    printf("\n--- 边界条件 ---\n");
    test_empty_path();
    test_null_path();

    printf("\n====================\n");
    printf("结果: %d 通过 / %d 失败 / %d 总计\n", tests_passed, tests_failed, tests_run);
    return tests_failed > 0 ? 1 : 0;
}
