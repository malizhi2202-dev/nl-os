/*
 * rule_engine.h — 规则引擎 Layer 2
 *
 * 关键词 + 模板匹配。O(1) hash 查找，< 1ms 延迟。
 * 覆盖 80% 高频 NL 命令，未命中则进入 Layer 3 模型推理。
 */

#ifndef NL_RULE_ENGINE_H
#define NL_RULE_ENGINE_H

/* 意图类型（与模型输出的 intent enum 保持一致） */
typedef enum {
    NL_INTENT_CREATE_FILE = 0,
    NL_INTENT_CREATE_DIR,
    NL_INTENT_DELETE,
    NL_INTENT_MOVE,
    NL_INTENT_COPY,
    NL_INTENT_LIST_DIR,
    NL_INTENT_VIEW_FILE,
    NL_INTENT_FIND_FILES,
    NL_INTENT_CHANGE_DIR,
    NL_INTENT_SHOW_PROCESS,
    NL_INTENT_SEARCH_CONTENT,
    NL_INTENT_RUN_CMD,
    NL_INTENT_OTHER
} NlIntentType;

/* 槽位值 */
#define NL_MAX_SLOTS 8
typedef struct {
    char *name;   /* 槽位名（如 "target", "location", "condition"） */
    char *value;  /* 提取的值 */
} NlSlot;

/* 规则匹配结果 */
typedef struct {
    int           matched;        /* 1=命中, 0=未命中 */
    int           rule_id;        /* 命中的规则 ID */
    NlIntentType  intent;         /* 意图类型 */
    int           template_id;    /* 对应的命令模板 ID */
    NlSlot        slots[NL_MAX_SLOTS]; /* 提取的槽位值 */
    int           slot_count;
    float         confidence;     /* 规则引擎命中固定 1.0 */
} NlRuleResult;

/*
 * 规则匹配
 * 参数:
 *   input  - 用户输入的 NL 文本
 *   result - 输出匹配结果
 * 返回: 0=命中, -1=未命中
 */
int nl_rule_match(const char *input, NlRuleResult *result);

/*
 * 释放匹配结果中的槽位内存
 */
void nl_rule_result_free(NlRuleResult *result);

/*
 * 获取意图名称字符串
 */
const char *nl_intent_name(NlIntentType intent);

/*
 * 获取规则总数（用于测试覆盖率统计）
 */
int nl_rule_count(void);

#endif /* NL_RULE_ENGINE_H */
