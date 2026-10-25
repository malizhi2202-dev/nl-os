# CHANGELOG

## 2026-10-25 · nl-capability-runtime 段 2 收尾 — decoder 接线 + 分档评测（T11b/T13）

**新增**

- `libnlc/decision_model.c`：把"模型相关"隔离出决策层——档位→后端映射、Choice prompt、
  受约束解码（GBNF 只枚举候选 + `<refuse>`）、输出解析、置信度阈值
- `model_loader` 补齐：`nl_model_infer_grammar()`（`llama_sampler_init_grammar`）、
  `nl_model_format_chat()`（模型自带 chat 模板 + ChatML 回退）、`nl_model_last_confidence()`、
  `n_batch=256`、每次推理前清 KV、`n_threads` 按实际核数
- `nlsh --decide <自然语言>`：只决策不执行，输出 chosen/method/tier/backend/candidates
- `nlsh --eval <评测集>`：逐条决策 + 汇总（`schema_violations` / `fallbacks` / `accuracy` / P50 / P95）
- `scripts/verify_segment2.sh`：AC-6~AC-10 共 17 条断言
- `tests/unit/decision_model_test.c`：档位映射 / prompt 完整性 / 输出解析 / 有模型时的准确率与延迟

**行为变更**

- 档位自降档接线：A1（≤1GB）不加载 decoder；模型缺失或解码失败降档到规则并落审计
- **低置信度阈值默认由 0.5 改为 0（关闭）**：实测该指标无区分度（阈值 0→0.5 时准确率 85.7%→80.6%，
  却丢掉 53% 正确回答）。`NL_DECIDE_MIN_CONF` 仍可开启

**实测（同批 120 条中文指令）**

- 受约束解码在候选集内 **66/67 = 98.5%**，规则 top-1 同口径 61/67 = 91.0%
- 分档：A1(模拟) 79.2% / B(本机 2 vCPU, 0.5B q4_0) 85.7%·P50 1493ms·P95 2788ms / C 未测
- 瓶颈确认在候选召回（漏救 33%，平均候选 0.85），不在模型选择

## 2026-10-25 · capability-protocol — 统一能力调用协议（CIP v1）+ 本地 broker

**新增**

- `nlbroker`：独立能力调用 broker（Unix socket，权限 0600，fail-closed）
- CIP v1 五报文：`discover / describe / invoke / commit / audit`（JSON Lines）
- 两阶段执行：`invoke` 只裁决并返回 `plan`；`commit` 凭一次性 ticket 执行
- 四个执行面：`cli:`（argv，不经 shell）/ `dbus:`（busctl 子进程桥）/ `mcp:`（stdio JSON-RPC）/ `http:`（beta，仅 http://）
- MCP 门面：把本地能力以 MCP 工具暴露，**仅 `perm ≤ L2`**
- 注册表 v2：`namespace` / `effect` / `reversible` / `effect_defaulted`，兼容读 v1
- 内置 SHA-256（FIPS 180-4，NIST 向量验证）
- 结构化审计（JSONL 0600）：`corr` 关联键、原因码、不含明文参数
- 能力发现：PATH 白名单扫描 + DBus 方法自省

**安全**

- 威胁模型：模型/agent 不可信，系统用户可信；`effect` 只认注册表
- 未标注 `effect` 一律按 `delete`（缺省从严）
- ticket：一次性 + TTL 60s + `args_hash` 绑定 + 表上限 1024 环形淘汰 + invoke 限速 100/s
- MCP 客户端无确认通道 → 门面拒绝 `confirm` 类调用

**验证**

- `ctest` 20/20；`scripts/verify_all.sh` 31 断言 0 失败（AC-1~AC-10）
- 性能：invoke p50 0.21ms / p95 0.50ms；broker VmHWM 5.3MB（Release，不含模型）

**已知未验证**

- ARM64 / musl / OpenWrt 未验证（无交叉编译链）→ ADR-015 已将 A1/B/C 承诺降级为"未验证的设计目标"
- 未接入真实 Claude Code / Cursor；未跑 SAST

## 2026-07-02 · nl-os — 一期 MVP

- 四层管线（安全网关 → 规则引擎 → 模型推理 → 模板填充）+ L1–L4 权限 + `.tools` 脚本
