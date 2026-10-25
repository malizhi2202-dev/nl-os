# ADR-018: 执行面适配策略 —— 子进程桥与零新增依赖

- **状态**: accepted
- **日期**: 2026-10-25
- **关联**: `ADR-006`（依赖上限）、`ADR-016`（协议）、`ADR-015`（端侧档位）

## Context

四个执行面（CLI / DBus / MCP / HTTP）在 C11 里都要"原生绑定"就会引入 libdbus、libcurl、TLS 栈，直接冲破 ADR-006（依赖 ≤3）与端侧体积约束。

## Decision

| 命名空间 | 实现 | 约束 |
|---|---|---|
| `cli:` | `fork` + `execvp(argv)` | 不经 shell；路径过 Layer 1 校验 |
| `dbus:` | 子进程调用 `busctl`（缺则 `gdbus`） | 方法名/参数做白名单校验，argv 传参不拼串；超时映射为进程超时 |
| `mcp:` | stdio JSON-RPC 2.0 子进程 | 工具名 → 能力 ID 映射；子进程崩溃即该命名空间不可用 |
| `http:` | 内置最小 HTTP/1.1 客户端（`http://` only） | **https 返回 `E_SCHEME_UNSUPPORTED`**，`alternatives` 建议 `cli:curl` |

**总原则**：能用"系统已有工具"就不要引库；子进程是端侧的**可接受税**（约 10–15ms），换来零新增依赖与更小的攻击面（外部工具由发行版维护）。

## Consequences

- ✅ 维持 ADR-006：`ldd nlbroker` 中不出现 libdbus/libcurl/openssl
- ✅ OpenWrt/512MB 设备可运行
- ❌ 每次 DBus/HTTP 调用多一次进程创建；高频调用场景（>100/s）不可接受
- ❌ 依赖系统存在 `busctl`/`gdbus`（缺失时该命名空间为空，需在 `discover` 中不出现而不是报错）
- ❌ 不做 TLS：https 只能经 `cli:curl` 间接完成，语义上有一次 shell 级调用（仍受 argv 约束）

## 复评条件

| 触发条件 | 动作 |
|---|---|
| DBus 调用成为热路径（实测 >15ms 影响体验） | 引入 libdbus（需修订 ADR-006） |
| 出现必须原生 https 的需求 | 引 libcurl + TLS（需修订 ADR-006） |
| MCP 子进程在高并发下不稳定 | 改为长驻连接池（v2） |
