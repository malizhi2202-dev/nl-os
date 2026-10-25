/*
 * nlc_api.h — libnlc 公共 API
 *
 * NL-OS 核心库的唯一外部入口。包含四层管线 + Agent 扩展点。
 */

#ifndef NL_NLC_API_H
#define NL_NLC_API_H

#include "template_fill.h"
#include "intent_extract.h"
#include "model_loader.h"
#include "path_safety.h"

/* 解析来源 */
/* 解析来源：规则 / 模型 / 决策层兜底（ADR-022） */
typedef enum { NL_SOURCE_RULE = 0, NL_SOURCE_MODEL = 1, NL_SOURCE_DECISION = 2 } NlSource;

/* 完整解析结果 */
typedef struct {
    NlIntentType  intent;
    char         *command;
    NlPermLevel   perm_level;
    NlSource      source;
    float         confidence;
    NlSlot        slots[NL_MAX_SLOTS];
    int           slot_count;
    int           needs_confirm;
    /* 1 = 命令是受限管道（只用 '|' 连接；每段已校验为只读能力）。
     * 执行时必须走 nl_exec_execute_pipeline()，不能用普通的单命令执行器。 */
    int           is_pipeline;
    char         *normalized_paths[NL_MAX_PATHS];
    int           path_count;
    char          debug_info[512];
} NlParseResult;

/* Agent 步骤 */
typedef struct { char *action; char *command; NlPermLevel perm; } NlStep;
typedef struct { NlStep *steps; int count; int capacity; } NlPlan;
typedef struct { int done; char *output; int exit_code; } NlExecResult;
typedef struct { int status; char *summary; } NlObserveResult;

/* ── 生命周期 ── */
int  nl_init(const char *model_path);
/*
 * 惰性初始化：只记录路径并检查可读性，**不加载模型**；等规则层未命中、真要用模型
 * （四层管线 Layer 3）时才 nl_model_init。
 * 动机：常见说法（`创建一个叫 X 的文件夹`）规则层就能决定，却为每次调用付 428MB gguf
 * 的加载成本（实测 +2.1s CPU / 779MB RSS）——在 2 vCPU 机器上等于打满一核。
 */
int  nl_init_lazy(const char *model_path);
/* 模型是否已真正载入内存（0 = 尚未加载或未配置） */
int  nl_model_is_loaded(void);
void nl_free(void);

/* ── 核心解析（四层管线）── */
int  nl_parse(const char *input, NlParseResult *result);

struct NlCapRegistry;   /* 前向声明（capability_registry.h 里定义） */

/* 注入本机能力注册表（借用，不接管所有权）：NL 解析的第三层要用它做
 * 候选收缩 + 受约束选择（ADR-024 续）。不设置则跳过该层。 */
void nl_set_registry(const struct NlCapRegistry *reg);
const struct NlCapRegistry *nl_get_registry(void);
/*
 * 只跑"安全网关 + 规则引擎 + 模板填充"，**不碰模型**。
 * 返回 1 = 规则层已决定（*result 已填好，调用方负责 free）；
 *      0 = 规则未命中，需要模型层（*result 为空）；
 *     -1 = 入参错误。
 * 用途：判断"这次调用到底需不需要模型"，从而决定要不要问常驻进程（ADR-021）。
 */
int  nl_parse_rules_only(const char *input, NlParseResult *result);
void nl_parse_result_free(NlParseResult *r);

/* ── Agent 扩展点（MVP 单步退化）── */
int  nl_plan(const char *goal, NlPlan *plan);
int  nl_execute_step(const NlStep *step, NlExecResult *result);
int  nl_observe(const NlExecResult *result, NlObserveResult *obs);
void nl_plan_free(NlPlan *p);
void nl_exec_result_free(NlExecResult *r);
void nl_observe_result_free(NlObserveResult *r);

/* ── 错误 ── */
const char *nl_get_last_error(void);

/* ── 调试 ── */
int  nl_rule_count(void);
const char *nl_intent_name(NlIntentType intent);

#endif /* NL_NLC_API_H */
