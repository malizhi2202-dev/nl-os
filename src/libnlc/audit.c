/*
 * audit.c — 结构化审计实现（T07）
 */
#include "audit.h"
#include "sha256.h"
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

const char *nl_audit_path(void)
{
    static char buf[PATH_MAX];
    const char *base = getenv("XDG_STATE_HOME");
    if (base && base[0]) {
        snprintf(buf, sizeof(buf), "%s/nl-os/audit.jsonl", base);
    } else {
        const char *home = getenv("HOME");
        snprintf(buf, sizeof(buf), "%s/.local/state/nl-os/audit.jsonl",
                 (home && home[0]) ? home : "/tmp");
    }
    return buf;
}

static void make_audit_id(char *out, size_t cap)
{
    unsigned char raw[8] = { 0 };
    FILE *f = fopen("/dev/urandom", "rb");
    if (f) {
        size_t got = fread(raw, 1, sizeof(raw), f);
        fclose(f);
        if (got != sizeof(raw)) memset(raw, 0, sizeof(raw));
    }
    static const char hex[] = "0123456789abcdef";
    size_t n = (cap - 1) / 2;
    if (n > sizeof(raw)) n = sizeof(raw);
    for (size_t i = 0; i < n; i++) {
        out[i * 2]     = hex[raw[i] >> 4];
        out[i * 2 + 1] = hex[raw[i] & 0x0f];
    }
    out[n * 2] = '\0';
}

int nl_audit_emit(const NlAuditRec *rec)
{
    if (!rec) return -1;
    const char *path = nl_audit_path();

    char dir[PATH_MAX];
    snprintf(dir, sizeof(dir), "%s", path);
    char *slash = strrchr(dir, '/');
    if (slash) *slash = '\0';
    if (ensure_dir(dir) != 0) return -1;

    cJSON *o = cJSON_CreateObject();
    if (!o) return -1;

    char id[24];
    make_audit_id(id, sizeof(id));

    char in_hash[17] = "";
    if (rec->input && rec->input[0]) {
        char full[65];
        nl_sha256_hex(rec->input, strlen(rec->input), full);
        memcpy(in_hash, full, 16);
        in_hash[16] = '\0';
    }

    cJSON_AddStringToObject(o, "audit_id", id);
    cJSON_AddNumberToObject(o, "ts", (double)time(NULL));
    cJSON_AddStringToObject(o, "capability_id", rec->capability_id ? rec->capability_id : "");
    cJSON_AddStringToObject(o, "verdict", rec->verdict ? rec->verdict : "");
    cJSON_AddStringToObject(o, "effect", rec->effect ? rec->effect : "");
    cJSON_AddNumberToObject(o, "perm_level", (double)rec->perm_level);
    cJSON_AddBoolToObject(o, "fast_path", rec->fast_path ? 1 : 0);
    cJSON_AddNumberToObject(o, "probability",
                            (rec->probability < 0) ? -1 : rec->probability);
    cJSON_AddNumberToObject(o, "duration_us", (double)rec->duration_us);
    cJSON_AddStringToObject(o, "result", rec->result ? rec->result : "");
    cJSON_AddStringToObject(o, "reason", rec->reason ? rec->reason : "");
    cJSON_AddStringToObject(o, "args_hash", in_hash);
    cJSON_AddStringToObject(o, "corr", rec->corr ? rec->corr : "");

    char *line = cJSON_PrintUnformatted(o);
    cJSON_Delete(o);
    if (!line) return -1;

    int fd = open(path, O_WRONLY | O_CREAT | O_APPEND, 0600);
    if (fd < 0) {
        free(line);
        return -1;
    }
    (void)flock(fd, LOCK_EX);
    size_t len = strlen(line);
    ssize_t w = write(fd, line, len);
    if (w == (ssize_t)len) {
        /* 换行写失败不影响主记录（已在同一 fd 上 flush） */
        if (write(fd, "\n", 1) < 0) { /* ignore */ }
    }
    (void)flock(fd, LOCK_UN);
    close(fd);
    free(line);
    return 0;
}

int nl_audit_query(long since, int limit, char *out, size_t cap)
{
    if (!out || cap < 3) return -1;
    if (limit <= 0 || limit > NL_AUDIT_MAX_LIMIT) limit = 50;

    FILE *f = fopen(nl_audit_path(), "r");
    if (!f) {
        snprintf(out, cap, "[]");
        return 0;
    }

    /* 环形保留"最新的 limit 条"：审计查询用于排障，用户要的是最近记录 */
    char **ring = calloc((size_t)limit, sizeof(char *));
    if (!ring) {
        fclose(f);
        snprintf(out, cap, "[]");
        return -1;
    }
    int seen = 0;
    char *line = NULL;
    size_t line_cap = 0;
    ssize_t n;

    while ((n = getline(&line, &line_cap, f)) > 0) {
        while (n > 0 && (line[n - 1] == '\n' || line[n - 1] == '\r')) line[--n] = '\0';
        if (n == 0) continue;

        cJSON *o = cJSON_Parse(line);
        if (!o) continue;
        cJSON *ts = cJSON_GetObjectItem(o, "ts");
        double tsv = cJSON_IsNumber(ts) ? cJSON_GetNumberValue(ts) : 0;
        cJSON_Delete(o);
        if ((long)tsv < since) continue;

        int slot = seen % limit;
        free(ring[slot]);
        ring[slot] = strdup(line);
        seen++;
    }
    free(line);
    fclose(f);

    int count = (seen < limit) ? seen : limit;
    int first = (seen < limit) ? 0 : (seen % limit);

    /* 组装为有序数组（旧 → 新） */
    const char *items[NL_AUDIT_MAX_LIMIT];
    for (int i = 0; i < count; i++)
        items[i] = ring[(first + i) % limit];

    /* 字节不够时**从最旧的开始丢**，保证最近记录一定在结果里 */
    size_t total = 2;   /* [] */
    for (int i = 0; i < count; i++) total += strlen(items[i]) + 1;
    int start = 0;
    while (start < count && total + 1 > cap) {
        total -= strlen(items[start]) + 1;
        start++;
    }

    size_t used = 0;
    out[used++] = '[';
    for (int i = start; i < count; i++) {
        size_t rl = strlen(items[i]);
        if (used + rl + 2 >= cap) break;
        if (used > 1) out[used++] = ',';
        memcpy(out + used, items[i], rl);
        used += rl;
    }
    out[used++] = ']';
    out[used] = '\0';

    for (int i = 0; i < limit; i++) free(ring[i]);
    free(ring);
    return count - start;
}
