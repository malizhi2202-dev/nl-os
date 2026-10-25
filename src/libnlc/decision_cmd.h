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
#include "model_loader.h"

/*
 * 返回 1 = 已得出命令（*r 填好，调用方 free）；
 *      0 = 没把握/不适合兜底（不猜，让上层继续）；
 *     -1 = 参数错误。
 */
int nl_parse_by_decision(const char *input, const NlCapRegistry *reg, NlParseResult *r);

/*
 * 受约束选择（ADR-024 续）：模型在**可执行的候选**里选一个（GBNF 只允许候选集）。
 *
 * 与 nl_parse_by_decision 的分工：那个是毫秒级、只按词典分数取 top-1；
 * 这个是"说法模糊、候选不止一个"时才走，代价是一次解码（常驻进程里约 2s）。
 * 候选还必须能在审查过的只读形态表里生成命令 —— 选出来却拼不出参数没有意义。
 *
 * 返回 1 = 已得出命令（*r 填好）；0 = 不适用/模型拒绝（继续走后续层）；-1 = 参数错误。
 */
int nl_parse_by_choice(struct NlModel *m, const char *input,
                       const NlCapRegistry *reg, NlParseResult *r);

#endif /* NL_DECISION_CMD_H */
