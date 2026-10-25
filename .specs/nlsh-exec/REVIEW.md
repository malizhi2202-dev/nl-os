# REVIEW: nlsh 执行接线

- **Change ID**: `nlsh-exec`
- **日期**: 2026-10-25
- **审查范围**: `git diff`（`budget.c` / `exec.c,h` / `dispatch.c,h` / `main.c` / `line_edit.c` / `nl-run/main.c` / 测试 / 脚本 / 文档）
- **结论**: **4/4 有条件通过**，条件 2 条（见 §4），均不阻塞合入但必须在下一 change 前处理

---

## 1. 四角色投票

| 角色 | 结论 | 关键判据 |
|---|---|---|
| 产品经理 | ✅ 通过 | CHANGE 的四条"人话判据"全部可复现；UAT S3/S4 从"不过"变"过"；"说一句就生效"这条最初承诺终于成立（AC-3/AC-5/AC-6 实跑） |
| 用户评测员 | ✅ 通过（有条件） | 12 个 UAT 场景重跑 12/12；条件：交互式 UTF-8 只在管道与 TTY 各测一遍，未测 IME 组合串 |
| 架构师 | ✅ 通过（有条件） | 执行器抽出后 `nlsh`/`nl-run` 只有一份实现，防注入面统一；条件：`nl-run` 的 `allow_l2` 与 `nlsh` 的"问一次"是两套确认策略，须在下一 change 统一 |
| 安全审计师 | ✅ 通过 | 三处真问题（S4 降级、子命令注入、中文吞字）都已修并有回归；无新增 shell 解释面；审计含 hash 不含明文 |

## 2. 安全审计明细

| 编号 | 问题 | 处置 | 回归 |
|---|---|---|---|
| S-1 | `创建一个叫 x; touch hacked` 经规则层变成 `touch hacked` 并执行——**生成命令是干净的，命令级 guard 拦不住** | 在 dispatch 入口加输入级闸门（`; \| & $ `` > <` → 拒绝） | `verify_exec.sh` AC-8 端到端 + `exec_test` 9 反例 |
| S-2 | UAT S4 复合句被降级成 `rm -rf 原文件` | 预算层新增两个维度（多动作连接词 / 无对应能力操作词） | `budget_test` `test_refuse_uat_s4` + `verify_exec.sh` AC-1 |
| S-3 | 非 TTY 输入中文被静默丢弃，导致"用户说了一句、系统当没说" | 行编辑器区分 TTY/管道；TTY 接受多字节 | 实跑 REPL 管道用例 |
| S-4 | `fork` 前未 `fflush` 会让缓冲内容在父子各写一次 | 执行前 `fflush(stdout/stderr)` | 人工核对输出（无重复行） |

**残留风险（已接受，未修）**

- 快路径（用户直接敲 shell 命令）仍走 `popen`，含 `; | >` 类输入**按设计**交给 shell——这是"传统命令"语义，不是 NL 路径。文档已在 README 写明两条路径的区别。
- `nlsh` 的确认门是 `y/N` 文本，非 TTY 且无输入时取消（fail-closed），但不区分 `sudo` 会话时效。

## 3. 质量审查

| 项 | 结论 |
|---|---|
| 重复实现 | 无：执行器已抽出（`exec.c`），`nl-run` 删除本地副本 |
| 注释与命名 | `nl_dispatch(..., int dry_run)` 语义在头文件注释；`say()` 统一打印出口 |
| 错误处理 | `popen`/`fork`/`fgets`/`snprintf` 返回均检查；`nl_exec_execute` 超时 kill 并回收 |
| 测试覆盖 | 新增 6 组单测 + 36 条验收断言；反例（过拒绝）与正例（不误伤）都有 |
| 边界 | 未越界到 `rule_engine.c` / `perm.sock` / 模型；`line_edit.c` 属扩范围，已在 TASK 与 T04b 记录 |

## 4. 未关闭条件

1. **确认策略不统一**：`nl-run` 对 L2 自动放行（`allow_l2`），`nlsh` 对 L2 问一次。两条路径语义不同，用户会困惑。建议下个 change 统一到一个 `nl_confirm_policy` 里，并给 `nl-run` 加 `--yes`。
2. **IME 组合串**：交互式中文输入已可用，但输入法正在组合（pending）时的光标/退格行为未处理；需真机 + 输入法验证。

## 5. 门禁结论

四票全过（两票带条件，条件不属于本次 diff 阻断项），**允许进入 7-integration**。
条件登记进 `STATE.md` 的「阻塞与待决策」表，作为下一个 change 的首项候选。
