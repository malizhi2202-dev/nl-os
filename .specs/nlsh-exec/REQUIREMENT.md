# REQUIREMENT: nlsh 执行接线

- **Change ID**: `nlsh-exec`
- **来源**: `.specs/nl-capability-runtime/UAT.md` 实跑结果（S3/S4 不通过）+ STATE 遗留项「自然语言命令不执行」

---

## 用户故事

| ID | 故事 |
|---|---|
| US-1 | 作为用户，我说一句做不到的事，机器要告诉我**为什么**做不到、能改成做什么，而不是拒绝或乱做 |
| US-2 | 作为用户，我说一句能做的事，机器要么直接做（只读），要么问我一次然后做（写操作），而不是只把命令打印给我看 |
| US-3 | 作为用户，我误删了东西还能撤销；我拒绝了确认就什么都不该发生 |
| US-4 | 作为运维者，我写脚本时能跳过交互（`NL_ASSUME_YES`）或只预演（`--dry-run`） |

---

## 验收标准

### AC-1 复合语义必须拒绝（安全）

```bash
./build/nlsh -c '把 /var/log 下所有 .log 按大小排序压缩后发到远程服务器并清理原文件'
```
- 输出包含"解析不了（超出能力边界）"与至少一个维度（复合语义 / 跨能力过多 / 能力不存在）
- **不得**出现任何形如 `rm -rf …` / `mv …` 的可执行命令
- 退出码非 0，不产生任何文件系统副作用

### AC-2 能力缺失要给候选

```bash
./build/nlsh -c '重启 nginx'
```
- 输出包含"本机没有该能力"或"未支持"，且列出 ≥1 个注册表中的候选能力
- 不生成任何命令，退出码非 0

### AC-3 写操作真的执行

```bash
NL_ASSUME_YES=1 ./build/nlsh -c '创建一个叫 uat-x 的文件夹'
test -d uat-x        # 通过
```
- 命令文本与执行结果都可见

### AC-4 拒绝确认 = 不执行

```bash
printf 'no\n' | ./build/nlsh -c '创建一个叫 uat-y 的文件夹'
test -d uat-y        # 不存在
```
- 退出码非 0，审计记录为 cancelled

### AC-5 写操作留快照、可撤销

```bash
printf '删除 keepme\n' > del.tools && nl-run --freeze del.tools && printf 'yes\n' | nl-run del.tools
NL_ASSUME_YES=1 ./build/nlsh -c '删除 keepme2'（先建 keepme2/f.txt）
./build/nlsh --undo
```
- 删除后目录消失；`--undo` 后文件与内容恢复

### AC-6 只读操作不问、直接执行

```bash
./build/nlsh -c '列出当前目录'
```
- 直接给出目录列表，无确认提示，退出码 0

### AC-7 `--dry-run` 零副作用

```bash
./build/nlsh --dry-run -c '创建一个叫 uat-z 的文件夹'
test -d uat-z        # 不存在
```
- 打印将要执行的命令 + `[dry-run]`，退出码 0

### AC-8 不引入 shell 解释

- 生成命令经 `execvp` 执行；命令中含 `;|&$()` 等元字符时拒绝执行并报错（复用 `nl-run` 的 guard）
- 单元测试覆盖该 guard

### AC-9 审计可追溯

- 每次执行写一条审计，含 capability_id、verdict、result（ok/error/cancelled）、duration_us、args_hash
- 不含用户输入明文

### AC-10 不回归

- `ctest` 全绿
- `scripts/verify_segment1.sh` / `verify_segment2.sh` / `verify_segment3.sh` / `verify_all.sh` 全绿
- `nl-run` 全部 12 个 UAT 场景行为不变（S2/S6 的预期随本 change 更新）

---

## 域语言（新增）

| 术语 | 定义 |
|---|---|
| **复合语义** | 一句话里要求两个及以上动作（"创建并进入"），本系统不支持链式，必须拒绝 |
| **无对应能力操作** | 本机/本层没有实现的动词（压缩、排序、发送到远程），必须拒绝而不是近似 |
| **确认门** | 执行前的人机确认；写操作默认开启，`NL_ASSUME_YES=1` 可跳过 |
| **执行器** | `nl_exec_execute()`：guard → argv 拆分 → fork/execvp → 超时回收，`nlsh` 与 `nl-run` 共用 |

---

## 成功判据（可测）

| # | 判据 | 验证 |
|---|---|---|
| 1 | AC-1/AC-2 场景输出符合预期 | `verify_exec.sh` |
| 2 | 写操作真的落盘，拒绝确认不落盘 | `verify_exec.sh` |
| 3 | 删除可撤销、私钥不进快照 | `verify_exec.sh` + 既有快照测试 |
| 4 | 无 shell 注入面 | 单测 `exec_test` |
| 5 | 全量回归零失败 | `verify_all.sh` |
