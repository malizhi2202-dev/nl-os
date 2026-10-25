# SUMMARY: T01 — 编译骨架：新增 4 个模块占位 + 测试注册

- **Change ID**: `nl-capability-runtime`
- **Task ID**: T01
- **完成时间**: 2026-10-25
- **AI 角色**: Dev

---

## 做了什么

1. 新建四个模块的完整头文件接口 + 占位实现（全部返回失败值，不实现逻辑）：
   - `capability_registry`：`nl_cap_load / nl_cap_save_atomic / nl_cap_find / nl_cap_count / nl_cap_add / nl_cap_create / nl_cap_free / nl_cap_default_path / nl_cap_err_str`
   - `capability_scan`：`nl_cap_scan_dirs(dirs, n, out)`（dirs 由调用方传入，测试可传临时目录）/ `nl_cap_selftest` / `nl_cap_default_dirs`
   - `budget`：`nl_budget_check` / `nl_budget_dim_str`，含 `NlBudgetDim` 枚举与 `NL_BUDGET_MAX_CAPS=3`
   - `audit`：`nl_audit_emit(rec)` / `nl_audit_path`，`NlAuditRec` 只存 input 供内部哈希，不落明文
2. 四个 `.c` 接入 `libnlc` target
3. `tests/CMakeLists.txt` 按既有 `if(EXISTS ...)` 模式注册 5 个测试名（capability_registry / capability_scan / capability_selftest / budget / audit）
4. **模块命名决策**：结构化审计模块定为 `libnlc/audit.c`，**不叫 `audit_log.c`**——`src/nl-perm/audit_log.c` 已存在（syslog，daemon 侧），同名头文件在不同 include 路径下会造成解析歧义。DESIGN.md 与 TASK.md 已同步改名。

## 改动文件

| 文件 | 性质 |
|---|---|
| `src/libnlc/capability_registry.{c,h}` | 新增 |
| `src/libnlc/capability_scan.{c,h}` | 新增 |
| `src/libnlc/budget.{c,h}` | 新增 |
| `src/libnlc/audit.{c,h}` | 新增 |
| `CMakeLists.txt` | 修改（nlc 源列表 +4） |
| `tests/CMakeLists.txt` | 修改（+5 测试注册块） |
| `.specs/nl-capability-runtime/DESIGN.md` | 修改（audit 模块名与职责表述） |
| `.specs/nl-capability-runtime/TASK.md` | 新增（3-task 产物） |

## verify 输出

```
$ cd build && cmake .. -DCMAKE_BUILD_TYPE=Debug -DBUILD_TESTING=ON && make -j$(nproc)
[100%] Built target test_schema_validation
[100%] Built target test_nlc_api
[100%] Built target test_schema_validation

$ nm -D libnlc.so | grep -E 'nl_cap_load|nl_cap_scan_dirs|nl_audit_emit|nl_budget_check' | wc -l
4

$ ctest
100% tests passed, 0 tests failed out of 9
Total Test time (real) =   0.33 sec
```

构建期曾出现一次 `-Werror` 失败：`audit.c` 缺 `<stddef.h>` 导致 `NULL` 未声明。已补 include 并通过。**这就是骨架阶段的价值——先让编译器和 `-Werror` 把接口层问题暴露出来。**

## 边界检查

`git status --porcelain` 的 12 项全部落在 T01 的 `write_files` 内（8 个新增源文件 + 2 个 CMake + 2 个工件）。**无越界**。

## 遗留

- 5 个测试注册块此刻都不会编译出 target（测试文件尚不存在）——这是既有工程约定，T02–T06 各自补测试文件后自动生效
- `nl_cap_default_dirs` 的白名单常量已在 `capability_scan.c` 落地，但 T03 才实现遍历逻辑
