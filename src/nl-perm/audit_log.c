/*
 * audit_log.c — syslog 审计
 */
#include "audit_log.h"
#include <syslog.h>
#include <stdio.h>

void nl_audit_open(const char *ident) { openlog(ident?ident:"nl-perm", LOG_PID|LOG_NDELAY, LOG_AUTH); }
void nl_audit_close(void) { closelog(); }

void nl_audit_log(const NlPermRequest *req, const NlPermResponse *resp) {
    syslog(LOG_NOTICE, "user=%s cwd=%s nl='%s' cmd='%s' decision=%s level=L%d reason='%s' audit=%s",
        req->user?req->user:"?",
        req->cwd?req->cwd:"?",
        req->nl_input?req->nl_input:"",
        req->generated_cmd?req->generated_cmd:"",
        resp->decision==0?"allow":(resp->decision==1?"confirm":"deny"),
        resp->level, resp->reason, resp->audit_id);
}
