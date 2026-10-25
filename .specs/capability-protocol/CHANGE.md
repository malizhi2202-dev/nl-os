# CHANGE: 统一能力调用协议（CIP v1）+ 本地 broker

- **Change ID**: `capability-protocol`
- **创建日期**: 2026-10-25
- **路径建议**: 完整（`REQUIREMENT → DESIGN → TASK → DEV → TEST → REVIEW → INTEGRATION`）
- **状态**: draft
- **前置**: 暂停 `nl-capability-runtime`（T01–T03 完成，T04–T08 待续，见其 `PROGRESS.md`）；本 change 定义的能力描述符 schema 将反过来成为 T08 格式冻结的输入

---

## Why（为什么做）

用户目标：**把这些能力整合成一个自己的框架，哪里都能跑，统一调用协议。**

现状与目标的差距是三件事：

1. **能力已能发现，但只能在一台机器、一个进程里用。** `nl-capability-runtime` 段 1 产出了能力注册表（JSONL）与扫描器，但它没有对外的调用接口——`nlsh` 自己用，别人用不了。
2. **执行面是分裂的。** CLI 二进制、DBus 接口、MCP 工具、HTTP 服务，四类能力各有各的调用方式；没有统一描述，就没有统一策略。
3. **现有「统一协议」MCP 缺了关键的一半。** MCP 假设调用方可信、假设有运行时、且**没有"危险"这个概念**——所有工具调用等价。NL-OS 面对的恰恰是不可信的调用方（模型）与后果不对称的操作（`rm -rf /` vs `ls`）。

**与既有赛道的区别**（见 `@.specs/discovery/claim-verification.md`）：Belay / Leash / Pipelock 等 agent 防火墙守的是开发机上的 Claude Code，形态是 Python/Rust/容器；本 change 要把"裁决"做成**协议的一部分**，并且能跑在 ≤4GB 的端侧设备上（纯 C、无解释器）。

## What（做什么）

### 协议：CIP v1（Capability Invocation Protocol）

JSON Lines over Unix socket（默认 `/run/nl-os/broker.sock`）：

| 报文 | 请求 | 响应 |
|---|---|---|
| `discover` | `{tier?}` | `{tier, capabilities:[{id,name,perm,effect,reversible}]}` |
| `describe` | `{id}` | `{slots[], effect, reversible, timeout_ms, cost}` |
| `invoke` | `{id, args, dry_run}` | `{verdict: allow\|confirm\|deny\|refused, reason, plan:{argv}, ticket?, alternatives[]}` |
| `commit` | `{ticket}` | `{status, output, snapshot_ref, audit_id}` |
| `audit` | `{since, limit}` | `{records[]}` |

**三条不可协商的规则**：

1. **能力 ID 带命名空间**：`cli:ls` / `dbus:org.freedesktop.NetworkManager|Device.Wireless.RequestScan` / `mcp:filesystem/read_text_file` / `http:GET|https://host/path`。这是"哪里都能跑"的机制——换执行面只换命名空间，协议不变。
2. **`invoke` 不执行，只裁决**；执行只能由 `commit` 触发。确认环节（`confirm`）是协议的一部分，调用方必须拿 ticket 再提交。
3. **`effect` / `reversible` 由注册表给出，调用方无权声明**。否则模型自称"只读"即可绕过所有策略。

### broker：独立进程

- 承载能力注册表、裁决引擎、审计、快照与 ticket
- 被 systemd 托管；崩了不影响 shell（fail-closed：不可达则拒绝执行，但传统命令透传不受影响）
- 单一入口，四个适配器注册能力：`cli` / `dbus` / `mcp` / `http`

### 执行面 v1（四个都要）

| 命名空间 | v1 实现方式（**零新增 C 依赖**） | 说明 |
|---|---|---|
| `cli:` | `fork` + `execvp` | 复用已做的 T03 扫描结果 |
| `dbus:` | 经 `busctl` / `gdbus` 子进程桥接 | 不引入 libdbus；v2 再评估原生绑定 |
| `mcp:` | stdio JSON-RPC 2.0 子进程 | 反向吸收现有 MCP server 的工具 |
| `http:` | 内置最小 HTTP/1.1 客户端 | **仅 http://**；https 走 `cli:curl` 桥（见风险 R2） |

### 对外门面

- **自家 `nlsh` / `nl-run`**：走 Unix socket（主要路径）
- **MCP server 门面**：把本地能力暴露成 MCP 工具（stdio），让 Claude Code / Cursor 之类能直接调用

## 影响面

- [x] 影响 `REQUIREMENT.md`：协议报文、适配器、门面各有 AC
- [x] 影响 `DESIGN.md` / 引入新 ADR（见下）
- [x] 影响现有 AC：`nl-capability-runtime` T08 的"注册表格式冻结"改为消费本 change 的 descriptor schema
- [x] 影响数据模型：新增 ticket、快照引用、审计记录的结构
- [x] 影响外部接口：新增 Unix socket 协议（自有）+ MCP stdio（标准）

## 架构层影响声明（0.4 选项 2 强制）

| 冲击对象 | 影响 |
|---|---|
| **ADR-007 模块边界** | 新增项目级模块「broker」（`src/nlbroker/`）+ 协议编解码（`libnlc/cip.c`）；`nlsh` 从"自己调 libnlc"改为"协议客户端" |
| **ADR-012 能力注册表** | 注册表增加 `effect` / `reversible` / `namespace` 字段；`format_version` 升级到 2（本 change 定义，T08 冻结以此为准） |
| **ADR-013 类型化决策层** | 决策层成为 broker 内的一个裁决实现（`allow/confirm/deny/refused`），协议层不假设决策方式 |
| **新增 ADR** | **ADR-016** CIP v1 协议与命名空间；**ADR-017** broker 形态、fail-closed 与 ticket 生命周期 |
| **既有赛道对齐** | 与 MCP 的关系定义为"上游包装"：CIP 不替代 MCP，而是把 MCP 工具吸收为一种能力，并额外提供 MCP 没有的裁决语义 |

## 范围排除（这次不做）

- ❌ Android UI / 视觉执行面（另一条产品线，见 `@.specs/discovery/phone-gui-agents.md`）
- ❌ Windows / macOS 执行面
- ❌ 多用户、远程 broker、网络传输（v1 仅本地 Unix socket）
- ❌ 云端模型调用与遥测
- ❌ libdbus / libcurl 原生绑定（v1 用子进程桥，规避 ADR-006 依赖上限）
- ❌ 鉴权体系（v1 单用户本地，靠文件权限 0600 + socket 权限）

## 验收线（粗粒度）

1. `discover` 能返回四个命名空间的能力，`describe` 能给出 `effect`/`reversible`；**同一报文格式对四种执行面一致**
2. 危险调用（`cli:rm -rf /etc`）在 `invoke` 阶段返回 `deny`，**不产生可执行 plan**
3. `confirm` 类调用必须带 ticket 才能 `commit`；无 ticket 或过期 ticket 一律拒绝
4. 模型无法通过参数自报 `effect=read` 绕过策略（构造该攻击用例必须被拦）
5. Claude Code / Cursor 通过 MCP 门面能列出并调用本地能力，且危险调用被 broker 拦下
6. 全链路审计可回放：每个 `invoke`/`commit` 有 `audit_id`，含 verdict、耗时、拒绝原因

## 风险与未知

| # | 风险 | 等级 | 处理 |
|---|---|---|---|
| R1 | **四个执行面一起做，范围偏大** | 🔴 高 | 按适配器拆任务，`cli` 先落地（已有扫描），其余三个各自独立 verify；不允许一个任务跨两个适配器 |
| R2 | **HTTP/TLS 与 ADR-006 依赖上限冲突**：纯 C 无 TLS | 🔴 高 | v1 只支持 `http://`；https 通过 `cli:curl` 能力间接完成；若必须原生 TLS，需修订 ADR-006（显式决策，不偷偷加依赖） |
| R3 | **DBus 原生绑定需要 libdbus** | 🟡 中 | v1 用 `busctl`/`gdbus` 子进程桥（约 10ms 开销），v2 再评估 |
| R4 | 两阶段 `invoke/commit` 的 ticket 泄露或重放 | 🔴 高 | ticket 一次性 + TTL + 绑定能力 ID 与参数哈希；重放必须被拒 |
| R5 | MCP 门面把本地能力暴露给不可信客户端 | 🔴 高 | 门面只暴露 `perm ≤ L2` 的能力；L3/L4 一律返回 `deny` 并记审计 |
| R6 | 注册表 `format_version` 从 1 升 2 导致已暂停的段 1 返工 | 🟡 中 | 本 change 的 descriptor schema 先定稿，段 1 的 T08 直接按 v2 冻结（返工面可控：`capability_registry.c` 的解析与写出） |
| R7 | 协议字段（effect/reversible）标注错误比没有更危险 | 🔴 高 | 未标注 `effect` 的能力默认按 `delete`（最严）处理；标注必须可测 |

---

> 后续 AC 与设计细节进入 `REQUIREMENT.md` / `DESIGN.md`，本文件不再扩展。

---

## 🛡️ G1 需求门投票记录（2026-10-25，三轮讨论后）

### 第 1 轮 · 竞品与先例

- **架构师**：MCP 已是事实上的"统一调用协议"，任何新协议必须先解释"为什么不直接用 MCP"。答案成立：MCP 无裁决语义、假设调用方可信、假设有运行时。两阶段调用有成熟先例（Terraform plan/apply、`kubectl --dry-run`、D-Bus 的方法调用 + polkit 的交互式授权）。
- **产品经理**：D-Bus 本身就是 Linux 上的"统一能力调用协议"，Android 的 Binder 同理——**"统一协议"这件事在 OS 层从来不是新想法，新的是把策略放进协议**。
- **安全审计师**：Belay 的对比表说明：现有工具全在"命令字符串"层面做匹配，可以被 `eval $(echo ... | base64 -d)` 绕过。CIP 把裁决放在"能力 + 参数 + 效果"层面，理论上更难绕过——但这要求 `effect` 元数据可信。
- **用户评测员**：两阶段对用户意味着"每个写操作要确认两次"？架构师澄清：`invoke`→`confirm` 后由 UI 收一次确认，`commit` 是同一动作的提交，不是第二次确认。

### 第 2 轮 · 领域深化（4 个决策）

1. **`discover` 必须支持过滤与分页**：1000 个能力一次性返回约 200KB，不可接受。定：`discover {prefix?: string, limit?: int, cursor?: string}`，默认 limit=100。
2. **ticket 的安全参数**：一次性 + TTL（默认 60s）+ 128bit 随机 + 绑定 `capability_id` 与 `args` 的哈希；重放或参数篡改一律拒绝。
3. **`effect` 取值收敛为五类**：`read | write | delete | privilege | network`；**未标注一律按 `delete` 处理**（最严），并计入审计。
4. **MCP 门面只暴露 `perm ≤ L2`**：L3/L4 通过 MCP 调用一律 `deny`——因为 MCP 客户端没有可信的确认通道。

### 第 3 轮 · 交叉验证（两个真问题）

- **安全 → 架构**：`effect` 由注册表给，但注册表是本地文件（0600 只防其他用户），属主或 root 可改。架构师承认：**威胁模型必须写死为「模型/agent 不可信，系统用户可信」**，否则这条设计不成立。→ 强制写入 ADR-016。
- **架构 → 产品**：四个执行面同批做，TASK 会碎成 20+ 个任务。产品经理建议把 HTTP 降到 v2；但与用户明确要求冲突，折中为：**HTTP 适配器只做最小实现（http:// 的 GET/POST JSON），标记 beta，TLS 不做**。

### 投票

```
🗳️ G1 需求门: CHANGE.md 是否通过？

   🟫 高级产品经理: ⚠️ 有条件通过。四个执行面同批做风险集中在 TASK 层；条件：HTTP 适配器
                     仅 beta（http:// only），且 TASK 必须按适配器拆分、每个适配器可独立 verify。

   🟦 资深用户评测员: ✅ 两阶段不会导致重复确认（commit 是提交不是再确认）；拒绝话术已有
                     alternatives 字段，符合此前"拒绝要给替代"的要求。

   🟩 架构师: ✅ 条件通过。条件：① ADR-016 必须显式写威胁模型（模型不可信 / 系统用户可信）；
                     ② discover 必须有 limit 与 cursor，不允许全量返回；③ 段 1 的 T08 冻结改按 v2 schema。

   🔴 安全审计师: ✅ 条件通过。条件：① ticket 一次性 + TTL + 绑定 args 哈希，重放测试必做；
                     ② 未标注 effect 一律按 delete；③ MCP 门面只暴露 perm ≤ L2；
                     ④ 必须有一条"模型自报 effect=read 绕过策略"的攻击用例。

   结果: 3/4 → 多数通过 ✅（PM 弃权改为条件通过；记录三条强制条件，进入需求阶段）
```

### 通过后生效的强制条件

1. ADR-016 必须写死威胁模型：**模型/agent 不可信，系统用户可信**（架构师）
2. `discover` 必须支持 `limit` + `cursor`，不允许全量返回（架构师）
3. ticket：一次性 + TTL + 绑定 `capability_id` 与 `args` 哈希，重放必须被拒（安全）
4. 未标注 `effect` 一律按 `delete`；MCP 门面仅暴露 `perm ≤ L2`（安全）
5. 必须有一条"模型自报 `effect=read` 绕过策略"的对抗用例（安全）
6. HTTP 适配器仅 beta（`http://` only），TASK 按适配器拆分、各自独立 verify（PM）
