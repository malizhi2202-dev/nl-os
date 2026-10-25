/*
 * dispatch.h — CIP 报文分发（T08）
 *
 * 把 5 条报文接到：注册表 / 裁决器 / ticket / cli 适配器 / 审计。
 */
#ifndef NL_BROKER_DISPATCH_H
#define NL_BROKER_DISPATCH_H

#include "capability_registry.h"

#include <stddef.h>

typedef struct {
    NlCapRegistry *registry;
} NlDispatch;

int  nl_dispatch_init(NlDispatch *d, const char *registry_path);
void nl_dispatch_free(NlDispatch *d);

/* 与 NlLineHandler 同签名，便于注入 server */
int  nl_dispatch_line(void *ctx, const char *line, char *out, size_t out_cap);

#endif /* NL_BROKER_DISPATCH_H */
