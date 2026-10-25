# SUMMARY: T01 — CIP 协议编解码

- **Change ID**: `capability-protocol`
- **Task ID**: T01
- **完成时间**: 2026-10-25
- **AI 角色**: Dev

---

## 做了什么

`src/libnlc/cip.c/h`（约 275 行）：CIP v1 的解析与渲染，**不含业务语义**。

1. 信封解析：`v` / `id` / `op`；`v != 1` → `E_VERSION`；缺 `id` / 未知 `op` / 空行 → `E_BAD_REQ`
2. 五种 op 各自字段：`discover(prefix,limit,cursor)` / `describe(ids[])` / `invoke(capability,args,dry_run)` / `commit(ticket)` / `audit(since,limit)`
3. `args` 子树原样保留为 JSON 字符串（后续裁决与 ticket 哈希要用）
4. `audit` 的 `limit` 钳制：`<=0` 或 `>500` → 50
5. 渲染：`{v,id,ok,result|error}`，带结尾换行，截断返回 -1
6. 9 个错误码 + `nl_cip_error_str()`

## 实现中澄清并更正的契约问题

**信封 `id` 与能力 ID 撞名**：DESIGN 早期草案写成 `{"op":"invoke","id":"cli:mkdir"}`——`id` 同时表示"请求 id"和"能力 id"，无法区分。已更正为：

```json
{"v":1,"id":"r2","op":"invoke","capability":"cli:mkdir","args":{...}}
```

DESIGN.md §3 与 REQUIREMENT.md 的 AC 示例已同步（R18：决定必须落盘，不能只活在对话里）。

## 改动文件

| 文件 | 性质 |
|---|---|
| `src/libnlc/cip.h` / `cip.c` | 新增 |
| `tests/unit/cip_test.c` | 新增（7 case） |
| `CMakeLists.txt` / `tests/CMakeLists.txt` | 修改（接入 nlc + 测试注册） |
| `.specs/capability-protocol/{DESIGN,REQUIREMENT}.md` | 更正 `capability` 字段 |

## verify 输出

```
$ ./tests/test_cip
  TEST parse valid discover ... PASS
  TEST unknown op rejected ... PASS
  TEST v=2 rejected ... PASS
  TEST missing id rejected ... PASS
  TEST empty line rejected ... PASS
  TEST invoke fields + render roundtrip ... PASS
  TEST commit / audit fields ... PASS
7/7 passed, 0 failed

$ ctest
100% tests passed, 0 tests failed out of 12
```

## 边界检查

8 个改动项全部在 T01 的 `write_files` 内（外加两份文档的契约更正），无越界。

## 遗留

- `describe` 的 `ids` 最多 8 个（`NL_CIP_MAX_REQIDS`），超出静默截断——DESIGN 未规定，v2 需明确是截断还是报错
- 渲染用字符串拼接（`payload_json` 原样嵌入），调用方必须保证 payload 是合法 JSON；后续 T08 由 dispatch 负责构造
