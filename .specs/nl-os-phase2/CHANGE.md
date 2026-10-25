# CHANGE: NL-OS Phase 2 — 真实推理 + 安全强化

- **Change ID**: nl-os-phase2
- **创建日期**: 2026-07-01
- **状态**: draft

---

## Why

Phase 1 MVP 验证了四层管线架构可行，但模型推理仍是 stub。Phase 2 聚焦两个核心方向：**真实模型接入**和**安全深度强化**。

## What

### Block A · 真实推理（需网络下载 ggml）

| # | 特性 | 依赖 |
|---|---|---|
| A1 | ggml 源码 vendor + BUILD_STUB_MODEL=OFF | 网络 |
| A2 | 加载真实 GGUF 模型（Qwen2.5-1.5B Q4_0） | A1 |
| A3 | Benchmark 跑分（1000 条）→ 数据驱动选型 | A2 |
| A4 | 动态 few-shot 检索（余弦相似度） | A3 |

### Block B · 安全强化（离线可做）

| # | 特性 | 说明 |
|---|---|---|
| B1 | eBPF 权限拦截 | LSM hook，系统调用级过滤 |
| B2 | .tools freeze 机制 | 首次翻译后锁定命令 |
| B3 | .tools 预授权 `# @perm:` | 脚本头声明权限级别 |
| B4 | 会话上下文 `$参照上一次` | 简单状态记忆 |

### Block C · 工程完善

| # | 特性 |
|---|---|
| C1 | CI/CD pipeline（GitHub Actions） |
| C2 | Docker 端到端测试 |
| C3 | man page + 完整文档 |

## 范围排除

- 不做 .nl 完整编程语言（永远不做 · ADR-011）
- 不做多用户协作（Phase 3）
- 不做模型微调（永远不做）
- 不做 GUI/语音（永远不做）

## 验收线

1. 加载真实 GGUF 模型，benchmark 意图分类准确率 ≥ 85%
2. eBPF 程序能拦截对 `/etc` 的 `unlink` 系统调用
3. `nl-run --freeze build.tools` 生成 frozen 文件，之后离线执行成功
