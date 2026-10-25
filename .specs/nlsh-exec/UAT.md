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


## 场景 5 · 统一确认门（ADR-019，一套词表/一个开关/一个取消码）

```bash
N=/path/to/nl-os/build/nlsh          # 或已安装的 ~/.local/bin/nlsh
R=/path/to/nl-os/build/nl-run

# ① 只读不问：直接出结果，不该出现"需要确认"
$N --no-model -c '列出所有文件'

# ② 五个词都算同意（y 以前在 nl-run 里是"取消"）
for w in y Y yes YES 是; do printf '%s\n' "$w" | $N --no-model -c "创建一个叫 u_$w 的文件夹"; done

# ③ 空行 / 无输入 = 取消，退出码 3（不是 1）
printf '\n' | $N --no-model -c '创建一个叫 u_cancel 的文件夹'; echo "空行退出码=$?"
$N --no-model -c '创建一个叫 u_cancel 的文件夹' </dev/null;      echo "EOF退出码=$?"

# ④ 拒绝 ≠ 取消：能力不存在时退出码 1
$N --no-model -c '重启 nginx'; echo "拒绝退出码=$?"

# ⑤ nl-run 同一套词表；# @perm: allow-l2 固化后仍生效，但只到 L2
printf '# @perm: allow-l2\n创建一个叫 u_l2 的文件夹\n' > u.tools
$R --freeze u.tools && $R u.tools </dev/null; echo "allow-l2 创建退出码=$?"   # 0，且不提问
printf '# @perm: allow-l2\n删除 u_l2\n' > d.tools
$R --freeze d.tools && printf 'n\n' | $R d.tools; echo "allow-l2 删除退出码=$?"  # 3，目录还在
```

**预期**：①不提问；②五个目录都出现；③两个 `3` 且目录**没**出现；④`1`（与 3 可区分）；
⑤创建不问（0）、删除仍问（3，`test -d u_l2` 仍成立）。

## 场景 6 · 开销按需（模型惰性加载）

```bash
N=/path/to/nl-os/build/nlsh

# ① 规则能决定的说法：应该 0.1–0.3s、约 19MB，不该看见模型加载
/usr/bin/time -f "elapsed=%e rss=%MKB" $N --yes -c '创建一个叫 u_cost 的文件夹'

# ② 规则兜不住的说法：此时才加载模型（3–5s、约 780MB），属预期
/usr/bin/time -f "elapsed=%e rss=%MKB" $N -c '显示当前工作目录'
```
**预期**：①`rss` 在 20MB 上下（改前是 780MB、耗时 3.5s）；②`rss` 约 780MB——说明加载发生在
"真需要"的时候，而不是每次启动。

---

## 已知未达标项

| 项 | 现状 | 影响 |
|---|---|---|
| IME 组合串 | 交互式中文可输入，未测输入法 pending 光标 | 拼音输入的边缘行为待真机验证 |
| 快照对 `mkdir` | 目标不存在时 SKIP（L2 不提示） | 正常的创建操作不会积累无意义快照 |

---

## 签核

| 角色 | 结论 | 备注 |
|---|---|---|
| AI 实跑（2026-10-25） | ✅ 场景 1–3、5；场景 4 的 TTY 分支已用 `pty.fork()` 驱动验证（中文回显 + 退格 + 落盘），仍需你用手感确认一次 | 明细见 `TEST.md` §3、§4 与 `T10-SUMMARY.md` |
| 用户 | ⏳ 待填 | 任一不符合请记场景编号 |
