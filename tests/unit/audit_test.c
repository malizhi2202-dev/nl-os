/*
 * 审计单元测试（T07）
 * 覆盖：写入可解析 / 无明文参数 / since 过滤 / limit 钳制 / 文件 0600
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static int tests_run = 0, tests_passed = 0, tests_failed = 0;
#define TEST(n) do { tests_run++; printf("  TEST %s ... ", n); fflush(stdout); } while (0)
#define PASS()  do { tests_passed++; printf("PASS\n"); } while (0)
#define FAIL(m) do { tests_failed++; printf("FAIL: %s\n", m); return; } while (0)
#define ASSERT(c,m) do { if (!(c)) { FAIL(m); } } while (0)

#include "audit.h"
#include "cJSON.h"

static void emit(const char *cap, const char *verdict, const char *reason, const char *input)
{
    NlAuditRec r;
    memset(&r, 0, sizeof(r));
    r.capability_id = cap;
    r.perm_level = 3;
    r.verdict = verdict;
    r.effect = "delete";
    r.fast_path = 0;
    r.probability = -1;
    r.duration_us = 812;
    r.result = "refused";
    r.reason = reason;
    r.input = input;
    (void)nl_audit_emit(&r);
}

static int count_lines(void)
{
    FILE *f = fopen(nl_audit_path(), "r");
    if (!f) return 0;
    int n = 0, c;
    while ((c = fgetc(f)) != EOF) if (c == '\n') n++;
    fclose(f);
    return n;
}

static void test_emit_parsable(void)
{
    TEST("emit produces parsable JSONL");
    emit("cli:rm", "deny", "protected path", "rm -rf /etc/passwd");
    ASSERT(count_lines() == 1, "one line expected");
    FILE *f = fopen(nl_audit_path(), "r");
    ASSERT(f != NULL, "audit file missing");
    char buf[2048];
    ASSERT(fgets(buf, sizeof(buf), f) != NULL, "read failed");
    fclose(f);
    cJSON *o = cJSON_Parse(buf);
    ASSERT(o != NULL, "line must be valid JSON");
    ASSERT(cJSON_GetObjectItem(o, "audit_id") != NULL, "audit_id missing");
    ASSERT(cJSON_GetObjectItem(o, "verdict") != NULL, "verdict missing");
    ASSERT(cJSON_GetObjectItem(o, "args_hash") != NULL, "args_hash missing");
    cJSON_Delete(o);
    PASS();
}

static void test_no_plaintext(void)
{
    TEST("no plaintext args in file");
    (void)count_lines();
    FILE *f = fopen(nl_audit_path(), "r");
    ASSERT(f != NULL, "open failed");
    char buf[4096];
    size_t n = fread(buf, 1, sizeof(buf) - 1, f);
    buf[n] = '\0';
    fclose(f);
    ASSERT(strstr(buf, "/etc/passwd") == NULL, "plaintext path must not appear");
    ASSERT(strstr(buf, "rm -rf") == NULL, "plaintext command must not appear");
    PASS();
}

static void test_since_filter(void)
{
    TEST("since filters out older records");
    char out[4096];
    int n = nl_audit_query(4102444800L /* 2100-01-01 */, 50, out, sizeof(out));
    ASSERT(n == 0, "no records after 2100");
    ASSERT(strcmp(out, "[]") == 0, "must render empty array");
    PASS();
}

static void test_limit_and_mode(void)
{
    TEST("limit clamped + file mode 0600");
    for (int i = 0; i < 5; i++) emit("cli:ls", "allow", "read", "ls -la");
    char out[8192];
    int n = nl_audit_query(0, 9999, out, sizeof(out));
    ASSERT(n >= 1 && n <= NL_AUDIT_MAX_LIMIT, "limit must clamp within range");
    struct stat st;
    ASSERT(stat(nl_audit_path(), &st) == 0, "stat failed");
    ASSERT((st.st_mode & 0777) == 0600, "audit file must be 0600");
    PASS();
}

int main(void)
{
    char tmpl[] = "/tmp/nlaudit-XXXXXX";
    char *d = mkdtemp(tmpl);
    if (!d) { printf("mkdtemp failed\n"); return 1; }
    setenv("XDG_STATE_HOME", d, 1);

    printf("audit tests (%s)\n", d);
    test_emit_parsable();
    test_no_plaintext();
    test_since_filter();
    test_limit_and_mode();
    printf("\n%d/%d passed, %d failed\n", tests_passed, tests_run, tests_failed);
    return tests_failed == 0 ? 0 : 1;
}
