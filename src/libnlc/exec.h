/*
 * exec.h — 共享命令执行器（nlsh 与 nl-run 共用）
 *
 * 生成命令一律经 fork/execvp 执行，不经 shell：`; | & $()` 等元字符直接拒绝，
 * 消除注入面（R-security）。从 src/nl-run/main.c 抽出，避免两份实现。
 */
#ifndef NL_EXEC_H
#define NL_EXEC_H

#define NL_EXEC_PATH_MAX 512

/* 1 = 命令含 shell 元字符（拒绝执行）；0 = 干净 */
int nl_exec_has_metachars(const char *cmd);

/* 执行一条已生成命令。返回退出码（0 = 成功），-1 = 拒绝/超时/无法执行 */
int nl_exec_execute(const char *cmd, int timeout_ms);

/* 从生成命令里提取目标路径（跳过 argv0 与 -flag），返回条数 */
int nl_exec_collect_paths(const char *cmd, char paths[][NL_EXEC_PATH_MAX], int max);

#endif /* NL_EXEC_H */
