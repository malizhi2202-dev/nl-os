# NL-OS — 自然语言操作系统

在终端用自然语言操作 Linux。无需安装 agent，纯 C 语言 + CPU 推理。

## 快速开始

```bash
mkdir build && cd build && cmake .. && make -j$(nproc)
export NL_MODEL_PATH=/path/to/model.gguf
./nlsh
```

## 组件

| 组件 | 说明 |
|---|---|
| `nlsh` | NL-Shell REPL（自然语言终端） |
| `nl-perm` | 权限守护进程（L1-L4 分级） |
| `nl-run` | .tools 脚本执行器 |
| **`nlbroker`** | **能力调用 broker**：CIP v1 协议（discover/describe/invoke/commit/audit），裁决 + ticket + 审计 |
| `libnlc.so` | 核心 NL 理解库（含 CIP 编解码、注册表、SHA-256、审计） |

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
