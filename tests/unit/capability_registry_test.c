/*
 * capability_registry 单元测试（T02）
 *
 * 覆盖 4 类必需 case + 1 个附加 case：
 *   1. 往返读写一致（含 slots / perm / path）
 *   2. format_version 更高 → 拒绝加载并返回 NL_CAP_ERR_VERSION
 *   3. 并发写：锁被占用时 save 超时失败，且不留下半成品文件
 *   4. 生成文件权限 0600
 *   5. 坏行跳过并计数，不整体失败
 *
 * 链接 nlc（不直接 include .c，避免与共享库重复符号）。
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <dirent.h>
#include <sys/stat.h>
#include <sys/file.h>
#include <sys/wait.h>

static int tests_run = 0;
static int tests_passed = 0;
static int tests_failed = 0;

#define TEST(name) do { tests_run++; printf("  TEST %s ... ", name); fflush(stdout); } while (0)
#define PASS()     do { tests_passed++; printf("PASS\n"); } while (0)
#define FAIL(msg)  do { tests_failed++; printf("FAIL: %s\n", msg); return; } while (0)
#define ASSERT(cond, msg) do { if (!(cond)) { FAIL(msg); } } while (0)

#include "capability_registry.h"

static char g_dir[256];

static void path_for(int n, char *out, size_t sz)
{
    snprintf(out, sz, "%s/cap%d.jsonl", g_dir, n);
}

static void make_cap(NlCapability *c, const char *id, const char *path, int perm)
{
    memset(c, 0, sizeof(*c));
    snprintf(c->id, sizeof(c->id), "%s", id);
    snprintf(c->name, sizeof(c->name), "%s", id);
    c->path = (char *)path;      /* nl_cap_add 会深拷贝 */
    c->source = (char *)"scan";
    c->perm_level = perm;
    c->trusted = 1;
    c->kind = NL_CAP_KIND_FILE;
}

/* ---------------- case 1 ---------------- */
static void test_roundtrip(void)
{
    TEST("roundtrip save/load");
    char path[300];
    path_for(1, path, sizeof(path));

    NlCapRegistry *r = nl_cap_create();
    ASSERT(r != NULL, "create failed");

    NlCapability ls, rm;
    make_cap(&ls, "ls", "/usr/bin/ls", 1);
    ls.slots[0] = (char *)"path";
    ls.slots[1] = (char *)"flags";
    ls.slot_count = 2;
    make_cap(&rm, "rm", "/usr/bin/rm", 3);

    ASSERT(nl_cap_add(r, &ls) == NL_CAP_OK, "add ls failed");
    ASSERT(nl_cap_add(r, &rm) == NL_CAP_OK, "add rm failed");
    ASSERT(nl_cap_count(r) == 2, "count should be 2");
    ASSERT(nl_cap_save_atomic(r, path) == NL_CAP_OK, "save failed");
    nl_cap_free(r);

    NlCapLoadErr err = NL_CAP_ERR_IO;
    NlCapRegistry *l = nl_cap_load(path, &err);
    ASSERT(l != NULL, "load failed");
    ASSERT(err == NL_CAP_OK, "load err should be OK");
    ASSERT(nl_cap_count(l) == 2, "loaded count should be 2");

    const NlCapability *c = nl_cap_find(l, "ls");
    ASSERT(c != NULL, "ls not found");
    ASSERT(c->perm_level == 1, "ls perm should be L1");
    ASSERT(strcmp(c->path, "/usr/bin/ls") == 0, "ls path mismatch");
    ASSERT(c->slot_count == 2, "ls slots should be 2");
    ASSERT(strcmp(c->slots[1], "flags") == 0, "ls slot[1] mismatch");
    ASSERT(c->trusted == 1, "ls should be trusted");

    c = nl_cap_find(l, "rm");
    ASSERT(c != NULL, "rm not found");
    ASSERT(c->perm_level == 3, "rm perm should be L3");
    ASSERT(c->slot_count == 0, "rm slots should be 0");

    /* 原子写：不应残留临时文件 */
    DIR *d = opendir(g_dir);
    ASSERT(d != NULL, "opendir failed");
    struct dirent *e;
    while ((e = readdir(d)) != NULL) {
        ASSERT(strstr(e->d_name, ".tmp.") == NULL, "leftover tmp file found");
    }
    closedir(d);

    nl_cap_free(l);
    PASS();
}

/* ---------------- case 2 ---------------- */
static void test_newer_version_rejected(void)
{
    TEST("format_version 3 rejected");
    char path[300];
    path_for(2, path, sizeof(path));

    FILE *f = fopen(path, "w");
    ASSERT(f != NULL, "open for write failed");
    fputs("{\"format_version\":3,\"scanner\":\"future\"}\n", f);
    fputs("{\"id\":\"x\",\"path\":\"/bin/x\",\"perm\":\"L1\",\"source\":\"scan\",\"trusted\":true}\n", f);
    fclose(f);

    NlCapLoadErr err = NL_CAP_OK;
    NlCapRegistry *r = nl_cap_load(path, &err);
    ASSERT(r == NULL, "should refuse newer version");
    ASSERT(err == NL_CAP_ERR_VERSION, "err should be VERSION");
    PASS();
}

/* ---------------- case 3 ---------------- */
static void test_lock_blocks_concurrent_save(void)
{
    TEST("lock blocks concurrent save");
    char path[300], lock[320];
    path_for(3, path, sizeof(path));
    snprintf(lock, sizeof(lock), "%s.lock", path);
    unlink(path);

    int lfd = open(lock, O_CREAT | O_RDWR, 0600);
    ASSERT(lfd >= 0, "lock file open failed");
    ASSERT(flock(lfd, LOCK_EX | LOCK_NB) == 0, "parent lock failed");

    NlCapRegistry *r = nl_cap_create();
    NlCapability c;
    make_cap(&c, "ps", "/usr/bin/ps", 1);
    ASSERT(nl_cap_add(r, &c) == NL_CAP_OK, "add failed");

    pid_t pid = fork();
    ASSERT(pid >= 0, "fork failed");
    if (pid == 0) {
        int rc = nl_cap_save_atomic(r, path);
        _exit(rc == NL_CAP_ERR_LOCK ? 0 : (rc == NL_CAP_OK ? 2 : 3));
    }
    int status = 0;
    waitpid(pid, &status, 0);
    ASSERT(WIFEXITED(status) && WEXITSTATUS(status) == 0,
           "child should fail with LOCK error");

    /* 锁被占用期间不应产生目标文件或半成品 */
    struct stat st;
    ASSERT(stat(path, &st) != 0, "target file must not exist while locked");

    flock(lfd, LOCK_UN);
    close(lfd);

    ASSERT(nl_cap_save_atomic(r, path) == NL_CAP_OK, "save after unlock should succeed");
    nl_cap_free(r);
    PASS();
}

/* ---------------- case 4 ---------------- */
static void test_file_mode_0600(void)
{
    TEST("saved file mode is 0600");
    char path[300];
    path_for(4, path, sizeof(path));

    NlCapRegistry *r = nl_cap_create();
    NlCapability c;
    make_cap(&c, "cat", "/usr/bin/cat", 1);
    ASSERT(nl_cap_add(r, &c) == NL_CAP_OK, "add failed");
    ASSERT(nl_cap_save_atomic(r, path) == NL_CAP_OK, "save failed");

    struct stat st;
    ASSERT(stat(path, &st) == 0, "stat failed");
    ASSERT((st.st_mode & 0777) == 0600, "mode must be 0600");
    nl_cap_free(r);
    PASS();
}

/* ---------------- case 5 ---------------- */
static void test_bad_line_skipped(void)
{
    TEST("bad line skipped and counted");
    char path[300];
    path_for(5, path, sizeof(path));

    FILE *f = fopen(path, "w");
    ASSERT(f != NULL, "open for write failed");
    fputs("{\"format_version\":1,\"scanner\":\"test\"}\n", f);
    fputs("{\"id\":\"ls\",\"path\":\"/usr/bin/ls\",\"perm\":\"L1\",\"source\":\"scan\",\"trusted\":true}\n", f);
    fputs("{\"id\":\"broken\"}\n", f);                       /* 缺 path/perm/source/trusted */
    fputs("{\"id\":\"bad2\",\"path\":\"/x\",\"perm\":\"L9\",\"source\":\"scan\",\"trusted\":true}\n", f);
    fclose(f);

    NlCapLoadErr err = NL_CAP_OK;
    NlCapRegistry *r = nl_cap_load(path, &err);
    ASSERT(r != NULL, "load should succeed with skips");
    ASSERT(err == NL_CAP_OK, "err should be OK");
    ASSERT(nl_cap_count(r) == 1, "only valid line loaded");
    ASSERT(nl_cap_skipped(r) == 2, "two bad lines skipped");
    nl_cap_free(r);
    PASS();
}

static void test_v2_explicit_effect(void)
{
    TEST("v2 explicit effect survives roundtrip");
    char path[300];
    path_for(6, path, sizeof(path));

    NlCapRegistry *r = nl_cap_create();
    NlCapability c;
    make_cap(&c, "cli:ls", "/usr/bin/ls", 1);
    c.effect = NL_CAP_EFFECT_READ;
    c.reversible = 1;
    snprintf(c.namespace, sizeof(c.namespace), "cli");
    ASSERT(nl_cap_add(r, &c) == NL_CAP_OK, "add failed");
    ASSERT(nl_cap_save_atomic(r, path) == NL_CAP_OK, "save failed");
    nl_cap_free(r);

    NlCapLoadErr err = NL_CAP_ERR_IO;
    NlCapRegistry *l = nl_cap_load(path, &err);
    ASSERT(l != NULL, "load failed");
    ASSERT(err == NL_CAP_OK, "err should be ok");
    const NlCapability *x = nl_cap_find(l, "cli:ls");
    ASSERT(x != NULL, "cap missing");
    ASSERT(x->effect == NL_CAP_EFFECT_READ, "effect must be read");
    ASSERT(x->reversible == 1, "reversible must be 1");
    ASSERT(x->effect_defaulted == 0, "explicit effect must not be flagged defaulted");
    nl_cap_free(l);
    PASS();
}

static void test_v1_file_defaults(void)
{
    TEST("v1 file: effect=delete + namespace inferred");
    char path[300];
    path_for(7, path, sizeof(path));

    FILE *f = fopen(path, "w");
    ASSERT(f != NULL, "open for write failed");
    fputs("{\"format_version\":1,\"scanner\":\"old\"}\n", f);
    fputs("{\"id\":\"dbus:org.x|M\",\"path\":\"/x\",\"perm\":\"L2\","
          "\"source\":\"scan\",\"trusted\":true}\n", f);
    fclose(f);

    NlCapLoadErr err = NL_CAP_ERR_IO;
    NlCapRegistry *l = nl_cap_load(path, &err);
    ASSERT(l != NULL, "load failed");
    const NlCapability *x = nl_cap_find(l, "dbus:org.x|M");
    ASSERT(x != NULL, "cap missing");
    ASSERT(x->effect == NL_CAP_EFFECT_DELETE, "v1 effect must default to delete");
    ASSERT(x->effect_defaulted == 1, "must be flagged as defaulted");
    ASSERT(strcmp(x->namespace, "dbus") == 0, "namespace must be inferred from id");
    nl_cap_free(l);
    PASS();
}

static void test_invalid_effect_falls_back(void)
{
    TEST("invalid effect falls back to delete");
    char path[300];
    path_for(8, path, sizeof(path));

    FILE *f = fopen(path, "w");
    ASSERT(f != NULL, "open for write failed");
    fputs("{\"format_version\":2}\n", f);
    fputs("{\"id\":\"cli:x\",\"path\":\"/x\",\"perm\":\"L1\",\"source\":\"scan\","
          "\"trusted\":true,\"effect\":\"bogus\"}\n", f);
    fclose(f);

    NlCapLoadErr err = NL_CAP_ERR_IO;
    NlCapRegistry *l = nl_cap_load(path, &err);
    ASSERT(l != NULL, "load failed");
    const NlCapability *x = nl_cap_find(l, "cli:x");
    ASSERT(x != NULL, "cap missing");
    ASSERT(x->effect == NL_CAP_EFFECT_DELETE, "invalid effect must fall back to delete");
    ASSERT(x->effect_defaulted == 1, "must be flagged as defaulted");
    nl_cap_free(l);
    PASS();
}

int main(void)
{
    char tmpl[] = "/tmp/nlcap-registry-XXXXXX";
    char *d = mkdtemp(tmpl);
    if (!d) {
        printf("cannot create temp dir\n");
        return 1;
    }
    snprintf(g_dir, sizeof(g_dir), "%s", d);

    printf("capability_registry tests (%s)\n", g_dir);
    test_roundtrip();
    test_newer_version_rejected();
    test_lock_blocks_concurrent_save();
    test_file_mode_0600();
    test_bad_line_skipped();
    test_v2_explicit_effect();
    test_v1_file_defaults();
    test_invalid_effect_falls_back();

    printf("\n%d/%d passed, %d failed\n", tests_passed, tests_run, tests_failed);
    return tests_failed == 0 ? 0 : 1;
}
