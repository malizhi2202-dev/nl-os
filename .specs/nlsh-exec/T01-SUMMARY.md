# T01 · 预算层新增两个拒绝来源

- **状态**: ✅ 完成
- **write_files**: `src/libnlc/budget.c`（+2 词表 / 阈值 3→1）｜`tests/unit/budget_test.c`（复用已有文件，非新建）
- **实现**: `CHAIN_WORDS` 加 `并/最后/之后`，阈值从「≥3 次」改为「≥1 次」；新增 `UNSUPPORTED_WORDS`（压缩/解压/打包/排序/替换/发送/远程/上传/下载/定时/监控/循环/递归/批量/并发/守护）→ `NL_DIM_UNKNOWN_CAP`（枚举早有、一直未使用）
- **verify**:
  ```
  ./build/tests/test_budget
    TEST 5 complex inputs refused with dims + alternatives ... PASS
    TEST UAT S4 sentence refused (multi-action + unsupported op) ... PASS
    TEST 3 simple inputs pass ... PASS
    TEST single-action sentences still pass (no over-refusal) ... PASS
    TEST empty input passes (no false refuse) ... PASS
  5/5 passed, 0 failed
  ```
- **副作用检查**: 120 条评测集 `--eval` 结果与改动前完全一致（93 ok / 27 refused / 91.4%），无误伤
- **偏差**: TASK 里写「新建 budget_test.c」，实际已有同名测试，改为扩写（更符合 ponytail「仓库里已有吗」）
