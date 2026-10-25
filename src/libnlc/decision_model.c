/*
 * decision_model.c — 解码后端：档位选择 + 受约束 Choice（T11b/T13）
 *
 * 安全性质（与 ADR-013 一致）：
 *   - 语法只枚举候选集，模型无法生成候选之外的能力 id
 *   - 即使语法构造失败退回贪心，输出仍要经 nl_choose_parse 过滤；
 *     不在候选集内一律按 ERR 处理，**不会失败即放行**
 *   - <refuse> 是合法输出，走拒答路径而不是硬选一个
 */
#include "decision_model.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define NL_PROMPT_CAP   4096
#define NL_GRAMMAR_CAP  4096
#define NL_RAW_CAP      256

#define MB ((size_t)1024 * 1024)
/*
 * 置信度阈值默认 0（关闭）。
 * 依据（T13 实测，77 次选择）：本指标是"整段字面量的 token 概率几何平均"，
 * 其中 `"`、`cli` 等被语法强制的位置概率天然偏低，导致它与"选择是否正确"
 * 没有单调关系——阈值从 0 提到 0.5，接受样本的准确率反而从 85.7% 降到 80.6%，
 * 却丢掉 53% 的正确回答。因此默认不启用，只保留开关给需要"宁可少做"的场景。
 * 真正有区分度的信号是"候选为空"与模型主动 <refuse>，这两条恒有效。
 */
#define NL_MIN_CONF_DEFAULT 0.0

double nl_choose_min_conf(void)
{
    const char *env = getenv("NL_DECIDE_MIN_CONF");
    if (env && env[0]) {
        char *end = NULL;
        double v = strtod(env, &end);
        if (end && *end == '\0' && v >= 0.0 && v <= 1.0) return v;
    }
    return NL_MIN_CONF_DEFAULT;
}

/* 规则层有把握时不必问模型：top-1 领先次优 >= margin（默认 20）视为"已确定"。
 * 依据：规则意图提示是 +50，词典命中只有 +8/+10；合并后实测 top-1 准确率 98.8%，
 * 而模型在"规则也没把握"的少数样本上并没有更好。NL_DECIDE_MARGIN 可调（0=总是问模型）。 */
static int rule_confident_margin(void)
{
    const char *env = getenv("NL_DECIDE_MARGIN");
    if (env && env[0]) {
        int v = atoi(env);
        if (v >= 0 && v <= 100) return v;
    }
    return 20;
}

int nl_choose_solo_decode(void)
{
    const char *env = getenv("NL_DECIDE_SOLO_DECODE");
    return (env && env[0] == '1') ? 1 : 0;
}

size_t nl_total_ram_bytes(void)
{
    FILE *f = fopen("/proc/meminfo", "r");
    if (!f) return 0;
    char line[256];
    size_t bytes = 0;
    while (fgets(line, sizeof(line), f)) {
        unsigned long kb = 0;
        if (sscanf(line, "MemTotal: %lu kB", &kb) == 1) {
            bytes = (size_t)kb * 1024u;
            break;
        }
    }
    fclose(f);
    return bytes;
}

int nl_tier_from_bytes(size_t total_ram)
{
    if (total_ram == 0) return 1;                 /* 探测失败按最保守档 */
    if (total_ram < (size_t)1024 * MB) return 1;  /* A1 128-768MB（含边界以下） */
    if (total_ram < (size_t)8192 * MB) return 2;  /* A2 1-4GB（<8GB 归 A2） */
    if (total_ram < (size_t)32768 * MB) return 3; /* B 8-16GB */
    return 4;                                     /* C >=32GB */
}

NlBackend nl_backend_for_tier(int tier)
{
    switch (tier) {
    case 1: return NL_BACKEND_EMBEDDING; /* A1：decoder 完全不上，避免 OOM */
    case 2:                              /* A2：可选 0.35B/0.5B decoder */
    case 3:                              /* B：0.5B-1.5B decoder */
    case 4: return NL_BACKEND_LLM;       /* C：3B-7B decoder */
    default: return NL_BACKEND_RULE;     /* 未知档位按规则层，缺省从严 */
    }
}

int nl_choose_user_prompt(const char *input, const NlCandidates *c,
                          char *out, size_t cap)
{
    if (!input || !c || !out || cap == 0 || c->count <= 0) return -1;

    size_t used = (size_t)snprintf(out, cap, "用户输入：%s\n候选：\n", input);
    if (used == 0 || used >= cap) return -1;

    for (int i = 0; i < c->count; i++) {
        int n = snprintf(out + used, cap - used, "- \"%s\"\n", c->items[i].id);
        /* 写满只可能是输入或候选异常长：直接失败，不截断成半截 prompt */
        if (n < 0 || (size_t)n >= cap - used) return -1;
        used += (size_t)n;
    }
    int n = snprintf(out + used, cap - used, "输出：");
    if (n < 0 || (size_t)n >= cap - used) return -1;
    used += (size_t)n;
    return (int)used;
}

int nl_choose_prompt(NlModel *m, const char *input, const NlCandidates *c,
                     char *out, size_t cap)
{
    if (!m || !input || !c || !out || cap == 0 || c->count <= 0) return -1;

    static const char SYSTEM[] =
        "你是 Linux 命令路由器。根据用户输入，从候选 id 里选出最匹配的一个。"
        "只输出选中 id 的 JSON 字符串字面量（带双引号），不要输出任何解释。"
        "若候选都不匹配，输出 \"<refuse>\"。";

    char user[NL_PROMPT_CAP];
    if (nl_choose_user_prompt(input, c, user, sizeof(user)) < 0) return -1;

    return nl_model_format_chat(m, SYSTEM, user, out, cap);
}

int nl_choose_parse(const char *raw, const NlCandidates *c)
{
    if (!raw || !c || c->count <= 0) return -1;

    /* 去掉两侧空白 */
    const char *s = raw;
    while (*s == ' ' || *s == '\t' || *s == '\n' || *s == '\r') s++;
    size_t len = strlen(s);
    while (len > 0) {
        char last = s[len - 1];
        if (last == ' ' || last == '\t' || last == '\n' || last == '\r') len--;
        else break;
    }
    if (len == 0) return -1;

    /* 去掉两侧引号（语法产生的是 "\"cli:ls\"" 形式，token 化后可能带引号） */
    if (len >= 2 && s[0] == '"' && s[len - 1] == '"') { s++; len -= 2; }

    char tok[NL_CAP_MAX_ID_LEN];
    if (len == 0 || len >= sizeof(tok)) return -1;
    memcpy(tok, s, len);
    tok[len] = '\0';

    if (strcmp(tok, "<refuse>") == 0) return -2;

    for (int i = 0; i < c->count; i++)
        if (strcmp(tok, c->items[i].id) == 0) return i;

    return -1; /* 不在候选集内：拒绝，绝不猜 */
}

NlChoiceStatus nl_choose_with_model(NlModel *m, const char *input,
                                    const NlCandidates *c,
                                    char *chosen, size_t cap)
{
    if (!m || !input || !c || !chosen || cap == 0) return NL_CHOICE_ERR;
    if (c->count <= 0) return NL_CHOICE_ERR;

    /* 规则已经确定时不问模型：单候选（唯一可选）或 top-1 明显领先 */
    int decisive = (c->count == 1);
    if (!decisive && c->count >= 2) {
        int margin = rule_confident_margin();
        if (margin > 0 && c->items[0].score - c->items[1].score >= margin) decisive = 1;
    }
    if (decisive && !nl_choose_solo_decode()) {
        size_t n = strlen(c->items[0].id);
        if (n >= cap) return NL_CHOICE_ERR;
        memcpy(chosen, c->items[0].id, n + 1);
        return NL_CHOICE_OK;
    }

    char grammar[NL_GRAMMAR_CAP];
    if (nl_decide_grammar(c, grammar, sizeof(grammar)) < 0) return NL_CHOICE_ERR;

    char prompt[NL_PROMPT_CAP];
    if (nl_choose_prompt(m, input, c, prompt, sizeof(prompt)) < 0) return NL_CHOICE_ERR;

    char raw[NL_RAW_CAP];
    if (nl_model_infer_grammar(m, prompt, grammar, raw, sizeof(raw)) <= 0)
        return NL_CHOICE_ERR;

    int idx = nl_choose_parse(raw, c);
    if (idx == -2) return NL_CHOICE_REFUSE;
    if (idx < 0) return NL_CHOICE_ERR;

    /* AC-8：概率不达标只列候选，绝不给"待执行命令" */
    double min_conf = nl_choose_min_conf();
    if (min_conf >= 0.0) {
        double p = nl_model_last_confidence(m);
        if (p >= 0.0 && p < min_conf) return NL_CHOICE_UNCERTAIN;
    }

    size_t n = strlen(c->items[idx].id);
    if (n >= cap) return NL_CHOICE_ERR;
    memcpy(chosen, c->items[idx].id, n + 1);
    return NL_CHOICE_OK;
}
