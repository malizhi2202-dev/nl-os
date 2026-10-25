/*
 * dispatch.c — CIP 报文分发（T08）
 */
#include "dispatch.h"
#include "cip.h"
#include "verdict.h"
#include "ticket.h"
#include "adapter_cli.h"
#include "adapter_dbus.h"
#include "adapter_mcp.h"
#include "audit.h"
#include "cJSON.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static long now_sec(void) { return (long)time(NULL); }

static long now_us(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (long)ts.tv_sec * 1000000L + ts.tv_nsec / 1000L;
}

int nl_dispatch_init(NlDispatch *d, const char *registry_path)
{
    if (!d) return -1;
    memset(d, 0, sizeof(*d));
    const char *p = (registry_path && registry_path[0]) ? registry_path : nl_cap_default_path();
    NlCapLoadErr err = NL_CAP_OK;
    d->registry = nl_cap_load(p, &err);
    if (!d->registry) d->registry = nl_cap_create();   /* 加载失败降级为空表 */
    return d->registry ? 0 : -1;
}

void nl_dispatch_set_mcp(NlDispatch *d, NlMcpClient *c)
{
    if (d) d->mcp = c;
}

/* 解析 mcp:<server>/<tool>；成功返回 0 */
static int parse_mcp_id(const char *id, char *server, size_t scap,
                        char *tool, size_t tcap)
{
    if (!id || strncmp(id, "mcp:", 4) != 0) return -1;
    const char *p = id + 4;
    const char *slash = strchr(p, '/');
    if (!slash || slash == p || slash[1] == '\0') return -1;
    size_t sl = (size_t)(slash - p);
    size_t tl = strlen(slash + 1);
    if (sl >= scap || tl >= tcap) return -1;
    memcpy(server, p, sl);
    server[sl] = '\0';
    memcpy(tool, slash + 1, tl + 1);
    return 0;
}

static int add_mcp_plan_json(cJSON *root, const char *server, const char *tool)
{
    cJSON *p = cJSON_CreateObject();
    cJSON *m = cJSON_CreateObject();
    if (!p || !m) return -1;
    cJSON_AddStringToObject(m, "server", server);
    cJSON_AddStringToObject(m, "tool", tool);
    cJSON_AddItemToObject(p, "mcp", m);
    cJSON_AddBoolToObject(p, "executed", 0);
    cJSON_AddItemToObject(root, "plan", p);
    return 0;
}

void nl_dispatch_free(NlDispatch *d)
{
    if (!d) return;
    nl_cap_free(d->registry);
    d->registry = NULL;
}

/*
 * 审计里只存"原因码"，不存人类可读原文——原文里会带路径，属于敏感参数。
 * 面向用户的完整 reason 仍在响应里返回。
 */
static const char *reason_code(const NlVerdictResult *vr, const NlCapability *c)
{
    if (vr->verdict == NL_VERDICT_DENY) {
        if (strstr(vr->reason, "保护名单") != NULL) return "protected-path";
        if (c->perm_level >= 4) return "l4-denied";
        if (strstr(vr->reason, "特权") != NULL) return "privilege-denied";
        return "policy-denied";
    }
    if (vr->verdict == NL_VERDICT_CONFIRM) return "needs-confirm";
    return "read-only";
}

static void emit_audit(const char *cap_id, const char *verdict, const char *effect,
                       int perm, const char *result, const char *reason,
                       const char *args_json, long dur_us)
{
    NlAuditRec r;
    memset(&r, 0, sizeof(r));
    r.capability_id = cap_id;
    r.perm_level = perm;
    r.verdict = verdict;
    r.effect = effect;
    r.fast_path = 0;
    r.probability = -1;
    r.duration_us = dur_us;
    r.result = result;
    r.reason = reason;
    r.input = args_json;
    (void)nl_audit_emit(&r);
}

static int set_payload(NlCipResp *resp, cJSON *obj)
{
    char *s = cJSON_PrintUnformatted(obj);
    cJSON_Delete(obj);
    if (!s) return -1;
    int rc = (strlen(s) < sizeof(resp->payload_json)) ? 0 : -1;
    if (rc == 0) snprintf(resp->payload_json, sizeof(resp->payload_json), "%s", s);
    free(s);
    return rc;
}

/* 按命名空间选择适配器；v1 支持 cli 与 dbus，mcp/http 在 W4/W5 */
static int build_plan_for(const NlCapability *c, const char *args, NlPlan *plan)
{
    if (strncmp(c->id, "cli:", 4) == 0) return nl_cli_build_plan(c, args, plan);
    if (strncmp(c->id, "dbus:", 5) == 0) return nl_dbus_build_plan(c, args, plan);
    return -1;
}

static cJSON *cap_brief(const NlCapability *c)
{
    cJSON *o = cJSON_CreateObject();
    if (!o) return NULL;
    char perm[4];
    snprintf(perm, sizeof(perm), "L%d", c->perm_level);
    cJSON_AddStringToObject(o, "id", c->id);
    cJSON_AddStringToObject(o, "name", c->name);
    cJSON_AddStringToObject(o, "namespace", c->namespace);
    cJSON_AddStringToObject(o, "perm", perm);
    cJSON_AddStringToObject(o, "effect", nl_cap_effect_str(c->effect));
    cJSON_AddBoolToObject(o, "reversible", c->reversible);
    return o;
}

static int32_t do_discover(NlDispatch *d, const NlCipReq *req, NlCipResp *resp)
{
    cJSON *root = cJSON_CreateObject();
    cJSON *arr = cJSON_CreateArray();
    if (!root || !arr) return -1;

    int total = nl_cap_count(d->registry);
    int start = (req->cursor[0] != '\0') ? atoi(req->cursor) : 0;
    if (start < 0) start = 0;
    int limit = (req->limit > 0) ? req->limit : 100;
    int emitted = 0;
    int next = start;

    for (int i = start; i < total && emitted < limit; i++) {
        const NlCapability *c = nl_cap_at(d->registry, i);
        if (!c) continue;
        if (req->prefix[0] != '\0' &&
            strncmp(c->id, req->prefix, strlen(req->prefix)) != 0) continue;
        cJSON *o = cap_brief(c);
        if (o) {
            cJSON_AddItemToArray(arr, o);
            emitted++;
        }
        next = i + 1;
    }

    cJSON_AddStringToObject(root, "tier", "A");
    cJSON_AddItemToObject(root, "capabilities", arr);
    if (next < total && emitted > 0) {
        char cur[16];
        snprintf(cur, sizeof(cur), "%d", next);
        cJSON_AddStringToObject(root, "cursor", cur);
    } else {
        cJSON_AddNullToObject(root, "cursor");
    }
    resp->ok = 1;
    return set_payload(resp, root);
}

static int do_describe(NlDispatch *d, const NlCipReq *req, NlCipResp *resp)
{
    cJSON *root = cJSON_CreateObject();
    cJSON *arr = cJSON_CreateArray();
    if (!root || !arr) return -1;

    for (int i = 0; i < req->id_count; i++) {
        const NlCapability *c = nl_cap_find(d->registry, req->ids[i]);
        if (!c) continue;
        cJSON *o = cap_brief(c);
        if (!o) continue;
        cJSON *slots = cJSON_CreateArray();
        if (slots) {
            for (int k = 0; k < c->slot_count; k++)
                cJSON_AddItemToArray(slots, cJSON_CreateString(c->slots[k]));
            cJSON_AddItemToObject(o, "slots", slots);
        }
        cJSON_AddBoolToObject(o, "effect_defaulted", c->effect_defaulted);
        cJSON_AddNumberToObject(o, "timeout_ms", 5000);
        cJSON_AddItemToArray(arr, o);
    }
    cJSON_AddItemToObject(root, "capabilities", arr);
    resp->ok = 1;
    return set_payload(resp, root);
}

/* 把 plan 序列化到 payload 里的 plan 字段 */
static int add_plan_json(cJSON *root, const NlPlan *plan)
{
    cJSON *p = cJSON_CreateObject();
    cJSON *argv = cJSON_CreateArray();
    if (!p || !argv) return -1;
    for (int i = 0; plan->argv[i] != NULL && i <= NL_CLI_MAX_ARGV; i++)
        cJSON_AddItemToArray(argv, cJSON_CreateString(plan->argv[i]));
    cJSON_AddItemToObject(p, "argv", argv);
    cJSON_AddBoolToObject(p, "executed", 0);   /* AC-3：invoke 不执行 */
    cJSON_AddItemToObject(root, "plan", p);
    return 0;
}

static int do_invoke(NlDispatch *d, const NlCipReq *req, NlCipResp *resp)
{
    long t0 = now_us();
    const NlCapability *c = nl_cap_find(d->registry, req->capability);
    if (!c) {
        resp->ok = 0;
        resp->error = NL_CIP_E_CAP_UNKNOWN;
        return 0;
    }

    NlVerdictResult vr;
    if (nl_verdict_eval(c, req->args_json, &vr) != 0) {
        resp->ok = 0;
        resp->error = NL_CIP_E_BAD_REQ;
        return 0;
    }

    cJSON *root = cJSON_CreateObject();
    if (!root) return -1;

    if (vr.verdict == NL_VERDICT_DENY) {
        cJSON_AddStringToObject(root, "verdict", "deny");
        cJSON_AddStringToObject(root, "reason", vr.reason);
        cJSON_AddNullToObject(root, "plan");
        cJSON *alts = cJSON_CreateArray();
        if (alts) {
            for (int i = 0; i < vr.alt_count; i++)
                cJSON_AddItemToArray(alts, cJSON_CreateString(vr.alternative_ids[i]));
            cJSON_AddItemToObject(root, "alternatives", alts);
        }
        cJSON_AddBoolToObject(root, "untrusted_effect_attempt", vr.untrusted_effect_attempt);
        emit_audit(c->id, "deny", nl_cap_effect_str(c->effect), c->perm_level,
                   "refused", reason_code(&vr, c), req->args_json, now_us() - t0);
        resp->ok = 1;
        return set_payload(resp, root);
    }

    char mcp_server[NL_MCP_NAME_MAX], mcp_tool[NL_MCP_NAME_MAX];
    int is_mcp = (parse_mcp_id(c->id, mcp_server, sizeof(mcp_server),
                               mcp_tool, sizeof(mcp_tool)) == 0);

    if (is_mcp) {
        if (!d->mcp ||
            strcmp(nl_mcp_server_name(d->mcp), mcp_server) != 0) {
            cJSON_Delete(root);
            resp->ok = 0;
            resp->error = NL_CIP_E_SCHEME_UNSUPPORTED;
            return 0;
        }
        char token[NL_TICKET_TOKEN_LEN];
        if (nl_ticket_issue(c->id, req->args_json, (int)c->effect, now_sec(), token) != 0) {
            cJSON_Delete(root);
            resp->ok = 0;
            resp->error = NL_CIP_E_BAD_REQ;
            return 0;
        }
        cJSON_AddStringToObject(root, "verdict",
                                vr.verdict == NL_VERDICT_ALLOW ? "allow" : "confirm");
        cJSON_AddStringToObject(root, "reason", vr.reason);
        (void)add_mcp_plan_json(root, mcp_server, mcp_tool);
        cJSON_AddStringToObject(root, "ticket", token);
        cJSON_AddNumberToObject(root, "ttl_ms", (double)(NL_TICKET_TTL_SEC * 1000));
        cJSON_AddBoolToObject(root, "untrusted_effect_attempt", vr.untrusted_effect_attempt);
        emit_audit(c->id, vr.verdict == NL_VERDICT_ALLOW ? "allow" : "confirm",
                   nl_cap_effect_str(c->effect), c->perm_level, "ok", reason_code(&vr, c),
                   req->args_json, now_us() - t0);
        resp->ok = 1;
        return set_payload(resp, root);
    }

    NlPlan plan;
    if (build_plan_for(c, req->args_json, &plan) != 0) {
        cJSON_Delete(root);
        resp->ok = 0;
        resp->error = NL_CIP_E_SCHEME_UNSUPPORTED;
        return 0;
    }

    char token[NL_TICKET_TOKEN_LEN];
    if (nl_ticket_issue(c->id, req->args_json, (int)c->effect, now_sec(), token) != 0) {
        nl_cli_plan_free(&plan);
        cJSON_Delete(root);
        resp->ok = 0;
        resp->error = NL_CIP_E_BAD_REQ;
        return 0;
    }

    cJSON_AddStringToObject(root, "verdict",
                            vr.verdict == NL_VERDICT_ALLOW ? "allow" : "confirm");
    cJSON_AddStringToObject(root, "reason", vr.reason);
    (void)add_plan_json(root, &plan);
    cJSON_AddStringToObject(root, "ticket", token);
    cJSON_AddNumberToObject(root, "ttl_ms", (double)(NL_TICKET_TTL_SEC * 1000));
    cJSON_AddBoolToObject(root, "untrusted_effect_attempt", vr.untrusted_effect_attempt);

    emit_audit(c->id, vr.verdict == NL_VERDICT_ALLOW ? "allow" : "confirm",
               nl_cap_effect_str(c->effect), c->perm_level, "ok", reason_code(&vr, c),
               req->args_json, now_us() - t0);

    nl_cli_plan_free(&plan);
    resp->ok = 1;
    return set_payload(resp, root);
}

static int do_commit(NlDispatch *d, const NlCipReq *req, NlCipResp *resp)
{
    long t0 = now_us();
    const NlCapability *c = nl_cap_find(d->registry, req->capability);
    if (!c) {
        resp->ok = 0;
        resp->error = NL_CIP_E_CAP_UNKNOWN;
        return 0;
    }

    NlTicketStatus ts = nl_ticket_check(req->ticket, c->id, req->args_json, now_sec());
    if (ts != NL_TICKET_OK) {
        NlCipErr e = NL_CIP_E_TICKET_INVALID;
        if (ts == NL_TICKET_EXPIRED) e = NL_CIP_E_TICKET_EXPIRED;
        else if (ts == NL_TICKET_MISMATCH) e = NL_CIP_E_TICKET_MISMATCH;
        emit_audit(c->id, "commit", nl_cap_effect_str(c->effect), c->perm_level,
                   "refused", nl_ticket_status_str(ts), req->args_json, now_us() - t0);
        resp->ok = 0;
        resp->error = e;
        return 0;
    }

    char mcp_server[NL_MCP_NAME_MAX], mcp_tool[NL_MCP_NAME_MAX];
    if (parse_mcp_id(c->id, mcp_server, sizeof(mcp_server), mcp_tool, sizeof(mcp_tool)) == 0) {
        if (!d->mcp || strcmp(nl_mcp_server_name(d->mcp), mcp_server) != 0) {
            resp->ok = 0;
            resp->error = NL_CIP_E_SCHEME_UNSUPPORTED;
            return 0;
        }
        char out_buf[NL_CLI_MAX_OUT];
        int call_rc = nl_mcp_call_tool(d->mcp, mcp_tool, req->args_json,
                                       out_buf, sizeof(out_buf));
        cJSON *root = cJSON_CreateObject();
        if (!root) return -1;
        cJSON_AddStringToObject(root, "status", call_rc == 0 ? "ok" : "error");
        cJSON_AddNumberToObject(root, "exit", call_rc == 0 ? 0 : 1);
        cJSON_AddStringToObject(root, "output", call_rc == 0 ? out_buf : "");
        cJSON_AddNullToObject(root, "snapshot_ref");
        char aid[24] = "a-";
        snprintf(aid + 2, sizeof(aid) - 2, "%ld", now_us());
        cJSON_AddStringToObject(root, "audit_id", aid);
        emit_audit(c->id, "commit", nl_cap_effect_str(c->effect), c->perm_level,
                   call_rc == 0 ? "ok" : "error", "mcp-call",
                   req->args_json, now_us() - t0);
        resp->ok = 1;
        return set_payload(resp, root);
    }

    NlPlan plan;
    if (build_plan_for(c, req->args_json, &plan) != 0) {
        resp->ok = 0;
        resp->error = NL_CIP_E_SCHEME_UNSUPPORTED;
        return 0;
    }

    NlCliResult r;
    int rc = nl_cli_execute(&plan, 5000, &r);
    nl_cli_plan_free(&plan);

    cJSON *root = cJSON_CreateObject();
    if (!root) return -1;
    cJSON_AddStringToObject(root, "status", (rc == 0 && r.status == NL_CLI_OK) ? "ok" : "error");
    cJSON_AddNumberToObject(root, "exit", (double)r.exit_code);
    cJSON_AddStringToObject(root, "output", r.output);
    cJSON_AddNullToObject(root, "snapshot_ref");
    {
        char aid[24] = "a-";
        snprintf(aid + 2, sizeof(aid) - 2, "%ld", now_us());
        cJSON_AddStringToObject(root, "audit_id", aid);
    }

    emit_audit(c->id, "commit", nl_cap_effect_str(c->effect), c->perm_level,
               (r.status == NL_CLI_OK) ? "ok" : "error",
               nl_cli_status_str(r.status), req->args_json, now_us() - t0);

    resp->ok = 1;
    return set_payload(resp, root);
}

static int do_audit(const NlCipReq *req, NlCipResp *resp)
{
    /* 响应体受 payload_json 限制（4KB）：约可容纳 15 条；更多请用 since 翻页 */
    char buf[3500];
    (void)nl_audit_query(req->since, req->audit_limit, buf, sizeof(buf));
    static const char prefix[] = "{\"records\":";
    static const char suffix[] = "}";
    size_t pl = sizeof(prefix) - 1;
    size_t bl = strlen(buf);
    size_t sl = sizeof(suffix) - 1;
    if (pl + bl + sl >= sizeof(resp->payload_json)) return -1;
    memcpy(resp->payload_json, prefix, pl);
    memcpy(resp->payload_json + pl, buf, bl);
    memcpy(resp->payload_json + pl + bl, suffix, sl + 1);
    resp->ok = 1;
    return 0;
}

int nl_dispatch_line(void *ctx, const char *line, char *out, size_t out_cap)
{
    NlDispatch *d = (NlDispatch *)ctx;
    NlCipReq req;
    NlCipResp resp;
    memset(&resp, 0, sizeof(resp));
    resp.v = NL_CIP_VERSION;

    int rc = nl_cip_parse(line, &req);
    if (rc != NL_CIP_OK) {
        resp.ok = 0;
        resp.error = (NlCipErr)rc;
        if (req.id[0] != '\0') snprintf(resp.id, sizeof(resp.id), "%s", req.id);
        else snprintf(resp.id, sizeof(resp.id), "-");
        return nl_cip_render(&resp, out, out_cap);
    }
    snprintf(resp.id, sizeof(resp.id), "%s", req.id);

    switch (req.op) {
    case NL_CIP_OP_DISCOVER: if (do_discover(d, &req, &resp) != 0) return -1; break;
    case NL_CIP_OP_DESCRIBE: if (do_describe(d, &req, &resp) != 0) return -1; break;
    case NL_CIP_OP_INVOKE:   if (do_invoke(d, &req, &resp) != 0) return -1;   break;
    case NL_CIP_OP_COMMIT:   if (do_commit(d, &req, &resp) != 0) return -1;   break;
    case NL_CIP_OP_AUDIT:    if (do_audit(&req, &resp) != 0) return -1;       break;
    default:
        resp.ok = 0;
        resp.error = NL_CIP_E_BAD_REQ;
        break;
    }
    return nl_cip_render(&resp, out, out_cap);
}
