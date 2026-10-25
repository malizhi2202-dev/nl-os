# SUMMARY: T04 — capability_selftest（三档分类）

- **Change ID**: `nl-capability-runtime`（暂停后恢复）· **Task ID**: T04 · **完成时间**: 2026-10-25

## 做了什么

`capability_scan.c` 补 `nl_cap_selftest` 与只读探测 `probe_exec`：

1. `access(X_OK)` 不通过 / `trusted=0` → `missing`
2. `perm >= L2` → `needs-privilege`（**不做实际执行探测**，避免触发写操作或特权动作）
3. L1 且可执行 → `fork` + `execv(path, {"--version"})`，stdout/stderr 重定向 `/dev/null`，
   200ms 超时后 `SIGKILL`；退出码 127 视为 exec 失败 → `missing`
4. **全程只读**：不创建/修改任何文件；测试用 `st_mtime` + `st_size` 前后对比证明

## verify

```
capability_selftest tests
  classify usable / needs-privilege / missing ... PASS
  selftest does not modify files ................ PASS
2/2 passed, 0 failed
ctest → 22/22
```

## 边界 / 遗留

- 探测只 `--version` 一次，不解析输出（符合 ADR-012「不解析 help 语义」）
- `needs-privilege` 判定优先于执行探测 → 高权限能力的"是否真的存在"未验证（有意：不在自检阶段执行特权命令）
