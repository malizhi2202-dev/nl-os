# ADR-017: broker 作为独立进程（fail-closed + ticket 生命周期）

- **状态**: accepted
- **日期**: 2026-10-25
- **关联**: `ADR-007`（模块边界）、`ADR-016`（协议）
- **前置**: G1 安全审计师条件（ticket 一次性 + TTL + 绑定参数哈希）

## Context

需要决定裁决与执行的载体形态。候选：独立进程 / 库 / 并入既有 `nl-perm`。约束：端侧常驻内存 ≤80MB（ADR-015 A1）、崩溃不得影响 shell、审计必须集中。

## Decision

**独立进程 `nlbroker`**：

- 唯一入口是 Unix socket（`/run/nl-os/broker.sock`，权限 **0600**，属主为安装用户）
- 承载：能力注册表、裁决器、ticket 表、审计（JSONL，0600）、快照引用
- 由 systemd（有则）或启动脚本托管；**崩溃不影响 `nlsh`**

**fail-closed**：socket 不可达 / broker 忙 / 裁决超时 → 客户端**拒绝执行 NL 操作**；传统 shell 命令透传不受影响（与会话降级策略一致）。

**ticket 生命周期**：

- 128bit CSPRNG；一次性；TTL 60s（可配）
- 内容绑定 `capability_id` + `args_hash(SHA-256)` + `effect` + `nonce`
- `commit` 校验：存在 / 未过期 / args_hash 一致；已用 nonce 进短时黑名单
- **broker 重启 → 全部 ticket 作废**（内存表），客户端必须重新 `invoke`

**裁决不并发**：v1 单线程顺序处理，每次 `invoke` 受 `timeout_ms` 约束；并发留 v2（避免在端侧引入线程池与锁复杂度）。

## Consequences

- ✅ 崩溃隔离；注册表与审计集中；可被 systemd 重启策略保护
- ✅ 单线程让裁决顺序确定、审计可重放
- ❌ 多一个进程与一次 socket 往返（本地开销 <1ms）
- ❌ 重启即作废 ticket：长任务被中断后需要重新 invoke（安全侧可接受）
- ❌ 单线程下，慢能力会阻塞后续请求（v1 用 timeout 兜底）

## 被否掉的备选

| 备选 | 否掉原因 |
|---|---|
| 库（进程内） | 注册表与审计无法跨进程共享；`nlsh` 崩了带崩裁决状态 |
| 并入 `nl-perm` | 职责混杂：权限守护是安全边界，broker 是能力网关，混在一起后两者都无法独立降级 |
| 多线程裁决 | 端侧收益小、状态复杂度高；v1 不需要 |
