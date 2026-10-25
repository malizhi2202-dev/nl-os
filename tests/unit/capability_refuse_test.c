/*
 * 能力存在性 / 预算拒绝 / 快路径审计（T06 + T07）
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static int tests_run = 0, tests_passed = 0, tests_failed = 0;
#define TEST(n) do { tests_run++; printf("  TEST %s ... ", n); fflush(stdout); } while (0)
#define PASS()  do { tests_passed++; printf("PASS\n"); } while (0)
#define FAIL(m) do { tests_failed++; printf("FAIL: %s\n", m); return; } while (0)
#define ASSERT(c,m) do { if (!(c)) { FAIL(m); } } while (0)

#include "../../src/nlsh/ipc_client.c"
#include "../../src/nlsh/dispatch.c"

static void add_cap(NlCapRegistry *r, const char *id, const char *path, int perm)
{
    NlCapability c;
    memset(&c, 0, sizeof(c));
    snprintf(c.id, sizeof(c.id), "%s", id);
    snprintf(c.name, sizeof(c.name), "%s", id);
    c.path = (char *)path;
    c.source = (char *)"scan";
    c.trusted = 1;
    c.perm_level = perm;
    c.effect = NL_CAP_EFFECT_READ;
    c.reversible = 1;
    snprintf(c.namespace, sizeof(c.namespace), "cli");
    (void)nl_cap_add(r, &c);
}

static void test_fast_path_audited(void)
{
    TEST("fast path executes without model and is audited");
    nl_dispatch_set_registry(NULL);
    char out[4096];
    int rc = nl_dispatch("echo fast-ok", out, sizeof(out), 0);
    ASSERT(rc == 0, "fast path should succeed");
    ASSERT(strstr(out, "fast-ok") != NULL, "output must contain command result");

    FILE *f = fopen(nl_audit_path(), "r");
    ASSERT(f != NULL, "audit file must exist");
    char buf[8192];
    size_t n = fread(buf, 1, sizeof(buf) - 1, f);
    buf[n] = '\0';
    fclose(f);
    ASSERT(strstr(buf, "\"fast_path\":true") != NULL, "audit must mark fast_path");
    PASS();
}

static void test_capability_missing(void)
{
    TEST("missing capability refused with candidates");
    NlCapRegistry *r = nl_cap_create();
    ASSERT(r != NULL, "create");
    add_cap(r, "cli:stat", "/usr/bin/stat", 1);
    add_cap(r, "cli:cat", "/usr/bin/cat", 1);
    nl_dispatch_set_registry(r);

    char out[4096];
    int rc = nl_dispatch("列出当前目录", out, sizeof(out), 0);
    ASSERT(rc != 0, "must refuse");
    ASSERT(strstr(out, "本机没有该能力") != NULL, "must say capability missing");
    ASSERT(strstr(out, "cli:") != NULL, "must suggest candidates");
    nl_dispatch_set_registry(NULL);
    nl_cap_free(r);
    PASS();
}

static void test_budget_refuse(void)
{
    TEST("over-budget input refused with dims + alternatives");
    NlCapRegistry *r = nl_cap_create();
    ASSERT(r != NULL, "create");
    add_cap(r, "cli:ls", "/usr/bin/ls", 1);
    add_cap(r, "cli:stat", "/usr/bin/stat", 1);
    nl_dispatch_set_registry(r);

    char out[4096];
    int rc = nl_dispatch("每天凌晨三点把日志压缩后上传备份", out, sizeof(out), 0);
    ASSERT(rc != 0, "must refuse");
    ASSERT(strstr(out, "解析不了") != NULL, "must say cannot parse");
    ASSERT(strstr(out, "替代步骤") != NULL, "must give alternatives");
    nl_dispatch_set_registry(NULL);
    nl_cap_free(r);
    PASS();
}

static void test_capability_present(void)
{
    TEST("present capability goes through NL path");
    NlCapRegistry *r = nl_cap_create();
    ASSERT(r != NULL, "create");
    add_cap(r, "cli:ls", "/usr/bin/ls", 1);
    nl_dispatch_set_registry(r);

    char out[4096];
    int rc = nl_dispatch("列出当前目录", out, sizeof(out), 0);
    ASSERT(rc == 0, "should succeed");
    ASSERT(strstr(out, "ls") != NULL, "output must contain generated command");
    nl_dispatch_set_registry(NULL);
    nl_cap_free(r);
    PASS();
}

int main(void)
{
    char tmpl[] = "/tmp/nlrefuse-XXXXXX";
    char *d = mkdtemp(tmpl);
    if (!d) { printf("mkdtemp failed\n"); return 1; }
    setenv("XDG_STATE_HOME", d, 1);

    printf("capability_refuse tests (%s)\n", d);
    test_fast_path_audited();
    test_capability_missing();
    test_budget_refuse();
    test_capability_present();
    printf("\n%d/%d passed, %d failed\n", tests_passed, tests_run, tests_failed);
    return tests_failed == 0 ? 0 : 1;
}
