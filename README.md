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
| `libnlc.so` | 核心 NL 理解库 |

## 架构

四层混合管线：安全网关 → 规则引擎 → 模型推理 → 模板填充

详见 `.specs/nl-os/DESIGN.md`

## 许可证

MIT
