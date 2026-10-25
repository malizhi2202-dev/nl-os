# CHANGELOG

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
