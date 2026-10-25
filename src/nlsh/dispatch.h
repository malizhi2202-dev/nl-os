#include <stddef.h>
#ifndef NL_DISPATCH_H
#define NL_DISPATCH_H

#include "capability_registry.h"
int nl_is_traditional_cmd(const char *input);
int nl_dispatch(const char *input, char *output, size_t out_sz);

/* 注入能力注册表（可为 NULL：退化为"不校验能力存在性"） */
void           nl_dispatch_set_registry(NlCapRegistry *r);
NlCapRegistry *nl_dispatch_registry(void);
#endif
