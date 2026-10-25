# SUMMARY: T10 — 候选收缩（词典/子串，不调模型）

- **Change ID**: `nl-capability-runtime` · **Task ID**: T10 · **完成时间**: 2026-10-25

## 做了什么

`libnlc/decision.{h,c}` 的 `nl_decide_candidates`：

1. 对注册表每条能力打分：**能力名出现在输入中 +10**，**中文动作词命中 +8/个**（词典 20+ 条：列出→ls、压缩→tar、删除→rm…）
2. 只保留 **top-K = 8**（插入排序，K 小、开销可忽略）
3. **`perm >= L4` 的能力永不进候选**——模型不该有机会"选择禁止执行"
4. 无命中 → `count = 0`，由调用方走拒绝路径（预算/能力缺失）

## verify

```
decision tests
  dictionary narrows to expected capability ... PASS   ← 列出→cli:ls、查看→cli:cat、文件夹→cli:mkdir、压缩→cli:tar
  top-K cap and L4 excluded ..................... PASS
  no match yields empty candidate set ........... PASS
```

## 边界 / 遗留

- 词典是"粗筛"，同义词覆盖有限；漏词会导致正确能力进不了 top-8（PM 在 G2 提的风险）——已通过评测集（T13）统计规则层命中率作为观测手段
- 中文分词未做，靠子串匹配；"文件夹" 与文件名冲突时可能误排
