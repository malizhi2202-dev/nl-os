# SUMMARY: T14 — .tools 固化与校验（SHA-256）

- **Change ID**: `nl-capability-runtime` · **Task ID**: T14 · **完成时间**: 2026-10-25

## 做了什么

1. `libnlc/freeze.{h,c}`：
   - `nl_freeze_write(path, body[], n)`：写 `# @format: 2` + `# @checksum: sha256:<64hex>` + 正文；**内容幂等**
   - `nl_freeze_verify(path)` → `OK / TAMPERED / PLAIN / IO_ERR`
   - 链式哈希：`h_i = sha256(h_{i-1} || line_i)`，头部存最终值
2. `nl-run` 接线：
   - `--freeze`：原地固化（重复执行结果一致）
   - 执行前校验：`TAMPERED` → 打印"校验和不匹配…拒绝执行任何步骤"、退出码非 0、**不执行任何步骤**
   - `PLAIN` → 警告"未固化脚本（将经模型）"
   - **固化脚本完全跳过模型加载**（AC-12 的"不经模型"是字面要求）
3. `nl-run` 新增步骤执行：按空白切 argv、**拒绝 shell 元字符**（`| ; & $ \` > < ( )` 等），不经 shell

## verify

```
freeze tests
  write then verify OK / idempotent / tampered body rejected /
  tampered checksum header rejected / plain file / missing file
6/6 passed, 0 failed

scripts/verify_segment3.sh：
  AC-11 头部含 format / sha256 / 两次固化一致 ...... PASS
  AC-12 提示使用固化脚本 / 步骤全部执行成功 ........ PASS
  AC-13 退出码非 0 / 报告校验失败 / 无副作用 ....... PASS
---- pass=10 fail=0
```

## 对 ADR-014 的修正

ADR-014 写"逐行链式 hash 可精确定位首个不一致行"——**实际做不到**：只存最终哈希，
没有逐行哈希就没有比对基准。已按实际实现收敛为"**只能判定是否被改动，无法定位到行**"，
并在 `freeze.h` 顶部显式声明该边界。定位能力若需要，得在头部存逐行哈希（体积换精度），v2 再议。

## 实现中修正的缺陷

校验和前缀长度算错（`"# @checksum: sha256:"` 是 20 字符，写成 21）→ 新写的文件自己都验不过。
用一个常量 `NL_FREEZE_SUM_PREFIX` 收口，避免再手数。
