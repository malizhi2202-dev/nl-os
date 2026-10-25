# SUMMARY: T08 — W2 端到端（discover/describe/invoke/commit/audit）

- **Change ID**: `capability-protocol` · **Task ID**: T08 · **完成时间**: 2026-10-25

## 做了什么

1. **`dispatch.{h,c}`**：五条报文全部接线
   - `discover`：prefix 过滤 + limit + cursor（游标=偏移量）
   - `describe`：slots / effect / reversible / `effect_defaulted`
   - `invoke`：裁决 → `deny`（无 plan + alternatives）或生成 plan + 签发 ticket（`plan.executed=false`）
   - `commit`：校验 ticket（存在/未过期/args 哈希）→ 执行 → 审计
   - `audit`：`nl_audit_query` 结果透传
2. **`server` 支持 handler 注入**：`nl_server_set_handler()`，T02 的骨架行为保留为 fallback
3. **`main` 增加 `--registry PATH`**，启动时加载注册表并注入 dispatch
4. **`scripts/verify_w2.sh`**：16 条断言覆盖 AC-1/2/3/4/9

## 实现中发现并修正的**协议缺陷**（重要）

`commit` 只带 `ticket` 时，**无法检测"参数被篡改"**——因为校验 args 哈希需要参数，而协议里没有。
修正为：**`commit` 必须回带 `capability` + `args`**，并同步改 `cip.c` 解析、`DESIGN.md` §3.2、
`REQUIREMENT.md` AC-4、`cip_test`（新增"commit 缺 capability 必须被拒"用例）。

这是 T08 存在的意义：**单任务测试过、拼起来才发现契约不闭合**。

## 另一个修正：审计不能存人类可读 reason

AC-9 要求审计无明文参数。首版把 `verdict.reason`（含 `目标路径 '/etc/passwd' 在保护名单中`）
直接写进审计 → **路径泄露**，被 `verify_w2.sh` 的断言抓到。
改为存**原因码**（`protected-path` / `l4-denied` / `needs-confirm` / `read-only` / `policy-denied`），
面向用户的完整 reason 仍在响应里返回。

## verify 输出

```
PASS AC-1 discover returns limit items
PASS AC-1 cursor present
PASS AC-1 prefix filter
PASS AC-2 effect defaults to delete
PASS AC-3 protected path denied
PASS AC-3 no plan on deny
PASS AC-4 confirm with ticket
PASS AC-4 plan marked not executed
PASS AC-4 no side effect before commit
PASS AC-4 commit succeeds
PASS AC-4 side effect after commit
PASS AC-4 replay rejected
PASS AC-4 tampered args rejected
PASS AC-9 audit has records
PASS AC-9 audit fields present
PASS AC-9 audit has no plaintext args
---- verify_w2 rc=0

$ ctest → 100% tests passed, 0 tests failed out of 16
```

其中 `AC-4 no side effect before commit` 与 `AC-3 no plan on deny` 一起构成对 AC-3 的可移植双断言
（不依赖 `strace`，符合需求门第三轮结论）。

## 边界 / 遗留

- 改动含 `server.{h,c}`、`main.c`、`cip.c`、`tests/unit/cip_test.c`、`CMakeLists.txt`、`scripts/verify_w2.sh`（按 TASK「边界补充」及契约修正扩范围）
- `invoke` 对 `allow` 也签发 ticket（保持单一代码路径）；客户端需再 `commit` —— 多一次往返，v2 可优化
- `describe` 的 `slots` 目前来自注册表，尚未从 `busctl introspect` / MCP schema 生成（W3/W4 补）
- `audit` 响应受 4KB payload 限制，约 15 条；更多用 `since` 翻页
