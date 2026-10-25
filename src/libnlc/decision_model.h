/*
 * decision_model.h — 类型化决策层的解码后端（ADR-013 / ADR-015，T11b/T13）
 *
 * 模型相关的部分单独隔离，保证"无模型也能跑"：
 *   1. 按设备档位选后端（A1 只做向量粗筛，不跑生成）
 *   2. Choice 的受约束解码：GBNF 只允许候选集 + <refuse>，模型无从生成非法 id
 *   3. 解析解码结果并回落到候选下标（含 refuse / 越界 / 乱码三类拒绝）
 */
#ifndef NL_DECISION_MODEL_H
#define NL_DECISION_MODEL_H

#include "decision.h"
#include "model_loader.h"

#include <stddef.h>

/* 决策后端（按档位降档；枚举值即严格度，便于比较） */
typedef enum {
    NL_BACKEND_RULE = 0,       /* 规则层直接决定：无模型 / A1 档 / 模型加载失败 */
    NL_BACKEND_EMBEDDING = 1,  /* A1 档：候选收缩可用，最终选择仍是规则 top-1 */
    NL_BACKEND_LLM = 2         /* A2/B/C：受约束生成 Choice */
} NlBackend;

/* 设备档位（ADR-015）：1=A1 128-768MB，2=A2 1-4GB，3=B 8-16GB，4=C >=32GB */
int nl_tier_from_bytes(size_t total_ram);

/* 档位 → 后端；未知档位按最保守（规则）处理 */
NlBackend nl_backend_for_tier(int tier);

/* 读 /proc/meminfo 估算物理内存；失败返回 0（调用方按保守档处理） */
size_t nl_total_ram_bytes(void);

/* 一次 Choice 解码的结果 */
typedef enum {
    NL_CHOICE_OK = 0,        /* 命中候选且置信度达标，chosen 写入能力 id */
    NL_CHOICE_REFUSE = 1,    /* 模型选择 <refuse>：拒答，调用方走能力缺失路径 */
    NL_CHOICE_UNCERTAIN = 2, /* 选出了候选但概率低于阈值：只列 top-3，不执行（AC-8） */
    NL_CHOICE_ERR = -1       /* 模型不可用 / 解码失败 / 语法构造失败 */
} NlChoiceStatus;

/* 低置信度阈值（AC-8）：默认 0（关闭），NL_DECIDE_MIN_CONF 可覆盖；<0 表示不设阈值 */
double nl_choose_min_conf(void);

/* 单候选时是否仍要跑 decode（默认否）。
 * GBNF 只枚举候选，单候选时模型唯一能做的选择就是 <refuse>；而"该不该拒"是规则层
 * 预算/能力的职责。实测 86 次选择里 refuse 命中 0 次，为它跑一次 ~1.9s 的 prefill 不值得。
 * NL_DECIDE_SOLO_DECODE=1 可强制解码（需要模型的拒答信号时打开）。 */
int nl_choose_solo_decode(void);

/* 构造 Choice 的 user 段（纯字符串，无模型依赖，便于单测）。
 * 返回写入长度；-1 参数错误或缓冲区不足。 */
int nl_choose_user_prompt(const char *input, const NlCandidates *c,
                          char *out, size_t cap);

/* 构造 Choice 的完整 prompt（system + user，经 chat 模板渲染）。
 * 返回写入长度；-1 参数错误或缓冲区不足。 */
int nl_choose_prompt(NlModel *m, const char *input, const NlCandidates *c,
                     char *out, size_t cap);

/* 解析解码输出：允许两侧引号/空白，只接受候选集内的 id 或 <refuse>。
 * 返回命中候选下标；<refuse> 返回 -2；无法识别返回 -1。 */
int nl_choose_parse(const char *raw, const NlCandidates *c);

/* 受约束 Choice：模型 + GBNF 只能从候选集里选。
 * 返回 NlChoiceStatus；NL_CHOICE_OK 时 chosen 写入能力 id（容量不足按 ERR 处理）。 */
NlChoiceStatus nl_choose_with_model(NlModel *m, const char *input,
                                    const NlCandidates *c,
                                    char *chosen, size_t cap);

#endif /* NL_DECISION_MODEL_H */
