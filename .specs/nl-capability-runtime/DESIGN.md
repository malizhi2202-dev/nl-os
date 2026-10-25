# DESIGN: NL-OS 能力运行时

- **Change ID**: `nl-capability-runtime`
- **关联**: `@.specs/nl-capability-runtime/CHANGE.md`、`@.specs/nl-capability-runtime/REQUIREMENT.md`、`@.specs/CONTEXT.md`
- **作者**: AI（Architect 角色）+ 四角色专家团三轮讨论

---

## 0. 技术栈选定

> 由 2-design 步骤 0 锁定。**本次不引入任何新外部依赖**（ADR-006 维持 ≤3）。

- 语言 C11 / CMake ≥3.16 / 既有 cJSON / 既有系统 llama.cpp（`BUILD_STUB_MODEL=ON` 时完全不带模型）
- 新增系统能力：`flock(2)`、`realpath(3)`、`lstat(2)`、`rename(2)` —— 均属 POSIX，无新依赖
- SHA-256：**内置实现**（约 150 行 + NIST 测试向量），不引入 OpenSSL/libsodium（ADR-014）
- 约束解码：llama.cpp 内置 GBNF sampler（`llama_sampler_init_grammar`），不引入 llguidance

---

## 0.5 既有架构对齐（brownfield 必填）

| 既有对象 | 本次处理 |
|---|---|
| ADR-001 四层管线 | Layer 3 语义改为"类型化判断"；Layer 1（安全网关）/Layer 2（规则）/Layer 4（模板填充）保持不变 |
| ADR-002 L1–L4 + 安全网关前置 | 不改。新增能力的权限等级来自注册表映射表，缺省 L4 |
| ADR-003 `.tools` | 向后兼容扩展：新增 `# @format/@checksum/@cap` 头；无头文件按 v1 解析 |
| ADR-007 模块边界 | 新增 3 个模块（capability_registry / decision / freeze+snapshot），接口归属 `libnlc`，`nlsh` 不得私有持有 |
| ADR-008 Agent 架构 | **推翻**（见 ADR-013） |
| 一期 `DESIGN.md` 的 CLI 契约 | 不改：`nlsh [-c] [--dry-run] [--verbose] [--no-model]`，新增 `--scan` / `--self-test` / `--eval` / `--freeze` / `--undo` / `--audit` 子命令 |

---

## 1. 决策清单

| # | 决策 | 备选 | 理由 | 代价 |
|---|---|---|---|---|
| **D11** | 注册表用 JSON Lines + `format_version` + `flock` + `rename` 原子写 | 二进制 / SQLite / 单 JSON 对象 | 可读可 diff；cJSON 已有；行式便于增量追加；原子写避免半成品 | 1000 项约 200KB，需流式解析 |
| **D12** | 能力权限等级来自**显式映射表**，未登记能力默认 **L4** | 从 `--help` 推断 / 默认 L1 | 安全默认（deny-by-default）；可审计 | 映射表需人工维护，新命令默认不可用 |
| **D13** | 决策走**两阶段**：规则粗筛出 top-K（K=8）→ 仅对 K 做 GBNF Choice | 全量枚举 / 端到端生成 | 全量 GBNF 语法体积与解码开销爆炸；两阶段保持输出 ≤16 token | 粗筛即规则，成为新瓶颈（PM 反对点） |
| **D14** | **Score 由规则计算，不调模型** | 模型打分 | 同一输入两次推理会使延迟翻倍；复杂度预算是结构化判断 | 打分维度受限于规则可表达的特征 |
| **D15** | **模型只能"多要确认"，不能"少要确认"**：`deny` 永远由规则/`nl-perm` 给出 | 模型可 deny/allow | 防提示注入绕过安全关卡 | 模型判断力被刻意限制，可能多打扰用户 |
| **D16** | 校验和用**内置 SHA-256**，逐行链式 hash 定位篡改行 | OpenSSL / CRC / FNV | 依赖上限；能定位首个差异行 | 新增约 150 行安全相关代码，无密钥、不防恶意重算 |
| **D17** | 快照：仅 L2/L3、单文件 ≤8MB、敏感清单排除、总上限 200MB、保留 10 次、`flock` 互斥 | 全量快照 / reflink / 不设限 | 端侧存储有限；敏感内容不得落盘 | 大文件操作不可撤销，需在提示中明说 |
| **D18** | 决策层可整体禁用，降级到规则预算；降级路径**必须有测试** | 模型必需 | OpenWrt 档跑不了模型 | 功能被分成两档，评测需覆盖两档 |

---

## 2. 架构与数据流

```
                       ┌──────────────────────────────────────────┐
  用户输入 ───────────▶ │ nlsh                                                        │
                       │  ① 传统命令/已注册函数调用包? ── 是 ─▶ 快路径 (不经模型)     │
                       │                                          └▶ 审计 fast_path │
                       │  ② 否 ─▶ Layer 1 安全网关 (realpath/白名单/控制字符)          │
                       │        └ REJECT ─▶ refused（+ 替代步骤，不生成命令）          │
                       │  ③ ─▶ Layer 2 规则命中? ── 是 ─▶ Layer 4 模板填充            │
                       │  ④ 否 ─▶ 候选收缩（规则/词典，≤1ms）→ top-K (K=8)            │
                       │        └ K==0 → 复杂度预算判定 → refused / 提示拆解          │
                       │  ⑤ ─▶ Choice（GBNF，≤16 token）→ capability_id + p           │
                       │        └ p < 阈值 → 列 top-3 候选，不生成命令               │
                       │  ⑥ ─▶ Noul（规则优先）→ allow / confirm / deny               │
                       │        （模型只能把 allow 抬升为 confirm，不能反向）          │
                       │  ⑦ ─▶ Layer 4 模板填充 → 快照(如需) → 执行 → 审计            │
                       └──────────────────────────────────────────┘
                                        │
                       ┌────────────────┴───────────────┐
                       ▼                                ▼
            能力注册表 capabilities.jsonl        审计 audit.jsonl (0600)
            （0600, flock, 原子替换）           （结构化，可 replay）
```

### 新增模块

| 模块 | 职责 | 关键接口 |
|---|---|---|
| `libnlc/capability_scan.c` | 白名单目录扫描、`lstat` 校验、systemd 发现（可选） | `nl_cap_scan(opts, out)` |
| `libnlc/capability_registry.c` | 读写 JSONL、`format_version` 校验、并发锁、增量判定、ID 查询 | `nl_cap_load()`, `nl_cap_save_atomic()`, `nl_cap_find(id)` |
| `libnlc/decision.c` | 候选收缩、GBNF 构造、Choice/Score/Noul、降级 | `nl_decide(input, K, out)` |
| `libnlc/freeze.c` | `.tools` 头解析、逐行链式 hash、固化生成 | `nl_freeze_write()`, `nl_freeze_verify()` |
| `libnlc/snapshot.c` | 快照写入、敏感排除、LRU、撤销 | `nl_snapshot_take()`, `nl_snapshot_undo(n)` |
| `libnlc/audit.c` | 结构化审计 JSONL（与 `nl-perm/audit_log.c` 的 syslog 并存，职责不同、不互相替代） | `nl_audit_emit(rec)` |

---

## 3. 关键状态机

### 3.1 输入解析状态

```
IDLE → FAST_PATH? ──yes──▶ EXECUTE(fast)
     └no──▶ GATEWAY ──reject──▶ REFUSED(reason, alternatives)
              └pass──▶ RULE_HIT? ──yes──▶ FILL → PERM → EXECUTE
                        └no──▶ CANDIDATES(K)
                                 ├ K==0 → BUDGET → REFUSED | ASK_DECOMPOSE
                                 └ K≥1  → CHOICE
                                            ├ p<low  → AMBIGUOUS(top3, 不执行)
                                            ├ p≥high → NOUL
                                            └ 其余   → NOUL(confirm)
                                                       └ deny → REFUSED
                                                       └ allow/confirm → FILL → EXECUTE → SNAPSHOT?
```

### 3.2 扫描状态

```
NOT_SCANNED → SCAN_DIRS(逐个白名单目录, lstat)
            → SELF_TEST(只读, 退出码探测) → WRITE(tmp→rename, flock) → READY
            └ 任一目录不可读 → 记 warning 继续（不整体失败）
            └ 锁等待超时 → 放弃写，沿用旧注册表（不阻塞启动）
```

### 3.3 撤销状态

```
UNDO(1) → 定位最近快照 → 存在? ──no──▶ 报"不可撤销"(可能已淘汰)
                          └yes──▶ 校验目标未被外部修改? ──yes──▶ 恢复
                                                        └no───▶ 拒绝并提示冲突
```

---

## 4. ADR 索引

| ADR | 标题 | 状态 |
|---|---|---|
| 001–010 | 一期：四层管线/权限/`.tools`/模型选型/内核路线/依赖/模块边界/Agent/测试/C11 | 有效（008 被 013 推翻） |
| 011 | 放弃 `.nl` 完整语言 | accepted |
| **012** | 能力注册表与自发现 | accepted（本次） |
| **013** | 类型化决策层（Choice/Score/Noul）替代 Agent Loop | accepted（本次） |
| **014** | 固化与快照格式 | accepted（本次） |

---

## 5. 风险

| # | 风险 | 类型 | 缓解 |
|---|---|---|---|
| R1 | 三段耦合返工 | 交付 | 段 1 格式冻结 + 独立 verify 后才开段 2（G1 条件） |
| R2 | 粗筛即规则，成为新瓶颈 | 产品 | benchmark 单独统计粗筛命中率（含"粗筛失败但模型可救"样本） |
| R3 | 内置 SHA-256 无密钥，防不住恶意重算 | 安全 | 明示边界；靠 0600 + 目录属主 + 审计；签名进 v2 |
| R4 | 8MB 上限导致大文件不可撤销 | 体验 | 确认提示中明写"不可撤销" |
| R5 | 权限映射表不全 → 大量能力默认 L4 | 体验 | `--scan --self-test` 报告引导批量放行；提供 `nlsh --allow <id>` 显式操作 |
| R6 | 1000 项 JSONL 流式解析内存峰值 | 性能 | 分块解析 + 只保留 id/name/perm/slots 常驻，路径按需查 |
| R7 | GBNF 每轮动态构造开销 | 性能 | 按候选集哈希缓存语法；实测 >5ms 则改为常驻 K 槽语法 |
| R8 | 中文槽位/能力名标注质量差 | 质量 | 标注脚本 + 人工 review；benchmark 里单独统计"槽位错"与"能力选错" |

---

## 6. 不在范围

- Agent Loop / 多步自主规划（ADR-013 已推翻）
- 跨机器能力同步、远程执行、多机编排
- 第三方能力包插件化（v2）
- `.tools` 公私钥签名（v2，端侧无 PKI）
- 模型微调、云端调用、GUI/语音/多用户

---

## 7. 🛡️ G2 方案门投票记录（2026-10-25，三轮讨论后）

```
🗳️ G2 方案门: DESIGN.md + ADR-012/013/014 是否通过？

   🟫 高级产品经理: ❌ 反对。D13 的粗筛靠规则，等于把"规则覆盖 80%"的老问题换个位置；
                     若粗筛没把正确能力放进 top-8，模型再准也没用。条件（若通过）：
                     benchmark 必须单独统计"粗筛命中率"与"粗筛漏救率"，且 v1 不得宣称
                     "自然语言理解能力强"，只允许宣称"在已注册能力集内可靠"。

   🟦 资深用户评测员: ✅ D15（模型只能多要确认）会让用户多按几次确认，但可接受；
                     AC-5 的替代步骤改由"能力→替代能力"映射表产出，可做到可直接复制执行。

   🟩 架构师: ✅ 支持。两阶段决策 + 格式版本冻结 + 原子写解决了前两轮的核心风险。
                     要求：R7（GBNF 构造开销）必须在段 2 开工前实测，超过 5ms 需改常驻语法。

   🔴 安全审计师: ✅ 条件通过。承认 R3（无密钥校验和）无法消除，只能明示；
                     要求 ① 快照/索引 0600 在实现中作为断言而非注释；② 内置 SHA-256 必须有
                     NIST 向量测试；③ `nl_audit_emit` 不得写入明文参数（含路径中的用户名可保留）。

   结果: 3/4 → 多数通过 ✅（记录 PM 反对意见与三方条件 → 进入 TASK）
```

### 带入 3-task 的强制条件

1. 段 1 注册表格式冻结 + 独立 verify 后才拆段 2 任务（G1 条件）
2. 段 2 开工前先实测 GBNF 构造开销（架构师条件）
3. benchmark 单独统计粗筛命中率 / 粗筛漏救率（PM 条件）
4. 0600 作为实现断言；SHA-256 必须有 NIST 测试向量；审计不含明文敏感参数（安全条件）
