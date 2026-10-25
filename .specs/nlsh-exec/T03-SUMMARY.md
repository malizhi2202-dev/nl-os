# T03 · 抽出共享执行器，`nl-run` 改用

- **状态**: ✅ 完成
- **write_files**: `src/libnlc/exec.h`（新）、`src/libnlc/exec.c`（新）、`CMakeLists.txt`（nlc 源列表 +1）、`src/nl-run/main.c`（删除本地 `run_command`/`collect_paths`，改调共享实现，并加同款预算闸门）
- **接口**: `nl_exec_has_metachars` / `nl_exec_execute(cmd, timeout_ms)` / `nl_exec_collect_paths`
- **verify**: `ctest` 30/30（含新 `exec` 6 组）；`verify_segment3.sh` 16/16；`verify_exec.sh` 的 `nl-run` 两条不回归断言通过
- **顺带**: `nl-run` 每步解析前也跑 `nl_budget_check(..., NULL, ...)`——`.tools` 里的复合句子同样整步拒绝，不再可能生成单条破坏性命令
