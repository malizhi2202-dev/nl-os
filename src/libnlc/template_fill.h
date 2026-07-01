/*
 * template_fill.h — 模板填充 Layer 4
 *
 * 根据 intent + entities 填充命令模板。
 * 模型不直接生成命令——只填模板槽位，保证命令结构安全。
 */

#ifndef NL_TEMPLATE_FILL_H
#define NL_TEMPLATE_FILL_H

#include "rule_engine.h"

/* 权限级别（与 nl-perm 的 L1-L4 对齐） */
typedef enum {
    NL_PERM_L1 = 1,  /* 读安全，自动放行 */
    NL_PERM_L2 = 2,  /* 修改需确认 */
    NL_PERM_L3 = 3,  /* 删除高危 */
    NL_PERM_L4 = 4   /* 禁止执行 */
} NlPermLevel;

/* 生成的命令 */
#define NL_MAX_PATHS 16
typedef struct {
    char        *command;       /* 生成的命令字符串 */
    NlPermLevel  perm_level;    /* 权限级别 */
    int          path_count;    /* 涉及的路径数量 */
    char        *paths[NL_MAX_PATHS]; /* 涉及的路径（供安全网关二次检查） */
    int          needs_confirm; /* 是否需要用户确认 */
} NlCommand;

/*
 * 模板填充
 * 参数:
 *   intent       - 意图类型
 *   template_id  - 模板编号（来自规则引擎或模型）
 *   slots        - 槽位键值对
 *   slot_count   - 槽位数量
 *   result       - 输出命令
 * 返回: 0=成功, -1=失败（无匹配模板）
 */
int nl_template_fill(NlIntentType intent, int template_id,
                     const NlSlot *slots, int slot_count,
                     NlCommand *result);

/*
 * 释放命令
 */
void nl_command_free(NlCommand *cmd);

/*
 * 根据 intent + template_id 获取权限级别（供上层预判）
 */
NlPermLevel nl_intent_perm_level(NlIntentType intent);

#endif /* NL_TEMPLATE_FILL_H */
