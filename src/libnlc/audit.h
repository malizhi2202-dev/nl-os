/*
 * audit.h — 结构化审计（JSON Lines，0600）
 *
 * 与 src/nl-perm/audit_log.c（syslog，daemon 侧）互不替代、各自独立。
 * 输入只存 hash，不落明文参数。
 */
#ifndef NL_AUDIT_H
#define NL_AUDIT_H

typedef struct {
    const char *capability_id;
    int         perm_level;
    int         fast_path;
    double      probability;   /* 无决策层时为 -1 */
    const char *result;        /* ok | refused | error */
    const char *reason;
    const char *input;         /* 原文；内部只落 hash */
} NlAuditRec;

int         nl_audit_emit(const NlAuditRec *rec);
const char *nl_audit_path(void);

#endif /* NL_AUDIT_H */
