# SUMMARY: T15 — 快照与撤销（AC-14 / AC-15）

- **Change ID**: `nl-capability-runtime` · **Task ID**: T15 · **完成时间**: 2026-10-25

## 做了什么

1. `libnlc/snapshot.{h,c}`（约 380 行）：
   - `nl_snapshot_take(paths[], n, out_dir)` → 快照到 `${XDG_STATE_HOME}/nl-os/snapshots/<epoch_ms>/`，`flock` 互斥
   - **敏感路径不落盘**：`~/.ssh ~/.gnupg ~/.aws ~/.kube`（含路径尾段）、`*.pem/*.key/*.p12`、`*secret*/*token*/*password*`；可用 `NL_SNAPSHOT_EXCLUDE` 追加
   - **单文件/单目录 ≤ 8MB**（`NL_SNAPSHOT_MAX_FILE` 可覆盖）；超限整次快照返回 `SKIPPED`，不留半成品
   - **总量 200MB + 保留 10 次**（`NL_SNAPSHOT_MAX_TOTAL` / `NL_SNAPSHOT_KEEP`），LRU 淘汰
   - `manifest` 记录 `F/D/S` 三类（文件/目录/敏感跳过），权限 0600
   - `nl_snapshot_undo(n)`：按 manifest 逆序恢复（文件复制 + 目录重建）
2. `nl-run`：**L2/L3 步骤执行前自动快照**（从生成命令里提取目标路径，跳过 argv0 与 `-flag`）
3. `nlsh --undo [N]`：撤销第 N 次快照

## verify

```
snapshot tests
  snapshot then undo restores content ....... PASS
  sensitive path is skipped ................. PASS
  file over per-file limit → SKIPPED ........ PASS
  keep limit enforced ....................... PASS
4/4 passed

scripts/verify_segment3.sh（AC-11~AC-16 全量）
  AC-14 删除已执行 / 撤销成功 / 目录与内容已恢复 .... PASS
  AC-15 敏感目录已删除 / 快照中无敏感内容 / 未被恢复  PASS
---- pass=16 fail=0
```

## 实现中抓到的两个真 bug

**1. 敏感目录判定漏了"路径尾段"**：清单里写的是 `/.ssh/`（带尾斜杠），
而实际传入的是相对路径 `.ssh` 或结尾路径 `/work/.ssh` → **判不出来，密钥被快照进去了**。
改为按段匹配（前缀/中段/尾段都算）。这是安全类漏判，靠 E2E 的"grep 快照目录无密钥标记"抓到。

**2. 快照记录的是相对路径** → `nlsh --undo` 在别的 cwd 下执行时恢复失败（"撤销成功"但文件没回来）。
改为快照时 `realpath` 绝对化。

## 边界 / 遗留

- 恢复时的文件权限固定 0644（未保存原 mode）→ v2 应写进 manifest
- 目录快照不保存空目录以外的元数据（owner/xattr 不保留）
- 撤销不删除快照本身（可重复撤销；`KEEP` 会自然淘汰）
