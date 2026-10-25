# SUMMARY: T13 — 中文评测集扩到 120 条 + 规则层基线

- **Change ID**: `nl-capability-runtime` · **Task ID**: T13 · **完成时间**: 2026-10-25

## 做了什么

1. `scripts/gen_benchmark.py`：12 个 intent × 7–9 种句式展开，与既有 33 条合并
   → `models/benchmarks/nl-commands-zh-v2.json` **120 条**（新增 87），覆盖 12 个 intent
2. `tests/unit/benchmark_test.c`：
   - 结构校验：≥100 条、每条 intent 合法
   - **规则层基线测量**：用 `nl_parse` 跑全部 120 条，统计"解析出命令"与"intent 正确"

## verify

```
benchmark tests
  benchmark file loads and has >=100 entries ...... PASS
  every entry has a valid intent ................. PASS
  rule-layer baseline recorded ................... PASS
  [88/120 解析出命令，intent 正确 82/120 = 68.3%]
```

**基线结论**：纯规则层（无模型）在 120 条中文指令上 intent 准确率 **68.3%**、覆盖 **73.3%**。
这是段 2 引入决策层前的对照基线——未来 embedding/decoder 后端必须超过它才有意义。

## 降级路径（本任务另一项）

决策层缺席时（无模型/无候选）系统行为：
- 候选为 0 → `nl_decide_grammar` 返回 -1 → 调用方走"解析不了 + 替代"（段 1 已实现并有测试）
- `nlsh --no-model` 下规则层照常工作（AC-3/段 1 已验证）

## 边界 / 遗留

- 评测集是"模板展开"，句式多样性有限；真实用户口语句式仍需采集
- 68.3% 是**规则层**基线，不含模型；是否达标需与需求线（设计目标 85%）对照——目前**未达标**，属段 2 要解决的问题
