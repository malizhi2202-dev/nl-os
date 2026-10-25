# ADR-016: CIP v1 —— 把裁决放进协议（统一能力调用协议）

- **状态**: accepted
- **日期**: 2026-10-25
- **关联**: `ADR-007`（模块边界）、`ADR-012`（注册表）、`ADR-013`（决策层）、G1 架构师条件（威胁模型必须显式）

## Context

调研确认（`@.specs/discovery/claim-verification.md`）：「统一调用协议」这件事 MCP 已经做了，但 MCP 有三个不适用于本项目的假设——调用方可信、有运行时、**没有"危险"这个概念**（所有工具调用等价）。

同时，agent 防火墙赛道（Belay / Pipelock / Leash 等）都在**命令字符串**层面做匹配，Belay 自己的 README 承认可被 `eval $(echo ...|base64 -d)` 绕过。

## Decision

**协议：CIP v1**，JSON Lines over Unix socket，五条报文 `discover / describe / invoke / commit / audit`。

**三条不可协商**：

1. **能力 ID 带命名空间**：`cli:` `dbus:` `mcp:` `http:`。换执行面只换前缀，协议不变。
2. **两阶段**：`invoke` 只裁决并返回 `plan`（不执行）；`commit` 凭 ticket 执行。确认是协议的一部分。
3. **`plan` 是 argv 数组，不是命令字符串**：从根上消除 shell 注入面。

**元数据**：`effect ∈ {read, write, delete, privilege, network}`；**未标注一律 `delete`**；`effect` 只能由注册表给出。

**威胁模型（硬件级前提，必须写死）**：

> **模型 / agent 不可信；系统用户（属主、root）可信。**

由此：注册表文件（0600 + 属主）可信；协议中传入的一切字段（含 `effect`）不可信。若未来要支持不可信系统用户，本 ADR 必须重写。

## Consequences

- ✅ 裁决语义成为协议的一等公民，客户端无法跳过确认
- ✅ argv 数组使注入面在协议层消失（相对字符串防火墙是结构性优势）
- ✅ 与 MCP 是"包装"关系，不竞争
- ❌ 不支持管道/重定向；复杂操作需多能力组合（有意的表达力上限）
- ❌ 客户端多一次往返（invoke → commit）
- ❌ 新增能力必须显式标注 `effect`，否则默认按最严处理，用户会感觉"什么都不能做"

## 被否掉的备选

| 备选 | 否掉原因 |
|---|---|
| 直接用 MCP | 无裁决语义、假设调用方可信 |
| HTTP/REST 作为本地传输 | 端侧不必要地引入端口与 TLS 问题 |
| CBOR/MessagePack | 无可读性，调试成本高，收益是体积（200KB 级别不值得） |
| 单阶段 invoke（直接执行） | 确认无法进入协议，客户端可绕过 |
| 命令字符串 + 转义 | 转义是无穷漏洞库；argv 数组是结构性解法 |
| 由调用方声明 effect | 模型可自称只读，安全模型崩塌 |
