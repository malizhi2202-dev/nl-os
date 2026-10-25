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
    /* 网络状态（网卡地址、域名解析）：ADR-024。
     * 独立成 intent 而不是塞进 show_process —— show_process 已经被 uptime/lsof
     * 借住过一次，再借下去"进程"这个词就彻底不指进程了；
     * 也不能用 NL_INTENT_OTHER：template_id==0 在模板层是"该 intent 的第一个模板"，
     * OTHER 里放模板会让 {"怎么"}→OTHER 的规则静默变成执行网络命令。 */
    NL_INTENT_SHOW_NETWORK,
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
 * 从一句话里抠出第一个合法域名（允许与中文粘连，例如「解析www.baidu.com这个域名」）。
 * 返回 malloc 的字符串，调用方 free；认不出返回 NULL。
 * 通用动作识别用不到它，它是"结构化参数抽取"这一半：意图可以靠语义认，
 * 参数必须靠形状校验（见 path_safety.h 的 nl_domain_check）。
 */
char *nl_extract_domain(const char *input);

/* 从一句话里抠出文件名（带扩展名，可直接粘在中文动词后）。返回 malloc，调用方 free。 */
char *nl_extract_filename(const char *input);

/* 句子里有没有约束条件（大小/时间/数量…）：字面量模板表达不了，命中也会答非所问。 */
int nl_input_has_constraint(const char *input);

/* 从一句话里抠出服务名（systemd unit 形状）。返回 malloc 字符串，调用方 free。 */
char *nl_extract_service_name(const char *input);

/*
 * 获取意图名称字符串
 */
const char *nl_intent_name(NlIntentType intent);

/*
 * 获取规则总数（用于测试覆盖率统计）
 */
int nl_rule_count(void);

#endif /* NL_RULE_ENGINE_H */
