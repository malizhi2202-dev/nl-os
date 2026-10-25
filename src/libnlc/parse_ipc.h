/*
 * parse_ipc.h — 常驻解析守护进程（nlsh --serve）的客户端接口（ADR-021）
 *
 * 规则层未命中时，把句子交给常驻进程解析：模型只加载一次，空闲自动卸载。
 * 守护进程不可用时一律返回 -1，调用方退回进程内解析（不得因为 IPC 失败而让命令失败）。
 */

#ifndef NL_PARSE_IPC_H
#define NL_PARSE_IPC_H

#include <stddef.h>
#include "nlc_api.h"

/* 解析用的 unix socket 路径（$XDG_RUNTIME_DIR/nl-os/parse.sock，无 XDG 则 /tmp/nl-os-<uid>/） */
int nl_inferd_socket_path(char *buf, size_t sz);

/*
 * 请求常驻进程解析一句话。
 * 成功返回 0 并填好 *r（调用方用 nl_parse_result_free 释放）；
 * 不可用/失败返回 -1，err 里给出原因（可为 NULL）。
 * 首次调用会自动拉起守护进程（NL_INFERD=0 可禁用）。
 */
int nl_parse_remote(const char *input, NlParseResult *r, char *err, size_t errsz);

/* 结果 ↔ JSON（守护进程与客户端共用，便于单测） */
char *nl_parse_result_to_json(const NlParseResult *r);
int   nl_parse_result_from_json(const char *json, NlParseResult *r, char *err, size_t errsz);

#endif /* NL_PARSE_IPC_H */
