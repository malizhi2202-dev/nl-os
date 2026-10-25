/*
 * cip.h — CIP v1 协议编解码（ADR-016 / T01）
 *
 * JSON Lines，单行一个请求/响应。本文件只负责「解析与渲染」，不含任何业务语义。
 */
#ifndef NL_CIP_H
#define NL_CIP_H

#include <stddef.h>

#define NL_CIP_VERSION     1
#define NL_CIP_MAX_ID      128
#define NL_CIP_MAX_REQIDS  8

typedef enum {
    NL_CIP_OP_UNKNOWN = 0,
    NL_CIP_OP_DISCOVER,
    NL_CIP_OP_DESCRIBE,
    NL_CIP_OP_INVOKE,
    NL_CIP_OP_COMMIT,
    NL_CIP_OP_AUDIT
} NlCipOp;

typedef enum {
    NL_CIP_OK = 0,
    NL_CIP_E_VERSION,
    NL_CIP_E_BAD_REQ,
    NL_CIP_E_CAP_UNKNOWN,
    NL_CIP_E_SCHEME_UNSUPPORTED,
    NL_CIP_E_POLICY_DENY,
    NL_CIP_E_TICKET_INVALID,
    NL_CIP_E_TICKET_EXPIRED,
    NL_CIP_E_TICKET_MISMATCH,
    NL_CIP_E_EXEC_FAIL
} NlCipErr;

/* 解析后的请求（各 op 只填自己的字段） */
typedef struct {
    int      v;
    char     id[NL_CIP_MAX_ID];
    NlCipOp  op;
    /* discover */
    char     prefix[64];
    int      limit;
    char     cursor[NL_CIP_MAX_ID];
    /* describe */
    char     ids[NL_CIP_MAX_REQIDS][NL_CIP_MAX_ID];
    int      id_count;
    /* invoke */
    char     capability[NL_CIP_MAX_ID];
    char     args_json[1024];   /* 原样保留的 args 子树 */
    int      dry_run;
    /* commit */
    char     ticket[NL_CIP_MAX_ID];
    /* audit */
    long     since;
    int      audit_limit;
} NlCipReq;

/* 响应：payload_json 为已序列化的 result 对象/数组（可为空） */
typedef struct {
    int      v;
    char     id[NL_CIP_MAX_ID];
    int      ok;
    NlCipErr error;
    char     payload_json[4096];
} NlCipResp;

/* 解析一行；成功返回 NL_CIP_OK，失败返回具体错误码 */
int nl_cip_parse(const char *line, NlCipReq *out);

/* 渲染响应到 buf（含结尾换行）；返回写入长度，失败（截断）返回 -1 */
int nl_cip_render(const NlCipResp *r, char *buf, size_t cap);

const char *nl_cip_op_name(NlCipOp op);
NlCipOp     nl_cip_op_from_name(const char *name);
const char *nl_cip_error_str(NlCipErr e);

#endif /* NL_CIP_H */
