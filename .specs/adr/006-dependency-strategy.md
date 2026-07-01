# ADR-006: Fork+Vendor 依赖策略

- **状态**: accepted
- **日期**: 2026-07-01
- **关联决策**: D3, D4

## Context

NL-OS 的核心外部依赖是 ggml（张量运算 + GGUF 加载）。但 ggml/llama.cpp 上游更新极其频繁，几乎每天都有 breaking change。同时，NL-OS 的目标平台（ARM64 嵌入式）可能没有系统包管理器，或者包版本过旧。需要一种依赖管理策略，既保证可重复构建，又不依赖外部网络。

## Decision

**Fork + Vendor，锁定 commit**。

| 依赖 | 管理方式 | 位置 |
|---|---|---|
| ggml | vendor 核心文件 | `third_party/ggml/` |
| cJSON | vendor 单文件 | `third_party/cJSON/` |
| greatest.h | vendor header-only | `third_party/greatest.h` |

**总外部依赖 ≤ 3 个**，全部纳入仓库。

**升级策略**：
- 每个 NL-OS 大版本发布前评估一次上游更新
- 更新流程：merge 上游 → 跑完整测试套件 → 跑 benchmark 回归 → 通过后更新锁定 commit
- 不在小版本中更新依赖

## Consequences

- ✅ 可重复构建（不依赖外部网络/包管理器）
- ✅ 不受上游 breaking change 影响
- ✅ 二进制分发不依赖系统包管理器
- ❌ 需要手动跟踪上游安全补丁
- ❌ vendor 目录占用仓库空间（ggml ~2MB）
- ❌ 如果 ggml 架构重大变更，merge 成本高
