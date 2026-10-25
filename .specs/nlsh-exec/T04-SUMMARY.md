# T04 · dispatch 接 确认→权限→快照→执行→审计

- **状态**: ✅ 完成
- **write_files**: `src/nlsh/dispatch.c`（重写执行段）、`dispatch.h`（`nl_dispatch(..., int dry_run)`）、`src/nlsh/main.c`（打印职责交给 dispatch；`-c` 也支持 `--dry-run`）
- **实现**:
  - 输入级闸门：NL 输入含 `; | & $ \` > <` 直接拒绝（见 REVIEW 的 S-1）
  - 确认门：`needs_confirm || perm_level>=2` 才问；`NL_ASSUME_YES=1` 跳过；`fgets` 失败（EOF）= 取消
  - 权限：保留 `perm.sock` 查询（不存在则跳过，不阻塞）
  - 快照：写操作执行前 `nl_snapshot_take`，`perm_level>=3` 且 SKIPPED 才提示
  - 执行：`nl_exec_execute(cmd, 5000)`，fork 前 `fflush(stdout/stderr)`（避免缓冲副本重复输出）
  - 审计：`result` ∈ ok/error/cancelled/dry-run/refused，带 `duration_us`
- **verify**: `verify_exec.sh` 中 AC-1~AC-9 共 30 条断言全绿
