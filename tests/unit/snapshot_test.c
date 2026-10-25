/*
 * 快照与撤销（T15）
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <dirent.h>
#include <unistd.h>

static int tests_run = 0, tests_passed = 0, tests_failed = 0;
#define TEST(n) do { tests_run++; printf("  TEST %s ... ", n); fflush(stdout); } while (0)
#define PASS()  do { tests_passed++; printf("PASS\n"); } while (0)
#define FAIL(m) do { tests_failed++; printf("FAIL: %s\n", m); return; } while (0)
#define ASSERT(c,m) do { if (!(c)) { FAIL(m); } } while (0)

#include "snapshot.h"

static char g_work[256];

static void write_file(const char *path, const char *content)
{
    FILE *f = fopen(path, "w");
    if (f) { fputs(content, f); fclose(f); }
}

static int read_contains(const char *path, const char *needle)
{
    FILE *f = fopen(path, "r");
    if (!f) return 0;
    char buf[4096];
    size_t n = fread(buf, 1, sizeof(buf) - 1, f);
    buf[n] = '\0';
    fclose(f);
    return strstr(buf, needle) != NULL;
}

static int count_snapshot_dirs(void)
{
    DIR *d = opendir(nl_snapshot_root());
    if (!d) return 0;
    int n = 0;
    struct dirent *e;
    while ((e = readdir(d)) != NULL) if (e->d_name[0] != '.') n++;
    closedir(d);
    return n;
}

static void test_take_and_undo(void)
{
    TEST("snapshot then undo restores content");
    char f[512], copy[512];
    snprintf(f, sizeof(f), "%s/keep.txt", g_work);
    write_file(f, "ac14-content");

    const char *paths[1] = { f };
    char dir[512];
    ASSERT(nl_snapshot_take(paths, 1, dir, sizeof(dir)) == NL_SNAP_OK, "take failed");
    ASSERT(dir[0] == '/', "snapshot dir returned");

    unlink(f);
    ASSERT(access(f, F_OK) != 0, "file should be gone");
    char err[256];
    ASSERT(nl_snapshot_undo(1, err, sizeof(err)) == 0, err);
    ASSERT(read_contains(f, "ac14-content"), "content restored");
    (void)copy;
    PASS();
}

static void test_sensitive_skipped(void)
{
    TEST("sensitive path is skipped");
    char sec[512], normal[512];
    snprintf(sec, sizeof(sec), "%s/my_secret.txt", g_work);
    snprintf(normal, sizeof(normal), "%s/plain.txt", g_work);
    write_file(sec, "SECRET-VALUE");
    write_file(normal, "plain-value");
    ASSERT(nl_snapshot_is_sensitive(sec) == 1, "must detect sensitive");

    const char *paths[2] = { sec, normal };
    char dir[512];
    NlSnapStatus st = nl_snapshot_take(paths, 2, dir, sizeof(dir));
    ASSERT(st == NL_SNAP_OK, "mixed snapshot ok");

    /* 敏感内容不得出现在快照目录中 */
    char cmd[1024];
    snprintf(cmd, sizeof(cmd), "grep -rq 'SECRET-VALUE' %s 2>/dev/null", dir);
    ASSERT(system(cmd) != 0, "secret content must not be in snapshot");
    unlink(sec);
    unlink(normal);
    char err[256];
    ASSERT(nl_snapshot_undo(1, err, sizeof(err)) == 0, err);
    ASSERT(access(sec, F_OK) != 0, "sensitive file must NOT be restored");
    ASSERT(read_contains(normal, "plain-value"), "normal file restored");
    PASS();
}

static void test_too_big_skipped(void)
{
    TEST("file over per-file limit → SKIPPED");
    setenv("NL_SNAPSHOT_MAX_FILE", "16", 1);
    char big[512];
    snprintf(big, sizeof(big), "%s/big.bin", g_work);
    write_file(big, "0123456789ABCDEF0123456789");   /* 26 > 16 */
    const char *paths[1] = { big };
    char dir[512];
    ASSERT(nl_snapshot_take(paths, 1, dir, sizeof(dir)) == NL_SNAP_SKIPPED, "must skip");
    unsetenv("NL_SNAPSHOT_MAX_FILE");
    PASS();
}

static void test_quota_keep(void)
{
    TEST("keep limit enforced");
    setenv("NL_SNAPSHOT_KEEP", "2", 1);
    int before = count_snapshot_dirs();
    for (int i = 0; i < 3; i++) {
        char p[512];
        snprintf(p, sizeof(p), "%s/q%d.txt", g_work, i);
        write_file(p, "quota");
        const char *paths[1] = { p };
        char dir[512];
        (void)nl_snapshot_take(paths, 1, dir, sizeof(dir));
        usleep(3000);
    }
    int after = count_snapshot_dirs();
    ASSERT(after <= 2, "must keep at most 2 snapshots");
    (void)before;
    unsetenv("NL_SNAPSHOT_KEEP");
    PASS();
}

int main(void)
{
    char tmpl[] = "/tmp/nlsnap-XXXXXX";
    char *d = mkdtemp(tmpl);
    if (!d) { printf("mkdtemp failed\n"); return 1; }
    snprintf(g_work, sizeof(g_work), "%s", d);
    setenv("XDG_STATE_HOME", d, 1);

    printf("snapshot tests (%s)\n", d);
    test_take_and_undo();
    test_sensitive_skipped();
    test_too_big_skipped();
    test_quota_keep();
    printf("\n%d/%d passed, %d failed\n", tests_passed, tests_run, tests_failed);
    return tests_failed == 0 ? 0 : 1;
}
