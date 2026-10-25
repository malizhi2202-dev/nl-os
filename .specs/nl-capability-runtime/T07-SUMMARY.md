# SUMMARY: T07 — 能力缺失 / 预算拒绝接入 nlsh

- **Change ID**: `nl-capability-runtime` · **Task ID**: T07 · **完成时间**: 2026-10-25

## 做了什么

1. `nlsh` 新增 `--registry PATH` / `--scan` / `--self-test`
   - `--scan`：扫描白名单目录 → 原子写注册表 → 打印"新增 N 条，总计 M 条"
   - `--self-test`：三档统计（usable / needs-privilege / missing / total）
2. `nl_dispatch_set_registry()`：启动时加载注册表注入 dispatch
3. NL 路径新增两道关卡：
   - **预算**：`nl_budget_check` 拒绝 → 输出"解析不了（超出能力边界）：<维度>" + "替代步骤：<能力 ID>"
   - **能力存在性**：从生成命令的 argv0 推出 `cli:<cmd>`，注册表中没有 → "本机没有该能力：cli:X" + 候选（按公共前缀/子串粗排前 3），**不产生可执行命令**

## verify

```
capability_refuse tests
  missing capability refused with candidates ....... PASS
  over-budget input refused with dims + alternatives PASS
  present capability goes through NL path .......... PASS
4/4 passed
```

## 实现中修掉的集成缺陷（重要）

段 1 扫描产出的能力 ID 是裸文件名（`ls`），而消费方（nlsh / broker / CIP）期望带命名空间（`cli:ls`）。
**后果**：真实扫描出来的注册表会让所有 NL 命令都被判"本机没有该能力"。
已改 `capability_scan.c` 统一产出 `cli:<name>`（冲突时 `cli:<name>@hash8`），并同步 `capability_scan_test`。
修复后 AC-5 的替代建议从 fallback（lsof/chmod/tar）恢复为预期（cli:ls/cli:stat/cli:cat）——这也是该缺陷的可观测证据。

## 边界 / 遗留

- 能力 ID 由生成命令的 argv0 推导，尚未支持"一个 intent 对应多个候选能力"的语义（段 2 决策层）
