/*
 * budget.c — 复杂度预算（T01 占位骨架，T05 实现）
 */
#include "budget.h"

NlBudgetVerdict nl_budget_check(const char *input, const NlCapRegistry *r,
                                NlBudgetDetail *detail)
{
    (void)input; (void)r; (void)detail;
    return NL_BUDGET_PASS;
}

const char *nl_budget_dim_str(NlBudgetDim d)
{
    switch (d) {
    case NL_DIM_NONE:            return "none";
    case NL_DIM_TOO_MANY_CAPS:   return "too-many-capabilities";
    case NL_DIM_COMPOSITE:       return "composite-semantics";
    case NL_DIM_PRIVILEGE:       return "needs-privilege";
    case NL_DIM_UNKNOWN_CAP:     return "unknown-capability";
    default:                     return "unknown";
    }
}
