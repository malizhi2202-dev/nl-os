# ADR-007: 模块边界与 IPC 协议

- **状态**: accepted
- **日期**: 2026-07-01
- **关联决策**: D2, D3, D5

## Context

NL-OS 需要划分模块边界：哪些功能放在一个进程？哪些独立为进程？核心约束：树莓派 4 只有 4GB RAM，模型本身占约 2GB，多进程各自加载模型会导致内存耗尽。

## Decision

**4 个独立组件，1 个共享库**：

```
libnlc.so  ← 共享库，模型 mmap 一次
    ↑                ↑
    │                │
  nlsh            nl-run
  (REPL 进程)     (.tools 执行器)
    │
    │ Unix Socket (IPC)
    ▼
  nl-perm
  (权限守护 · 独立进程)
```

**边界规则**：

| 组件 | 类型 | 职责 | 不允许 |
|---|---|---|---|
| `libnlc.so` | 共享库 | 四层管线 + 模型管理 | 不允许直接执行系统命令 |
| `nlsh` | 可执行文件 | REPL + 命令分发 + IPC 客户端 | 不允许绕过权限守护执行命令 |
| `nl-perm` | daemon 进程 | 权限判定 + 审计 | 不允许执行任何用户命令 |
| `nl-run` | 可执行文件 | .tools 解析 + 逐行执行 | 不允许修改 .tools 文件 |

**IPC 协议** (Unix Domain Socket, SOCK_STREAM)：
- 路径：`/run/nl-os/perm.sock`
- 请求：JSON (type, pid, user, cwd, nl_input, generated_cmd, paths[], command_type)
- 响应：JSON (decision: allow|confirm|deny, level: L1|L2|L3|L4, reason, audit_id)

## Consequences

- ✅ 共享库避免多进程重复加载模型（节省 ~2GB RAM）
- ✅ 独立权限进程 fail-closed 安全边界
- ✅ 每个组件职责单一，独立测试
- ❌ IPC 增加通信开销（< 1ms 可忽略）
- ❌ 权限守护成为单点——但 fail-closed 使单点变为安全特性而非可用性风险
