/*
 * 能力自检单元测试（T04）
 * 覆盖：可执行→usable / L2→needs-privilege / 不可执行→missing / 不存在→missing / 只读不改 mtime
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

#include "capability_registry.h"
#include "capability_scan.h"

static char g_dir[256];

static void make_cap(NlCapability *c, const char *id, const char *path, int perm, int trusted)
{
    memset(c, 0, sizeof(*c));
    snprintf(c->id, sizeof(c->id), "%s", id);
    snprintf(c->name, sizeof(c->name), "%s", id);
    c->path = (char *)path;
    c->source = (char *)"scan";
    c->perm_level = perm;
    c->trusted = trusted;
    c->effect = NL_CAP_EFFECT_READ;
    c->reversible = 1;
    snprintf(c->namespace, sizeof(c->namespace), "cli");
}

static int write_script(const char *path)
{
    FILE *f = fopen(path, "w");
    if (!f) return -1;
    fputs("#!/bin/sh\nexit 0\n", f);
    fclose(f);
    chmod(path, 0755);
    return 0;
}

static void test_classification(void)
{
    TEST("classify usable / needs-privilege / missing");
    char good[512], noexec[512], absent[512];
    snprintf(good, sizeof(good), "%s/good.sh", g_dir);
    snprintf(noexec, sizeof(noexec), "%s/noexec.sh", g_dir);
    snprintf(absent, sizeof(absent), "%s/absent-bin", g_dir);

    ASSERT(write_script(good) == 0, "write good");
    ASSERT(write_script(noexec) == 0, "write noexec");
    ASSERT(chmod(noexec, 0644) == 0, "chmod noexec");

    NlCapRegistry *r = nl_cap_create();
    ASSERT(r != NULL, "create failed");
    NlCapability a, b, c, d;
    make_cap(&a, "cli:good", good, 1, 1);
    make_cap(&b, "cli:priv", good, 2, 1);     /* L2 → needs-privilege，不做执行探测 */
    make_cap(&c, "cli:noexec", noexec, 1, 1);
    make_cap(&d, "cli:absent", absent, 1, 1);
    ASSERT(nl_cap_add(r, &a) == NL_CAP_OK, "add a");
    ASSERT(nl_cap_add(r, &b) == NL_CAP_OK, "add b");
    ASSERT(nl_cap_add(r, &c) == NL_CAP_OK, "add c");
    ASSERT(nl_cap_add(r, &d) == NL_CAP_OK, "add d");

    NlSelfTestReport rep;
    ASSERT(nl_cap_selftest(r, &rep) == 4, "total should be 4");
    ASSERT(rep.usable == 1, "one usable");
    ASSERT(rep.needs_privilege == 1, "one needs-privilege");
    ASSERT(rep.missing == 2, "two missing");
    nl_cap_free(r);
    PASS();
}

static void test_read_only(void)
{
    TEST("selftest does not modify files");
    char good[512];
    snprintf(good, sizeof(good), "%s/good.sh", g_dir);
    struct stat before, after;
    ASSERT(stat(good, &before) == 0, "stat before");

    NlCapRegistry *r = nl_cap_create();
    NlCapability a;
    make_cap(&a, "cli:good", good, 1, 1);
    ASSERT(nl_cap_add(r, &a) == NL_CAP_OK, "add");
    NlSelfTestReport rep;
    ASSERT(nl_cap_selftest(r, &rep) == 1, "total");
    ASSERT(stat(good, &after) == 0, "stat after");
    ASSERT(before.st_mtime == after.st_mtime, "mtime must not change");
    ASSERT(before.st_size == after.st_size, "size must not change");
    nl_cap_free(r);
    PASS();
}

int main(void)
{
    char tmpl[] = "/tmp/nlselftest-XXXXXX";
    char *d = mkdtemp(tmpl);
    if (!d) { printf("mkdtemp failed\n"); return 1; }
    snprintf(g_dir, sizeof(g_dir), "%s", d);
    printf("capability_selftest tests (%s)\n", g_dir);
    test_classification();
    test_read_only();
    printf("\n%d/%d passed, %d failed\n", tests_passed, tests_run, tests_failed);
    return tests_failed == 0 ? 0 : 1;
}
