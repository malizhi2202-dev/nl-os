/*
 * capability_scan.c — 能力自发现（ADR-012 / T03）
 *
 * 只读：不创建、不修改任何系统文件。
 * 规则：
 *   - 只扫调用方传入的目录（CLI 传编译期白名单，测试传临时目录）
 *   - lstat 区分普通文件与符号链接；符号链接一律 trusted=0（只列不执行）
 *   - 权限等级查 capability_perm_map.h，未登记 → L4
 *   - 同 (dev, inode) 只登记一次；同名不同路径 → id 追加 @<hash8>
 *   - 目录不可读只跳过，不整体失败
 */
#include "capability_scan.h"
#include "capability_perm_map.h"

#include <dirent.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#define NL_SCAN_MAX_SEEN 8192

static const char *const DEFAULT_DIRS[] = {
    "/usr/bin", "/bin", "/usr/sbin", "/sbin", "/usr/local/bin", "/usr/local/sbin"
};

const char *const *nl_cap_default_dirs(int *count)
{
    if (count) *count = (int)(sizeof(DEFAULT_DIRS) / sizeof(DEFAULT_DIRS[0]));
    return DEFAULT_DIRS;
}

/* FNV-1a 32bit → 8 hex，用于 id 冲突后缀 */
static void fnv8(const char *s, char out[9])
{
    unsigned long long h = 1469598103934665603ULL;
    if (s) {
        for (const unsigned char *p = (const unsigned char *)s; *p; p++) {
            h ^= (unsigned long long)*p;
            h *= 1099511628211ULL;
        }
    }
    snprintf(out, 9, "%08llx", (unsigned long long)(h & 0xffffffffULL));
}

typedef struct {
    unsigned long inode;
    dev_t         dev;
} NlSeenInode;

int nl_cap_scan_dirs(const char *const *dirs, int n, NlCapRegistry *out)
{
    if (!dirs || n <= 0 || !out) return -1;

    static NlSeenInode seen[NL_SCAN_MAX_SEEN];
    int seen_n = 0;
    int added = 0;

    for (int i = 0; i < n; i++) {
        const char *dir = dirs[i];
        if (!dir || !*dir) continue;
        DIR *d = opendir(dir);
        if (!d) continue;   /* 不可读：跳过该目录，不整体失败 */

        struct dirent *e;
        while ((e = readdir(d)) != NULL) {
            if (e->d_name[0] == '\0' || e->d_name[0] == '.') continue;

            char full[PATH_MAX];
            int w = snprintf(full, sizeof(full), "%s/%s", dir, e->d_name);
            if (w < 0 || w >= (int)sizeof(full)) continue;

            struct stat st;
            if (lstat(full, &st) != 0) continue;
            if (!S_ISREG(st.st_mode) && !S_ISLNK(st.st_mode)) continue;

            /* 同设备同 inode 去重（/bin → /usr/bin 之类的硬链接/挂载） */
            int dup = 0;
            for (int k = 0; k < seen_n; k++) {
                if (seen[k].inode == (unsigned long)st.st_ino && seen[k].dev == st.st_dev) {
                    dup = 1;
                    break;
                }
            }
            if (dup) continue;
            if (seen_n < NL_SCAN_MAX_SEEN) {
                seen[seen_n].inode = (unsigned long)st.st_ino;
                seen[seen_n].dev = st.st_dev;
                seen_n++;
            }

            NlCapability c;
            memset(&c, 0, sizeof(c));
            snprintf(c.id, sizeof(c.id), "%.127s", e->d_name);
            snprintf(c.name, sizeof(c.name), "%.127s", e->d_name);
            c.kind = S_ISLNK(st.st_mode) ? NL_CAP_KIND_SYMLINK : NL_CAP_KIND_FILE;
            c.trusted = (c.kind == NL_CAP_KIND_FILE) ? 1 : 0;
            c.inode = (unsigned long)st.st_ino;
            c.perm_level = nl_cap_perm_for(e->d_name);
            c.source = (char *)"scan";

            /* 普通文件登记 realpath；符号链接登记字面路径（保留指向关系供人查看） */
            char rp[PATH_MAX];
            if (c.kind == NL_CAP_KIND_FILE && realpath(full, rp) != NULL) {
                c.path = strdup(rp);
            } else {
                c.path = strdup(full);
            }
            if (!c.path) continue;

            /* id 冲突：同名不同路径 → 追加 @hash8；仍冲突则丢弃 */
            if (nl_cap_find(out, c.id) != NULL) {
                char suf[9];
                fnv8(full, suf);
                char newid[NL_CAP_MAX_ID_LEN];
                snprintf(newid, sizeof(newid), "%.118s@%s", e->d_name, suf);
                snprintf(c.id, sizeof(c.id), "%s", newid);
                if (nl_cap_find(out, c.id) != NULL) {
                    free(c.path);
                    continue;
                }
            }

            if (nl_cap_add(out, &c) == NL_CAP_OK) added++;
            free(c.path);   /* nl_cap_add 已深拷贝 */
        }
        closedir(d);
    }
    return added;
}

/* T04 实现 */
int nl_cap_selftest(NlCapRegistry *r, NlSelfTestReport *rep)
{
    (void)r;
    (void)rep;
    return -1;
}
