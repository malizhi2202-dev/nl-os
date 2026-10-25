#include <stddef.h>
#ifndef NL_DISPATCH_H
#define NL_DISPATCH_H

#include "capability_registry.h"

/* 规则未命中时是否允许走常驻解析进程（--no-model 时关掉） */
void nl_dispatch_set_remote(int on);

/* 专用 NL2Bash 小模型开关（ADR-025）：1 开 / 0 关 / -1 跟随默认（有模型就开） */
void nl_dispatch_set_cmdgen(int on);

/* --verbose：把解析过程的 debug_info 打到 stderr（排障用） */
void nl_dispatch_set_verbose(int on);
int nl_is_traditional_cmd(const char *input);
/* dry_run: 1 = 只解析/预演，不执行、不落快照（--dry-run） */
int nl_dispatch(const char *input, char *output, size_t out_sz, int dry_run);

/* 注入能力注册表（可为 NULL：退化为"不校验能力存在性"） */
void           nl_dispatch_set_registry(NlCapRegistry *r);
NlCapRegistry *nl_dispatch_registry(void);
#endif
