# SUMMARY: T03 — capability_scan：白名单扫描 + symlink 标记 + 权限映射

- **Change ID**: `nl-capability-runtime`
- **Task ID**: T03
- **完成时间**: 2026-10-25
- **AI 角色**: Dev

---

## 做了什么

1. **`capability_perm_map.h`（新增，50 行）**：显式权限映射表 —— L1 只读 25 项、L2 修改 16 项、L3 高危 8 项、L4 禁止 13 项；**表中没有的能力一律 L4**（`nl_cap_perm_for()`）
2. **`capability_scan.c`（骨架 → 约 190 行）**：
   - 只读遍历调用方传入的目录（CLI 传编译期白名单，测试传临时目录）
   - `lstat` 区分普通文件 / 符号链接：符号链接 `trusted=0` 且 `kind=symlink`，只列不执行
   - 普通文件登记 `realpath()` 结果；符号链接登记字面路径（保留指向关系供人查看）
   - **同 (dev, inode) 去重**（处理 `/bin → /usr/bin` 这类挂载/硬链接），避免同一能力重复登记
   - **ID 冲突**：同名不同路径 → 追加 `@<fnv8>` 后缀；仍冲突则丢弃
   - 目录不可读只跳过，不整体失败
3. **修掉 T02 遗留的 inode 截断**：注册表写侧 `(int)c->inode` → `nl_json_add_float(..., (double)c->inode)`
4. **新增 `nl_cap_at()` 只读遍历接口**（`fnv8` 冲突后缀无法用 `nl_cap_find` 反查，必须能遍历）

## 改动文件

| 文件 | 性质 |
|---|---|
| `src/libnlc/capability_perm_map.h` | 新增 |
| `src/libnlc/capability_scan.c` | 实现（骨架 → 190 行） |
| `src/libnlc/capability_registry.{c,h}` | **扩范围**：`nl_cap_at()` + inode double 修复 |
| `tests/unit/capability_scan_test.c` | 新增（3 个 case） |

### 边界扩范围说明（R7.1）

T03 原 `write_files` 不含注册表模块。两处必须动：① `nl_cap_at()`——冲突后的 `ls@xxxxxxxx` 无法用 `find` 反查，没有遍历接口这个 case 就验不了；② inode 截断是 T02-SUMMARY 里明确"留给 T03 一并修"的遗留。已在 `TASK.md` 的 T03 `write_files` 补入并注明。

## verify 输出

```
$ ./tests/test_capability_scan
capability_scan tests (/tmp/nlcap-scan-pU856J)
  TEST scan regular file / symlink / unknown ... PASS
  TEST same name different path gets @hash8 id ... PASS
  TEST unreadable dir skipped, scan still succeeds ... PASS

3/3 passed, 0 failed

$ ctest
100% tests passed, 0 tests failed out of 11
Total Test time (real) =   1.23 sec
```

## 过程中发现的三个真问题

1. **`-Werror=format-truncation` 连撞两轮**：`dirent.d_name` 最大 255 字节，而能力 ID 只有 128 字节，`snprintf("%s")` 被 GCC 判为可能截断。改用精度说明符 `%.127s` / `%.118s@%s`。这不是告警洁癖——**超长文件名确实会被静默截断成同一个 ID**，属于数据正确性问题。
2. **"不可读目录"测试一开始是假的**：第一版用 `mkdir(0700)` 构造"不可读目录"，但当前用户就是属主，0700 自己完全可读，这条 case 实际什么都没验。改成 `0000` 并在 `geteuid()==0` 时显式打印 `SKIP`——**测试通过不等于测试有效**。
3. **同 inode 去重必须用 `(dev, inode)` 组合**：跨文件系统的 inode 值会重复，只比 inode 会把不同分区里的两个不同程序误判为同一个。已按组合去重。

## 遗留

- `nl_cap_selftest()` 仍是 T04 的空实现（返回 -1）
- 扫描未处理 `/usr/libexec`（ADR-012 说"仅记录不执行"，T07 接 CLI 时再定是否纳入清单）
- 注册表未做"单次扫描上限"保护（极端情况下 `/usr/bin` 有几千项时内存增长线性，T08 集成验证时观察）
