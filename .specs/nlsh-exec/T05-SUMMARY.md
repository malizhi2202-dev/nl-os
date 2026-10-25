# T05 · 验收脚本与单测

- **状态**: ✅ 完成
- **write_files**: `scripts/verify_exec.sh`（新，36 断言）、`tests/unit/exec_test.c`（新，6 组）、`tests/CMakeLists.txt`（注册 `exec`）、`tests/unit/budget_test.c`（扩写）
- **结果**: `verify_exec.sh` → 36 通过 / 0 失败；`ctest` → 30/30（Debug 与 Release 各一遍）
- **设计取舍**: 验收脚本一律 `mktemp -d` 工作目录 + 临时 `XDG_STATE_HOME`，不碰用户真实注册表/快照/审计；每个断言独立可读，失败时打印用例名
