# DESIGN: nlsh 执行接线

- **Change ID**: `nlsh-exec`
- **输入**: `REQUIREMENT.md`（AC-1~AC-10）、`nl-capability-runtime/DESIGN.md`（§2 管线）

---

## 1. 现状管线（要补的是后半段）

```
输入 ──► 快路径(传统命令, popen) ────────────────────────────────► 输出
  └──► 复杂度预算(词表) ──► 规则/决策 ──► 能力存在性 ──► [展示命令+需确认] ✗ 到此为止
                                                  └──► 权限(perm.sock, 仅查询)
```

缺的是：确认门、快照、执行、审计结果。`nl-run` 已经有这一整套（`src/nl-run/main.c:105-131`），但没共享。

## 2. 目标管线

```
输入 ──► 快路径（不变）
  └──► 复杂度预算（新增两维度）──► 拒绝：维度 + 替代步骤
        └──► 规则/决策 ──► 解析失败 ──► 拒绝：原因 + 候选能力
              └──► 能力存在性 ──► 缺失：拒绝 + 候选
                    └──► 确认门（仅写/高危）── 否 ──► cancelled（不执行）
                          └──► 权限裁决（perm.sock，保留原逻辑）
                                └──► 快照（写操作）
                                      └──► 执行器（execvp，无 shell）
                                            └──► 审计（result/duration）
```

## 3. 决策

### D1 · 执行器从 `nl-run` 抽出，双处复用

`src/libnlc/exec.c`（新），接口：

```c
int  nl_exec_has_metachars(const char *cmd);                                  /* 1 = 含 shell 元字符 */
int  nl_exec_execute(const char *cmd, int timeout_ms);                        /* -1 = 拒绝/失败 */
int  nl_exec_collect_paths(const char *cmd, char paths[][512], int max);      /* 槽位候选路径 */
```

理由（ponytail 阶梯 2）：仓库里已有能工作的实现，复制第二份等于两个 bug 源。
`nl-run/main.c` 改为调用它，行为不变（含 5s 超时、元字符拒绝、execvp 无 shell）。

不采纳：`system()` / `popen()` —— 引入 shell 解释，生成命令一旦含 `;` 就是注入面（AC-8）。

### D2 · 确认门策略

| 条件 | 行为 |
|---|---|
| `needs_confirm == 0 && perm_level < 2` | 不问，直接执行（只读） |
| `needs_confirm == 1 \|\| perm_level >= 2` | 问 `执行? [y/N]`，读一行，非 `y/yes/是` 即取消 |
| `NL_ASSUME_YES=1` | 跳过问询（脚本/验收用） |
| `--dry-run` | 打印命令，不执行，不落快照 |
| stdin 非 TTY 且无行可读（EOF） | 视为取消（fail-closed） |

`nl-run` 现有语义是 `allow_l2` 自动放行 L2；`nlsh` 交互场景沿用"问一次"更保守，但对 `nl-run` 内部循环不改。

### D3 · 快照时机

`needs_confirm || perm_level >= 2` 且能提取到路径时，**执行前**调用 `nl_snapshot_take()`；
`NL_SNAP_SKIPPED` 时打印原因继续（与 `nl-run` 一致，敏感路径由 `path_safety` 负责不落明文）。

### D4 · fork 前 `fflush(stdout)`

子进程继承 stdout：`printf` 的缓冲会被父子各写一次，导致输出重复。执行前 `fflush(stdout); fflush(stderr);`（这是唯一容易踩的坑，写进测试）。

### D5 · 复杂度预算新增两个拒绝来源

`budget.c` 现在只查三张词表，S4 一个都不命中。新增：

1. **多动作连接词**：`并`（后接动词时）、`然后`、`接着`、`最后`、`之后`、`同时` → `NL_DIM_TOO_MANY_CAPS`（原来要求 ≥3 次，改为 ≥1；`并且/同时` 仍是明显多动作）
2. **无对应能力的操作词**：`压缩`、`解压`、`打包`、`排序`、`替换`、`发送`、`远程`、`上传`、`下载`、`定时`、`监控`、`循环`、`递归`、`批量`、`并发`、`守护` → `NL_DIM_UNKNOWN_CAP`（枚举里早就有，一直没被设置）

误伤评估：`清理 /tmp 目录`、`创建一个叫 uat 的文件夹`、`列出当前目录`、`删除 keepme` 均不含上述词，不受影响；`备份 config.json` 本来就因 `备份`∈COMPOSITE 被拒（既有行为不变）。

### D6 · 解析失败时的候选提示（AC-2）

`dispatch.c` 加静态小表 `{关键词, argv0}`：`重启→systemctl`、`安装/卸载→apt`、`端口→ss`、`进程→ps`、`内存→free`、`磁盘→df`、`网络/IP→ip`、`服务→systemctl`、`定时任务→crontab`、`防火墙→ufw`。
命中且在注册表存在才输出 `本机没有该能力：cli:systemctl（需特权）` + 候选；全不命中回退 `无法理解`（保留原语义，附 `--self-test` 提示）。

不采纳：调模型做意图分类 —— 解析失败就说明当前档位无能为力，再调一次只会更慢且同样无权。

### D7 · 审计与输出

- 执行后 `audit_emit(input, cap_id, "confirm"|"allow", "ok"|"error"|"cancelled", reason, 0, dur)`，`dur` 用 `clock_gettime(MONOTONIC)`
- 命令本身与结果都打到 stdout（命令先 `→ cmd`，输出由子进程直接写终端）
- 缓冲区：`dispatch()` 的 `output` 参数继续只承载摘要，子进程输出不经过它（避免 4KB 截断）

### D8 · 不做什么

- 不实现链式执行（多动作 = 拒绝，不是排队）
- 不改规则层槽位算法
- 不改 `perm.sock` 协议（不在本 change 面内）
- 不动档位映射（D9 只改"何时加载模型"，不改档位与后端选择）

### D9 · 模型惰性加载（用户反馈"占用 CPU 很大、很快就满了"后追加）

**问题**：`nlsh` 对任何非传统命令都在启动时 `nl_init()`——哪怕规则层一句话就能决定。
实测 `创建一个叫 X 的文件夹`：**3.57s / 779MB RSS / 2.1s CPU**；同一句 `--no-model` 只要
**0.22s / 18MB**。在 2 vCPU 机器上，每条规则命令白占满一核约 2 秒、白吃 780MB。

**做法**：新增 `nl_init_lazy()`——只记录路径 + `access(R_OK)` 检查，不加载；
四层管线走到 Layer 3（规则未命中、真要解码）时才 `ensure_model()` 加载。
`nl_init()` 语义不变（显式要求已加载的场景仍可用）。

**结果**：规则可决定的命令 **0.16–0.34s / 19MB**（与 `--no-model` 持平）；
规则未命中时仍会真加载（RSS 786MB，行为不变）；模型缺失时降档到规则、不阻塞。

**不采纳**：常驻模型进程（`nl-infer` 守护）——那是另一个 change 的议题；
本次只是别为"用不上的模型"付钱。

## 4. 风险

| 风险 | 缓解 |
|---|---|
| 新增拒绝维度误伤正常句子 | 单测正反例（`budget_test`）：5 条必须拒绝 + 6 条必须放行 |
| 确认门在管道里吞掉用户输入 | `NL_ASSUME_YES` + 非 TTY EOF = 取消（fail-closed，不会静默执行） |
| fork 缓冲重复 | D4 明确 `fflush`，`verify_exec.sh` 断言输出行数 |
| 执行后残留目录影响其它测试 | 验收脚本一律在 `mktemp -d` 的工作目录 + 临时 `XDG_STATE_HOME` |
| `nl-run` 行为改变 | 抽出后跑 `verify_segment3.sh` + S2b/S8/S9/S10/S11 场景 |

## 5. 实施偏差记录（实现后回填）

| 设计项 | 实际 | 原因 |
|---|---|---|
| D5 只列了两个拒绝来源 | 追加**输入级闸门**（输入含 shell 元字符即拒绝） | 实测 `创建一个叫 x; touch hacked` 被解析成 `touch hacked`，生成命令干净、命令级 guard 无效（安全审计 S-1） |
| D2 确认门 | 落地一致；额外规定 `fgets` 返回 NULL（EOF）= 取消 | fail-closed：非 TTY 无输入时绝不能默认执行 |
| D4 `fflush` | 落地，且同样 `fflush(stderr)` | 子进程可能写 stderr（如 `ls` 报错），顺序同样会乱 |
| — | 新增 `line_edit.c` 修复（T04b） | 原实现只接受 ASCII，中文输入整段丢失；不修则 REPL 路径无法验收 |
| — | `--decide` 新增 `rule-decisive` 方法名 | 原实现把"没解码"标成 `choice-llm`，与审计口径冲突 |
| D6 提示表 | 落地 15 条关键词 | 只提示注册表里真实存在的能力，避免"建议一个本机没有的命令" |

其余（D1/D3/D7/D8）与设计一致。
