/*
 * adapter_mcp.h — mcp: 执行面（ADR-018 / T11）
 *
 * 通过 stdio JSON-RPC 2.0 与 MCP server 子进程通信；能力 ID 形态 mcp:<server>/<tool>。
 */
#ifndef NL_BROKER_ADAPTER_MCP_H
#define NL_BROKER_ADAPTER_MCP_H

#include "capability_registry.h"

#include <stddef.h>

#define NL_MCP_MAX_TOOLS 64
#define NL_MCP_NAME_MAX  96

typedef struct {
    char name[NL_MCP_NAME_MAX];
    char description[256];
    char schema[1024];     /* inputSchema 的 JSON 文本 */
} NlMcpTool;

typedef struct NlMcpClient NlMcpClient;

/* 启动 server 子进程并完成 initialize；失败返回 NULL */
NlMcpClient *nl_mcp_start(const char *server_name, const char *cmd,
                          char *const *args, int nargs);
void         nl_mcp_stop(NlMcpClient *c);
const char  *nl_mcp_server_name(const NlMcpClient *c);

int          nl_mcp_list_tools(NlMcpClient *c, NlMcpTool *out, int max, int *count);
int          nl_mcp_call_tool(NlMcpClient *c, const char *tool, const char *args_json,
                              char *out, size_t cap);

/* 把工具登记为 mcp:<server>/<tool>；返回新增条数，-1 表示失败 */
int          nl_mcp_add_to_registry(NlMcpClient *c, NlCapRegistry *reg);

#endif /* NL_BROKER_ADAPTER_MCP_H */
