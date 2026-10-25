# SUMMARY: T06 — 快路径 + 审计（fast_path）

- **Change ID**: `nl-capability-runtime` · **Task ID**: T06 · **完成时间**: 2026-10-25

## 做了什么

`src/nlsh/dispatch.c` 的快路径分支：

1. 传统命令判定命中 → `popen` 直接执行（**不经模型、不经规则引擎**）
2. 执行前后计时，写审计：`capability_id="fastpath"`、`verdict="allow"`、**`fast_path=true`**、`result=ok/error`
3. 模型缺失（`NL_MODEL_PATH` 指向不存在）时快路径照常工作——AC-3 的核心断言

## verify

```
capability_refuse tests
  fast path executes without model and is audited ... PASS   ← 输出含结果 + 审计含 "fast_path":true
```

## 边界 / 遗留

- 快路径不解析参数、不做能力存在性检查（有意：传统命令是用户显式输入的）
- `popen` 仍经 `/bin/sh`——**这是传统命令透传的既有行为**，不在 CIP 的 argv 约束内（CIP 管的是 NL→命令）
