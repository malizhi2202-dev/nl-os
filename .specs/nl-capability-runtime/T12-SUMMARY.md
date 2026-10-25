# SUMMARY: T12 — Noul 单调性（模型只能更严格）

- **Change ID**: `nl-capability-runtime` · **Task ID**: T12 · **完成时间**: 2026-10-25

## 做了什么

`nl_noul_combine(rule_level, model_level)`：**取两者较严者**（`allow=0 < confirm=1 < deny=2`）。

- 模型可以把 `allow` 抬成 `confirm` / `deny`
- 模型**不能**把 `confirm`/`deny` 降回 `allow`（与 ADR-013 的单调性规则一致）
- 非法取值按 `deny` 处理（缺省从严）

## verify

```
TEST Noul: model can only tighten ... PASS
  allow+allow=allow / allow+confirm=confirm / allow+deny=deny
  confirm+allow=confirm / deny+allow=deny   ← 关键：不可放宽
```

## 边界 / 遗留

- 这是纯逻辑层；模型信号来源（Noul 推理）随 T11b 一起等模型
- `nl_noul_str()` 与 broker 的 verdict 字符串一致（allow/confirm/deny），便于后续接线
