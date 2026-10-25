# NL-OS — 自然语言操作系统

在终端用自然语言操作 Linux。无需安装 agent，纯 C 语言 + CPU 推理。

## 快速开始

```bash
# 无 root 安装到 ~/.local（推荐：装完直接敲 nlsh）
bash scripts/install_local.sh
nlsh --scan            # 首次：发现本机能力
nlsh                   # 进入自然语言终端

# 或者只在源码目录里试用
mkdir build && cd build && cmake .. && make -j$(nproc)
./nlsh
```

模型会自动从 `$NL_MODEL_PATH` → `/usr/share/nl-os` → `~/.local/share/nl-os` 里找第一个 `*.gguf`，
也可以用 `bash models/download_model.sh small` 下载（约 428MB）。

## 组件

| 组件 | 说明 |
|---|---|
| `nlsh` | NL-Shell REPL（自然语言终端） |
| `nl-perm` | 权限守护进程（L1-L4 分级） |
| `nl-run` | .tools 脚本执行器 |
| **`nlbroker`** | **能力调用 broker**：CIP v1 协议（discover/describe/invoke/commit/audit），裁决 + ticket + 审计 |
| `libnlc.so` | 核心 NL 理解库（含 CIP 编解码、注册表、SHA-256、审计） |

## 用自然语言干活

```bash
./build/nlsh --scan                       # 首次：发现本机能力
./build/nlsh -c '创建一个叫 demo 的文件夹'   # 写操作：问一次 → 留快照 → 执行 → 审计
./build/nlsh -c '列出当前目录'              # 只读操作：直接执行
./build/nlsh --undo                       # 撤销上一次写操作
```

- 只说能做的：跨多个动作（"创建并进入"）、本机没有对应能力的操作（压缩/发送到远程）会被
  明确拒绝并给出可执行的替代步骤，**不会**降级成一条近似的破坏性命令。
- 脚本/自动化：`NL_ASSUME_YES=1`（或 `--yes`）跳过问询，`--dry-run` 只预演不执行。
- 确认是一套策略（ADR-019）：`y / Y / yes / YES / 是` 都算同意；**直接回车 / 无输入 = 取消**；
  退出码 `0` 成功、`1` 失败或被拒绝、`3` 用户取消。同一套策略也用于 `.tools` 脚本（`nl-run`），
  其中 `# @perm: allow-l2` 只豁免 L2，L3 删除仍要确认。
- 开销按需：常见说法由规则层决定，**不加载模型**（约 0.2s / 19MB）；
  只有规则层未命中时才加载 428MB 模型（约 3s / 780MB）。所以 `nlsh -c '创建一个叫 x 的文件夹'`
  在 2 核机器上也不会打满 CPU。
- 交互式中文输入不再被吞（行编辑器支持 UTF-8，管道输入走独立读取路径）。


## 本地 broker（CIP v1）

```bash
# 扫描本机能力（PATH 二进制 + DBus 方法）
./build/nlbroker --scan-dbus --registry ~/.local/state/nl-os/capabilities.jsonl

# 以服务方式启动（socket 权限 0600）
./build/nlbroker --socket /tmp/nl-os.sock --registry ~/.local/state/nl-os/capabilities.jsonl

# 以 MCP 门面运行（只暴露 perm ≤ L2 的能力）
./build/nlbroker --facade-mcp --registry ~/.local/state/nl-os/capabilities.jsonl
```

协议要点：`invoke` 只裁决、`commit` 凭一次性 ticket 执行；`plan` 为 argv 数组（不经 shell）；
`effect` 未标注一律按 `delete` 处理。详见 `.specs/adr/016-cip-protocol.md`。

## 架构

四层混合管线：安全网关 → 规则引擎 → 模型推理 → 模板填充

详见 `.specs/nl-os/DESIGN.md`（一期 MVP）与 `.specs/archive/2026-10-25-capability-protocol/DESIGN.md`（CIP v1）

## 许可证

MIT
