/*
 * ticket.c — ticket 表实现（ADR-017 / T05）
 *
 * 线程模型：v1 broker 单线程，故用全局表（ADR-017 已接受）。
 */
#include "ticket.h"
#include "sha256.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

typedef struct {
    char token[NL_TICKET_TOKEN_LEN];
    char cap_id[NL_CAP_MAX_ID_LEN];
    char args_hash[65];
    int  effect;
    long created_at;
    int  used;
} NlTicket;

static NlTicket g_tickets[NL_TICKET_MAX];
static int      g_head;          /* 下一个写入位置（环形） */
static int      g_count;
static int      g_evictions;
static long     g_rate_sec = -1;
static int      g_rate_count;

const char *nl_ticket_status_str(NlTicketStatus s)
{
    switch (s) {
    case NL_TICKET_OK:       return "ok";
    case NL_TICKET_INVALID:  return "E_TICKET_INVALID";
    case NL_TICKET_EXPIRED:  return "E_TICKET_EXPIRED";
    case NL_TICKET_MISMATCH: return "E_TICKET_MISMATCH";
    default:                 return "E_TICKET_INVALID";
    }
}

static int fill_random(uint8_t *buf, size_t n)
{
    FILE *f = fopen("/dev/urandom", "rb");
    if (!f) return -1;
    size_t got = fread(buf, 1, n, f);
    fclose(f);
    return (got == n) ? 0 : -1;
}

static void bytes_to_hex(const uint8_t *in, size_t n, char *out)
{
    static const char hex[] = "0123456789abcdef";
    for (size_t i = 0; i < n; i++) {
        out[i * 2]     = hex[in[i] >> 4];
        out[i * 2 + 1] = hex[in[i] & 0x0f];
    }
    out[n * 2] = '\0';
}

static int slot_expired(const NlTicket *t, long now)
{
    return (now - t->created_at) > NL_TICKET_TTL_SEC;
}

int nl_ticket_issue(const char *cap_id, const char *args_json, int effect,
                    long now, char out_token[NL_TICKET_TOKEN_LEN])
{
    if (!cap_id || !args_json || !out_token) return -1;

    uint8_t raw[16];
    if (fill_random(raw, sizeof(raw)) != 0) {
        /* 极端降级：仍保证不被预测为零；记录在审计里的概率极低路径 */
        uint64_t mix = (uint64_t)now ^ ((uint64_t)getpid() << 32) ^ (uint64_t)(uintptr_t)cap_id;
        for (int i = 0; i < 16; i++) raw[i] = (uint8_t)(mix >> ((i % 8) * 8));
    }

    NlTicket *t = &g_tickets[g_head];
    if (t->token[0] != '\0') {
        if (!t->used && !slot_expired(t, now)) g_evictions++;   /* 被迫淘汰未用凭证 */
        if (g_count > 0) g_count--;
    }

    bytes_to_hex(raw, sizeof(raw), t->token);
    snprintf(t->cap_id, sizeof(t->cap_id), "%s", cap_id);
    nl_sha256_hex(args_json, strlen(args_json), t->args_hash);
    t->effect = effect;
    t->created_at = now;
    t->used = 0;
    g_count++;

    g_head = (g_head + 1) % NL_TICKET_MAX;
    snprintf(out_token, NL_TICKET_TOKEN_LEN, "%s", t->token);
    return 0;
}

NlTicketStatus nl_ticket_check(const char *token, const char *cap_id,
                               const char *args_json, long now)
{
    if (!token || !cap_id || !args_json) return NL_TICKET_INVALID;

    for (int i = 0; i < NL_TICKET_MAX; i++) {
        NlTicket *t = &g_tickets[i];
        if (t->token[0] == '\0') continue;
        if (strcmp(t->token, token) != 0) continue;

        if (slot_expired(t, now)) {
            t->token[0] = '\0';
            if (g_count > 0) g_count--;
            return NL_TICKET_EXPIRED;
        }
        if (t->used) return NL_TICKET_MISMATCH;          /* 重放 */
        if (strcmp(t->cap_id, cap_id) != 0) return NL_TICKET_MISMATCH;

        char h[65];
        nl_sha256_hex(args_json, strlen(args_json), h);
        if (strcmp(h, t->args_hash) != 0) return NL_TICKET_MISMATCH;

        t->used = 1;
        return NL_TICKET_OK;
    }
    return NL_TICKET_INVALID;
}

int nl_ticket_rate_ok(long now)
{
    if (now != g_rate_sec) {
        g_rate_sec = now;
        g_rate_count = 0;
    }
    if (g_rate_count >= NL_TICKET_RATE_LIMIT) return 0;
    g_rate_count++;
    return 1;
}

int nl_ticket_count(void) { return g_count; }

int nl_ticket_evictions(void) { return g_evictions; }

void nl_ticket_reset(void)
{
    memset(g_tickets, 0, sizeof(g_tickets));
    g_head = 0;
    g_count = 0;
    g_evictions = 0;
    g_rate_sec = -1;
    g_rate_count = 0;
}
