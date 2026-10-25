/*
 * audit.h — 结构化审计（JSON Lines，0600）
 *
 * 与 src/nl-perm/audit_log.c（syslog，daemon 侧）并存，职责不同、不互相替代。
 * 输入只落 hash，不落明文参数。
 */
#ifndef NL_AUDIT_H
#define NL_AUDIT_H

#include <stddef.h>

#define NL_AUDIT_MAX_LIMIT 500

typedef struct {
    const char *capability_id;
    int         perm_level;
    const char *verdict;       /* allow | confirm | deny | refused */
    const char *effect;        /* read | write | delete | ... */
    int         fast_path;
    double      probability;   /* 无决策层时 -1 */
    long        duration_us;
    const char *result;        /* ok | refused | error */
    const char *reason;
    const char *input;         /* 原文；内部只落 sha256 前 16 hex */
} NlAuditRec;

int         nl_audit_emit(const NlAuditRec *rec);
const char *nl_audit_path(void);

/* 按 since 过滤（ts >= since），最多 limit 条，输出 JSON 数组文本 */
int         nl_audit_query(long since, int limit, char *out, size_t cap);

#endif /* NL_AUDIT_H */
