/*
 * freeze.c — .tools 固化与校验（ADR-014 / T14）
 */
#include "freeze.h"
#include "sha256.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define NL_FREEZE_LINE_MAX 4096
#define NL_FREEZE_SUM_PREFIX 20   /* "# @checksum: sha256:" 的长度 */
#define NL_FREEZE_MAX_LINES 4096

static int read_lines(const char *path, char **lines, int max, int *count)
{
    FILE *f = fopen(path, "r");
    if (!f) return -1;
    int n = 0;
    char buf[NL_FREEZE_LINE_MAX];
    while (fgets(buf, sizeof(buf), f) != NULL) {
        size_t len = strlen(buf);
        while (len > 0 && (buf[len - 1] == '\n' || buf[len - 1] == '\r')) buf[--len] = '\0';
        if (n >= max) { fclose(f); return -1; }
        lines[n] = strdup(buf);
        if (!lines[n]) { fclose(f); return -1; }
        n++;
    }
    fclose(f);
    *count = n;
    return 0;
}

static void free_lines(char **lines, int n)
{
    for (int i = 0; i < n; i++) free(lines[i]);
}

/* 逐行链式 hash：h_i = sha256(h_{i-1} || line_i)，返回最终 hex */
static void chain_hash(char *const *lines, int n, char out[65])
{
    unsigned char h[32];
    memset(h, 0, sizeof(h));
    for (int i = 0; i < n; i++) {
        char buf[64 + NL_FREEZE_LINE_MAX + 2];
        char prev[65];
        for (int k = 0; k < 32; k++) snprintf(prev + k * 2, 3, "%02x", h[k]);
        int m = snprintf(buf, sizeof(buf), "%s%s", prev, lines[i]);
        if (m < 0) m = 0;
        nl_sha256(buf, (size_t)m, h);
    }
    for (int k = 0; k < 32; k++) snprintf(out + k * 2, 3, "%02x", h[k]);
    out[64] = '\0';
}

int nl_freeze_is_frozen(const char *path)
{
    FILE *f = fopen(path, "r");
    if (!f) return 0;
    char buf[256];
    int found = 0;
    while (fgets(buf, sizeof(buf), f) != NULL) {
        if (strncmp(buf, "# @checksum:", 12) == 0) { found = 1; break; }
        if (buf[0] != '#' && buf[0] != '\n' && buf[0] != '\r') break;   /* 头部已结束 */
    }
    fclose(f);
    return found;
}

int nl_freeze_write(const char *path, char *const *body, int n_body,
                    const char *policy_line)
{
    if (!path || !body || n_body <= 0) return -1;
    /* 只接受注释行，避免调用方把正文/元字符塞进策略头 */
    if (policy_line && policy_line[0] != '#') return -1;

    char digest[65];
    if (policy_line) {
        /* 校验覆盖策略头 + 正文：改 allow-l2 也算篡改 */
        int n_all = n_body + 1;
        char **all = malloc((size_t)n_all * sizeof(char *));
        if (!all) return -1;
        all[0] = (char *)policy_line;
        for (int i = 0; i < n_body; i++) all[i + 1] = body[i];
        chain_hash(all, n_all, digest);
        free(all);
    } else {
        chain_hash(body, n_body, digest);
    }

    char tmp[4096];
    snprintf(tmp, sizeof(tmp), "%s.tmp", path);
    FILE *f = fopen(tmp, "w");
    if (!f) return -1;
    fprintf(f, "# @format: %d\n", NL_FREEZE_FORMAT);
    fprintf(f, "# @checksum: sha256:%s\n", digest);
    if (policy_line) fprintf(f, "%s\n", policy_line);
    for (int i = 0; i < n_body; i++) fprintf(f, "%s\n", body[i]);
    fflush(f);
    fclose(f);
    if (rename(tmp, path) != 0) {
        remove(tmp);
        return -1;
    }
    return 0;
}

NlFreezeStatus nl_freeze_verify(const char *path)
{
    if (!path) return NL_FREEZE_IO_ERR;

    char *lines[NL_FREEZE_MAX_LINES];
    int n = 0;
    if (read_lines(path, lines, NL_FREEZE_MAX_LINES, &n) != 0) {
        free_lines(lines, n);
        return NL_FREEZE_IO_ERR;
    }

    char expect[65] = "";
    int body_start = -1;
    for (int i = 0; i < n; i++) {
        if (strncmp(lines[i], "# @checksum: sha256:", NL_FREEZE_SUM_PREFIX) == 0) {
            snprintf(expect, sizeof(expect), "%s", lines[i] + NL_FREEZE_SUM_PREFIX);
            body_start = i + 1;
        }
    }
    if (body_start < 0) {
        free_lines(lines, n);
        return NL_FREEZE_PLAIN;
    }

    char got[65];
    chain_hash(lines + body_start, n - body_start, got);
    free_lines(lines, n);
    return (strcmp(got, expect) == 0) ? NL_FREEZE_OK : NL_FREEZE_TAMPERED;
}
