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
typedef enum { NL_SOURCE_RULE = 0, NL_SOURCE_MODEL = 1 } NlSource;

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
void nl_free(void);

/* ── 核心解析（四层管线）── */
int  nl_parse(const char *input, NlParseResult *result);
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
