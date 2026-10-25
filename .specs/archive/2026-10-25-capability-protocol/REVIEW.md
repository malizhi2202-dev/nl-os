# REVIEW: 统一能力调用协议（CIP v1）+ 本地 broker

- **Change ID**: `capability-protocol`
- **关联**: `@.specs/archive/2026-10-25-capability-protocol/{REQUIREMENT,DESIGN,TASK,TEST}.md`
- **审查基线**: `git log --oneline 729a96b..HEAD`（T01–T14 全部提交）
- **UI 轮**: ❌ 跳过（纯 CLI/daemon 项目，无界面；按 R2.10 豁免）

---

## 第 1 轮 · Spec 一致性审查（REQUIREMENT / DESIGN vs 实现）

| # | 检查项 | 结论 | 处置 |
|---|---|---|---|
| S1 | AC-4：`commit` 需回带 `capability`+`args` 才能校验篡改 | **实现与原始契约不一致**（原 DESIGN 只有 `{ticket}`） | 已改文档（DESIGN §3.2 / AC-4）+ 代码；记录为契约修正 |
| S2 | AC-6：L3/L4 通过 MCP 调用"一律拒绝**并写审计**" | **只拒绝，未写审计**（门面在 dispatch 之前就 return 了） | **T-FIX-01 已修**：门面拒绝路径直接 `nl_audit_emit` |
| S3 | AC-9：`invoke` 与 `commit` 各一条，**可用 audit_id 关联** | **两条记录无法关联**（各自随机 audit_id，无公共键） | **T-FIX-02 已修**：审计新增 `corr` 字段 = ticket 前缀 |
| S4 | AC-8：https 返回 `E_SCHEME_UNSUPPORTED` + 建议 `cli:curl` | 首版**失败响应丢 error 字段**（render 顺序 bug） | 已修（先处理失败分支），文档与实现一致 |
| S5 | AC-7：`dbus:<service>\|<method>` | 实现同时支持 2 段与 4 段；2 段时 path=`/`、iface=service | 文档补注（T09-SUMMARY），未改 AC |
| S6 | AC-1：`discover` 支持 prefix/limit/cursor | 一致 | — |
| S7 | DESIGN §4 威胁模型（模型不可信） | 一致：`effect` 只认注册表；`untrusted_effect_attempt` 有单测与 E2E 双覆盖 | — |
| S8 | ADR-006 依赖上限（≤3） | 一致：`ldd nlbroker` 无 libdbus/libcurl/libssl | — |

---

## 第 2 轮 · 质量审查（ponytail 四段格式）

### 删除

| 位置 | 内容 | 理由 |
|---|---|---|
| `adapter_http.c` | 死变量 `size_t used` + `(void)used` | 从未被读取，纯噪音 |

### 简化

| 位置 | 内容 | 理由 |
|---|---|---|
| `cip.c` describe | `ids` 超过 8 个时**静默截断** | 静默截断会让调用方以为全部处理了。改为 `E_BAD_REQ` |
| `audit.c` query | 阻塞式"先到先得"输出 | 改为**优先保留最新记录**（排障要最近，不是最早） |

### 保留（看似可删但判定不该删）

| 位置 | 内容 | 为什么不删 |
|---|---|---|
| `dispatch.c` | 5 处相似 `emit_audit(...)` 调用 | 合并需引入参数结构体 + 间接层，可读性反而下降；每处字段组合不同 |
| `ticket.c` | `/dev/urandom` 失败时的弱随机降级 | 端侧可能无 `/dev/urandom`；有降级比直接失败好，已注释风险 |
| `verdict.c` | 静态替代能力表（3 项） | 与 `alternatives` 协议字段直接对应，不是重复实现 |

### 风险接受（登记，不修）

| 位置 | 内容 | 理由 |
|---|---|---|
| `dispatch.c` describe | `timeout_ms` 硬编码 5000 | 注册表尚未存该字段（v2） |
| `adapter_http.c` | POST body 未从 args 传入 | v1 beta 范围 |
| `facade_mcp.c` | L2 能力一律拒绝而非自动确认 | **有意为之**：MCP 客户端无可信确认通道 |

---

## 第 3 轮 · 安全审查

| # | 发现 | 严重度 | 状态 |
|---|---|---|---|
| SEC-1 | **invoke 限速已实现但从未接线**（`nl_ticket_rate_ok` 全仓零调用）→ ADR-017 的 DoS 缓解形同虚设 | 🔴 高 | **T-FIX-03 已修**：dispatch invoke 入口调用，超限 `E_POLICY_DENY` + 审计 |
| SEC-2 | 门面 L3/L4 拒绝无审计（同 S2） | 🔴 高 | T-FIX-01 已修 |
| SEC-3 | invoke/commit 无法关联（同 S3），事后取证困难 | 🟡 中 | T-FIX-02 已修 |
| SEC-4 | 审计响应按字节截断时**丢最新记录**，排障时看到的是最老的事件 | 🟡 中 | **T-FIX-04 已修**：从最旧开始丢弃 |
| SEC-5 | 命令注入面 | ✅ 无 | `plan` 为 argv；`';'` 字面量有测试；dbus/http 参数走白名单 |
| SEC-6 | 重放 / 篡改 / 伪造 ticket | ✅ 已覆盖 | 三态拒绝 + 单测 + E2E |
| SEC-7 | 敏感数据落盘 | ✅ 已覆盖 | 审计只落 hash + 原因码；E2E 断言无明文 |
| SEC-8 | 无 SAST、ARM64/musl 未验证 | 🟡 | 登记（TEST.md G3/G4），不阻塞本 change 的代码质量判定 |

---

## T-FIX 清单

| ID | 内容 | 严重度 | 状态 | 验证 |
|---|---|---|---|---|
| **T-FIX-01** | 门面拒绝 L3/L4 时写审计 | 🔴 | ✅ 已修 | `verify_all.sh` → `T-FIX-01 facade deny audited` PASS |
| **T-FIX-02** | 审计新增 `corr` 关联键（invoke/commit 可配对） | 🟡 | ✅ 已修 | `T-FIX-02 invoke/commit correlated by corr` PASS |
| **T-FIX-03** | 接线 invoke 限速 | 🔴 | ✅ 已修 | `T-FIX-03 invoke rate limited` PASS |
| **T-FIX-04** | 审计响应保留最新记录（不丢新） | 🟡 | ✅ 已修 | `audit_test` 4/4 + E2E |
| **T-FIX-05** | `cip` describe 超限静默截断 → 显式拒绝 | 🟢 | ✅ 已修 | `cip_test` 7/7 |

修复后全量回归：**`ctest 20/20` + `verify_all.sh` 31 断言 0 失败**。

---

## 🛡️ G4 审查门投票（2026-10-25）

```
🟫 高级产品经理:      ✅ 三轮审查有真发现（3 个高危项已修），不是走过场；T-FIX 全部闭环
🟦 资深用户评测员:    ✅ 拒绝可解释、可替代（cli:curl / cli:ls）；审计现在能看到"最近发生了什么"
🟩 架构师:           ❌ 反对：TEST.md 的 G3（ARM64/musl 未验证）在 review 里仍未闭环，
                        而 ADR-015 已对 A1 档作出承诺。条件：7-integration 必须二选一——
                        ① 补交叉编译验证；② 在 CHANGE/ADR-015 显式把 A1 标注为"未验证承诺"
🔴 安全审计师:        ✅ SEC-1~4 全部修复且有回归断言；SEC-5~7 证据充分；SEC-8 已登记

结果: 3/4 → 多数通过 ✅（记录架构师条件 → 进入 7-integration）
```

### 带入 7-integration 的强制条件

1. **G3 必须在集成阶段给出明确处置**：闭环交叉编译，或在 ADR-015 / CHANGE 中显式降级 A1 承诺（架构师）
2. 修复项已全部有回归断言，集成阶段不得回退
3. UAT 必须覆盖"按需求文档"的完整链路，不是演示版
