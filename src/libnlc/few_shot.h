/*
 * few_shot.h — Few-shot 示例检索
 *
 * MVP: 静态返回 3 条固定示例。
 * Phase 2: 基于相似度检索动态选择最匹配的示例。
 */

#ifndef NL_FEW_SHOT_H
#define NL_FEW_SHOT_H

/* 获取 few-shot 示例（拼接到 prompt 中） */
const char *nl_few_shot_examples(void);

/* 获取系统 prompt */
const char *nl_system_prompt(void);

#endif /* NL_FEW_SHOT_H */
