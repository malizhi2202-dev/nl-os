# DISCOVERY: 既有结论的外部核查（网络 / 论坛 / GitHub）

- **类型**: 结论校正（D-discovery），不进入 change 阶段
- **日期**: 2026-10-25
- **动机**: 把此前对项目做过的判断逐条拿到外部核实，**只保留站得住的**
- **最重要的结果**: 「安全执行层是空白」**被推翻**；「能力注册表没先例」**部分被推翻**

---

## 1. 逐条核查

| # | 此前的结论 | 核查结论 | 证据 |
|---|---|---|---|
| 1 | 「安全执行层 / AI 命令的可证明安全」近乎空白，是最大机会 | ❌ **被推翻**。2025–2026 已形成一个「agent / MCP 防火墙」赛道，至少 7 个项目在用不同层做同一件事 | 见 §2 |
| 2 | 能力注册表 / 能力自发现「没有先例」，最接近的是 PowerShell `Get-Command` | ❌ **部分被推翻**。MCP 工具注册表、AWS Agent Registry、HF "Agentic Resource Discovery" 都在做运行时工具发现 | [AWS Agent Registry](https://aws.amazon.com/cn/blogs/machine-learning/discover-tools-at-runtime-with-aws-agent-registry/)、[Agentic Resource Discovery](https://huggingface.co/blog/agentic-resource-discovery-launch) |
| 3 | `n_batch=1` 会严重拖慢 prefill | ✅ **成立**，且有上游 issue 佐证 | [llama.cpp #29175](https://github.com/ggml-org/llama.cpp/issues/29175)（prefill chunk 与 `-b`/`-ub` 直接相关）、[gpt4all #2605](https://github.com/nomic-ai/gpt4all/issues/2605)（prompt processing 被 batch 上限卡住）、[llama.cpp discussion #229](https://github.com/ggml-org/llama.cpp/discussions/229)（长 prompt 为什么慢） |
| 4 | 手机端 GUI agent 是成熟红海 | ✅ **成立** | 三个万级 star 仓库（UI-TARS 11.6k / mobilerun 9.8k / MobileAgent 9.3k）+ 蚂蚁 Napaxi + vivo BlueLM-GUI，见 `@.specs/discovery/phone-gui-agents.md` |
| 5 | 中文 NL→Linux 命令的模型/数据「是空白」 | ⚠️ **未证实**。没找到反例，但也只搜了一轮；不支持写成结论 | 需单独一轮检索（HF / ModelScope / 论文） |
| 6 | Linux 上「无 CLI 的能力」的结构化等价物是 DBus，不是屏幕点击 | ⚠️ **假设未验证**。找到桌面自动化项目（deskwright）但未确认其是否走 DBus；仍作为待验证假设 | — |
| 7 | 端侧 ≤4GB 的真实下沿是 512MB（扫地机 SoC） | ✅ **成立** | 全志 Cortex-A7 + 512MB DDR3 + 4GB eMMC 拆解 |

## 2. 「安全执行层已经有人做了」——现有赛道

Belay（`agentbelay`，PyPI 2026-10-21，MIT）自述为"AI agent 工具调用的本地确定性防火墙"，并在其 README 的对比表里列出了同赛道：

| 项目 | 强制点 | 策略模型 | 确定性 | 本地 |
|---|---|---|---|---|
| **Belay** | Claude Code hook + MCP 代理 + Python API + CI | YAML → 契约：顺序、状态、参数、预算、审批 | ✅（约 77µs/次） | ✅ |
| **Claude Code permissions/hooks** | Claude Code 内部 | allow/ask/deny 模式；auto 模式用 LLM（不确定性） | 规则✅ / auto❌ | ✅ |
| **Docker MCP Gateway** | MCP 代理 + 每 server 一容器 | 工具过滤 / 密钥拦截；付费版 Cedar | ✅ | ✅（治理付费） |
| **Invariant Guardrails**（Snyk） | LLM/MCP 代理或轨迹 | 序列语言 + ML/LLM 检测器 | 规则✅ / 检测器❌ | 自托管/云 |
| **Lasso MCP Gateway** | MCP 代理 | 密钥/PII 脱敏；云侧自定义策略 | 脱敏✅ | 脱敏本地 |
| **StrongDM Leash** | 容器 + **eBPF LSM** + HTTP MITM | Cedar → 内核与代理规则 | ✅ | ✅（需 Docker） |
| **Cupcake** | agent hooks | **OPA/Rego** → Wasm + 外部信号 | ✅（可选 LLM 判定） | ✅ |
| **Pipelock** | 出口代理 + MCP 代理 + CC hooks + OS 沙箱 | YAML 规则 + DLP/注入模式 + 调用链 + 预算 | ✅ | 核心本地 |

另外还有 `cmdpolicy`（edge agent 的命令策略模块）与 `deskwright`（桌面自动化）这类边缘实现。

### 这意味着什么

- 「让 AI 不闯祸」这个需求**已经被市场承认并有人投入**，不是空位
- 但它们**几乎全是开发者工具形态**：Python/Rust/Go、跑在开发机或容器里、守护的是 Claude Code / MCP 这类云端或宿主 agent
- **没有一个是**：端侧 Linux（≤4GB / OpenWrt / 512MB SoC）、纯 C11、零运行时、把"本机能力清单 + 权限分级 + 模板填充"做成一体的

## 3. 修正后的定位

原来那句「没有人在做可证明安全的 NL 执行」**必须删掉**，换成可辩护的版本：

> **不是"没有安全执行层"，而是"安全执行层只有开发机形态，没有端侧形态"。**
>
> 现有项目解决的是「我让 Claude Code 别把我 prod 库删了」；
> 本项目解决的是「一台 512MB 的机器，刚上电、没网、没 Python，怎么让它听懂话又不会自己拆掉自己」。

差异点因此收窄为三条（都可验证、都还没被占）：
1. **端侧资源档位**：≤4GB / 512MB 级设备可用（现有方案依赖 Python/Rust/容器，物理上装不进）
2. **零运行时**：C11 + 系统 llama.cpp，无解释器、无容器（OpenWrt 上可用）
3. **中文 + 本机能力自发现**：中文 NL、能力清单随机器走（现有方案假设目标是 POSIX 开发机）

已被占的部分（不要重复造）：策略语言（YAML/Rego/Cedar）、MCP 代理、CI 轨迹回放、容器/eBPF 强隔离。

## 4. 对既有文档的影响

- `@.specs/nl-os/CHANGE.md` §Why 的「目前没有人在做…」是**不准确的历史表述**，已在该文件追加日期化更正（不改写原文）
- 本 change 的产品经理 G1 意见「差异化成立于 中文+端侧+安全+可固化」方向仍成立，但"安全"这一项要按 §3 收窄
- **建议**：段 2 的 T10/T13 评测里，加一组与 Belay 的对照说明（同为"决策前拦截"，形态不同），避免对外表述被打
