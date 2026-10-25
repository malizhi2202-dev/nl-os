/*
 * decision_cmd.c — 决策层兜底：认出工具就用"审查过的只读默认参数"执行
 *
 * 背景：规则层未命中时，原来的唯一出路是模型的自由生成（实测单次 20s 量级，
 * 且对绝大多数输入返回"无法理解"）。而决策层（候选收缩 + 受约束 Choice）能在毫秒级
 * 认出"这句话在说哪个工具"——只是它只给能力 id，不给参数。
 *
 * 做法：维护一张**极小、逐条审查过**的只读默认参数表，命中即拼成命令，走原有的
 * 确认门/权限/快照/审计链路执行。表外的一律不猜（返回 0，让上层继续或如实拒绝）。
 *
 * 安全边界（ADR-022）：
 *   1. 只对 capability_perm_map.h 里 **L1** 且 trusted 的普通文件生效；
 *      注册表未登记 = L4 = 不放行（`ss` 就属于这类，不偷偷提权）。
 *   2. 参数来自本文件里的**字面量**，不接受任何来自输入/模型的参数拼接。
 *   3. 不在这张表里的工具，即使决策层认出来了也不执行。
 */

#include "decision.h"
#include "nlc_api.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* 只读默认参数表：name → 参数（"" 表示不带参数）。全部是 L1 只读工具。 */
typedef struct {
    const char *name;
    const char *args;
    NlIntentType intent;
} NlReadOnlyDefault;

static const NlReadOnlyDefault READ_DEFAULTS[] = {
    { "pwd",    "",              NL_INTENT_OTHER        },
    { "ls",     "-la",           NL_INTENT_LIST_DIR     },
    { "ps",     "aux",           NL_INTENT_SHOW_PROCESS },
    { "df",     "-h",            NL_INTENT_OTHER        },
    { "free",   "-m",            NL_INTENT_OTHER        },
    { "uptime", "",              NL_INTENT_OTHER        },
    { "whoami", "",              NL_INTENT_OTHER        },
    { "uname",  "-a",            NL_INTENT_OTHER        },
    { "date",   "",              NL_INTENT_OTHER        },
    { "lsof",   "-i -P -n",      NL_INTENT_OTHER        },  /* 端口/连接 */
    { NULL, NULL, NL_INTENT_OTHER }
};

/* 命中则返回参数指针（可能是 ""），未命中返回 NULL */
static const char *default_args(const char *name, NlIntentType *intent)
{
    for (int i = 0; READ_DEFAULTS[i].name; i++) {
        if (strcmp(READ_DEFAULTS[i].name, name) == 0) {
            if (intent) *intent = READ_DEFAULTS[i].intent;
            return READ_DEFAULTS[i].args;
        }
    }
    return NULL;
}

/* 候选分数下限：词典命中一次就是 8，低于它等于没认出任何东西 */
#define NL_DECISION_MIN_SCORE 8

int nl_parse_by_decision(const char *input, const NlCapRegistry *reg, NlParseResult *r)
{
    if (!input || !reg || !r) return -1;

    NlCandidates c;
    if (nl_decide_candidates(input, reg, &c) != 0) return -1;
    if (c.count == 0) return 0;

    for (int k = 0; k < c.count; k++) {
        if (c.items[k].score < NL_DECISION_MIN_SCORE) break;   /* 已按分数降序 */

        const NlCapability *cap = nl_cap_find(reg, c.items[k].id);
        if (!cap || !cap->path || !cap->trusted) continue;
        if (cap->kind != NL_CAP_KIND_FILE) continue;
        /* 只兜底 L1 只读：L2 要确认、L3 高危、L4 禁止，都不是"兜底"该干的事 */
        if (cap->perm_level != 1) continue;

        const char *local = strrchr(cap->id, ':');
        local = local ? local + 1 : cap->id;
        NlIntentType intent = NL_INTENT_OTHER;
        const char *args = default_args(local, &intent);
        if (!args) continue;               /* 表外工具：不猜 */

        char cmd[1024];
        int n = args[0] ? snprintf(cmd, sizeof(cmd), "%s %s", cap->path, args)
                        : snprintf(cmd, sizeof(cmd), "%s", cap->path);
        if (n < 0 || (size_t)n >= sizeof(cmd)) continue;

        memset(r, 0, sizeof(*r));
        r->command = strdup(cmd);
        if (!r->command) return -1;
        r->intent = intent;
        r->perm_level = (NlPermLevel)cap->perm_level;
        r->source = NL_SOURCE_DECISION;
        r->confidence = 1.0f;
        r->needs_confirm = (cap->perm_level >= 2);
        snprintf(r->debug_info, sizeof(r->debug_info),
                 "[决策层] 认出 %s（score=%d）→ 只读默认参数，未加载模型",
                 cap->id, c.items[k].score);
        return 1;
    }
    return 0;
}
