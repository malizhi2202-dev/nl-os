/*
 * audit_log.h — syslog 审计日志
 */
#ifndef NL_AUDIT_LOG_H
#define NL_AUDIT_LOG_H

#include "perm_engine.h"

void nl_audit_open(const char *ident);
void nl_audit_log(const NlPermRequest *req, const NlPermResponse *resp);
void nl_audit_close(void);

#endif
