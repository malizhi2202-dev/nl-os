/*
 * facade_mcp.c — MCP 门面实现（T12）
 */
#include "facade_mcp.h"
#include "cip.h"
#include "audit.h"
#include "cJSON.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

/* 能力 ID → MCP 工具名：nl_ 前缀 + 非字母数字替换为 '_' */
static void tool_name_of(const NlCapability *c, char *out, size_t cap)
{
    size_t k = 0;
    if (cap > 3) { out[k++] = 'n'; out[k++] = 'l'; out[k++] = '_'; }
    for (const unsigned char *p = (const unsigned char *)c->id; *p && k + 1 < cap; p++)
        out[k++] = isalnum(*p) ? (char)*p : '_';
    out[k] = '\0';
}

static const NlCapability *find_by_tool_name(NlDispatch *d, const char *name)
{
    char buf[256];
    for (int i = 0; i < nl_cap_count(d->registry); i++) {
        const NlCapability *c = nl_cap_at(d->registry, i);
        if (!c) continue;
        tool_name_of(c, buf, sizeof(buf));
        if (strcmp(buf, name) == 0) return c;
    }
    return NULL;
}

/* vendor cJSON 子集没有 cJSON_Duplicate，这里手工搬 id */
static void add_id(cJSON *root, cJSON *id)
{
    if (!root || !id) return;
    if (cJSON_IsNumber(id)) {
        cJSON_AddNumberToObject(root, "id", cJSON_GetNumberValue(id));
    } else if (cJSON_IsString(id)) {
        cJSON_AddStringToObject(root, "id", cJSON_GetStringValue(id));
    }
}

static int write_result(cJSON *id, cJSON *result, char *out, size_t cap)
{
    cJSON *root = cJSON_CreateObject();
    if (!root) return -1;
    cJSON_AddStringToObject(root, "jsonrpc", "2.0");
    add_id(root, id);
    if (result) cJSON_AddItemToObject(root, "result", result);
    char *s = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    if (!s) return -1;
    int n = snprintf(out, cap, "%s\n", s);
    free(s);
    return (n > 0 && (size_t)n < cap) ? n : -1;
}

static int write_error(cJSON *id, int code, const char *msg, char *out, size_t cap)
{
    cJSON *root = cJSON_CreateObject();
    if (!root) return -1;
    cJSON_AddStringToObject(root, "jsonrpc", "2.0");
    add_id(root, id);
    cJSON *err = cJSON_CreateObject();
    if (err) {
        cJSON_AddNumberToObject(err, "code", (double)code);
        cJSON_AddStringToObject(err, "message", msg);
        cJSON_AddItemToObject(root, "error", err);
    }
    char *s = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    if (!s) return -1;
    int n = snprintf(out, cap, "%s\n", s);
    free(s);
    return (n > 0 && (size_t)n < cap) ? n : -1;
}

static int do_initialize(cJSON *id, char *out, size_t cap)
{
    cJSON *result = cJSON_CreateObject();
    if (!result) return -1;
    cJSON_AddStringToObject(result, "protocolVersion", "2024-11-05");
    cJSON_AddItemToObject(result, "capabilities", cJSON_CreateObject());
    cJSON *si = cJSON_CreateObject();
    if (si) {
        cJSON_AddStringToObject(si, "name", "nlbroker-facade");
        cJSON_AddStringToObject(si, "version", "0.2");
        cJSON_AddItemToObject(result, "serverInfo", si);
    }
    return write_result(id, result, out, cap);
}

static int do_tools_list(NlDispatch *d, cJSON *id, char *out, size_t cap)
{
    cJSON *result = cJSON_CreateObject();
    cJSON *tools = cJSON_CreateArray();
    if (!result || !tools) return -1;

    for (int i = 0; i < nl_cap_count(d->registry); i++) {
        const NlCapability *c = nl_cap_at(d->registry, i);
        if (!c) continue;
        if (c->perm_level > 2) continue;          /* AC-6：只暴露 perm ≤ L2 */

        cJSON *t = cJSON_CreateObject();
        if (!t) continue;
        char name[256];
        tool_name_of(c, name, sizeof(name));
        cJSON_AddStringToObject(t, "name", name);
        char desc[300];
        snprintf(desc, sizeof(desc), "NL-OS capability %s (L%d, %s)",
                 c->id, c->perm_level, nl_cap_effect_str(c->effect));
        cJSON_AddStringToObject(t, "description", desc);
        cJSON *schema = cJSON_CreateObject();
        if (schema) {
            cJSON_AddStringToObject(schema, "type", "object");
            cJSON_AddItemToObject(t, "inputSchema", schema);
        }
        cJSON_AddItemToArray(tools, t);
    }
    cJSON_AddItemToObject(result, "tools", tools);
    return write_result(id, result, out, cap);
}

static int do_tools_call(NlDispatch *d, cJSON *id, cJSON *params,
                         char *out, size_t cap)
{
    cJSON *nm = params ? cJSON_GetObjectItem(params, "name") : NULL;
    const char *name = cJSON_IsString(nm) ? cJSON_GetStringValue(nm) : NULL;
    if (!name) return write_error(id, -32602, "missing tool name", out, cap);

    const NlCapability *c = find_by_tool_name(d, name);
    if (!c) return write_error(id, -32601, "unknown tool", out, cap);

    if (c->perm_level >= 3) {
        /* AC-6：L3/L4 一律拒绝，并留审计痕迹 */
        NlAuditRec rec;
        memset(&rec, 0, sizeof(rec));
        rec.capability_id = c->id;
        rec.perm_level = c->perm_level;
        rec.verdict = "deny";
        rec.effect = nl_cap_effect_str(c->effect);
        rec.probability = -1;
        rec.result = "refused";
        rec.reason = "facade-not-exposed";
        rec.input = "{}";
        rec.corr = "";
        (void)nl_audit_emit(&rec);
        return write_error(id, -32000,
                           "denied: capability requires level >= L3 and is not exposed via MCP",
                           out, cap);
    }

    cJSON *args = params ? cJSON_GetObjectItem(params, "arguments") : NULL;
    char *args_txt = args ? cJSON_PrintUnformatted(args) : NULL;
    if (!args_txt) {
        args_txt = strdup("{}");
        if (!args_txt) return -1;
    }

    /* 复用 CIP：invoke 拿裁决与 ticket，再 commit */
    char req[2048], resp[8192];
    snprintf(req, sizeof(req),
             "{\"v\":1,\"id\":1,\"op\":\"invoke\",\"capability\":\"%s\",\"args\":%s}",
             c->id, args_txt);
    if (nl_dispatch_line(d, req, resp, sizeof(resp)) <= 0) {
        free(args_txt);
        return write_error(id, -32603, "dispatch failed", out, cap);
    }

    cJSON *ir = cJSON_Parse(resp);
    cJSON *result_obj = ir ? cJSON_GetObjectItem(ir, "result") : NULL;
    cJSON *verdict = result_obj ? cJSON_GetObjectItem(result_obj, "verdict") : NULL;
    const char *verdict_s = cJSON_IsString(verdict) ? cJSON_GetStringValue(verdict) : NULL;
    cJSON *ticket = result_obj ? cJSON_GetObjectItem(result_obj, "ticket") : NULL;
    const char *ticket_s = cJSON_IsString(ticket) ? cJSON_GetStringValue(ticket) : NULL;

    if (!verdict_s || (strcmp(verdict_s, "deny") == 0)) {
        cJSON *reason = result_obj ? cJSON_GetObjectItem(result_obj, "reason") : NULL;
        const char *rs = cJSON_IsString(reason) ? cJSON_GetStringValue(reason) : "denied";
        cJSON_Delete(ir);
        free(args_txt);
        return write_error(id, -32000, rs, out, cap);
    }
    if (strcmp(verdict_s, "confirm") == 0) {
        /* MCP 客户端没有可信确认通道 → 拒绝，让用户走 nlsh */
        cJSON_Delete(ir);
        free(args_txt);
        return write_error(id, -32000,
                           "needs human confirmation: run this capability via nlsh",
                           out, cap);
    }

    snprintf(req, sizeof(req),
             "{\"v\":1,\"id\":2,\"op\":\"commit\",\"ticket\":\"%s\",\"capability\":\"%s\",\"args\":%s}",
             ticket_s ? ticket_s : "", c->id, args_txt);
    free(args_txt);
    cJSON_Delete(ir);

    if (nl_dispatch_line(d, req, resp, sizeof(resp)) <= 0)
        return write_error(id, -32603, "commit failed", out, cap);

    cJSON *cr = cJSON_Parse(resp);
    cJSON *cres = cr ? cJSON_GetObjectItem(cr, "result") : NULL;
    cJSON *cout = cres ? cJSON_GetObjectItem(cres, "output") : NULL;
    const char *out_s = cJSON_IsString(cout) ? cJSON_GetStringValue(cout) : "";

    cJSON *result = cJSON_CreateObject();
    cJSON *content = cJSON_CreateArray();
    if (content) {
        cJSON *item = cJSON_CreateObject();
        if (item) {
            cJSON_AddStringToObject(item, "type", "text");
            cJSON_AddStringToObject(item, "text", out_s ? out_s : "");
            cJSON_AddItemToArray(content, item);
        }
        cJSON_AddItemToObject(result, "content", content);
    }
    cJSON_AddBoolToObject(result, "isError", 0);
    cJSON_Delete(cr);
    return write_result(id, result, out, cap);
}

int nl_facade_handle_line(NlDispatch *d, const char *line, char *out, size_t out_cap)
{
    if (!d || !line || !out || out_cap < 8) return -1;
    cJSON *msg = cJSON_Parse(line);
    if (!msg) return write_error(NULL, -32700, "parse error", out, out_cap);

    cJSON *id = cJSON_GetObjectItem(msg, "id");
    cJSON *method = cJSON_GetObjectItem(msg, "method");
    const char *m = cJSON_IsString(method) ? cJSON_GetStringValue(method) : NULL;
    int rc;

    if (!m) {
        rc = write_error(id, -32600, "invalid request", out, out_cap);
    } else if (strcmp(m, "initialize") == 0) {
        rc = do_initialize(id, out, out_cap);
    } else if (strcmp(m, "notifications/initialized") == 0) {
        out[0] = '\0';
        rc = 0;
    } else if (strcmp(m, "tools/list") == 0) {
        rc = do_tools_list(d, id, out, out_cap);
    } else if (strcmp(m, "tools/call") == 0) {
        cJSON *params = cJSON_GetObjectItem(msg, "params");
        rc = do_tools_call(d, id, params, out, out_cap);
    } else {
        rc = write_error(id, -32601, "method not found", out, out_cap);
    }
    cJSON_Delete(msg);
    return rc;
}

int nl_facade_run(NlDispatch *d, int in_fd, int out_fd)
{
    if (!d || in_fd < 0 || out_fd < 0) return -1;
    char line[8192], out[16384];
    size_t used = 0;

    for (;;) {
        ssize_t r = read(in_fd, line + used, sizeof(line) - 1 - used);
        if (r <= 0) break;
        used += (size_t)r;
        line[used] = '\0';

        char *start = line;
        char *nl;
        while ((nl = strchr(start, '\n')) != NULL) {
            *nl = '\0';
            if (nl > start) {
                int n = nl_facade_handle_line(d, start, out, sizeof(out));
                if (n > 0) {
                    size_t off = 0;
                    while (off < (size_t)n) {
                        ssize_t w = write(out_fd, out + off, (size_t)n - off);
                        if (w <= 0) return -1;
                        off += (size_t)w;
                    }
                }
            }
            start = nl + 1;
        }
        used = strlen(start);
        memmove(line, start, used + 1);
        if (used >= sizeof(line) - 1) return -1;
    }
    return 0;
}
