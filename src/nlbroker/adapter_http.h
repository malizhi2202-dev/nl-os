/*
 * adapter_http.h — http: 执行面（ADR-018 / T13，beta）
 *
 * v1 只支持 http://（无 TLS）。https 返回 E_SCHEME_UNSUPPORTED，建议走 cli:curl。
 */
#ifndef NL_BROKER_ADAPTER_HTTP_H
#define NL_BROKER_ADAPTER_HTTP_H

#include <stddef.h>

typedef struct {
    char host[256];
    int  port;
    char path[512];
    int  https;
} NlHttpUrl;

/* 0 = http 可处理；1 = https（不支持）；-1 = 非法 URL */
int nl_http_parse_url(const char *url, NlHttpUrl *out);

/* 0 = 成功；-2 = 方案不支持（https）；-1 = 其它失败 */
int nl_http_request(const char *method, const char *url, const char *body,
                    int timeout_ms, char *out, size_t cap);

#endif /* NL_BROKER_ADAPTER_HTTP_H */
