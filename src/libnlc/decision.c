/*
 * decision.c — 决策层的模型无关部分（T10/T11/T12）
 */
#include "decision.h"
#include "rule_engine.h"

#include <stdio.h>
#include <string.h>

/* 词典粗筛：中文动作词 → 能力名（只用于排候选，不做最终判定） */
typedef struct {
    const char *keyword;
    const char *cap_name;
} NlDecideHint;

static const NlDecideHint HINTS[] = {
    { "列出", "ls" },   { "查看目录", "ls" },  { "当前目录", "ls" },
    { "查看", "cat" },  { "内容", "cat" },     { "读取", "cat" },
    { "创建目录", "mkdir" }, { "文件夹", "mkdir" }, { "新建目录", "mkdir" },
    { "创建文件", "touch" }, { "空文件", "touch" },
    { "删除", "rm" },   { "删掉", "rm" },
    { "移动", "mv" },   { "重命名", "mv" },
    { "复制", "cp" },   { "拷贝", "cp" },
    { "查找", "find" }, { "搜索", "find" },   { "找到", "find" },
    { "进程", "ps" },   { "占用", "ps" },
    { "压缩", "tar" },  { "打包", "tar" },    { "解压", "tar" },
    { "下载", "curl" }, { "抓取", "curl" },
    { "权限", "chmod" },{ "授权", "chmod" },
    { "磁盘", "df" },   { "空间", "df" },
    { "帮助", "man" },  { "文档", "man" },
    { NULL, NULL }
};

static int hint_score_for(const char *name, const char *input)
{
    int score = 0;
    for (int i = 0; HINTS[i].keyword != NULL; i++) {
        if (strcmp(HINTS[i].cap_name, name) != 0) continue;
        if (strstr(input, HINTS[i].keyword) != NULL) score += 8;
    }
    return score;
}

/* 把能力 ID 的本地名取出（cli:mkdir → mkdir） */
static const char *local_name(const NlCapability *c)
{
    const char *colon = strchr(c->id, ':');
    return colon ? colon + 1 : c->id;
}

/* 保持 top-K，插入排序（K=8，开销可忽略） */
static void offer(NlCandidates *out, const char *id, int score)
{
    if (score <= 0) return;

    int pos = out->count;
    for (int i = 0; i < out->count; i++) {
        if (strcmp(out->items[i].id, id) == 0) {
            if (score > out->items[i].score) out->items[i].score = score;
            else return;
            pos = i;
            goto bubble;
        }
    }
    if (out->count < NL_DECIDE_K) {
        snprintf(out->items[pos].id, NL_CAP_MAX_ID_LEN, "%s", id);
        out->items[pos].score = score;
        out->count++;
    } else if (score <= out->items[NL_DECIDE_K - 1].score) {
        return;
    } else {
        snprintf(out->items[NL_DECIDE_K - 1].id, NL_CAP_MAX_ID_LEN, "%s", id);
        out->items[NL_DECIDE_K - 1].score = score;
        pos = NL_DECIDE_K - 1;
    }

bubble:
    while (pos > 0 && out->items[pos].score > out->items[pos - 1].score) {
        NlCandidate t = out->items[pos];
        out->items[pos] = out->items[pos - 1];
        out->items[pos - 1] = t;
        pos--;
    }
}

/* 规则层意图 → 默认能力（与 nlsh -c 的执行路径保持同一判断，避免"能执行但选不出来"）。
 * change_dir 是 shell 内建、run_cmd/other 没有单一能力，这三类不出提示。 */
static const char *cap_for_intent(NlIntentType intent)
{
    switch (intent) {
    case NL_INTENT_CREATE_DIR:     return "cli:mkdir";
    case NL_INTENT_CREATE_FILE:    return "cli:touch";
    case NL_INTENT_DELETE:         return "cli:rm";
    case NL_INTENT_LIST_DIR:       return "cli:ls";
    case NL_INTENT_VIEW_FILE:      return "cli:cat";
    case NL_INTENT_FIND_FILES:     return "cli:find";
    case NL_INTENT_SHOW_PROCESS:   return "cli:ps";
    case NL_INTENT_MOVE:           return "cli:mv";
    case NL_INTENT_COPY:           return "cli:cp";
    case NL_INTENT_SEARCH_CONTENT: return "cli:grep";
    default:                       return NULL;
    }
}

int nl_decide_candidates(const char *input, const NlCapRegistry *reg, NlCandidates *out)
{
    if (!input || !reg || !out) return -1;
    memset(out, 0, sizeof(*out));

    int total = nl_cap_count(reg);
    out->total_scanned = total;

    /* 规则层先判一次：命中的意图给出强提示（+50），远高于词典的 +8/+10。
     * 这样"能执行的说法"与"决策层选得出来的说法"是同一套判断。 */
    NlRuleResult rr;
    if (nl_rule_match(input, &rr) == 0) {
        const char *hint = cap_for_intent(rr.intent);
        if (hint && nl_cap_find(reg, hint)) {
            const NlCapability *hc = nl_cap_find(reg, hint);
            if (hc && hc->perm_level < 4) offer(out, hint, 50);
        }
        nl_rule_result_free(&rr);
    }

    for (int i = 0; i < total; i++) {
        const NlCapability *c = nl_cap_at(reg, i);
        if (!c) continue;
        /* L4 能力不进候选：模型无从选择"禁止执行" */
        if (c->perm_level >= 4) continue;

        const char *name = local_name(c);
        int score = 0;
        if (name[0] != '\0' && strstr(input, name) != NULL) score += 10;
        score += hint_score_for(name, input);
        offer(out, c->id, score);
    }
    return 0;
}

int nl_decide_grammar(const NlCandidates *c, char *buf, size_t cap)
{
    if (!c || !buf || cap == 0) return -1;
    if (c->count <= 0) return -1;

    size_t used = (size_t)snprintf(buf, cap, "root ::= choice\nchoice ::= ");
    for (int i = 0; i < c->count; i++) {
        int n = snprintf(buf + used, cap - used, "%s\"\\\"%s\\\"\"",
                         (i ? " | " : ""), c->items[i].id);
        if (n < 0 || (size_t)n >= cap - used) return -1;
        used += (size_t)n;
    }
    /* 兜底选项：允许模型拒绝 */
    int n = snprintf(buf + used, cap - used, " | \"\\\"<refuse>\\\"\"\n");
    if (n < 0 || (size_t)n >= cap - used) return -1;
    used += (size_t)n;
    return (int)used;
}

NlNoulLevel nl_noul_combine(NlNoulLevel rule_level, NlNoulLevel model_level)
{
    /* 单调性：取较严者。模型只能让结果更严格，永远不能放宽规则判定 */
    return (model_level > rule_level) ? model_level : rule_level;
}

const char *nl_noul_str(NlNoulLevel level)
{
    switch (level) {
    case NL_NOUL_ALLOW:   return "allow";
    case NL_NOUL_CONFIRM: return "confirm";
    case NL_NOUL_DENY:    return "deny";
    default:              return "deny";
    }
}
