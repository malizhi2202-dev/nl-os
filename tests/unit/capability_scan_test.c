/*
 * capability_scan 单元测试（T03）
 *
 * 用临时目录构造 fixture，不依赖真实 /usr/bin 内容：
 *   1. 普通文件被登记为 trusted，权限按映射表（ls→L1）
 *   2. 符号链接被标记 untrusted，kind=symlink
 *   3. 未登记命令 → perm L4（deny-by-default）
 *   4. 同名不同路径 → 第二个 id 追加 @hash8
 *   5. 不可读目录不导致整体失败
 *
 * 链接 nlc（不 include .c）。
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>

static int tests_run = 0;
static int tests_passed = 0;
static int tests_failed = 0;

#define TEST(name) do { tests_run++; printf("  TEST %s ... ", name); fflush(stdout); } while (0)
#define PASS()     do { tests_passed++; printf("PASS\n"); } while (0)
#define FAIL(msg)  do { tests_failed++; printf("FAIL: %s\n", msg); return; } while (0)
#define ASSERT(cond, msg) do { if (!(cond)) { FAIL(msg); } } while (0)

#include "capability_registry.h"
#include "capability_scan.h"

static char g_base[256];
static char g_a[256];
static char g_b[256];
static char g_c[256];

static void touch_file(const char *path, mode_t mode)
{
    FILE *f = fopen(path, "w");
    if (f) fclose(f);
    chmod(path, mode);
}

static void test_scan_basic(void)
{
    TEST("scan regular file / symlink / unknown");
    NlCapRegistry *r = nl_cap_create();
    ASSERT(r != NULL, "create failed");

    const char *dirs[1] = { g_a };
    int added = nl_cap_scan_dirs(dirs, 1, r);
    ASSERT(added == 3, "should add 3 entries from dir a");

    const NlCapability *ls = nl_cap_find(r, "ls");
    ASSERT(ls != NULL, "ls not registered");
    ASSERT(ls->trusted == 1, "regular file must be trusted");
    ASSERT(ls->kind == NL_CAP_KIND_FILE, "ls kind must be file");
    ASSERT(ls->perm_level == 1, "ls must map to L1");
    ASSERT(strstr(ls->path, "/a/ls") != NULL, "path should be realpath in dir a");
    ASSERT(ls->inode != 0, "inode should be recorded");

    const NlCapability *lk = nl_cap_find(r, "linkx");
    ASSERT(lk != NULL, "symlink not registered");
    ASSERT(lk->trusted == 0, "symlink must be untrusted");
    ASSERT(lk->kind == NL_CAP_KIND_SYMLINK, "linkx kind must be symlink");

    const NlCapability *unk = nl_cap_find(r, "zzz-tool");
    ASSERT(unk != NULL, "unknown tool not registered");
    ASSERT(unk->perm_level == 4, "unregistered capability must default to L4");

    nl_cap_free(r);
    PASS();
}

static void test_id_conflict_suffix(void)
{
    TEST("same name different path gets @hash8 id");
    NlCapRegistry *r = nl_cap_create();
    ASSERT(r != NULL, "create failed");

    const char *dirs[2] = { g_a, g_b };
    int added = nl_cap_scan_dirs(dirs, 2, r);
    ASSERT(added == 4, "should add 4 entries across two dirs");
    ASSERT(nl_cap_count(r) == 4, "registry count should be 4");

    /* 统计以 "ls" 开头的 id：应恰好两个，其中一个带 '@' 且指向 dir b */
    int plain = 0, suffixed = 0;
    int suffixed_in_b = 0;
    for (int i = 0; i < nl_cap_count(r); i++) {
        const NlCapability *c = nl_cap_at(r, i);
        ASSERT(c != NULL, "nl_cap_at returned NULL");
        if (strncmp(c->id, "ls", 2) != 0) continue;
        if (strchr(c->id, '@') != NULL) {
            suffixed++;
            if (strstr(c->path, "/b/ls") != NULL) suffixed_in_b = 1;
        } else if (strcmp(c->id, "ls") == 0) {
            plain++;
        }
    }
    ASSERT(plain == 1, "exactly one plain 'ls'");
    ASSERT(suffixed == 1, "exactly one suffixed ls id");
    ASSERT(suffixed_in_b == 1, "suffixed entry should point at dir b");

    /* 冲突后的 id 仍可被 find 命中 */
    ASSERT(nl_cap_find(r, "ls") != NULL, "plain ls still findable");

    nl_cap_free(r);
    PASS();
}

static void test_unreadable_dir_does_not_fail(void)
{
    TEST("unreadable dir skipped, scan still succeeds");
    if (geteuid() == 0) { printf("SKIP (running as root)\n"); tests_passed++; return; }
    NlCapRegistry *r = nl_cap_create();
    ASSERT(r != NULL, "create failed");

    const char *dirs[2] = { g_c, g_a };
    int added = nl_cap_scan_dirs(dirs, 2, r);
    ASSERT(added == 3, "unreadable dir skipped, dir a still scanned");
    nl_cap_free(r);
    PASS();
}

int main(void)
{
    char tmpl[] = "/tmp/nlcap-scan-XXXXXX";
    char *d = mkdtemp(tmpl);
    if (!d) { printf("cannot create temp dir\n"); return 1; }
    snprintf(g_base, sizeof(g_base), "%s", d);
    snprintf(g_a, sizeof(g_a), "%.253s/a", g_base);
    snprintf(g_b, sizeof(g_b), "%.253s/b", g_base);
    snprintf(g_c, sizeof(g_c), "%.253s/c", g_base);

    if (mkdir(g_a, 0755) != 0 || mkdir(g_b, 0755) != 0 || mkdir(g_c, 0000) != 0) {
        printf("cannot create fixture dirs\n");
        return 1;
    }

    char p[512];
    snprintf(p, sizeof(p), "%s/ls", g_a);
    touch_file(p, 0755);
    snprintf(p, sizeof(p), "%s/zzz-tool", g_a);
    touch_file(p, 0755);
    snprintf(p, sizeof(p), "%s/linkx", g_a);
    symlink("ls", p);

    snprintf(p, sizeof(p), "%s/ls", g_b);
    touch_file(p, 0755);

    printf("capability_scan tests (%s)\n", g_base);
    test_scan_basic();
    test_id_conflict_suffix();
    test_unreadable_dir_does_not_fail();

    printf("\n%d/%d passed, %d failed\n", tests_passed, tests_run, tests_failed);
    return tests_failed == 0 ? 0 : 1;
}
