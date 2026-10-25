/*
 * verdict.h — 裁决器（ADR-016 / T04）
 *
 * 只回答 allow / confirm / deny；永不执行、永不生成 plan（plan 在 T06 的适配器里）。
 * 原则：effect 由注册表给出；args 里出现的 effect 一律不采信。
 */
#ifndef NL_BROKER_VERDICT_H
#define NL_BROKER_VERDICT_H

#include "capability_registry.h"

#include <stddef.h>

typedef enum {
    NL_VERDICT_ALLOW = 0,
    NL_VERDICT_CONFIRM,
    NL_VERDICT_DENY
} NlVerdict;

typedef struct {
    NlVerdict  verdict;
    char       reason[256];
    int        untrusted_effect_attempt;   /* args 里试图声明 effect */
    int        perm_level;
    NlCapEffect effect;
    char       alternative_ids[3][NL_CAP_MAX_ID_LEN];
    int        alt_count;
} NlVerdictResult;

/* 返回 0 成功；-1 参数错误 */
int         nl_verdict_eval(const NlCapability *cap, const char *args_json,
                            NlVerdictResult *out);
const char *nl_verdict_str(NlVerdict v);

#endif /* NL_BROKER_VERDICT_H */
