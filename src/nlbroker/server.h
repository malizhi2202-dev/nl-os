/*
 * server.h — broker 的 Unix socket 服务端（ADR-017 / T02）
 *
 * 职责：监听、按行分帧、把每行交给 nl_server_handle_line 处理。
 * 业务分发在 T08 的 dispatch.c 中替换。
 */
#ifndef NL_BROKER_SERVER_H
#define NL_BROKER_SERVER_H

#include <stddef.h>

typedef struct {
    const char *socket_path;
    int         dry_run;
} NlServerOpts;

/* 阻塞运行直到收到停止请求；返回 0 表示正常退出 */
int  nl_server_run(const NlServerOpts *opts);

/* 由信号处理器调用（只置标志，不做 I/O） */
void nl_server_request_stop(void);

/* 处理一行请求，写入响应（含换行）。返回写入长度，失败返回 -1。 */
int  nl_server_handle_line(const char *line, char *out, size_t out_cap);

/* 注入业务处理器（T08 接 dispatch）；未注入时退化为 T02 的骨架行为 */
typedef int (*NlLineHandler)(void *ctx, const char *line, char *out, size_t cap);
void nl_server_set_handler(NlLineHandler h, void *ctx);

#endif /* NL_BROKER_SERVER_H */
