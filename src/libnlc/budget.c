/*
 * budget.c — 规则版复杂度预算（ADR-013 D13/D14 / T05）
 *
 * 结构化判定，不调模型：复合语义 / 跨能力数 / 特权需求。
 * 超限即 refused，并给出注册表内可执行的替代能力。
 */
#include "budget.h"

#include <stdio.h>
#include <string.h>

static const char *const COMPOSITE_WORDS[] = {
    "每天", "每晚", "每小时", "定时", "循环", "每隔", "递归", "条件",
    "如果", "否则", "判断",
    "并发", "批量", "监控", "守护", "自动", "上传", "下载", "备份",
    NULL
};

static const char *const PRIV_WORDS[] = {
    "sudo", "root", "提权", "改密码", "安装", "卸载", "重启服务", "内核模块",
    NULL
};

/*
 * 多动作连接词：出现任意一个就说明这句话要两件以上的事。
 * 原来阈值是「≥3 次」，导致「…并清理原文件」被放行、随后被规则层降级成
 * 单条 `rm -rf`（UAT S4）。链式执行不在能力边界内 → 如实拒绝。
 */
static const char *const CHAIN_WORDS[] = {
    "并", "然后", "接着", "最后", "之后", "同时", NULL
};

/*
 * 本机/本层没有对应能力的操作词：宁可说"做不到"，也不近似成别的动作。
 * 与 COMPOSITE_WORDS 的区别是这里表达的是「缺少能力」而非「语义太复杂」。
 */
static const char *const UNSUPPORTED_WORDS[] = {
    "压缩", "解压", "打包", "排序", "替换", "发送", "远程",
    "上传", "下载", "定时", "监控", "循环", "递归", "批量",
    "并发", "守护", NULL
};

/* `压缩包` 是一个归档文件（名词），不是"压缩"这个动作。
 * 事故：「把这个压缩包搬到 /opt」被判成"需要压缩能力 → 本层做不到"而拒绝。 */
static int unsupported_hit(const char *input)
{
    for (int i = 0; UNSUPPORTED_WORDS[i] != NULL; i++) {
        const char *w = UNSUPPORTED_WORDS[i];
        size_t wl = strlen(w);
        const char *p = input;
        while ((p = strstr(p, w)) != NULL) {
            if (strcmp(w, "压缩") == 0 && strncmp(p + wl, "包", 3) == 0) {
                p += wl;
                continue;              /* 压缩包：不是动作 */
            }
            return 1;
        }
    }
    return 0;
}

static int contains_any(const char *input, const char *const *words)
{
    for (int i = 0; words[i] != NULL; i++)
        if (strstr(input, words[i]) != NULL) return 1;
    return 0;
}

static int count_chain(const char *input)
{
    int n = 0;
    for (int i = 0; CHAIN_WORDS[i] != NULL; i++) {
        const char *p = input;
        while ((p = strstr(p, CHAIN_WORDS[i])) != NULL) {
            n++;
            p += strlen(CHAIN_WORDS[i]);
        }
    }
    return n;
}

static void add_dim(NlBudgetDetail *d, NlBudgetDim dim)
{
    for (int i = 0; i < d->dim_count; i++)
        if (d->dims[i] == dim) return;
    if (d->dim_count < (int)(sizeof(d->dims) / sizeof(d->dims[0])))
        d->dims[d->dim_count++] = dim;
}

/* 替代能力：只从注册表里挑低危且真实存在的条目 */
static void fill_alternatives(const NlCapRegistry *r, NlBudgetDetail *d)
{
    static const char *const PREFERRED[] = { "ls", "stat", "cat", "echo" };
    d->alternative_count = 0;

    for (size_t k = 0; k < sizeof(PREFERRED) / sizeof(PREFERRED[0]); k++) {
        if (d->alternative_count >= NL_BUDGET_MAX_ALTERNATIVES) return;
        char id[NL_CAP_MAX_ID_LEN];
        snprintf(id, sizeof(id), "cli:%s", PREFERRED[k]);
        const NlCapability *c = nl_cap_find(r, id);
        if (c && c->perm_level <= 2) {
            snprintf(d->alternative_ids[d->alternative_count], NL_CAP_MAX_ID_LEN, "%s", id);
            d->alternative_count++;
        }
    }
    if (d->alternative_count > 0 || !r) return;

    for (int i = 0; i < nl_cap_count(r) && d->alternative_count < NL_BUDGET_MAX_ALTERNATIVES; i++) {
        const NlCapability *c = nl_cap_at(r, i);
        if (c && c->perm_level <= 2) {
            snprintf(d->alternative_ids[d->alternative_count], NL_CAP_MAX_ID_LEN, "%s", c->id);
            d->alternative_count++;
        }
    }
}

NlBudgetVerdict nl_budget_check(const char *input, const NlCapRegistry *r,
                                NlBudgetDetail *detail)
{
    if (!detail) return NL_BUDGET_PASS;
    memset(detail, 0, sizeof(*detail));
    if (!input || input[0] == '\0') return NL_BUDGET_PASS;

    if (contains_any(input, COMPOSITE_WORDS)) add_dim(detail, NL_DIM_COMPOSITE);
    if (contains_any(input, PRIV_WORDS)) add_dim(detail, NL_DIM_PRIVILEGE);
    if (unsupported_hit(input)) add_dim(detail, NL_DIM_UNKNOWN_CAP);
    if (count_chain(input) >= 1) add_dim(detail, NL_DIM_TOO_MANY_CAPS);

    if (detail->dim_count == 0) return NL_BUDGET_PASS;

    fill_alternatives(r, detail);
    return NL_BUDGET_REFUSE;
}

const char *nl_budget_dim_str(NlBudgetDim d)
{
    switch (d) {
    case NL_DIM_NONE:          return "none";
    case NL_DIM_TOO_MANY_CAPS: return "跨能力过多";
    case NL_DIM_COMPOSITE:     return "复合语义";
    case NL_DIM_PRIVILEGE:     return "需要特权";
    case NL_DIM_UNKNOWN_CAP:   return "能力不存在";
    default:                   return "unknown";
    }
}
