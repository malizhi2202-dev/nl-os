# SUMMARY: T03 — 注册表 v2（namespace / effect / reversible，兼容读 v1）

- **Change ID**: `capability-protocol`
- **Task ID**: T03
- **完成时间**: 2026-10-25
- **AI 角色**: Dev

---

## 做了什么

1. `NL_CAP_FORMAT_VERSION` 升 **2**；`NlCapability` 增加 `namespace[16]` / `effect` / `reversible` / `effect_defaulted`
2. 新增 `NlCapEffect`（`UNSET/READ/WRITE/DELETE/PRIVILEGE/NETWORK`）与 `nl_cap_effect_str/from_str`
3. **缺省从严落进代码**：`UNSET` 与非法字符串都映射为 `DELETE`，并置 `effect_defaulted=1`
4. **写出时不写 `effect` 字段**（当值为 UNSET）→ 读回时自然变成 `delete + effect_defaulted`，信息不丢
5. **v1 兼容读**：`format_version:1` 的文件正常加载；缺 `namespace` 时从能力 ID 前缀推断（`dbus:org.x|M` → `dbus`），无前缀按 `cli`
6. 版本校验语义不变：`ver > NL_CAP_FORMAT_VERSION` 才拒绝

## 改动文件

| 文件 | 性质 |
|---|---|
| `src/libnlc/capability_registry.h` | 版本号 + 枚举 + 字段 + 2 个访问器 |
| `src/libnlc/capability_registry.c` | 解析/写出 v2 字段 + 命名空间推断 |
| `tests/unit/capability_registry_test.c` | +3 例；旧用例从"v2 被拒"改为"v3 被拒"（v2 现已受支持） |

## verify 输出

```
capability_registry tests (/tmp/nlcap-registry-vdJeXr)
  TEST roundtrip save/load ... PASS
  TEST format_version 3 rejected ... PASS
  TEST lock blocks concurrent save ... PASS
  TEST saved file mode is 0600 ... PASS
  TEST bad line skipped and counted ... PASS
  TEST v2 explicit effect survives roundtrip ... PASS
  TEST v1 file: effect=delete + namespace inferred ... PASS
  TEST invalid effect falls back to delete ... PASS
8/8 passed, 0 failed

$ ctest → 100% tests passed, 0 tests failed out of 12
```

## 边界检查

3 个改动项全在 T03 `write_files` 内。

## 遗留

- `namespace` 只做字符串推断，未做合法性校验（非法值如 `foo:` 会被原样保留）——v2 再加白名单
- 写出时不做 `namespace` 归一化校验，依赖读取侧容错
