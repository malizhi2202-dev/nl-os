# SUMMARY: T05 — budget（规则版复杂度预算）

- **Change ID**: `nl-capability-runtime` · **Task ID**: T05 · **完成时间**: 2026-10-25

## 做了什么

`budget.c` 从 stub 改为真实实现（**不调模型**，符合 ADR-013 D14）：

1. **复合语义**（19 个关键词）：`每天/定时/循环/每隔/递归/条件/如果/否则/判断/并发/批量/监控/守护/自动/上传/下载/备份/每小时/每晚`
2. **跨能力过多**：链式词（`然后/接着/并且/同时`）出现 ≥ 3 次
3. **需要特权**：`sudo/root/提权/改密码/安装/卸载/重启服务/内核模块`
4. 命中任一 → `NL_BUDGET_REFUSE` + 维度列表
5. **替代能力只从注册表挑**（优先 `cli:ls/stat/cat/echo`，否则取前 3 个 `perm ≤ 2`）→ 保证"替代建议一定可执行"

## verify

```
budget tests
  5 complex inputs refused with dims + alternatives ... PASS
  3 simple inputs pass .............................. PASS
  empty input passes (no false refuse) .............. PASS
3/3 passed, 0 failed
```

## 实现中修正

首版词表漏了条件语义（`如果…就…`），"如果磁盘满了就删除旧日志"被误判 pass。
补 `如果/否则/判断` 后通过。**这类漏词会直接导致"答应了做不到的指令"**，属于安全向缺口。

## 边界 / 遗留

- 关键词判定会有误杀（如"每日报表"含"日"但不含"每天"）→ v2 应结合能力 ID 命中率
- 未做"目标能力不存在"维度（需要 NL→能力的候选提取，属段 2 的决策层）
