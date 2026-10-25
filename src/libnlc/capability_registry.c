/*
 * capability_registry.c — 能力注册表实现（ADR-012 / T02）
 *
 * 格式：JSON Lines，首行元数据（format_version），其余每行一项能力。
 * 写入：flock 排他锁 + 临时文件 + fsync + rename 原子替换，权限 0600。
 * 读取：按行流式解析（OpenWrt 内存约束），坏行跳过并计数，不整体失败。
 */
#include "capability_registry.h"
#include "json_helper.h"
#include "cJSON.h"

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

#define NL_CAP_GROW            32
#define NL_CAP_LOCK_TIMEOUT_MS 200
#define NL_CAP_LOCK_STEP_MS    10

struct NlCapRegistry {
    NlCapability *items;
    int           count;
    int           cap;
    int           skipped;
};

/* ============================================================
 * 小工具
 * ============================================================ */

static char *dup_or_null(const char *s) { return s ? strdup(s) : NULL; }

static void cap_clear(NlCapability *c)
{
    free(c->path);
    for (int i = 0; i < c->slot_count; i++) {
        free(c->slots[i]);
        c->slots[i] = NULL;
    }
    free(c->source);
    memset(c, 0, sizeof(*c));
}

/* mkdir -p，权限 0700 */
static int ensure_dir(const char *path)
{
    char buf[PATH_MAX];
    size_t n = strlen(path);
    if (n == 0 || n >= sizeof(buf)) return -1;
    memcpy(buf, path, n + 1);
    for (char *p = buf + 1; *p; p++) {
        if (*p != '/') continue;
        *p = '\0';
        if (mkdir(buf, 0700) != 0 && errno != EEXIST) return -1;
        *p = '/';
    }
    if (mkdir(buf, 0700) != 0 && errno != EEXIST) return -1;
    return 0;
}

static void parent_dir(const char *path, char *out, size_t n)
{
    snprintf(out, n, "%s", path);
    char *slash = strrchr(out, '/');
    if (slash) *slash = '\0';
    else snprintf(out, n, ".");
}

static const char *perm_to_str(int p)
{
    switch (p) {
    case 1:  return "L1";
    case 2:  return "L2";
    case 3:  return "L3";
    default: return "L4";
    }
}

static int perm_to_int(const char *s)
{
    if (!s || (s[0] != 'L' && s[0] != 'l')) return -1;
    if (s[1] < '1' || s[1] > '4' || s[2] != '\0') return -1;
    return s[1] - '0';
}

static int write_all(int fd, const char *buf, size_t len)
{
    size_t off = 0;
    while (off < len) {
        ssize_t w = write(fd, buf + off, len - off);
        if (w < 0) {
            if (errno == EINTR) continue;
            return -1;
        }
        off += (size_t)w;
    }
    return 0;
}

static int write_json_line(int fd, cJSON *obj)
{
    char *s = nl_json_print(obj);
    if (!s) return -1;
    int rc = write_all(fd, s, strlen(s));
    if (rc == 0) rc = write_all(fd, "\n", 1);
    free(s);
    return rc;
}

static void iso_now(char *buf, size_t n)
{
    time_t t = time(NULL);
    struct tm tm;
    gmtime_r(&t, &tm);
    strftime(buf, n, "%Y-%m-%dT%H:%M:%SZ", &tm);
}

/* 排他锁，超时返回 -1 且 errno=ETIMEDOUT */
static int lock_acquire(const char *lock_path)
{
    int fd = open(lock_path, O_CREAT | O_RDWR, 0600);
    if (fd < 0) return -1;
    for (int waited = 0; waited <= NL_CAP_LOCK_TIMEOUT_MS; waited += NL_CAP_LOCK_STEP_MS) {
        if (flock(fd, LOCK_EX | LOCK_NB) == 0) return fd;
        if (errno != EWOULDBLOCK && errno != EINTR) {
            close(fd);
            return -1;
        }
        struct timespec ts = { 0, NL_CAP_LOCK_STEP_MS * 1000000L };
        nanosleep(&ts, NULL);
    }
    close(fd);
    errno = ETIMEDOUT;
    return -1;
}

/* ============================================================
 * 单行解析
 * ============================================================ */

static int parse_cap_line(const char *line, NlCapability *out)
{
    NlJsonDoc doc;
    memset(out, 0, sizeof(*out));
    if (nl_json_parse(line, &doc) != 0) return -1;

    const char *id = NULL, *name = NULL, *path = NULL, *perm = NULL;
    const char *source = NULL, *kind = NULL;
    int trusted = 0;
    int rc = -1;

    if (nl_json_get_string(doc.root, "id", &id) != 0 || !id || !*id) goto done;
    if (nl_json_get_string(doc.root, "path", &path) != 0 || !path || !*path) goto done;
    if (nl_json_get_string(doc.root, "perm", &perm) != 0) goto done;
    if (nl_json_get_string(doc.root, "source", &source) != 0) goto done;
    if (nl_json_get_bool(doc.root, "trusted", &trusted) != 0) goto done;

    int pl = perm_to_int(perm);
    if (pl < 0) goto done;   /* 非法 perm → 该行无效 */

    snprintf(out->id, sizeof(out->id), "%s", id);
    if (nl_json_get_string(doc.root, "name", &name) == 0 && name && *name)
        snprintf(out->name, sizeof(out->name), "%s", name);
    else
        snprintf(out->name, sizeof(out->name), "%s", id);

    out->path = strdup(path);
    out->source = strdup(source);
    if (!out->path || !out->source) { cap_clear(out); goto done; }

    out->perm_level = pl;
    out->trusted = trusted ? 1 : 0;
    out->kind = NL_CAP_KIND_FILE;
    if (nl_json_get_string(doc.root, "kind", &kind) == 0 && kind &&
        strcmp(kind, "symlink") == 0)
        out->kind = NL_CAP_KIND_SYMLINK;

    cJSON *ino = cJSON_GetObjectItem(doc.root, "inode");
    if (cJSON_IsNumber(ino)) out->inode = (unsigned long)cJSON_GetNumberValue(ino);

    cJSON *slots = cJSON_GetObjectItem(doc.root, "slots");
    if (cJSON_IsArray(slots)) {
        int n = cJSON_GetArraySize(slots);
        for (int i = 0; i < n && out->slot_count < NL_CAP_MAX_SLOTS; i++) {
            cJSON *it = cJSON_GetArrayItem(slots, i);
            const char *sv = cJSON_GetStringValue(it);
            if (sv) out->slots[out->slot_count++] = strdup(sv);
        }
    }
    rc = 0;

done:
    if (rc != 0) cap_clear(out);
    nl_json_free(&doc);
    return rc;
}

/* ============================================================
 * 公开 API
 * ============================================================ */

NlCapRegistry *nl_cap_create(void)
{
    NlCapRegistry *r = calloc(1, sizeof(*r));
    if (!r) return NULL;
    r->cap = NL_CAP_GROW;
    r->items = calloc((size_t)r->cap, sizeof(*r->items));
    if (!r->items) {
        free(r);
        return NULL;
    }
    return r;
}

NlCapRegistry *nl_cap_load(const char *path, NlCapLoadErr *err)
{
    if (err) *err = NL_CAP_OK;
    if (!path) {
        if (err) *err = NL_CAP_ERR_IO;
        return NULL;
    }

    NlCapRegistry *r = nl_cap_create();
    if (!r) {
        if (err) *err = NL_CAP_ERR_MEM;
        return NULL;
    }

    FILE *f = fopen(path, "r");
    if (!f) {
        if (errno == ENOENT) return r;   /* 首次运行：空注册表不算错误 */
        if (err) *err = NL_CAP_ERR_IO;
        nl_cap_free(r);
        return NULL;
    }

    char *line = NULL;
    size_t line_cap = 0;
    ssize_t n;
    int seen_meta = 0;
    NlCapLoadErr local = NL_CAP_OK;

    while ((n = getline(&line, &line_cap, f)) > 0) {
        while (n > 0 && (line[n - 1] == '\n' || line[n - 1] == '\r')) line[--n] = '\0';
        if (n == 0) continue;

        if (!seen_meta) {
            NlJsonDoc doc;
            if (nl_json_parse(line, &doc) != 0) {
                local = NL_CAP_ERR_PARSE;
                break;
            }
            int ver = 0;
            int ok = nl_json_get_int(doc.root, "format_version", &ver);
            nl_json_free(&doc);
            if (ok != 0) { local = NL_CAP_ERR_PARSE; break; }
            if (ver > NL_CAP_FORMAT_VERSION) { local = NL_CAP_ERR_VERSION; break; }
            seen_meta = 1;
            continue;
        }

        NlCapability c;
        if (parse_cap_line(line, &c) != 0) {
            r->skipped++;
            continue;
        }
        int rc = nl_cap_add(r, &c);
        cap_clear(&c);
        if (rc != NL_CAP_OK) { local = NL_CAP_ERR_MEM; break; }
    }
    free(line);
    fclose(f);

    if (local != NL_CAP_OK) {
        if (err) *err = local;
        nl_cap_free(r);
        return NULL;
    }
    return r;
}

int nl_cap_save_atomic(NlCapRegistry *r, const char *path)
{
    if (!r || !path || !*path) return NL_CAP_ERR_IO;

    char dir[PATH_MAX], lock_path[PATH_MAX + 16], tmp_path[PATH_MAX + 32];
    parent_dir(path, dir, sizeof(dir));
    if (ensure_dir(dir) != 0) return NL_CAP_ERR_IO;

    snprintf(lock_path, sizeof(lock_path), "%s.lock", path);
    int lock_fd = lock_acquire(lock_path);
    if (lock_fd < 0) return (errno == ETIMEDOUT) ? NL_CAP_ERR_LOCK : NL_CAP_ERR_IO;

    snprintf(tmp_path, sizeof(tmp_path), "%s.tmp.%ld", path, (long)getpid());
    int fd = open(tmp_path, O_WRONLY | O_CREAT | O_TRUNC, 0600);
    if (fd < 0) {
        flock(lock_fd, LOCK_UN);
        close(lock_fd);
        return NL_CAP_ERR_IO;
    }

    int rc = NL_CAP_OK;
    char stamp[32];
    iso_now(stamp, sizeof(stamp));

    cJSON *meta = nl_json_new_object();
    if (!meta) rc = NL_CAP_ERR_MEM;
    if (rc == NL_CAP_OK) {
        nl_json_add_int(meta, "format_version", NL_CAP_FORMAT_VERSION);
        nl_json_add_string(meta, "scanned_at", stamp);
        nl_json_add_string(meta, "scanner", "nl-os/0.2");
        nl_json_add_bool(meta, "has_systemd",
                         access("/run/systemd/system", F_OK) == 0 ? 1 : 0);
        if (write_json_line(fd, meta) != 0) rc = NL_CAP_ERR_IO;
        cJSON_Delete(meta);
    }

    for (int i = 0; rc == NL_CAP_OK && i < r->count; i++) {
        const NlCapability *c = &r->items[i];
        cJSON *o = nl_json_new_object();
        if (!o) { rc = NL_CAP_ERR_MEM; break; }
        nl_json_add_string(o, "id", c->id);
        nl_json_add_string(o, "name", c->name);
        nl_json_add_string(o, "path", c->path ? c->path : "");
        nl_json_add_int(o, "inode", (int)c->inode);
        nl_json_add_string(o, "kind",
                           c->kind == NL_CAP_KIND_SYMLINK ? "symlink" : "file");
        nl_json_add_string(o, "perm", perm_to_str(c->perm_level));
        cJSON *slots = nl_json_new_array();
        if (slots) {
            for (int k = 0; k < c->slot_count; k++)
                cJSON_AddItemToArray(slots, cJSON_CreateString(c->slots[k]));
            nl_json_add_item(o, "slots", slots);
        }
        nl_json_add_string(o, "source", c->source ? c->source : "scan");
        nl_json_add_bool(o, "trusted", c->trusted);
        if (write_json_line(fd, o) != 0) rc = NL_CAP_ERR_IO;
        cJSON_Delete(o);
    }

    if (rc == NL_CAP_OK && fsync(fd) != 0) rc = NL_CAP_ERR_IO;
    close(fd);

    if (rc == NL_CAP_OK) {
        if (rename(tmp_path, path) != 0) {
            rc = NL_CAP_ERR_IO;
        } else {
            chmod(path, 0600);
            int dfd = open(dir, O_RDONLY);
            if (dfd >= 0) {
                fsync(dfd);
                close(dfd);
            }
        }
    }
    if (rc != NL_CAP_OK) unlink(tmp_path);

    flock(lock_fd, LOCK_UN);
    close(lock_fd);
    return rc;
}

int nl_cap_add(NlCapRegistry *r, const NlCapability *c)
{
    if (!r || !c || c->id[0] == '\0' || !c->path || c->path[0] == '\0')
        return NL_CAP_ERR_PARSE;

    if (r->count == r->cap) {
        int ncap = r->cap * 2;
        NlCapability *ni = realloc(r->items, (size_t)ncap * sizeof(*ni));
        if (!ni) return NL_CAP_ERR_MEM;
        memset(ni + r->cap, 0, (size_t)(ncap - r->cap) * sizeof(*ni));
        r->items = ni;
        r->cap = ncap;
    }

    NlCapability *d = &r->items[r->count];
    memset(d, 0, sizeof(*d));
    snprintf(d->id, sizeof(d->id), "%s", c->id);
    snprintf(d->name, sizeof(d->name), "%s", c->name[0] ? c->name : c->id);
    d->path = dup_or_null(c->path);
    d->source = dup_or_null(c->source ? c->source : "scan");
    if (!d->path || !d->source) {
        cap_clear(d);
        return NL_CAP_ERR_MEM;
    }
    d->inode = c->inode;
    d->kind = c->kind;
    d->perm_level = (c->perm_level >= 1 && c->perm_level <= 4) ? c->perm_level : 4;
    d->trusted = c->trusted ? 1 : 0;
    d->slot_count = 0;
    for (int i = 0; i < c->slot_count && i < NL_CAP_MAX_SLOTS; i++)
        if (c->slots[i]) d->slots[d->slot_count++] = dup_or_null(c->slots[i]);

    r->count++;
    return NL_CAP_OK;
}

const NlCapability *nl_cap_find(const NlCapRegistry *r, const char *id)
{
    if (!r || !id) return NULL;
    for (int i = 0; i < r->count; i++)
        if (strcmp(r->items[i].id, id) == 0) return &r->items[i];
    return NULL;
}

int nl_cap_count(const NlCapRegistry *r) { return r ? r->count : 0; }

int nl_cap_skipped(const NlCapRegistry *r) { return r ? r->skipped : 0; }

const char *nl_cap_default_path(void)
{
    static char buf[PATH_MAX];
    const char *base = getenv("XDG_STATE_HOME");
    if (base && base[0]) {
        snprintf(buf, sizeof(buf), "%s/nl-os/capabilities.jsonl", base);
    } else {
        const char *home = getenv("HOME");
        snprintf(buf, sizeof(buf), "%s/.local/state/nl-os/capabilities.jsonl",
                 (home && home[0]) ? home : "/tmp");
    }
    return buf;
}

const char *nl_cap_err_str(NlCapLoadErr e)
{
    switch (e) {
    case NL_CAP_OK:          return "ok";
    case NL_CAP_ERR_IO:      return "io error";
    case NL_CAP_ERR_PARSE:   return "parse error";
    case NL_CAP_ERR_VERSION: return "unsupported format_version";
    case NL_CAP_ERR_LOCK:    return "lock timeout";
    case NL_CAP_ERR_MEM:     return "out of memory";
    default:                 return "unknown";
    }
}

void nl_cap_free(NlCapRegistry *r)
{
    if (!r) return;
    for (int i = 0; i < r->count; i++) cap_clear(&r->items[i]);
    free(r->items);
    free(r);
}
