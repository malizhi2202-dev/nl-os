# ADR-005: 内核集成渐进路线

- **状态**: accepted
- **日期**: 2026-07-01
- **关联决策**: 无（独立架构决策）

## Context

需求文档要求"嵌入到 Linux 内核作为 Linux 的一个组件"。但直接编写内核模块（.ko）有严重风险：内核态代码崩溃 = 系统 panic；内核模块需要随内核版本更新；调试困难。同时，需求也强调"不能让用户误操作"——权限系统需要在正确的位置拦截。

## Decision

**三阶段渐进路线**：

**Phase 1 (MVP): 用户态权限守护**
- `nl-perm` 独立进程（systemd 管理）
- seccomp 约束 NL-Shell 子进程
- PAM 集成（可选）
- 零内核代码，零内核依赖

**Phase 2: eBPF 内核拦截**
- LSM hook (Landlock/BPF-LSM)
- 系统调用过滤（`open`/`unlink`/`rmdir` 等拦截）
- 无内核模块风险（eBPF verifier 保证安全性）
- 主线内核支持（≥ 5.10）

**Phase 3: 评估 .ko 必要性**
- 仅在 eBPF 无法覆盖的场景考虑（如需要修改内核调度策略）
- 需要严格的稳定性测试 + 多内核版本兼容
- 可能永远不需要（eBPF 已经能覆盖所有权限拦截需求）

## Consequences

- ✅ Phase 1 零内核风险，MVP 快速迭代
- ✅ eBPF 是 Linux 主线支持的"安全内核编程"，比 .ko 安全得多
- ✅ 如果 eBPF 够用，可能永远不需要写 .ko
- ❌ Phase 1 的权限只在 NL-Shell 内生效（用户绕过 NL-Shell 直接用 bash 不受限制）
- ❌ eBPF 对老内核（< 5.10）不可用
