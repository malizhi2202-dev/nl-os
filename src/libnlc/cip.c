/*
 * cip.c — CIP v1 编解码（ADR-016 / T01）
 *
 * 注意：vendor cJSON 是 API 子集——只用 GetObjectItem / GetStringValue / GetNumberValue，
 * 没有 GetObjectItemCaseSensitive，也不能直连 item->valuestring。
 */
#include "cip.h"
#include "json_helper.h"
#include "cJSON.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

const char *nl_cip_error_str(NlCipErr e)
{
    switch (e) {
    case NL_CIP_OK:                    return "OK";
    case NL_CIP_E_VERSION:             return "E_VERSION";
    case NL_CIP_E_BAD_REQ:             return "E_BAD_REQ";
    case NL_CIP_E_CAP_UNKNOWN:         return "E_CAP_UNKNOWN";
    case NL_CIP_E_SCHEME_UNSUPPORTED:  return "E_SCHEME_UNSUPPORTED";
    case NL_CIP_E_POLICY_DENY:         return "E_POLICY_DENY";
    case NL_CIP_E_TICKET_INVALID:      return "E_TICKET_INVALID";
    case NL_CIP_E_TICKET_EXPIRED:      return "E_TICKET_EXPIRED";
    case NL_CIP_E_TICKET_MISMATCH:     return "E_TICKET_MISMATCH";
    case NL_CIP_E_EXEC_FAIL:           return "E_EXEC_FAIL";
    default:                           return "E_UNKNOWN";
    }
}

const char *nl_cip_op_name(NlCipOp op)
{
    switch (op) {
    case NL_CIP_OP_DISCOVER: return "discover";
    case NL_CIP_OP_DESCRIBE: return "describe";
    case NL_CIP_OP_INVOKE:   return "invoke";
    case NL_CIP_OP_COMMIT:   return "commit";
    case NL_CIP_OP_AUDIT:    return "audit";
    default:                 return "unknown";
    }
}

NlCipOp nl_cip_op_from_name(const char *name)
{
    if (!name) return NL_CIP_OP_UNKNOWN;
    if (strcmp(name, "discover") == 0) return NL_CIP_OP_DISCOVER;
    if (strcmp(name, "describe") == 0) return NL_CIP_OP_DESCRIBE;
    if (strcmp(name, "invoke") == 0)   return NL_CIP_OP_INVOKE;
    if (strcmp(name, "commit") == 0)   return NL_CIP_OP_COMMIT;
    if (strcmp(name, "audit") == 0)    return NL_CIP_OP_AUDIT;
    return NL_CIP_OP_UNKNOWN;
}

static int get_str(const cJSON *obj, const char *key, char *dst, size_t cap)
{
    const char *s = NULL;
    size_t len;
    if (nl_json_get_string(obj, key, &s) != 0 || !s) return -1;
    len = strlen(s);
    if (len >= cap) return -1;
    memcpy(dst, s, len + 1);
    return 0;
}

/* 信封 id 允许字符串或数字（MCP 等上层协议常用数字） */
static int get_envelope_id(const cJSON *obj, char *dst, size_t cap)
{
    cJSON *it = cJSON_GetObjectItem(obj, "id");
    if (cJSON_IsString(it)) {
        const char *s = cJSON_GetStringValue(it);
        size_t len = s ? strlen(s) : 0;
        if (!s || len >= cap) return -1;
        memcpy(dst, s, len + 1);
        return 0;
    }
    if (cJSON_IsNumber(it)) {
        snprintf(dst, cap, "%ld", (long)cJSON_GetNumberValue(it));
        return 0;
    }
    return -1;
}

static int get_int(const cJSON *obj, const char *key, int *out)
{
    double v = 0;
    cJSON *it = cJSON_GetObjectItem(obj, key);
    if (!cJSON_IsNumber(it)) return -1;
    v = cJSON_GetNumberValue(it);
    *out = (int)v;
    return 0;
}

static int get_long(const cJSON *obj, const char *key, long *out)
{
    cJSON *it = cJSON_GetObjectItem(obj, key);
    if (!cJSON_IsNumber(it)) return -1;
    *out = (long)cJSON_GetNumberValue(it);
    return 0;
}

int nl_cip_parse(const char *line, NlCipReq *out)
{
    if (!line || !out) return NL_CIP_E_BAD_REQ;
    memset(out, 0, sizeof(*out));
    out->v = NL_CIP_VERSION;

    /* 跳过空白；空行是非法请求 */
    const char *p = line;
    while (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n') p++;
    if (*p == '\0') return NL_CIP_E_BAD_REQ;

    NlJsonDoc doc;
    if (nl_json_parse(p, &doc) != 0) return NL_CIP_E_BAD_REQ;

    int rc = NL_CIP_OK;
    int v = 0;
    const char *opname = NULL;

    if (get_int(doc.root, "v", &v) != 0) { rc = NL_CIP_E_BAD_REQ; goto done; }
    if (v != NL_CIP_VERSION) { rc = NL_CIP_E_VERSION; goto done; }
    out->v = v;

    if (get_envelope_id(doc.root, out->id, sizeof(out->id)) != 0) { rc = NL_CIP_E_BAD_REQ; goto done; }
    if (nl_json_get_string(doc.root, "op", &opname) != 0 || !opname) { rc = NL_CIP_E_BAD_REQ; goto done; }

    out->op = nl_cip_op_from_name(opname);
    if (out->op == NL_CIP_OP_UNKNOWN) { rc = NL_CIP_E_BAD_REQ; goto done; }

    switch (out->op) {
    case NL_CIP_OP_DISCOVER: {
        const char *pref = NULL, *cur = NULL;
        int lim = 100;
        if (nl_json_get_string(doc.root, "prefix", &pref) == 0 && pref)
            snprintf(out->prefix, sizeof(out->prefix), "%.63s", pref);
        if (nl_json_get_string(doc.root, "cursor", &cur) == 0 && cur)
            snprintf(out->cursor, sizeof(out->cursor), "%.127s", cur);
        if (get_int(doc.root, "limit", &lim) == 0) out->limit = lim;
        else out->limit = 100;
        if (out->limit <= 0) { rc = NL_CIP_E_BAD_REQ; goto done; }
        break;
    }
    case NL_CIP_OP_DESCRIBE: {
        cJSON *ids = cJSON_GetObjectItem(doc.root, "ids");
        if (!cJSON_IsArray(ids)) { rc = NL_CIP_E_BAD_REQ; goto done; }
        int n = cJSON_GetArraySize(ids);
        if (n <= 0) { rc = NL_CIP_E_BAD_REQ; goto done; }
        if (n > NL_CIP_MAX_REQIDS) { rc = NL_CIP_E_BAD_REQ; goto done; }
        for (int i = 0; i < n && out->id_count < NL_CIP_MAX_REQIDS; i++) {
            const char *sv = cJSON_GetStringValue(cJSON_GetArrayItem(ids, i));
            if (!sv) { rc = NL_CIP_E_BAD_REQ; goto done; }
            snprintf(out->ids[out->id_count], NL_CIP_MAX_ID, "%.127s", sv);
            out->id_count++;
        }
        break;
    }
    case NL_CIP_OP_INVOKE: {
        int dry = 0;
        /* 信封 id 与能力 ID 分离：能力用 capability 字段（见 DESIGN §3.2 更正） */
        if (get_str(doc.root, "capability", out->capability, sizeof(out->capability)) != 0) {
            rc = NL_CIP_E_BAD_REQ; goto done;
        }
        cJSON *args = cJSON_GetObjectItem(doc.root, "args");
        if (args) {
            char *s = nl_json_print(args);
            if (!s) { rc = NL_CIP_E_BAD_REQ; goto done; }
            if (strlen(s) >= sizeof(out->args_json)) { free(s); rc = NL_CIP_E_BAD_REQ; goto done; }
            snprintf(out->args_json, sizeof(out->args_json), "%.1023s", s);
            free(s);
        } else {
            snprintf(out->args_json, sizeof(out->args_json), "{}");
        }
        if (get_int(doc.root, "dry_run", &dry) == 0) out->dry_run = dry ? 1 : 0;
        break;
    }
    case NL_CIP_OP_COMMIT: {
        cJSON *args = NULL;
        if (get_str(doc.root, "ticket", out->ticket, sizeof(out->ticket)) != 0) {
            rc = NL_CIP_E_BAD_REQ; goto done;
        }
        /* commit 必须回带 capability 与 args，用于校验参数是否被篡改（AC-4） */
        if (get_str(doc.root, "capability", out->capability, sizeof(out->capability)) != 0) {
            rc = NL_CIP_E_BAD_REQ; goto done;
        }
        args = cJSON_GetObjectItem(doc.root, "args");
        if (args) {
            char *js = nl_json_print(args);
            if (!js) { rc = NL_CIP_E_BAD_REQ; goto done; }
            if (strlen(js) >= sizeof(out->args_json)) { free(js); rc = NL_CIP_E_BAD_REQ; goto done; }
            snprintf(out->args_json, sizeof(out->args_json), "%.1023s", js);
            free(js);
        } else {
            snprintf(out->args_json, sizeof(out->args_json), "{}");
        }
        break;
    }
    case NL_CIP_OP_AUDIT: {
        long since = 0;
        int lim = 50;
        if (get_long(doc.root, "since", &since) == 0) out->since = since;
        if (get_int(doc.root, "limit", &lim) == 0) out->audit_limit = lim;
        else out->audit_limit = 50;
        if (out->audit_limit <= 0 || out->audit_limit > 500) out->audit_limit = 50;
        break;
    }
    default:
        rc = NL_CIP_E_BAD_REQ;
        break;
    }

done:
    nl_json_free(&doc);
    return rc;
}

int nl_cip_render(const NlCipResp *r, char *buf, size_t cap)
{
    if (!r || !buf || cap == 0) return -1;
    int n;
    const char *ok = r->ok ? "true" : "false";

    /* 顺序很重要：失败响应必须先处理，否则带 payload 的失败会丢掉 error 字段 */
    if (!r->ok) {
        if (r->payload_json[0] != '\0') {
            n = snprintf(buf, cap,
                         "{\"v\":%d,\"id\":\"%s\",\"ok\":false,\"error\":\"%s\",\"result\":%s}\n",
                         r->v ? r->v : NL_CIP_VERSION, r->id,
                         nl_cip_error_str(r->error), r->payload_json);
        } else {
            n = snprintf(buf, cap, "{\"v\":%d,\"id\":\"%s\",\"ok\":false,\"error\":\"%s\"}\n",
                         r->v ? r->v : NL_CIP_VERSION, r->id, nl_cip_error_str(r->error));
        }
    } else if (r->payload_json[0] != '\0') {
        n = snprintf(buf, cap, "{\"v\":%d,\"id\":\"%s\",\"ok\":%s,\"result\":%s}\n",
                     r->v ? r->v : NL_CIP_VERSION, r->id, ok, r->payload_json);
    } else {
        n = snprintf(buf, cap, "{\"v\":%d,\"id\":\"%s\",\"ok\":true}\n",
                     r->v ? r->v : NL_CIP_VERSION, r->id);
    }
    if (n < 0 || (size_t)n >= cap) return -1;
    return n;
}
