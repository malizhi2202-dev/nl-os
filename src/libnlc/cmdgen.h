/*
 * cmdgen.h — 自然语言 → 命令 的专用小模型路径（ADR-025）
 *
 * 背景：原来的"模型层"是**通用指令模型 + 自由生成 JSON**，要求它同时做
 * 意图分类、实体抽取、格式遵守三件事；0.5B 的指令模型在这三件事上都不稳，
 * 实测大量"无法理解"。而专用微调模型（NL2Bash / NL2SH 系列，0.27B–1.5B）
 * 只做一件事：输入一句人话，输出一条命令 —— 提示词与停用符都是固定的。
 *
 * 这一层只负责"拿到一条候选命令"，**不做任何安全判断**：
 * 元字符、能力是否存在、权限等级、是否需要确认、审计，全部由 dispatch 的
 * 既有链路负责（与规则层生成的命令走同一条路）。
 */

#ifndef NL_CMDGEN_H
#define NL_CMDGEN_H

#include <stddef.h>

/*
 * 是否可用：配置了 NL_CMDGEN_MODEL 或标准目录里能找到 nl2bash/nl2sh 模型。
 * 返回 1/0。
 */
int nl_cmdgen_available(void);

/*
 * 生成一条命令（惰性加载模型，进程内复用）。
 *   input - 用户的一句话
 *   out   - 输出缓冲（调用方提供）
 * 返回 0 = 成功（*out 是去掉客套话/代码围栏后的单行命令）；-1 = 不可用/失败。
 *
 * 注意：输出是**模型自由生成**的文本，调用方必须先过 nl_exec_has_metachars()
 * 与"能力是否存在/权限等级"检查，再决定执行或拒绝。
 */
int nl_cmdgen_generate(const char *input, char *out, size_t cap);

/* 释放已加载的模型（进程退出前可选；常驻进程空闲时可调）。 */
void nl_cmdgen_unload(void);

/* 本层是否已加载模型（测试/自检用） */
int nl_cmdgen_loaded(void);

#endif /* NL_CMDGEN_H */
