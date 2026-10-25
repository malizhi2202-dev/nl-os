# SUMMARY: T02 — capability_registry：JSONL 读写 + format_version + flock + 原子写 0600

- **Change ID**: `nl-capability-runtime`
- **Task ID**: T02
- **完成时间**: 2026-10-25
- **AI 角色**: Dev

---

## 做了什么

按 ADR-012 实现能力注册表（`src/libnlc/capability_registry.c`，约 430 行）：

1. **流式读取**：`getline` 逐行解析，不整文件读入（OpenWrt 内存约束）；首行必须是元数据，`format_version > 1` → `NL_CAP_ERR_VERSION` 并返回 NULL（调用方降级为"仅规则层"）
2. **坏行不致命**：缺必填字段（id/path/perm/source/trusted）或 `perm` 非法 → 跳过该行 + `skipped++`，整体仍成功
3. **原子写**：`flock(LOCK_EX|LOCK_NB)` 轮询 20×10ms（共 200ms）→ 临时文件 `.tmp.<pid>` → 全量写 + `fsync` → `rename` → `chmod 0600` → `fsync` 父目录；超时返回 `NL_CAP_ERR_LOCK` 且**不产生半成品**
4. **深拷贝语义**：`nl_cap_add` 自己 strdup path/source/slots，调用方传入的指针无需存活（T03 依赖这点）
5. **安全默认**：`perm` 不在 1–4 → 按 4（L4）处理；`format_version` 只接受 `<= 1`
6. **`ENOENT` 不是错误**：首次运行无索引文件时返回空注册表 + `NL_CAP_OK`，避免把"还没扫过"当成故障

## 改动文件

| 文件 | 性质 |
|---|---|
| `src/libnlc/capability_registry.c` | 实现（骨架 → 430 行） |
| `src/libnlc/capability_registry.h` | **扩范围**：新增 `nl_cap_skipped()`（见下） |
| `tests/unit/capability_registry_test.c` | 新增（5 个 case） |

### 边界扩范围说明（R7.1）

T02 原 `write_files` 不含头文件，但"跳过的坏行数"必须有可观测出口（REQUIREMENT AC-2 的计数要求），否则计数是死代码。已在 `TASK.md` 的 T02 `write_files` 补入 `src/libnlc/capability_registry.h` 并注明原因，**不是悄悄扩张**。

## verify 输出

```
$ ./tests/test_capability_registry
capability_registry tests (/tmp/nlcap-registry-mfcG2g)
  TEST roundtrip save/load ... PASS
  TEST format_version 2 rejected ... PASS
  TEST lock blocks concurrent save ... PASS
  TEST saved file mode is 0600 ... PASS
  TEST bad line skipped and counted ... PASS

5/5 passed, 0 failed

$ ctest
100% tests passed, 0 tests failed out of 10
Total Test time (real) =   0.59 sec
```

## 过程中发现的两个真问题

1. **vendor 的 cJSON 不是上游完整版，是"API 兼容子集"**（`third_party/cJSON/cJSON.h` 只声明了 25 个符号）。第一次编译就炸在 `cJSON_GetObjectItemCaseSensitive` 和 `it->valuestring` 上。改用子集内 API：`cJSON_GetObjectItem` + `cJSON_GetStringValue` + `cJSON_GetNumberValue`。
   → **教训**：以后写 cJSON 相关代码前先 `grep '^cJSON' third_party/cJSON/cJSON.h`，不要按上游 API 记忆写。
2. **`inode` 用 `int` 存有溢出风险**（`NlCapability.inode` 是 `unsigned long`，但 JSON 里读回走 `double`）。当前写入时 `(int)c->inode` 会截断超大 inode。ADR-012 只要求"记录 inode 用于复校"，截断会让复校失效——**已登记为 T03 的前置修正**（写侧改为直接写 `double`）。

## 遗留

- `.lock` 文件常驻不删（零字节），属预期；`ensure_dir` 保证父目录 0700
- 写侧 `inode` 截断问题（见上）留给 T03 一并修
- `has_systemd` 用 `access("/run/systemd/system")` 判定，未做 T03 的降级路径验证
