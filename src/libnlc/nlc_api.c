/*
 * nlc_api.c — libnlc 公共 API 实现
 *
 * 组装四层管线：安全网关 → 规则引擎 → 模型推理 → 模板填充。
 * Agent 扩展点：MVP 单步退化版本。
 */

#include "nlc_api.h"
#include "resource_gate.h"
#include "path_safety.h"
#include "decision_cmd.h"
#include "capability_registry.h"
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static NlModel *g_model = NULL;
static const struct NlCapRegistry *g_registry = NULL;   /* 借用：由宿主注入（ADR-024）*/
static char g_model_path[512];   /* 惰性初始化记下的路径；空 = 未配置模型 */
static char g_error[1024];

/*
 * 资源闸门：内存不够就不加载模型。
 *
 * 事故：8GB 机器上可用内存只剩 657MB，一句"规则未命中"的话照旧加载 428MB 模型
 * （RSS 涨到 800MB），整机进入内存回收，用户看到的是"本来很快，现在全卡了"。
 * 这里的选择是：**宁可这条不理解，也不拿整台机器的响应性去换**。
 *
 * 注意两点：
 *   1. 拒绝加载时**不清空** g_model_path —— 内存释放后下一次请求能自动恢复；
 *   2. NL_MODEL_MEM_FORCE=1 可跳过闸门（调试/内存充裕但 /proc 读不到的容器里用）。
 */
static int model_mem_gate(void)
{
    const char *force = getenv("NL_MODEL_MEM_FORCE");
    if (force && strcmp(force, "1") == 0) return 0;

    long need  = nl_model_need_mb(g_model_path);
    long avail = nl_mem_available_mb();
    if (nl_resource_gate_allow(need, avail, NL_MODEL_HEADROOM_MB)) return 0;

    snprintf(g_error, sizeof(g_error),
             "内存不足，没有加载模型（需约 %ld MB，当前可用 %ld MB，需为其它程序留 %d MB）。"
             "这句话只能降级到规则层；释放内存后自动恢复，或用 NL_MODEL_MEM_FORCE=1 强制加载。",
             need > 0 ? need : -1, avail, NL_MODEL_HEADROOM_MB);
    fprintf(stderr,
            "⚠️ 内存不足，未加载模型（需约 %ld MB，可用 %ld MB）——为免拖垮整机，降级到规则层\n",
            need > 0 ? need : -1, avail);
    return -1;
}

/* 真要解码时才加载：规则层能决定的句子不该付模型加载的代价 */
static int ensure_model(void)
{
    if (g_model) return 0;
    if (!g_model_path[0]) return -1;
    if (model_mem_gate() != 0) return -1;
    g_model = nl_model_init(g_model_path);
    if (!g_model) {
        snprintf(g_error, sizeof(g_error), "nl_init: failed to load model '%s'", g_model_path);
        /* 一次加载失败就不再重试：否则 REPL 里每条命令都要白等一次加载 */
        fprintf(stderr, "⚠️ 模型加载失败，降档到规则引擎\n");
        g_model_path[0] = '\0';
        return -1;
    }
    g_error[0] = '\0';
    return 0;
}

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
    snprintf(g_model_path, sizeof(g_model_path), "%s", model_path);
    g_error[0] = '\0';
    return 0;
}

int nl_init_lazy(const char *model_path) {
    if (!model_path) {
        snprintf(g_error, sizeof(g_error), "nl_init_lazy: model_path is NULL");
        return -1;
    }
    if (g_model) { nl_model_free(g_model); g_model = NULL; }
    /* 只做一次可读性检查（nanoseconds 级），真加载推迟到 ensure_model() */
    if (access(model_path, R_OK) != 0) {
        g_model_path[0] = '\0';
        snprintf(g_error, sizeof(g_error), "nl_init_lazy: model not readable '%s'", model_path);
        return -1;
    }
    snprintf(g_model_path, sizeof(g_model_path), "%s", model_path);
    g_error[0] = '\0';
    return 0;
}

int nl_model_is_loaded(void) { return g_model ? 1 : 0; }

void nl_set_registry(const struct NlCapRegistry *reg) { g_registry = reg; }
const struct NlCapRegistry *nl_get_registry(void) { return g_registry; }

void nl_free(void) {
    if (g_model) { nl_model_free(g_model); g_model = NULL; }
    g_model_path[0] = '\0';
}

const char *nl_get_last_error(void) { return g_error; }


/* ============================================================
 * 安全网关：写操作的路径必须过黑名单/白名单（ADR-012 / ADR-024 补）
 *
 * 事故：「删除 /etc/passwd」一路走到 L3 确认，生成的命令是 `rm -rf /etc/passwd`。
 * 安全网关的 nl_path_check() 一直在，但只在 broker/verdict 那条路上被调用过；
 * 规则层/模板层的路径从没经过它 —— 也就是说黑名单对 nlsh 的主路径是**失效**的。
 *
 * 口径：只有 perm >= L2（会改东西）的命令才做检查；读操作不受写白名单约束
 * （`看看 /etc/hosts` 必须仍然能读，这本来就允许）。
 * ============================================================ */
static int cmd_paths_denied(const NlCommand *cmd) {
    if (!cmd || cmd->perm_level < NL_PERM_L2) return 0;
    for (int i = 0; i < cmd->path_count; i++) {
        /* 相对路径先按当前目录展开：`rm -rf error.log` 的目标是 $PWD/error.log，
         * 不展开就会被白名单当成"不在 /home /tmp 之下"而误拒。 */
        char resolved[PATH_MAX * 2];
        const char *p = cmd->paths[i];
        if (p && p[0] != '/' && p[0] != '~') {
            char cwd[PATH_MAX];
            if (getcwd(cwd, sizeof(cwd)))
                snprintf(resolved, sizeof(resolved), "%s/%.500s", cwd, p);
            else
                snprintf(resolved, sizeof(resolved), "%.500s", p);
            p = resolved;
        }
        NlPathResult pr = nl_path_check(p, 1);
        int bad = (pr.status != NL_PATH_PASS);
        if (bad)
            snprintf(g_error, sizeof(g_error), "安全网关拒绝：%s", pr.reason);
        nl_path_result_free(&pr);
        if (bad) return 1;
    }
    return 0;
}

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
            if (cmd_paths_denied(&cmd)) {       /* 写操作的目标路径被安全网关拒绝 */
                nl_command_free(&cmd);
                nl_rule_result_free(&rule_r);
                nl_sanitize_result_free(&sr);
                return -1;
            }
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

    /* ── Layer 3: 模型推理（惰性：到这里才真正加载模型） ── */
    if (ensure_model() != 0) {
        dbg_pos += snprintf(debug + dbg_pos, sizeof(debug) - dbg_pos,
                            "[模型推理] 模型未加载或加载失败，无法继续; ");
        snprintf(result->debug_info, sizeof(result->debug_info), "%s", debug);
        nl_sanitize_result_free(&sr);
        return -1;
    }

    /* ── Layer 3a: 受约束选择 ──
     * 说法模糊、但候选里有不止一个"能生成命令"的能力时，让模型在候选集内选
     * （GBNF 约束，它生成不出候选之外的 id）。选不出来就继续走经典 intent 提取。 */
    if (g_registry) {
        if (nl_parse_by_choice(g_model, clean_input, g_registry, result) == 1) {
            dbg_pos += snprintf(debug + dbg_pos, sizeof(debug) - dbg_pos,
                                "[受约束选择] cmd=%s perm=L%d; ",
                                result->command, result->perm_level);
            snprintf(result->debug_info, sizeof(result->debug_info), "%s", debug);
            nl_rule_result_free(&rule_r);
            nl_sanitize_result_free(&sr);
            return 0;
        }
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

    /* 结构化参数兜底（ADR-024）：模型负责判"是哪一类"，参数按形状抽。
     * 模型没给 domain 时，从原句里按域名形状抠一个（形状不合规就不给），
     * 这样「解析 www.qq.com 指向哪台机器」即使模型只答对了 intent，
     * 也能落到正确的命令上；而不是因为少一个参数就整条作废。 */
    {
        int has_domain = 0;
        for (int i = 0; i < int_r.slot_count; i++)
            if (int_r.slots[i].name && strcmp(int_r.slots[i].name, "domain") == 0) has_domain = 1;
        if (!has_domain && int_r.slot_count < NL_MAX_SLOTS) {
            char *dom = nl_extract_domain(clean_input);
            if (dom) {
                int_r.slots[int_r.slot_count].name = strdup("domain");
                int_r.slots[int_r.slot_count].value = dom;   /* 接管所有权 */
                int_r.slot_count++;
            }
        }
    }

    /* ── Layer 4: 模板填充 ── */
    NlCommand cmd;
    if (nl_template_fill(int_r.intent, 0, int_r.slots, int_r.slot_count, &cmd) == 0) {
        if (cmd_paths_denied(&cmd)) {
            nl_command_free(&cmd);
            nl_intent_result_free(&int_r);
            nl_sanitize_result_free(&sr);
            return -1;
        }
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

/* 只跑规则层：与 nl_parse 前两层的逻辑保持一致（unit test 断言两者判定一致） */
int nl_parse_rules_only(const char *input, NlParseResult *result) {
    if (!input || !result) return -1;
    memset(result, 0, sizeof(NlParseResult));

    NlSanitizeResult sr = nl_input_sanitize(input);
    const char *clean_input = sr.cleaned_input ? sr.cleaned_input : input;

    NlRuleResult rule_r;
    int hit = (nl_rule_match(clean_input, &rule_r) == 0);
    int rc = 0;
    if (hit) {
        NlCommand cmd;
        if (nl_template_fill(rule_r.intent, rule_r.template_id,
                             rule_r.slots, rule_r.slot_count, &cmd) == 0) {
            if (cmd_paths_denied(&cmd)) {
                nl_command_free(&cmd);
                nl_rule_result_free(&rule_r);
                nl_sanitize_result_free(&sr);
                return -1;
            }
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
            snprintf(result->debug_info, sizeof(result->debug_info),
                     "[规则引擎] 命中 rule#%d intent=%s（无模型）",
                     rule_r.rule_id, nl_intent_name(rule_r.intent));
            rc = 1;
        } else {
            nl_command_free(&cmd);
        }
    }
    nl_rule_result_free(&rule_r);
    nl_sanitize_result_free(&sr);
    return rc;
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
