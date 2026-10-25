/*
 * audit.c — 结构化审计实现（T01 占位骨架，T06 实现）
 */
#include "audit.h"
#include <stddef.h>

int nl_audit_emit(const NlAuditRec *rec)
{
    (void)rec;
    return -1;
}

const char *nl_audit_path(void) { return NULL; }
