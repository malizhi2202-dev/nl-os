# ADR-008: Agent 扩展点 API 设计

- **状态**: accepted
- **日期**: 2026-07-01
- **关联决策**: 无（独立架构决策）

## Context

需求文档隐含了多步交互场景（"进入 xxx 项目可以自己扫描出目录供用户选择进入"）。这不是单次"NL 输入→一条命令"的模式，而是一个 Agent Loop：Planner（规划步骤）→ Executor（执行）→ Observer（观察结果、决定是否继续）。但 MVP 阶段实现完整的 Agent Loop 风险高——在还没有验证单步 NL 理解准确率的情况下，多步规划会放大错误。

## Decision

**MVP 只做单步模式，API 预留 Agent 扩展点**。

`libnlc` 公共 API (`nlc_api.h`) 必须包含以下接口声明：

```c
// 核心解析（MVP 实现）
int nl_parse(const char *input, NlParseResult *result);

// Agent 扩展点（MVP 返回单步退化版本）
int nl_plan(const char *goal, NlPlan *plan);       // 返回步骤序列，MVP 返回 1 步
int nl_execute_step(const NlStep *step, NlExecResult *result);  // 执行单个步骤
int nl_observe(const NlExecResult *result, NlObserveResult *obs); // 观察结果
```

**MVP 行为**：
- `nl_plan()` 永远返回 1 个步骤
- `nl_observe()` 永远返回 `{status: DONE}`
- 结构体 `NlPlan` 定义为数组，但 MVP 只用 `steps[0]`

**Phase 2**：基于真实用户数据，实现真正的多步规划和观察循环。

## Consequences

- ✅ API 接口先行，二期实现不破坏兼容性
- ✅ MVP 复杂度可控（单步模式无状态管理）
- ✅ 二期的 Agent Loop 可以直接在既有 API 上构建
- ❌ 结构体设计（`NlPlan` 等）在 MVP 阶段可能不够完善，二期可能需要微调
- ❌ 接口"占坑"可能被误解为过度设计
