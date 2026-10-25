# CHANGE — nl-shell-runtime：让终端里的自然语言真正"能用完"

- **Change-ID**: `nl-shell-runtime`
- **提出/完成**: 2026-10-25
- **模式**: ponytail full；压缩路径（0→3→4→6）
- **前置**: `shell-nl-hook`（钩子已上线，`a982489`）

## 1. 为什么

钩子上线后，终端里已经可以"直接说人话"，但两个硬问题让体验断掉：

1. **`cd` 不生效**：`进入 /tmp 目录` 会说"本机没有该能力：cli:cd"；即使绕过，子进程 `chdir`
   也影响不到你的 shell。而且 bash 把 `command_not_found_handle` 放在**子 shell** 里跑，
   钩子自己也 `cd` 不了。
2. **规则未命中的句子每次都冷加载模型**：1.8s 加载 + 780MB，交互式每句一付。
   更糟的是排查中发现**模型路径根本没跑通过**——`GGML_ASSERT(n_tokens_all <= cparams.n_batch)`
   直接 abort（`rc=134`），因为 prompt 分块按 `n_ctx`(512) 而不是 `n_batch`(256)。

## 2. 目标

- 终端里 `进入 /tmp 目录`、`切换到上级目录` **真的改变当前目录**
- 规则未命中的句子不再每次重付模型加载成本，且模型路径不崩

## 3. 范围边界

### In

- ADR-020：`cd` 作为内建能力，`nlsh` 写 `__NL_CD__ <dir>` 指令，钩子在 `PROMPT_COMMAND` 里执行 `cd -- "$dir"`
- 修死模板：`cd ..`（template 901）新增 87/88/89 三条规则可达
- ADR-021：`nlsh --serve` 常驻解析进程（惰性加载 + 空闲卸载 + TTY 才自动拉起 + 三级回退）
- 修模型路径 `n_batch` 分块 bug（abort → 正常返回）

### Out（登记）

| 不做 | 理由 |
|---|---|
| 让推理本身变快 | 单次推理 20s 量级属模型层瓶颈（STATE「决策层延迟」），与本 change 无关 |
| `export`/`alias` 等其它内建 | 语义与信任边界各异，逐个来；本次只 cd |
| 系统级 systemd 单元 | 自动拉起 + 空闲退出已够用，多一个服务少一个自由度 |

## 4. 最小需求与 AC（压缩路径豁免声明）

> 走 `0→3→4→6`，总改动 ≈ 600 行（含测试与文档）。豁免独立 REQUIREMENT.md / DESIGN.md。

- **AC-15 `cd` 生效**：交互式终端里 `进入 /tmp 目录` 后提示符/`pwd` 都在 `/tmp`
- **AC-16 诚实失败**：无指令通道 / 目标不是目录 → 非 0 且说明原因，**不打印"已切换目录"**
- **AC-17 不 eval**：指令通道里塞命令一字节都不执行（只认 `__NL_CD__ ` + `cd -- "$dir"`）
- **AC-18 `cd ..` 可达**：`切换到上级目录` → `cd ..`（死模板已修）
- **AC-19 守护进程编解码无损**：`NlParseResult` → JSON → 结构，命令/权限/确认/槽位/路径都可还原
- **AC-20 规则判定一致**：`nl_parse_rules_only` 命中 ⟺ `nl_parse` 的 `source == RULE`
- **AC-21 非 TTY 不养常驻进程**：`stdin` 非 TTY 时既不拉起、也不留 socket
- **AC-22 回退**：守护进程不可用 → 与升级前行为一致（进程内加载），不因 IPC 失败而失败
- **AC-23 模型路径不崩**：规则未命中不再 `abort`（`rc` 不再是 134）

## 5. 最小设计与触碰模块

| 文件 | 改动 |
|---|---|
| `src/libnlc/shell_builtin.c/.h` | 新增：内建判定 + `__NL_CD__` 指令写入（路径展开、目录校验、无通道时诚实报错） |
| `src/libnlc/parse_ipc.c/.h` | 新增：socket 路径、客户端（自动拉起/回调）、结果编解码 |
| `src/nlsh/serve.c/.h` | 新增：守护进程循环（poll + 空闲卸载/退出 + 单连接处理） |
| `src/libnlc/nlc_api.c/.h` | 新增 `nl_parse_rules_only()`（判断"要不要模型"） |
| `src/nlsh/dispatch.c` | 内建分支（跳过注册表 + 指令通道）；`parse_smart()` 三级解析 |
| `src/nlsh/main.c` | `--serve/--socket/--idle/--max-idle`；`--no-model` 同时关闭远程 |
| `src/libnlc/rules_db.h` | 新增 87/88/89（上级/父目录/上一级 → 901） |
| `src/libnlc/model_loader.c` | 分块上限改为 `n_batch`（修 abort） |
| `src/libnlc/json_helper.c/.h` | 数组追加、子项读取、字符串值访问器、对象释放 |
| `scripts/nl-hook.bash` | 指令文件 + `PROMPT_COMMAND` 应用（保留用户原有 PROMPT_COMMAND） |
| `tests/unit/shell_builtin_test.c` / `parse_ipc_test.c` | 新增单测 |
| `scripts/verify_hook.sh` | 新增 AC-10~AC-13（35 断言） |

## 6. 风险

| 风险 | 缓解 |
|---|---|
| 覆盖用户的 `PROMPT_COMMAND` | 前插并保留原值；数组/字符串两种形态都处理；AC-11 覆盖 |
| 守护进程残留 | 空闲退出（默认 1h）+ 非 TTY 不拉起；socket 目录 0700 |
| 推理慢导致客户端超时丢结果 | 客户端超时 120s（> 单次推理） |
| 模型层仍慢 | 明确登记为独立瓶颈，本次不假装解决 |
