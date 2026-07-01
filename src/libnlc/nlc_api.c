/*
 * nlc_api.c — libnlc 公共 API 实现
 *
 * 组装四层管线：安全网关 → 规则引擎 → 模型推理 → 模板填充。
 * Agent 扩展点：MVP 单步退化版本。
 */

#include "nlc_api.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static NlModel *g_model = NULL;
static char g_error[256];

/* ============================================================
 * 生命周期
 * ============================================================ */

int nl_init(const char *model_path) {
    if (!model_path) {
        snprintf(g_error, sizeof(g_error), "nl_init: model_path is NULL");
        return -1;
    }
    if (g_model) {
        nl_model_free(g_model);
        g_model = NULL;
    }
    g_model = nl_model_init(model_path);
    if (!g_model) {
        snprintf(g_error, sizeof(g_error), "nl_init: failed to load model '%s'", model_path);
        return -1;
    }
    g_error[0] = '\0';
    return 0;
}

void nl_free(void) {
    if (g_model) { nl_model_free(g_model); g_model = NULL; }
}

const char *nl_get_last_error(void) { return g_error; }

/* ============================================================
 * 核心解析 · 四层管线
 * ============================================================ */

int nl_parse(const char *input, NlParseResult *result) {
    if (!input || !result) return -1;
    memset(result, 0, sizeof(NlParseResult));
    char debug[512] = {0};
    int dbg_pos = 0;

    /* ── Layer 1: 安全网关 ── */
    NlSanitizeResult sr = nl_input_sanitize(input);
    if (sr.chars_stripped > 0) {
        dbg_pos += snprintf(debug + dbg_pos, sizeof(debug) - dbg_pos,
                            "[安全网关] 剥离 %d 个不可见字符; ", sr.chars_stripped);
    } else {
        dbg_pos += snprintf(debug + dbg_pos, sizeof(debug) - dbg_pos,
                            "[安全网关] 通过; ");
    }
    const char *clean_input = sr.cleaned_input ? sr.cleaned_input : input;

    /* ── Layer 2: 规则引擎 ── */
    NlRuleResult rule_r;
    int rule_hit = (nl_rule_match(clean_input, &rule_r) == 0);

    if (rule_hit) {
        dbg_pos += snprintf(debug + dbg_pos, sizeof(debug) - dbg_pos,
                            "[规则引擎] 命中 rule#%d intent=%s conf=1.0; ",
                            rule_r.rule_id, nl_intent_name(rule_r.intent));

        /* ── Layer 4: 模板填充 ── */
        NlCommand cmd;
        if (nl_template_fill(rule_r.intent, rule_r.template_id,
                             rule_r.slots, rule_r.slot_count, &cmd) == 0) {
            result->intent = rule_r.intent;
            result->command = cmd.command;
            result->perm_level = cmd.perm_level;
            result->source = NL_SOURCE_RULE;
            result->confidence = 1.0f;
            result->needs_confirm = cmd.needs_confirm;
            result->slot_count = rule_r.slot_count;
            for (int i = 0; i < rule_r.slot_count && i < NL_MAX_SLOTS; i++) {
                result->slots[i].name = rule_r.slots[i].name;
                result->slots[i].value = rule_r.slots[i].value;
                rule_r.slots[i].name = NULL;
                rule_r.slots[i].value = NULL;
            }
            for (int i = 0; i < cmd.path_count && i < NL_MAX_PATHS; i++) {
                result->normalized_paths[i] = cmd.paths[i];
                cmd.paths[i] = NULL;
                result->path_count++;
            }
            cmd.command = NULL;
            nl_command_free(&cmd);
            dbg_pos += snprintf(debug + dbg_pos, sizeof(debug) - dbg_pos,
                                "[模板填充] cmd=%s perm=L%d; ",
                                result->command, result->perm_level);
            snprintf(result->debug_info, sizeof(result->debug_info), "%s", debug);
            nl_rule_result_free(&rule_r);
            nl_sanitize_result_free(&sr);
            return 0;
        }
        nl_command_free(&cmd);
    } else {
        dbg_pos += snprintf(debug + dbg_pos, sizeof(debug) - dbg_pos,
                            "[规则引擎] 未命中 → 进入模型推理; ");
    }
    nl_rule_result_free(&rule_r);

    /* ── Layer 3: 模型推理 ── */
    if (!g_model) {
        dbg_pos += snprintf(debug + dbg_pos, sizeof(debug) - dbg_pos,
                            "[模型推理] 模型未加载，无法继续; ");
        snprintf(result->debug_info, sizeof(result->debug_info), "%s", debug);
        nl_sanitize_result_free(&sr);
        return -1;
    }

    NlIntentResult int_r;
    if (nl_intent_extract(g_model, clean_input, &int_r) != 0 ||
        int_r.status != NL_INFER_OK) {
        dbg_pos += snprintf(debug + dbg_pos, sizeof(debug) - dbg_pos,
                            "[模型推理] 失败 status=%d; ", int_r.status);
        snprintf(result->debug_info, sizeof(result->debug_info), "%s", debug);
        nl_intent_result_free(&int_r);
        nl_sanitize_result_free(&sr);
        return -1;
    }

    dbg_pos += snprintf(debug + dbg_pos, sizeof(debug) - dbg_pos,
                        "[模型推理] intent=%s conf=%.2f latency=stub; ",
                        nl_intent_name(int_r.intent), int_r.confidence);

    /* ── Layer 4: 模板填充 ── */
    NlCommand cmd;
    if (nl_template_fill(int_r.intent, 0, int_r.slots, int_r.slot_count, &cmd) == 0) {
        result->intent = int_r.intent;
        result->command = cmd.command;
        result->perm_level = cmd.perm_level;
        result->source = NL_SOURCE_MODEL;
        result->confidence = int_r.confidence;
        result->needs_confirm = cmd.needs_confirm;
        result->slot_count = int_r.slot_count;
        for (int i = 0; i < int_r.slot_count && i < NL_MAX_SLOTS; i++) {
            result->slots[i].name = int_r.slots[i].name;
            result->slots[i].value = int_r.slots[i].value;
            int_r.slots[i].name = NULL;
            int_r.slots[i].value = NULL;
        }
        for (int i = 0; i < cmd.path_count && i < NL_MAX_PATHS; i++) {
            result->normalized_paths[i] = cmd.paths[i];
            cmd.paths[i] = NULL;
            result->path_count++;
        }
        cmd.command = NULL;
        nl_command_free(&cmd);
    }

    dbg_pos += snprintf(debug + dbg_pos, sizeof(debug) - dbg_pos,
                        "[模板填充] cmd=%s perm=L%d",
                        result->command ? result->command : "(none)", result->perm_level);
    snprintf(result->debug_info, sizeof(result->debug_info), "%s", debug);

    nl_intent_result_free(&int_r);
    nl_sanitize_result_free(&sr);
    return 0;
}

void nl_parse_result_free(NlParseResult *r) {
    if (!r) return;
    free(r->command);
    for (int i = 0; i < r->slot_count; i++) {
        free(r->slots[i].name);
        free(r->slots[i].value);
    }
    for (int i = 0; i < r->path_count; i++) free(r->normalized_paths[i]);
    memset(r, 0, sizeof(NlParseResult));
}

/* ============================================================
 * Agent 扩展点（MVP 单步退化 · ADR-008）
 * ============================================================ */

int nl_plan(const char *goal, NlPlan *plan) {
    if (!goal || !plan) return -1;
    memset(plan, 0, sizeof(NlPlan));
    /* MVP: 永远返回 1 个步骤 */
    plan->capacity = 1;
    plan->steps = calloc(1, sizeof(NlStep));
    if (!plan->steps) return -1;
    plan->steps[0].action = strdup(goal);
    plan->count = 1;
    return 0;
}

int nl_execute_step(const NlStep *step, NlExecResult *r) {
    if (!step || !r) return -1;
    memset(r, 0, sizeof(NlExecResult));
    NlParseResult pr;
    if (nl_parse(step->action, &pr) == 0 && pr.command) {
        r->output = strdup(pr.command);
        r->exit_code = 0;
        nl_parse_result_free(&pr);
    } else {
        r->output = strdup("(无法解析)");
        r->exit_code = -1;
    }
    r->done = 1;
    return 0;
}

int nl_observe(const NlExecResult *r, NlObserveResult *obs) {
    if (!r || !obs) return -1;
    memset(obs, 0, sizeof(NlObserveResult));
    /* MVP: 永远返回 DONE */
    obs->status = 0;
    obs->summary = strdup(r->output ? r->output : "(empty)");
    return 0;
}

void nl_plan_free(NlPlan *p) {
    if (!p) return;
    for (int i = 0; i < p->count; i++) free(p->steps[i].action);
    free(p->steps);
    memset(p, 0, sizeof(NlPlan));
}

void nl_exec_result_free(NlExecResult *r) { if (r) { free(r->output); memset(r,0,sizeof(NlExecResult)); } }
void nl_observe_result_free(NlObserveResult *r) { if (r) { free(r->summary); memset(r,0,sizeof(NlObserveResult)); } }
