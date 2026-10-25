# SUMMARY: T11 — mcp 客户端适配器（stdio JSON-RPC）

- **Change ID**: `capability-protocol` · **Task ID**: T11 · **完成时间**: 2026-10-25

## 做了什么

1. `adapter_mcp.{h,c}`（约 300 行）：启动 MCP server 子进程（双管道 + fork/exec），
   完成 `initialize` → `notifications/initialized` 握手
2. `tools/list` → 解析 `name/description/inputSchema`（schema 存 JSON 文本）
3. `tools/call` → 优先取 `content[0].text`，否则整体序列化
4. `nl_mcp_add_to_registry()`：登记为 `mcp:<server>/<tool>`，`effect=write`（工具可能改状态）、
   `effect_defaulted=1`、`perm=L2`
5. dispatch 增加 mcp 分支：
   - `invoke`：plan 形态为 `{"mcp":{"server","tool"},"executed":false}`（非 argv，属协议允许的命名空间差异）
   - `commit`：经客户端调用并回传输出
6. `nlbroker --mcp-name NAME --mcp-cmd PATH [--mcp-arg ARG]...` 接入

## verify

```
adapter_mcp tests
  tools/list returns 2 tools ........ PASS
  tools/call echoes args ............ PASS
  register mcp tools into registry .. PASS
3/3 passed, 0 failed        （fake server: tests/fixtures/fake_mcp_server.py）
```

端到端（broker 进程内）：
```
mcp caps: 2 | ids: ['mcp:fake/echo_low', 'mcp:fake/danger_high']
invoke: confirm | plan.mcp: {'server': 'fake', 'tool': 'echo_low'}
commit: ok | output: called echo_low with {"msg": "hi"}
```

## 实现中发现的问题

`mcp:<server>/<tool>` 拼接时 server 与 tool 各最长 95 字符，128 字节 ID 会溢出 →
改为**显式长度检查、超长跳过**（不用 `snprintf` 赌截断，同 LESSONS 里那条）。

## 边界 / 遗留

- v1 只接**一个** MCP server；多 server 需要配置结构与 ID 冲突策略（v2）
- 子进程崩溃后该命名空间不可用，但 broker 不崩（`rpc_call` 返回 -1）
- 无 `tools/call` 的权限差异：门面侧（T12）才做 L2 上限，客户端侧按注册表 effect 裁决
