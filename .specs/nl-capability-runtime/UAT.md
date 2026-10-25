# UAT: NL-OS 能力运行时 — 用户验收

- **Change ID**: `nl-capability-runtime`
- **状态**: ⏳ **待用户执行验收**（自动化验收见 `TEST.md`，本文件只列"人要用一遍"的场景）
- **前提**:
  ```bash
  cd /path/to/nl-os
  cmake -S . -B build && cmake --build build -j$(nproc)

  # 可选：让决策层跑起来（否则自动走规则层，其余场景照样能验）
  bash models/download_model.sh small
  export NL_MODEL_PATH=$HOME/.local/share/nl-os/qwen2.5-0.5b-q4_0.gguf

  # 用临时状态目录，不动你自己的注册表/快照/审计
  export XDG_STATE_HOME=$(mktemp -d)
  ./build/nlsh --no-model --scan          # 首次能力发现
  ```

> 每个场景都是**可直接复制执行**的命令 + 预期结果。任一条不符合就记为验收不通过。

---

## 场景 1 · 机器能干什么（能力自发现）

```bash
./build/nlsh --no-model --self-test
```
**预期**：输出 `usable=… needs-privilege=… missing=… total=…`，其中 `total` 与扫描条数一致（本机 1890）。
**验收点**：不需要读文档就知道本机哪些能力现在能用、哪些要授权。

## 场景 2 · 一句话 → 正确的命令（当前只展示不执行）

```bash
/path/to/nl-os/build/nlsh -c '创建一个叫 uat 的文件夹'
```
**预期**：输出 `[create_dir] mkdir -p uat` + `需确认: 是`。
**验收点**：命令本身必须是 `mkdir -p uat`（不是 `mkdir -p uat 的文件夹`、不是 `mkdir -p 一个 uat`）。

> ⚠️ **当前已知缺口**：`nlsh` 只展示解析结果，**不会执行**自然语言解析出的写操作
> （执行需要走 `.tools`/`nl-run`，见场景 8）。这一条是"翻译对不对"，不是"跑没跑"。

## 场景 2b · 真正执行（经 `.tools`）

```bash
mkdir -p /tmp/uat-demo && cd /tmp/uat-demo
printf '创建一个叫 uat 的文件夹\n' > mk.tools
/path/to/nl-os/build/nl-run --freeze mk.tools
printf 'yes\n' | /path/to/nl-os/build/nl-run mk.tools
test -d uat && echo "OK: 目录已创建"
```
**预期**：`OK: 目录已创建`。

## 场景 3 · 拒绝时不猜（能力缺失）

```bash
./build/nlsh -c '重启 nginx'
```
**预期**：说明本机没有该能力 + 给出 3 个语义相近候选，**不生成任何命令**，退出码非 0。
**验收点**：宁可拒绝也不乱猜。

## 场景 4 · 太复杂的指令拒绝并给替代

```bash
./build/nlsh -c '把 /var/log 下所有 .log 按大小排序压缩后发到远程服务器并清理原文件'
```
**预期**：`refused` + 说明超出哪个维度（跨能力数/条件循环/网络/特权）+ 至少一条**可直接复制执行**的替代步骤。

## 场景 5 · 看不懂就说看不懂

```bash
./build/nlsh --decide '弄一下那个'
```
**预期**：`"status":"refused"`、`"chosen":""`、`"method":"no-candidate"`，`candidates` 为空。

## 场景 6 · 决策层怎么选的（可解释）

```bash
./build/nlsh --decide '创建一个叫 uat2 的文件夹'
```
**预期**：`{"status":"ok","chosen":"cli:mkdir","method":"choice-llm"（有模型）或 "rule-fallback"（无模型）,"tier":…,"backend":…,"candidates":["cli:mkdir"]}`
**验收点**：每条决策都能解释"谁选的、依据什么候选"。

## 场景 7 · 决策层缺席不影响干活（降级）

```bash
NL_MODEL_PATH=/nonexistent ./build/nlsh --eval models/benchmarks/nl-commands-zh-v2.json | tail -1
```
**预期**：退出码 0，最后一行是 `summary`，`backend":"rules"`，`schema_violations:0`，不崩不卡。

## 场景 8 · 固化一次，之后离线重放

```bash
cd /tmp/uat-demo
printf '列出当前目录\ncat README.md\n' > demo.tools
echo hello-uat > README.md
/path/to/nl-os/build/nl-run --freeze demo.tools
head -2 demo.tools                       # 应看到 # @format: 2 / # @checksum: sha256:…
NL_MODEL_PATH=/nonexistent /path/to/nl-os/build/nl-run demo.tools
```
**预期**：无模型也能跑完全部步骤，提示"使用固化脚本"。

## 场景 9 · 篡改固化文件会被挡住

```bash
cd /tmp/uat-demo
sed -i 's/列出当前目录/列出根目录/' demo.tools
NL_MODEL_PATH=/nonexistent /path/to/nl-os/build/nl-run demo.tools; echo "exit=$?"
```
**预期**：`exit` 非 0，报校验和不匹配并指出第几步被改，**没有任何步骤被执行**。

## 场景 10 · 删除能撤销

```bash
cd /tmp/uat-demo
mkdir -p keepme && echo important > keepme/f.txt
printf '删除 keepme\n' > del.tools
/path/to/nl-os/build/nl-run --freeze del.tools
printf 'yes\n' | /path/to/nl-os/build/nl-run del.tools      # 删除
test -d keepme || echo "已删除"
/path/to/nl-os/build/nlsh --undo
test -f keepme/f.txt && cat keepme/f.txt                     # 应输出 important
```
**预期**：撤销后目录与文件内容都回来。

## 场景 11 · 私钥不会被快照

```bash
cd /tmp/uat-demo
mkdir -p .ssh && echo PRIVATE-KEY-MARKER > .ssh/id_rsa
printf '删除 .ssh\n' > keys.tools
/path/to/nl-os/build/nl-run --freeze keys.tools
printf 'yes\n' | /path/to/nl-os/build/nl-run keys.tools
/path/to/nl-os/build/nlsh --undo
test -f .ssh/id_rsa && echo "BAD: 私钥被恢复了" || echo "OK: 私钥没有被快照/恢复"
grep -r PRIVATE-KEY-MARKER "$XDG_STATE_HOME/nl-os/snapshots" 2>/dev/null && echo "BAD: 快照里有私钥内容" || echo "OK: 快照无敏感内容"
```
**预期**：两行都 `OK:`。

## 场景 12 · 事后能查账

```bash
./build/nlsh --audit --since 0 --limit 5
```
**预期**：JSON 数组，每条含时间、能力 ID、是否快路径、决策方式（`method`）、结果；**不含你输入的明文**（只有 hash）。

---

## 已知未达标项（验收时请一并确认是否接受）

| 项 | 现状 | 影响 |
|---|---|---|
| 快路径开销 | 实测 ≈30ms（目标 ≤5ms，Release） | 输一条命令比直接敲慢约 30ms；主因是每次进程都全量加载注册表 |
| B 档决策延迟 | P95 2788ms（目标 1500ms，本机 2 vCPU） | 带模型时每次判断要等 1.5–3 秒；需换硬件复测或做 prefix cache |
| 候选召回 | 已从 33% 漏救降到 **14%**（规则层并入候选收缩后） | 仍有 14 条硬句式（把字句、双槽位）识别不了 |
| 自然语言命令不执行 | `nlsh` 只展示命令 + 确认标志，真正执行要走 `.tools`/`nl-run` | 用户"说一句就生效"的期待未满足；这是下一个 change 的首项 |
| C 档 / ARM64 | 未实测 | ADR-015 的对应承诺仍是"未验证设计目标" |

---

## 签核

| 角色 | 结论 | 备注 |
|---|---|---|
| 用户 | ⏳ 待填 | 按上面 12 个场景跑一遍，任一不符请记下场景编号 |
