# T08 · 统一确认门（三套策略 → 一套，ADR-019）

- **状态**: ✅ 完成
- **触发**: 用户指出"策略不同意"，要求专家团讨论后统一
- **专家团结论**（`.specs/adr/019-unified-confirm-gate.md`）：无反对票，安全审计师三条底线全采纳
  - 产品经理：反对 `y` 在一处等于同意、另一处等于取消；反对快路径加确认
  - 用户评测员：一套词表 / 一个开关 / 一个取消码；快路径加可撤销即可
  - 架构师：抽 `confirm.c`，不许再有就地 `strncmp(yn,"yes",3)`
  - 安全审计师：① `allow-l2` 只能到 L2（原来实际豁免 L3）② 取消 fail-closed ③ `.tools` 必须留审计
- **实现**:
  | 决策 | 落地 |
  |---|---|
  | P-1 触发条件 | `nl_confirm_required(needs_confirm, perm_level)`，三处共用 |
  | P-2 回答词表 | `nl_confirm_yes()`：`y/Y/yes/YES/是`；空行 = 取消 |
  | P-3 绕过开关 | `NL_ASSUME_YES`（+ `nlsh --yes` / `nl-run --yes`）；`allow-l2` 收窄为"仅 L2" |
  | P-4 退出码 | 0 成功 / 1 失败 / **3 取消**（两处一致，`nl-run` 不再返回步骤序号） |
  | P-5 审计 | `nl-run` 每步写审计；快路径破坏性命令（rm/mv/dd/…）执行前留快照，`reason=fast-path-destructive` |
  | P-6 快路径不问 | 保持 shell 语义，但有快照 + 审计（不再"无痕"） |
- **verify**: `test_confirm` 4 组 + `freeze_test`（策略头保留/防篡改）；`verify_exec.sh` AC-11a~11k **40 条断言**
- **行为变化（需告知用户）**: `nl-run` 取消退出码 1/序号 → 3；`y` 现在到处都算同意；`# @perm: allow-l2` 不再豁免 L3 删除
- **落地测试补记（同日）**: 扩 AC-11 到 40 条时撞出缺陷——`--freeze` 只写 `@format/@checksum/正文`，
  策略头被丢，固化后 `allow-l2` 静默失效（AC-11c 这类负向用例抓不到）。已修：策略头写入固化文件并纳入校验和
