# CHANGE — shell-nl-hook：终端里直接说人话，不用敲 `nlsh`

- **Change-ID**: `shell-nl-hook`
- **提出**: 2026-10-25
- **完成**: 2026-10-25
- **模式**: ponytail full；**压缩路径**（0→3→4→6，见下方豁免声明）

## 1. 为什么

用户原话：

> 不想输入 nlsh，就想在机器上自己启动之后在终端直接输入 自然语言啥的可以自己识别操作。

现状是 `nlsh` 是个**程序**，想起来才能敲。而终端里的自然语言应该是**默认能力**：开终端就有，
`ls` 还是 `ls`，说了人话就自动接。

这正好也修正了上一轮"CPU 很快满了"的体验面：能力再强，用户想不起入口就等于没有。

## 2. 目标

新开一个终端，**不做任何额外操作**，直接输入中文自然语言即可被识别并执行（走 `nlsh` 的完整链路：
拒绝优先 → 确认门（ADR-019）→ 权限 → 快照 → 执行 → 审计）。

## 3. 范围边界

### In

- bash 的 `command_not_found_handle` 钩子：**只有"命令不存在"时才介入**，真命令零影响
- 触发判定：输入含**非 ASCII（中文等）**才交给 `nlsh`；英文未知命令仍走系统 `command-not-found`
- 关闭开关 `NL_SHELL_HOOK=0`；英文接管开关 `NL_SHELL_HOOK_ALL=1`（默认关）
- `install_local.sh` 把钩子写进 `~/.bashrc` 的**标记块**（幂等、可一键卸载、改前备份）
- 钩子本身**不 eval、不解析**自然语言，一律原样交给 `nlsh -c`，安全边界不变

### Out（明确不做，登记为后续）

| 不做 | 理由 | 去向 |
|---|---|---|
| 接管英文拼错（`git statsu`） | 模型可能把拼错"修正"成另一条真命令并执行 | `NL_SHELL_HOOK_ALL=1` 显式开启；默认关 |
| zsh | 本机没有 zsh（已核实），多一份 `command_not_found_handler` 无测试覆盖等于没做 | 用户要 zsh 时再开 change |
| `cd` / `export` 等 **shell 内建** | `nlsh` 在子进程执行，改不了父 shell 的 cwd；`cli:cd` 也未注册。要做必须先设计"内建能力由钩子在父 shell eval"的安全模型（确认门/审计怎么算） | 记为 v2 议题（STATE 待决策） |
| 模型常驻进程 | 钩子让"每次输入"都付一次冷启动代价，常驻收益变大，但那是独立的架构决策（STATE 已挂账） | 独立 change |

## 4. 最小需求与 AC（压缩路径豁免声明）

> 本 change 走**压缩路径**（`0→3→4→6`，总改动 ≈ 150 行）。按 code-kit §1.2 在此内联
> "最小需求/AC"与"最小设计/触碰模块"两节，并声明豁免独立 REQUIREMENT.md / DESIGN.md。

- **AC-1 中文未知输入 → 交给 nlsh**：`command_not_found_handle` 收到含中文的未知命令时，
  调用 `nlsh -c "<整行>"`，并把 nlsh 的退出码原样返回。
- **AC-2 英文未知命令不被接管**：`git statsu` 这类仍走系统 `command-not-found`，**不调用 nlsh**。
- **AC-3 真命令零影响**：`ls`、`echo` 等不触发钩子（由 bash 语义保证，测试固定住这一点）。
- **AC-4 可关闭**：`NL_SHELL_HOOK=0` 时钩子完全退化为系统行为。
- **AC-5 安装幂等且可卸载**：`install_local.sh` 重复执行只留**一段**标记块；`--uninstall-hook`
  精确移除（其余行一个字节不动）；首次写入前备份 `~/.bashrc`。
- **AC-6 不改系统文件**：只写 `~/.bashrc`；`/etc/bash.bashrc` 的 `command_not_found_handle` 被我们
  **覆盖后仍等价回退**（`/usr/lib/command-not-found -- "$1"`，`rc=127` 兜底）。
- **AC-7 钩子不 eval**：含 `;` `|` `&&` 的输入不得由钩子解释；交给 `nlsh` 由输入级闸门拒绝
  （已核实：`创建 a; rm -rf /tmp/x 的文件夹` → `rc=1`，不执行）。
- **AC-8 语法正确**：`bash -n` 通过；钩子被 `source` 多次不重复定义/不报错。
- **AC-9 真终端可用**：`bash -ic '<中文未知命令>'` 走完整链路（不是只在 `bash -c` 里成立）。

## 5. 最小设计与触碰模块

```
~/.bashrc  ──source──►  scripts/nl-hook.bash   （标记块内只放一行 source）
                              │
                     command_not_found_handle
                              │
              ┌───────────────┴───────────────┐
    含非 ASCII（中文）                    英文/纯 ASCII
              │                               │
        nlsh -c "<整行>"               /usr/lib/command-not-found
        （确认门/权限/快照/审计/执行）      （系统原行为，rc=127 兜底）
```

| 文件 | 改动 |
|---|---|
| `scripts/nl-hook.bash` | 新增：钩子函数 + 判定 + 回退（唯一新增逻辑，≈40 行） |
| `scripts/install_local.sh` | 增 `--no-hook` / `--uninstall-hook`；默认装钩子（标记块 + 备份 + 幂等） |
| `scripts/verify_hook.sh` | 新增：AC-1~AC-9 断言，用 PATH 里的假 `nlsh` 记录 argv |
| `README.md` | 新增"终端里直接说人话"一节 |

**为什么不新建程序**：bash 已经提供了 `command_not_found_handle` 这个原生挂点（ponytail 阶梯 3/4），
不需要 daemon、不需要改终端、不需要别名。

## 6. 决策记录（用户确认）

| 决策 | 选项 | 用户选择 | 理由 |
|---|---|---|---|
| 触发范围 | 只中文 / 所有未知命令 / 全部输入先过 NL | **只中文** | 英文拼错不被模型"猜"，风险最小 |
| 安装位置 | 写进 `~/.bashrc` 标记块 / 只生成 hook 文件自己 source | **写进 `~/.bashrc`** | 开终端即用；标记块 + 备份保证可逆 |

## 7. 风险

| 风险 | 缓解 |
|---|---|
| 每次中文输入都起一个 `nlsh` 进程（规则命中 0.04s / 未命中 1.9s） | 命中便宜；未命中的冷启动代价由"模型常驻"独立 change 解决 |
| 用户以为 `cd` 生效 | 首版文档明确；`进入 xx 目录` 目前走"无 cli:cd 能力"提示，v2 处理 |
| 覆盖系统 `command_not_found_handle` | 回退逻辑与 `/etc/bash.bashrc` 等价实现，测试固定 |
