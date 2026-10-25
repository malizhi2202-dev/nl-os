/*
 * decision.h — 类型化决策层的模型无关部分（ADR-013 / T10-T12）
 *
 * 三件事：
 *   1. 候选收缩（词典/子串，规则版，不调模型）
 *   2. Choice 的 GBNF 语法构造（只枚举候选集，避免全量枚举）
 *   3. Noul 单调性组合（模型信号只能让结果更严格）
 */
#ifndef NL_DECISION_H
#define NL_DECISION_H

#include "capability_registry.h"

#include <stddef.h>

#define NL_DECIDE_K 8

typedef struct {
    char id[NL_CAP_MAX_ID_LEN];
    int  score;
} NlCandidate;

typedef struct {
    NlCandidate items[NL_DECIDE_K];
    int         count;
    int         total_scanned;   /* 粗筛覆盖率统计用 */
} NlCandidates;

/* 0 成功（count 可为 0，表示没有候选）；-1 参数错误 */
int nl_decide_candidates(const char *input, const NlCapRegistry *reg, NlCandidates *out);

/* Choice 的 GBNF 语法；候选数为 0 时返回 -1。成功返回写入长度 */
int nl_decide_grammar(const NlCandidates *c, char *buf, size_t cap);

/* Noul：0=allow 1=confirm 2=deny；结果取两者较严者（单调性） */
typedef enum { NL_NOUL_ALLOW = 0, NL_NOUL_CONFIRM = 1, NL_NOUL_DENY = 2 } NlNoulLevel;
NlNoulLevel nl_noul_combine(NlNoulLevel rule_level, NlNoulLevel model_level);
const char *nl_noul_str(NlNoulLevel level);

#endif /* NL_DECISION_H */
