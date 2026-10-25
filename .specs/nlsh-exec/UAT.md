# UAT: nlsh 执行接线

- **Change ID**: `nlsh-exec`
- **日期**: 2026-10-25
- **自动化验收**: `bash scripts/verify_exec.sh`（49 断言，0 失败）
- **先装一遍（不然只有 `./build/nlsh`）**:
  ```bash
  bash scripts/install_local.sh      # → ~/.local/bin/nlsh，模型自动从 ~/.local/share/nl-os 找
  nlsh --scan                        # 首次扫描能力
  ```
  装完新开一个终端（或 `bash -lc`）就能直接敲 `nlsh`。
- **人要用一遍的部分**: 下面 4 条，路径都在 `/tmp` 的临时目录里；其中场景 4 必须在真终端里敲（管道无法验 TTY 分支）

---

## 场景 1 · 说一件做不到的事（应被说"不"）

```bash
cd /tmp && rm -rf uat-exec && mkdir uat-exec && cd uat-exec
export XDG_STATE_HOME=$(mktemp -d)
/path/to/nl-os/build/nlsh --no-model --scan >/dev/null
/path/to/nl-os/build/nlsh --no-model -c '把 /var/log 下所有 .log 按大小排序压缩后发到远程服务器并清理原文件'
```
**预期**：`解析不了（超出能力边界）：…` + `替代步骤：…`，**不出现任何 rm/mv**，退出码非 0。
**人看的点**：读完这段话，你能知道机器是"不能"还是"不懂"，并且知道下一步能做什么。

## 场景 2 · 说一件做得到的事（应被做掉）

```bash
NL_ASSUME_YES=1 /path/to/nl-os/build/nlsh --no-model -c '创建一个叫 uat 的文件夹' && test -d uat && echo 真的有这个目录了
```
**预期**：`→ mkdir -p uat` + `✅ 完成（N ms）` + `真的有这个目录了`。

## 场景 3 · 反悔（应能撤销）

```bash
mkdir -p keepme && echo important > keepme/f.txt
NL_ASSUME_YES=1 /path/to/nl-os/build/nlsh --no-model -c '删除 keepme'
test -d keepme || echo 已删除
/path/to/nl-os/build/nlsh --no-model --undo
cat keepme/f.txt     # 应输出 important
```

## 场景 4 · 交互式（REPL）说中文

```bash
/path/to/nl-os/build/nlsh --no-model
# 在提示符后输入：创建一个叫 repl-dir 的文件夹
# 确认时输入 y
# 退出后：
test -d repl-dir && echo OK
```
**预期**：中文能打进命令行（不再被吞字），执行前问一次，`y` 之后目录出现。

---

## 已知未达标项

| 项 | 现状 | 影响 |
|---|---|---|
| 确认策略不统一 | `nl-run` 对 L2 自动放行，`nlsh` 问一次 | 两条路径语义不同，已登记下一 change 首项 |
| IME 组合串 | 交互式中文可输入，未测输入法 pending 光标 | 拼音输入的边缘行为待真机验证 |
| 快照对 `mkdir` | 目标不存在时 SKIP（L2 不提示） | 正常的创建操作不会积累无意义快照 |

---

## 签核

| 角色 | 结论 | 备注 |
|---|---|---|
| AI 实跑（2026-10-25） | ✅ 场景 1–3；场景 4 的 TTY 分支已用 `pty.fork()` 驱动验证（中文回显 + 退格 + 落盘），仍需你用手感确认一次 | 明细见 `TEST.md` §3 与 `T10-SUMMARY.md` |
| 用户 | ⏳ 待填 | 任一不符合请记场景编号 |
