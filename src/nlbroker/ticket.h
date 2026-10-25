/*
 * ticket.h — 两阶段执行的凭证（ADR-017 / T05）
 *
 * 一次性 + TTL + 绑定 capability_id 与 args_hash；表上限 1024（环形淘汰）+ 每会话限速。
 * broker 重启即全部作废（内存表）。
 */
#ifndef NL_BROKER_TICKET_H
#define NL_BROKER_TICKET_H

#include "capability_registry.h"

#define NL_TICKET_MAX         1024
#define NL_TICKET_TTL_SEC     60
#define NL_TICKET_TOKEN_LEN   33      /* 32 hex + NUL */
#define NL_TICKET_RATE_LIMIT  100     /* 每秒最大 invoke 数 */

typedef enum {
    NL_TICKET_OK = 0,
    NL_TICKET_INVALID,    /* 不存在（含已淘汰 / 重启） */
    NL_TICKET_EXPIRED,    /* 超 TTL */
    NL_TICKET_MISMATCH    /* 重复使用 / capability 或参数不符 */
} NlTicketStatus;

/* 成功返回 0 并写 out_token */
int            nl_ticket_issue(const char *cap_id, const char *args_json,
                               int effect, long now,
                               char out_token[NL_TICKET_TOKEN_LEN]);
NlTicketStatus nl_ticket_check(const char *token, const char *cap_id,
                               const char *args_json, long now);

/* 每会话限速：1 允许，0 超限 */
int            nl_ticket_rate_ok(long now);

int            nl_ticket_count(void);
int            nl_ticket_evictions(void);
void           nl_ticket_reset(void);
const char    *nl_ticket_status_str(NlTicketStatus s);

#endif /* NL_BROKER_TICKET_H */
