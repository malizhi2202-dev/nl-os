# SUMMARY: T04 — 裁决器（effect 从严 + 权限分级 + 保护路径）

- **Change ID**: `capability-protocol`
- **Task ID**: T04
- **完成时间**: 2026-10-25
- **AI 角色**: Dev

---

## 做了什么

`src/nlbroker/verdict.{h,c}`（约 150 行）：

1. 顺序：**L4/特权 → 路径校验 → 影响面/权限等级 → allow**
2. `perm >= 4` 或 `effect == privilege` → `deny` + 给替代能力（`cli:ls/stat/cat`）
3. 递归扫描 `args` 中所有字符串，绝对路径与 `~/` 开头都过 `nl_path_check`（复用 Layer 1 安全网关）
4. `effect != read` 或 `perm >= 2` → `confirm`；纯只读且 L1 → `allow`
5. **不采信 args 里的 `effect` 字段**：只置 `untrusted_effect_attempt` 标记，判定仍按注册表
6. 裁决器**不生成 plan、不执行**（plan 在 T06 适配器）

## 过程中抓出的两个真问题

**1. vendor cJSON 的对象遍历陷阱（严重）**

`cJSON_GetArraySize(obj)` 对 **object 恒返回 0**，只对 array 有效。首版用
`for (i < GetArraySize(node)) scan(GetArrayItem(node,i))` 遍历 args → **永远进不去**，
`/etc/passwd` 完全没有被拦截，测试红了。
改为手动遍历 `node->child` / `it->next`。已写入 `@.specs/LESSONS.md`。

**2. `~/` 路径绕过校验**

`nl_path_check("~/.ssh/id_rsa")` 会返回 PASS（`~` 未展开、非绝对路径 → 走不到白名单逻辑）。
裁决器里先展开 `$HOME` 再校验，堵住这个口子。

## verify 输出

```
verdict tests
  TEST L1 read -> allow ... PASS
  TEST L2 write -> confirm ... PASS
  TEST L3 delete -> confirm ... PASS
  TEST L4 -> deny + alternatives ... PASS
  TEST protected path -> deny ... PASS
  TEST model self-reported effect=read still denied ... PASS
6/6 passed, 0 failed

$ ctest → 100% tests passed, 0 tests failed out of 13
```

## 边界 / 遗留

- 改动：`src/nlbroker/verdict.{h,c}`、`tests/unit/verdict_test.c`、`tests/CMakeLists.txt`（按 TASK「边界补充」允许接入测试）
- 新增 `.specs/LESSONS.md`（4 条教训）
- `effect_defaulted` 目前只在注册表侧记录，裁决器未据此额外加严（`delete` 已是最严，无需叠加）
- 替代能力是静态表，未按"失败原因"区分——T06 生成 plan 时再细化
