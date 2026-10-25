/*
 * snapshot.c — 快照与撤销（ADR-014 / T15）
 */
#include "snapshot.h"

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#define NL_SNAP_DEFAULT_MAX_FILE  (8L * 1024 * 1024)
#define NL_SNAP_DEFAULT_MAX_TOTAL (200L * 1024 * 1024)
#define NL_SNAP_DEFAULT_KEEP      10

static long env_long(const char *name, long def)
{
    const char *v = getenv(name);
    if (!v || !*v) return def;
    long n = atol(v);
    return n > 0 ? n : def;
}

const char *nl_snapshot_root(void)
{
    static char buf[PATH_MAX];
    const char *base = getenv("XDG_STATE_HOME");
    if (base && base[0]) snprintf(buf, sizeof(buf), "%s/nl-os/snapshots", base);
    else {
        const char *home = getenv("HOME");
        snprintf(buf, sizeof(buf), "%s/.local/state/nl-os/snapshots",
                 (home && home[0]) ? home : "/tmp");
    }
    return buf;
}

static int ci_contains(const char *hay, const char *needle)
{
    if (!hay || !needle) return 0;
    size_t n = strlen(needle);
    for (const char *p = hay; *p; p++) {
        size_t i = 0;
        while (i < n && p[i] &&
               ((p[i] >= 'A' && p[i] <= 'Z' ? p[i] + 32 : p[i]) == needle[i])) i++;
        if (i == n) return 1;
    }
    return 0;
}

int nl_snapshot_is_sensitive(const char *path)
{
    if (!path || !*path) return 0;

    static const char *const SUFFIX[] = { ".pem", ".key", ".p12", NULL };
    static const char *const SUBSTR[] = { "secret", "token", "password", NULL };
    static const char *const DIRS[] = { ".ssh", ".gnupg", ".aws", ".kube", NULL };

    for (int i = 0; DIRS[i]; i++) {
        char seg[64];
        snprintf(seg, sizeof(seg), "/%s", DIRS[i]);
        size_t sl = strlen(seg), pl = strlen(path);
        if (strstr(path, "/") != NULL && strstr(path, seg) != NULL) {
            /* 命中 /xxx/ 或结尾 /xxx */
            const char *hit = strstr(path, seg);
            char after = hit[sl];
            if (after == '\0' || after == '/') return 1;
        }
        if (pl > sl && strcmp(path + (pl - sl), seg) == 0) return 1;
        if (strcmp(path, DIRS[i]) == 0) return 1;
    }

    const char *base = strrchr(path, '/');
    base = base ? base + 1 : path;
    for (int i = 0; SUFFIX[i]; i++) {
        size_t bl = strlen(base), sl = strlen(SUFFIX[i]);
        if (bl > sl && strcasecmp(base + bl - sl, SUFFIX[i]) == 0) return 1;
    }
    for (int i = 0; SUBSTR[i]; i++)
        if (ci_contains(base, SUBSTR[i])) return 1;

    /* 运维/测试可追加：逗号分隔的子串 */
    const char *extra = getenv("NL_SNAPSHOT_EXCLUDE");
    if (extra && *extra) {
        char buf[512];
        snprintf(buf, sizeof(buf), "%s", extra);
        for (char *t = strtok(buf, ","); t; t = strtok(NULL, ","))
            if (strstr(path, t) != NULL) return 1;
    }
    return 0;
}

static int mkdir_p(const char *path, mode_t mode)
{
    char buf[PATH_MAX];
    size_t n = strlen(path);
    if (n == 0 || n >= sizeof(buf)) return -1;
    memcpy(buf, path, n + 1);
    for (char *p = buf + 1; *p; p++) {
        if (*p != '/') continue;
        *p = '\0';
        if (mkdir(buf, mode) != 0 && errno != EEXIST) return -1;
        *p = '/';
    }
    if (mkdir(buf, mode) != 0 && errno != EEXIST) return -1;
    return 0;
}

static int copy_file(const char *src, const char *dst, mode_t mode)
{
    FILE *in = fopen(src, "rb");
    if (!in) return -1;
    FILE *out = fopen(dst, "wb");
    if (!out) { fclose(in); return -1; }
    char buf[8192];
    size_t n;
    while ((n = fread(buf, 1, sizeof(buf), in)) > 0)
        if (fwrite(buf, 1, n, out) != n) { fclose(in); fclose(out); return -1; }
    fclose(in);
    fclose(out);
    chmod(dst, mode & 0777);
    return 0;
}

static long dir_size(const char *path, long limit)
{
    struct stat st;
    if (lstat(path, &st) != 0) return 0;
    if (S_ISREG(st.st_mode)) return (long)st.st_size;
    if (!S_ISDIR(st.st_mode)) return 0;

    long total = 0;
    DIR *d = opendir(path);
    if (!d) return 0;
    struct dirent *e;
    while ((e = readdir(d)) != NULL) {
        if (e->d_name[0] == '.') continue;
        char child[PATH_MAX];
        snprintf(child, sizeof(child), "%s/%s", path, e->d_name);
        total += dir_size(child, limit);
        if (total > limit) break;
    }
    closedir(d);
    return total;
}

/* 递归复制；返回 0 成功，1 超预算，-1 错误 */
static int copy_tree(const char *src, const char *dst, const char *manifest,
                     long *budget, char *err, size_t errcap)
{
    struct stat st;
    if (lstat(src, &st) != 0) return -1;

    if (nl_snapshot_is_sensitive(src)) {
        FILE *m = fopen(manifest, "a");
        if (m) { fprintf(m, "S %s\n", src); fclose(m); }
        return 0;
    }

    if (S_ISDIR(st.st_mode)) {
        if (mkdir_p(dst, 0700) != 0) return -1;
        FILE *m = fopen(manifest, "a");
        if (m) { fprintf(m, "D %s %s\n", src, dst); fclose(m); }
        DIR *d = opendir(src);
        if (!d) return -1;
        struct dirent *e;
        while ((e = readdir(d)) != NULL) {
            if (e->d_name[0] == '.') continue;
            char s[PATH_MAX], t[PATH_MAX];
            snprintf(s, sizeof(s), "%s/%s", src, e->d_name);
            snprintf(t, sizeof(t), "%s/%s", dst, e->d_name);
            int rc = copy_tree(s, t, manifest, budget, err, errcap);
            if (rc != 0) { closedir(d); return rc; }
        }
        closedir(d);
        return 0;
    }

    if (!S_ISREG(st.st_mode)) return 0;

    if ((long)st.st_size > *budget || (long)st.st_size > env_long("NL_SNAPSHOT_MAX_FILE", NL_SNAP_DEFAULT_MAX_FILE)) {
        snprintf(err, errcap, "文件超过单文件上限：%s", src);
        return 1;
    }
    *budget -= (long)st.st_size;

    char parent[PATH_MAX];
    snprintf(parent, sizeof(parent), "%s", dst);
    char *slash = strrchr(parent, '/');
    if (slash) { *slash = '\0'; if (mkdir_p(parent, 0700) != 0) return -1; }

    if (copy_file(src, dst, st.st_mode) != 0) return -1;
    FILE *m = fopen(manifest, "a");
    if (m) { fprintf(m, "F %s %s\n", src, dst); fclose(m); }
    return 0;
}

/* 列出快照目录（按名字升序 = 时间升序） */
static int list_dirs(char names[][64], int max)
{
    DIR *d = opendir(nl_snapshot_root());
    if (!d) return 0;
    int n = 0;
    struct dirent *e;
    while ((e = readdir(d)) != NULL && n < max) {
        if (e->d_name[0] == '.') continue;
        snprintf(names[n], 64, "%.63s", e->d_name);
        n++;
    }
    closedir(d);
    /* 名字是 epoch ms，位数不一致时按字符串排序仍近似有序；这里做简单插入排序 */
    for (int i = 1; i < n; i++) {
        char key[64];
        memcpy(key, names[i], sizeof(key));
        int j = i - 1;
        while (j >= 0 && atoll(names[j]) > atoll(key)) {
            memcpy(names[j + 1], names[j], sizeof(names[0]));
            j--;
        }
        memcpy(names[j + 1], key, sizeof(key));
    }
    return n;
}

static int snapshot_remove_tree(const char *path);

static void enforce_quota(void)
{
    char names[256][64];
    int n = list_dirs(names, 256);
    long keep = env_long("NL_SNAPSHOT_KEEP", NL_SNAP_DEFAULT_KEEP);
    long max_total = env_long("NL_SNAPSHOT_MAX_TOTAL", NL_SNAP_DEFAULT_MAX_TOTAL);

    long total = 0;
    for (int i = n - 1; i >= 0; i--) {
        char path[PATH_MAX];
        snprintf(path, sizeof(path), "%s/%s", nl_snapshot_root(), names[i]);
        total += dir_size(path, max_total * 4);
    }
    /* 先按数量淘汰 */
    for (int i = 0; i < n; i++) {
        if (n - i <= keep && total <= max_total) break;
        char path[PATH_MAX];
        snprintf(path, sizeof(path), "%s/%s", nl_snapshot_root(), names[i]);
        long sz = dir_size(path, max_total * 4);
        if (n - i > keep || total - sz >= 0) {
            snapshot_remove_tree(path);
            total -= sz;
        }
    }
}

static int snapshot_remove_tree(const char *path)
{
    struct stat st;
    if (lstat(path, &st) != 0) return 0;
    if (!S_ISDIR(st.st_mode)) return unlink(path);
    DIR *d = opendir(path);
    if (!d) return unlink(path);
    struct dirent *e;
    while ((e = readdir(d)) != NULL) {
        if (e->d_name[0] == '.') continue;
        char child[PATH_MAX];
        snprintf(child, sizeof(child), "%s/%s", path, e->d_name);
        snapshot_remove_tree(child);
    }
    closedir(d);
    return rmdir(path);
}

NlSnapStatus nl_snapshot_take(const char *const *paths, int n, char *out_dir, size_t cap)
{
    if (!paths || n <= 0) return NL_SNAP_ERR;
    const char *root = nl_snapshot_root();
    if (mkdir_p(root, 0700) != 0) return NL_SNAP_ERR;

    char lock[PATH_MAX];
    snprintf(lock, sizeof(lock), "%s/.lock", root);
    int lfd = open(lock, O_CREAT | O_RDWR, 0600);
    if (lfd < 0) return NL_SNAP_ERR;
    if (flock(lfd, LOCK_EX) != 0) { close(lfd); return NL_SNAP_ERR; }

    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    char dir[PATH_MAX * 2];
    snprintf(dir, sizeof(dir), "%.4000s/%ld%03ld", root, (long)ts.tv_sec, ts.tv_nsec / 1000000);

    if (mkdir_p(dir, 0700) != 0) { flock(lfd, LOCK_UN); close(lfd); return NL_SNAP_ERR; }

    char manifest[PATH_MAX * 2 + 16];
    snprintf(manifest, sizeof(manifest), "%.4000s/manifest", dir);
    FILE *m = fopen(manifest, "w");
    if (!m) { flock(lfd, LOCK_UN); close(lfd); return NL_SNAP_ERR; }
    fclose(m);
    chmod(manifest, 0600);

    long budget = env_long("NL_SNAPSHOT_MAX_TOTAL", NL_SNAP_DEFAULT_MAX_TOTAL);
    if (budget > env_long("NL_SNAPSHOT_MAX_FILE", NL_SNAP_DEFAULT_MAX_FILE))
        budget = env_long("NL_SNAPSHOT_MAX_FILE", NL_SNAP_DEFAULT_MAX_FILE);

    char err[256] = "";
    int copied = 0;
    NlSnapStatus status = NL_SNAP_OK;
    for (int i = 0; i < n; i++) {
        if (!paths[i] || !paths[i][0]) continue;
        char abs_path[PATH_MAX];
        if (realpath(paths[i], abs_path) == NULL) {
            /* 不存在则退回 cwd 拼接（删不存在的路径无需快照） */
            char cwd[PATH_MAX];
            if (!getcwd(cwd, sizeof(cwd))) continue;
            snprintf(abs_path, sizeof(abs_path), "%.2000s/%.1000s", cwd, paths[i]);
            struct stat st0;
            if (lstat(abs_path, &st0) != 0) continue;
        }
        char dest[PATH_MAX * 2 + 32];
        const char *base = strrchr(abs_path, '/');
        base = base ? base + 1 : abs_path;
        snprintf(dest, sizeof(dest), "%.4000s/data/%d/%.200s", dir, i, base);
        int rc = copy_tree(abs_path, dest, manifest, &budget, err, sizeof(err));
        if (rc == 1) { status = NL_SNAP_SKIPPED; break; }
        if (rc < 0) { status = NL_SNAP_ERR; break; }
        copied++;
    }

    if (status != NL_SNAP_OK) {
        snapshot_remove_tree(dir);
    } else if (copied == 0) {
        snapshot_remove_tree(dir);   /* 无可保留内容，不留空快照 */
        status = NL_SNAP_SKIPPED;
    } else {
        if (out_dir && cap) snprintf(out_dir, cap, "%s", dir);
        enforce_quota();
    }

    flock(lfd, LOCK_UN);
    close(lfd);
    return status;
}

int nl_snapshot_undo(int n, char *err, size_t errcap)
{
    if (n <= 0) n = 1;
    char names[256][64];
    int total = list_dirs(names, 256);
    if (total < n) {
        if (err) snprintf(err, errcap, "没有可撤销的快照（现有 %d 个）", total);
        return -1;
    }
    char dir[PATH_MAX];
    snprintf(dir, sizeof(dir), "%s/%s", nl_snapshot_root(), names[total - n]);
    char manifest[PATH_MAX * 2 + 16];
    snprintf(manifest, sizeof(manifest), "%.4000s/manifest", dir);

    FILE *m = fopen(manifest, "r");
    if (!m) {
        if (err) snprintf(err, errcap, "快照清单缺失：%s", manifest);
        return -1;
    }
    char line[PATH_MAX * 2 + 8];
    int restored = 0;
    while (fgets(line, sizeof(line), m) != NULL) {
        size_t len = strlen(line);
        while (len > 0 && (line[len - 1] == '\n' || line[len - 1] == '\r')) line[--len] = '\0';
        if (len < 3) continue;
        char kind = line[0];
        char *p1 = line + 2;
        char *sp = strchr(p1, ' ');
        if (!sp) continue;
        *sp = '\0';
        char *p2 = sp + 1;
        if (kind == 'S') continue;              /* 敏感项从未落盘 */
        if (kind == 'F') {
            char parent[PATH_MAX];
            snprintf(parent, sizeof(parent), "%s", p1);
            char *slash = strrchr(parent, '/');
            if (slash) { *slash = '\0'; mkdir_p(parent, 0700); }
            if (copy_file(p2, p1, 0644) == 0) restored++;
        } else if (kind == 'D') {
            if (mkdir_p(p1, 0700) == 0) restored++;
        }
    }
    fclose(m);
    if (restored == 0) {
        if (err) snprintf(err, errcap, "快照中没有可恢复项");
        return -1;
    }
    return 0;
}
