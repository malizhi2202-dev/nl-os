# SUMMARY: T12 — MCP 门面（仅暴露 perm ≤ L2）

- **Change ID**: `capability-protocol` · **Task ID**: T12 · **完成时间**: 2026-10-25

## 做了什么

1. `facade_mcp.{h,c}`：stdio JSON-RPC，实现 `initialize` / `tools/list` / `tools/call`
2. 工具名 `nl_` + 能力 ID 中非字母数字替换为 `_`（`cli:mkdir` → `nl_cli_mkdir`）
3. **`tools/list` 只返回 `perm ≤ L2`**（L3/L4 完全不出现）
4. 直接调用 L3/L4 工具 → JSON-RPC error 并说明原因
5. **`verdict=confirm` 的调用一律拒绝**（MCP 客户端没有可信确认通道），提示改用 `nlsh`
6. 执行路径复用 `nl_dispatch_line`（invoke → commit），**不复制任何策略逻辑**
7. `nlbroker --facade-mcp --registry PATH` 以 stdio 门面模式运行

## 实现中修掉的协议缺陷

门面构造 CIP 请求时用数字 `id`（MCP 习惯），但 `nl_cip_parse` 的信封 id 只接受字符串 →
所有调用返回 `E_BAD_REQ`。**修在源头**：`cip.c` 增加 `get_envelope_id()`，字符串与数字都接受。
影响面：`cip_test` 7/7 仍全绿，socket 路径不受影响。

## verify

```
facade_mcp tests
  initialize returns protocolVersion .......... PASS
  tools/list excludes L3+ ..................... PASS
  direct call to L3 tool denied ............... PASS
  L1 tool executes and returns content ........ PASS
  L2 (confirm) rejected without trusted channel PASS
5/5 passed, 0 failed

$ ctest → 100% tests passed, 0 failed out of 19
```

## 边界 / 遗留

- 无 `--facade-allow-confirm` 开关：v1 有意不提供（提供即等于放弃确认语义）
- 门面与 socket 共用同一份注册表与审计
- 测试中曾因断言失败提前 return 导致 ASAN 报内存泄漏 → 改为**先释放 dispatch 再断言**
