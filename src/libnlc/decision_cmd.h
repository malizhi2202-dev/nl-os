/*
 * decision_cmd.h — 决策层兜底（ADR-022）
 *
 * 规则未命中时，用候选收缩认出工具，配**审查过的只读默认参数**执行，
 * 不必加载模型。只对 L1 只读能力 + 表内字面量参数生效。
 */

#ifndef NL_DECISION_CMD_H
#define NL_DECISION_CMD_H

#include "nlc_api.h"
#include "capability_registry.h"

/*
 * 返回 1 = 已得出命令（*r 填好，调用方 free）；
 *      0 = 没把握/不适合兜底（不猜，让上层继续）；
 *     -1 = 参数错误。
 */
int nl_parse_by_decision(const char *input, const NlCapRegistry *reg, NlParseResult *r);

#endif /* NL_DECISION_CMD_H */
