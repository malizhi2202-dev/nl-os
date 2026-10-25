/*
 * facade_mcp.h — MCP 门面（ADR-016 / T12）
 *
 * 把本地能力以 MCP 工具形式暴露给外部客户端。安全约束：
 *   1. tools/list 只含 perm ≤ L2
 *   2. 对 perm ≥ L3 的调用一律拒绝并写审计
 *   3. verdict=confirm 的调用默认拒绝（MCP 客户端没有可信确认通道）
 */
#ifndef NL_BROKER_FACADE_MCP_H
#define NL_BROKER_FACADE_MCP_H

#include "dispatch.h"

#include <stddef.h>

/* 处理一行 JSON-RPC，返回写入字节数；不启动进程，便于单测 */
int  nl_facade_handle_line(NlDispatch *d, const char *line, char *out, size_t out_cap);

/* 在 in_fd/out_fd 上跑 stdio 循环（供 nlbroker --facade-mcp 使用） */
int  nl_facade_run(NlDispatch *d, int in_fd, int out_fd);

#endif /* NL_BROKER_FACADE_MCP_H */
