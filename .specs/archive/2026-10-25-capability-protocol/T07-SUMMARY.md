# SUMMARY: T07 — 审计（JSONL + 无明文 + 查询）

- **Change ID**: `capability-protocol` · **Task ID**: T07 · **完成时间**: 2026-10-25

## 做了什么

1. `libnlc/audit.c` 实现 `nl_audit_emit`：追加写、`O_APPEND` + `flock`、权限 **0600**、父目录 0700
2. 字段：`audit_id(16hex) / ts / capability_id / verdict / effect / perm_level / fast_path / probability / duration_us / result / reason / args_hash`
3. **不落明文参数**：`input` 只存 `sha256` 前 16 hex
4. `nl_audit_query(since, limit, ...)`：按 `ts >= since` 过滤，`limit` 默认 50、上限 500，输出 JSON 数组文本
5. 头部扩展 `NlAuditRec`（新增 verdict/effect/duration_us），`audit.h` 同步

## verify

```
audit tests (/tmp/nlaudit-nYNJSH)
  emit produces parsable JSONL ......... PASS
  no plaintext args in file ............ PASS   ← "rm -rf" 与 "/etc/passwd" 均不出现
  since filters out older records ...... PASS
  limit clamped + file mode 0600 ....... PASS
4/4 passed, 0 failed
```

## 边界 / 遗留

- 改动：`src/libnlc/audit.{h,c}`、`tests/unit/audit_test.c`
- **踩坑记录**：`tests/CMakeLists.txt` 里已存在同名 `test_audit` 注册块（前一个 change 的骨架），重复 `add_executable` 报错 → 删掉新加的重复块，保留已有的
- `flock` 只保证多进程不写坏行，不做防篡改（ADR-016 明确 v1 不提供）
