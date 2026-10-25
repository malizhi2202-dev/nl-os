/*
 * budget.h — 规则版复杂度预算（ADR-013 D13/D14）
 *
 * 结构化判定，不调模型：跨能力数 / 复合词 / 特权 / 未注册能力。
 */
#ifndef NL_BUDGET_H
#define NL_BUDGET_H

#include "capability_registry.h"

#define NL_BUDGET_MAX_CAPS      3   /* 单条指令最多跨几个能力 */
#define NL_BUDGET_MAX_ALTERNATIVES 3

typedef enum {
    NL_BUDGET_PASS   = 0,
    NL_BUDGET_REFUSE = 1
} NlBudgetVerdict;

typedef enum {
    NL_DIM_NONE = 0,
    NL_DIM_TOO_MANY_CAPS,
    NL_DIM_COMPOSITE,      /* 定时/循环/条件/并发/上传下载 等复合语义 */
    NL_DIM_PRIVILEGE,      /* 需要特权 */
    NL_DIM_UNKNOWN_CAP,    /* 目标能力不在注册表 */
    NL_DIM_MAX
} NlBudgetDim;

typedef struct {
    NlBudgetDim dims[8];
    int         dim_count;
    char        alternative_ids[NL_BUDGET_MAX_ALTERNATIVES][NL_CAP_MAX_ID_LEN];
    int         alternative_count;
} NlBudgetDetail;

NlBudgetVerdict nl_budget_check(const char *input, const NlCapRegistry *r,
                                NlBudgetDetail *detail);
const char     *nl_budget_dim_str(NlBudgetDim d);

#endif /* NL_BUDGET_H */
