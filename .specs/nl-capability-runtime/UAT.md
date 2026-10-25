# UAT: NL-OS 能力运行时 — 用户验收

- **Change ID**: `nl-capability-runtime`
- **状态**: 🔁 **已于 2026-10-25 由 AI 逐条实跑**：首轮 10/12（场景 3、4 不通过），两项已由 change `nlsh-exec` 修复，**修复后复跑 12/12**。仍等用户按下方命令自己走一遍并签核。
- **相关 change**: `nl-capability-runtime`（本文件本体）、`nlsh-exec`（修场景 3/4 + 把"只展示"改成"真的执行"）
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

## 场景 2 · 一句话 → 正确的命令 → 真的执行

```bash
cd /tmp && rm -rf uatdemo && mkdir uatdemo && cd uatdemo
NL_ASSUME_YES=1 /path/to/nl-os/build/nlsh -c '创建一个叫 uat 的文件夹'
test -d uat && echo "OK: 目录已创建"
```
**预期**：输出 `→ mkdir -p uat` + `✅ 完成（N ms）`，最后一行 `OK: 目录已创建`。
**验收点**：① 命令是 `mkdir -p uat`（不是 `mkdir -p uat 的文件夹`）；② 目录**真的出现**。

> 想只看翻译不执行：加 `--dry-run`（打印 `[dry-run] mkdir -p uat`，不落盘）。
> 不加 `NL_ASSUME_YES` 时会被问一次，输入 `y` 才执行、`n` 或直接回车则取消（不落盘）。

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
**预期**：`无法理解这句话；本机没有该能力或未支持该说法。` + `相近能力：cli:systemctl`，**不生成任何命令**，退出码非 0。
**验收点**：宁可拒绝也不乱猜；拒绝时要说清"为什么"并给候选（本轮由 `nlsh-exec` 补齐）。

## 场景 4 · 太复杂的指令拒绝并给替代

```bash
./build/nlsh -c '把 /var/log 下所有 .log 按大小排序压缩后发到远程服务器并清理原文件'
```
**预期**：
```
解析不了（超出能力边界）：能力不存在、跨能力过多
替代步骤：cli:ls、cli:stat、cli:cat
```
+ 退出码非 0，**不得出现任何 `rm -rf` 之类的命令**。
**验收点**：复杂句拒绝，而不是被降级成一条近似的破坏性命令（首轮这条**不过**，命令是 `rm -rf 原文件`；已修）。

## 场景 5 · 看不懂就说看不懂

```bash
./build/nlsh --decide '弄一下那个'
```
**预期**：`"status":"refused"`、`"chosen":""`、`"method":"no-candidate"`，`candidates` 为空。

## 场景 6 · 决策层怎么选的（可解释）

```bash
./build/nlsh --decide '创建一个叫 uat2 的文件夹'
```
**预期**：`{"status":"ok","chosen":"cli:mkdir","method":"…","tier":…,"backend":…,"candidates":["cli:mkdir"]}`，
其中 `method` ∈ `choice-llm`（模型真的解码了）/ `rule-decisive`（规则已确定、没去问模型）/ `rule-fallback`（无模型降档）/ `rule`（`--no-model`）。
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
| ~~自然语言命令不执行~~ | ✅ 已修（change `nlsh-exec`）：确认 → 权限 → 快照 → 执行 → 审计 已在 `nlsh` 内贯通 | 场景 2/10 现在验的是"真的生效" |
| 确认策略不统一 | `nl-run` 对 L2 自动放行，`nlsh` 对 L2 问一次 | 两条路径语义不同，需在下一 change 统一 |
| IME 组合串 | 交互式中文已可用，但输入法 pending 状态的光标/退格未处理 | 需真机 + 输入法验证 |
| C 档 / ARM64 | 未实测 | ADR-015 的对应承诺仍是"未验证设计目标" |

---

## 签核

| 角色 | 结论 | 备注 |
|---|---|---|
| AI 实跑（2026-10-25） | ✅ 12/12（首轮 10/12，场景 3/4 由 `nlsh-exec` 修复后复跑通过） | 明细见 `.specs/nlsh-exec/TEST.md` |
| 用户 | ⏳ 待填 | 按上面 12 个场景跑一遍，任一不符请记下场景编号 |
