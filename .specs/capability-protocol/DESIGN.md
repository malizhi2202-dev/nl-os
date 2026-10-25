# DESIGN: 统一能力调用协议（CIP v1）+ 本地 broker

- **Change ID**: `capability-protocol`
- **关联**: `@.specs/capability-protocol/{CHANGE,REQUIREMENT}.md`、`@.specs/discovery/*`、`@.specs/adr/015-device-tiers.md`
- **作者**: AI（Architect）+ 四角色专家团三轮讨论

---

## 0. 技术栈选定

**本次仍不引入任何新外部依赖**（ADR-006 维持 ≤3）。

- 语言 C11 / CMake ≥3.16 / 既有 cJSON / 系统 llama.cpp（推理可选）
- 传输：Unix domain socket（`AF_UNIX` + `SOCK_STREAM`），JSON Lines
- 执行：`fork` + `execvp`（**argv 数组，不经 shell**）
- 外部能力桥接：子进程调用系统已有工具（`busctl`/`gdbus`、MCP server 子进程）
- **明确不做**：libdbus、libcurl、TLS、eBPF、容器

## 0.5 既有架构对齐（brownfield 必填）

| 既有对象 | 本次处理 |
|---|---|
| ADR-001 四层管线 | 不变；broker 复用 Layer 1 安全网关与 Layer 4 模板填充产出 `plan` |
| ADR-007 模块边界 | 新增 `src/nlbroker/`（进程）与 `libnlc/cip.c`（协议编解码）；`nlsh` 改为协议客户端 |
| ADR-012 能力注册表 | `format_version` 升 2：新增 `namespace` / `effect` / `reversible`；T08 冻结以此为准 |
| ADR-013 类型化决策层 | 决策层成为 broker 内"裁决器"的一种实现；协议层不假设决策方式 |
| ADR-015 设备分档 | broker 常驻 ≤80MB 以满足 A1 档；模型后端按档位选择，broker 不强制带模型 |

---

## 1. 外部方案取舍矩阵（本次调研的收敛结果）

> 这是本方案的核心：调研到的每个东西，明确"采用什么 / 不采用什么 / 为什么"。

| 调研对象 | 它解决什么 | ✅ 采用 | ❌ 不采用 | 理由 |
|---|---|---|---|---|
| **MCP**（协议 + 工具注册） | 跨工具的统一调用格式 | 工具描述的字段形态（name/description/input schema）、JSON-RPC 传输、**门面互操作** | 无裁决语义、假设调用方可信、假设有运行时 | 我们要的正好是 MCP 缺的那半；替代不如包装 |
| **Belay**（本地确定性防火墙） | 执行前 allow/ask/deny | 裁决前置、JSONL 审计、**CI 轨迹回放**的思路、拒绝要可读的理由 | Python/YAML 运行时、**字符串级匹配** | 端侧装不了 Python；字符串匹配可被 `eval $(...)` 绕过 |
| **Pipelock** | 出口代理 + OS 沙箱 | 纵深防御的分层意识（协议层裁决 ≠ 隔离边界） | 代理 + 容器依赖 | 端侧无容器；本方案明确"不是沙箱" |
| **StrongDM Leash** | eBPF LSM 内核级强制 | 承认"内核隔离不可替代"这一点 | eBPF/内核模块 | 需要内核版本与 root，超出端侧假设；留作 v3 |
| **Docker MCP Gateway / Lasso** | 工具过滤、密钥脱敏 | 工具过滤、**门面暴露范围收敛** | 容器化 | 同上 |
| **Jev**（System One 决策模型） | 有限答案空间的类型化决策 | **Choice / Score / Noul 三原语**、概率参与控制流、政策留在代码 | SaaS 依赖、把 confidence 当准确率 | 原语本身是可靠的架构抽象 |
| **UI-TARS / GUI-Owl / AutoGLM** | 手机 UI 自动化 | 仅 `embedding` 意图分类（可移植到 NDK） | 视觉执行面 | 另一条产品线（见 discovery） |
| **llama.cpp**（生成 + embedding 两种模式） | 端侧推理 | 统一推理后端；`n_batch`/前缀缓存调优；GBNF 约束解码 | 端到端生成命令文本 | 与 ADR-013 一致：模型只判断 |
| **llama-bench / 相关 issue** | 性能基线 | **先测再谈优化**的方法（pp/tg 两个数） | — | `n_batch=1` 是实测可证的瓶颈 |
| **NL2SH / ShellWhisperer / EasyCommand** | 英文命令生成模型 | 未来可选的后端之一 | 作为 v1 依赖 | 中文能力未知，v1 不押注 |

### 关键结论（三条）

1. **不重造策略语言**：Belay 用 YAML、Cupcake 用 Rego、Leash 用 Cedar——都在为"通用策略表达力"付出解释器代价。本项目只有有限策略（权限等级 + 路径白名单 + effect 从严），**用编译期表 + 运行期规则**即可，不引入解释器。
2. **不与 MCP 竞争，做它的上游**：CIP 把 MCP 工具吸收为 `mcp:` 命名空间；同时把本地能力以 MCP 门面暴露出去。两边都受益。
3. **胜负手是"plan 用 argv 数组"**：Belay 类方案在命令字符串上匹配，`eval $(echo ...|base64 -d)` 可绕过（其 README 自认）。CIP 的 `plan` 是 `argv[]`，**从不经过 shell**——注入面在协议层就被消除。

---

## 2. 我们自己的方案（分层）

```
        Claude Code / Cursor          nlsh / nl-run
              │ MCP (stdio)                │ CIP (unix socket)
              ▼                            ▼
        ┌───────────────────────────────────────────┐
        │  nlbroker（独立进程，systemd 托管）          │
        │  ┌─────────────────────────────────────┐  │
        │  │ 协议层  cip.c：版本/报文/错误码        │  │
        │  ├─────────────────────────────────────┤  │
        │  │ 裁决层  verdict = f(cap, args)       │  │
        │  │  · effect 从严（缺省 delete）         │  │
        │  │  · L1–L4 权限映射                    │  │
        │  │  · 复杂度预算 / 能力存在性            │  │
        │  │  · ticket 签发与校验                 │  │
        │  ├─────────────────────────────────────┤  │
        │  │ 能力层  registry（format_version=2） │  │
        │  │  id / namespace / perm / effect /    │  │
        │  │  reversible / slots / timeout        │  │
        │  ├─────────────────────────────────────┤  │
        │  │ 适配层  cli | dbus | mcp | http      │  │
        │  └─────────────────────────────────────┘  │
        │  审计 JSONL（0600）· 快照 · ticket 表      │
        └───────────────────────────────────────────┘
```

**职责边界**：模型永远在 broker 之外；broker 不接受"要执行什么命令"，只接受"要调用哪个能力 + 参数"；`plan` 由 broker 生成。

---

## 3. CIP v1 契约

### 3.1 通用信封

```json
{"v":1,"id":"<客户端自增>","op":"discover|describe|invoke|commit|audit"}
```
响应：`{"v":1,"id":"...","ok":true|false,"error":"<code>?","result":<payload>?}`

> **字段约定（T01 实现时澄清）**：信封 `id` 是**客户端请求 id**；能力 ID 用独立字段 `capability`。
> 早期草案把两者都写成 `id`，会产生歧义，已更正。

版本协商：`v` 不匹配 → `E_VERSION`；broker 支持 `v=1` 的最小兼容集。

### 3.2 五条报文

```json
{"op":"discover","id":"r0","prefix":"dbus:","limit":100,"cursor":null}
→ {"ok":true,"tier":"A","capabilities":[
     {"id":"cli:ls","name":"ls","perm":"L1","effect":"read","reversible":true}],
   "cursor":"<opaque>"}

{"op":"describe","id":"r1","ids":["cli:rm"]}
→ {"ok":true,"capabilities":[{"id":"cli:rm","slots":[{"name":"paths","type":"path[]"}],
     "effect":"delete","reversible":false,"timeout_ms":5000,"cost":"fast",
     "effect_defaulted":true}]}

{"op":"invoke","id":"r2","capability":"cli:mkdir","args":{"paths":["/tmp/x"]},"dry_run":false}
→ {"ok":true,"verdict":"confirm","reason":"L2 write","plan":{"argv":["mkdir","-p","/tmp/x"]},
   "ticket":"<128bit>","ttl_ms":60000}
→ {"ok":true,"verdict":"deny","reason":"protected path /etc","plan":null,
   "alternatives":["cli:ls"]}

{"op":"commit","id":"r3","ticket":"<...>"}
→ {"ok":true,"status":"ok","exit":0,"output":"","snapshot_ref":null,"audit_id":"a-..."}

{"op":"audit","id":"r4","since":1699999999,"limit":50}
→ {"ok":true,"records":[{"audit_id":"a-..","ts":..,"capability_id":"cli:mkdir",
    "verdict":"confirm","effect":"write","duration_us":812,"reason":"L2 write"}]}
```

### 3.3 错误码

| code | 含义 |
|---|---|
| `E_VERSION` | 协议版本不支持 |
| `E_BAD_REQ` | 报文非法 |
| `E_CAP_UNKNOWN` | 能力不存在 |
| `E_SCHEME_UNSUPPORTED` | 执行面不支持该形式（如 https） |
| `E_POLICY_DENY` | 裁决拒绝 |
| `E_TICKET_INVALID` | ticket 不存在/伪造 |
| `E_TICKET_EXPIRED` | ticket 过期 |
| `E_TICKET_MISMATCH` | 参数与 invoke 时不一致（含重放） |
| `E_EXEC_FAIL` | 执行失败 |

### 3.4 ticket 规则

- 128bit CSPRNG；一次性；TTL 默认 60s（可配）
- 内容：`{capability_id, args_hash(SHA-256), effect, created_at, nonce}`
- `commit` 校验三项：存在、未过期、`args_hash` 一致；任一不符即拒并记审计
- 已提交的 nonce 进入短时黑名单（防重放）

---

## 4. 威胁模型与安全边界（ADR-016 强制项）

**信任假设**：**模型 / agent 不可信；系统用户（属主、root）可信。**
→ 由此推出：注册表文件（0600 + 属主）被认为可信；模型经协议传入的一切字段（含 `effect`）**一律不采信**。

| 攻击面 | 防护 |
|---|---|
| 模型自报 `effect=read` | 忽略请求中的 effect；只认注册表（AC-5） |
| 命令注入（`;` `|` `$(...)`） | **plan 为 argv 数组，不经 shell** |
| 重放/篡改 ticket | 一次性 + TTL + args 哈希 |
| 路径穿越 / 保护路径 | 复用 Layer 1 `path_safety`（已在 `libnlc`） |
| MCP 客户端无确认能力 | 门面只暴露 `perm ≤ L2` |
| broker 不可用 | fail-closed：拒绝执行，但传统命令透传不受影响 |
| 审计被删改 | 追加写 + 0600；v1 不做防篡改链（v2 可加哈希链） |

**明确不是**：不是沙箱、不是隔离边界。真正的隔离仍需容器/内核机制——本方案只保证"经协议的动作被裁决过"。

---

## 5. 关键决策

| # | 决策 | 备选 | 理由 | 代价 |
|---|---|---|---|---|
| DEC-1 | CIP 五报文、JSON Lines over Unix socket | HTTP/2 gRPC、CBOR | 端侧零依赖、可 `nc -U` 调试 | 无类型生成、需手写编解码 |
| DEC-2 | `invoke`/`commit` 两阶段 + ticket | 单阶段、回调 | 确认环节进协议，客户端无法跳过 | 客户端多一次往返 |
| DEC-3 | `plan` 用 argv 数组 | 命令字符串 | 消除 shell 注入面（对比 Belay 的已知上限） | 不支持管道/重定向，需多能力组合表达 |
| DEC-4 | effect 五类 + 缺省 `delete` | 由调用方声明、缺省 read | 从严缺省；调用方不可信 | 新增能力要显式标注，否则一律高危 |
| DEC-5 | 四执行面用子进程桥 / 最小实现 | libdbus/libcurl | 维持 ADR-006 依赖上限 | 每次多 ~10–15ms |
| DEC-6 | MCP 双向（吸收 + 门面） | 只做门面 / 只吸收 | 既白得生态，又不被 MCP 的权限模型限制 | 两套错误码需要映射 |
| DEC-7 | 不做策略语言，用编译期表 + 运行期规则 | YAML/Rego/Cedar | 策略空间有限，解释器是纯负担 | 表达力上限低，复杂策略需改代码 |
| DEC-8 | broker 独立进程，fail-closed | 库 / 并入 nl-perm | 崩溃隔离、可被 systemd 托管 | 多一个进程与 socket |

## 6. 落地路线（详细任务见 `TASK.md`）

| 波次 | 内容 | 出口 |
|---|---|---|
| W1 | `cip.c` 编解码 + broker 骨架 + socket + 版本/错误码 | 能收发 `discover`（返回空表） |
| W2 | 注册表 v2（namespace/effect/reversible）+ `cli` 适配器 + 裁决 + ticket + 审计 | **端到端可用**：invoke→confirm→commit 跑通（AC-1~5, AC-9） |
| W3 | `dbus` 适配器（子进程桥） | AC-7 |
| W4 | `mcp` 适配器 + MCP 门面 | AC-6 |
| W5 | `http` 适配器（beta）+ 集成 + 对抗测试 + 文档 | AC-8, AC-10 与全量回归 |

**依赖顺序**：W2 是终点也是起点——先把 `cli` 一条打通，其余三个适配器是它的同构复制。

## 7. 未采用清单与复评条件

| 未采用 | 复评条件 |
|---|---|
| TLS / https 直连 | 出现真实需求，且愿意修订 ADR-006 引入 libcurl |
| eBPF / 内核强制 | 目标设备内核 ≥5.10 且允许 root |
| 策略语言（YAML/Rego） | 出现"不改代码就能改策略"的真实需求 |
| IPC 原生绑定（libdbus） | 子进程桥开销被实测证明是瓶颈 |
| 视觉执行面 | 只在独立产品线（手机）考虑 |

## 8. 风险

| # | 风险 | 缓解 |
|---|---|---|
| R1 | 四适配器范围大 | 按 W2–W5 串行；`cli` 先闭环 |
| R2 | ticket 表在内存，broker 重启即失效 | 重启后所有 ticket 作废（安全侧可接受），客户端需重新 invoke |
| R3 | 子进程桥的参数需转义（busctl/http） | 一律 argv 传参，不拼 shell；对 `busctl` 的参数做白名单校验 |
| R4 | MCP 门面工具名冲突 | 统一前缀 `nl_`，并在 list 中标注来源命名空间 |
| R5 | 注册表 v2 升级影响已暂停的段 1 | T08 按 v2 冻结；解析器需兼容 v1（读旧文件时按缺省 effect=delete 处理） |
| R6 | 裁决在 broker 内联，单个慢能力会阻塞 | v1 单线程 + 每次 invoke 有 `timeout_ms`；并发在 v2 |

---

## 9. 🛡️ G2 方案门投票记录（2026-10-25，三轮）

**第 1 轮**：交叉核对"我们与 Belay/Pipelock 的差异是否真的成立"。结论成立的三条：① argv vs 命令字符串（结构性）；② 端侧零运行时（形态）；③ 裁决进协议（客户端不可跳过）。不成立的一条（已从对外表述中删除）："别人没做安全执行层"。

**第 2 轮**：`cli:` 走 `fork/execvp` 是否也算"子进程桥税"？澄清：CLI 本来就要一个进程，**没有额外税**；只有 `dbus:` / `http:` 需多付一次工具进程（10–15ms）。DEC-2 两阶段对用户是否可见？澄清：`nlsh` 内部封装，用户只看到一次确认。

**第 3 轮**：安全 → 架构：ticket 表在内存、无上限，反复 `invoke` 不 `commit` 可导致内存增长（本地 DoS）→ 新增约束：ticket 表上限 1024 + LRU 淘汰 + 每会话限速。架构 → PM：W1–W5 是否过粗 → 由 TASK 细化到可 2–10 分钟完成的粒度。

```
🟫 高级产品经理:        ✅ 取舍矩阵把"采用/不采用/理由"写死了，避免后续反复
🟦 资深用户评测员:      ✅ 两阶段对用户透明；https 有明确替代路径
🟩 架构师:             ✅ 分层与职责边界清晰；与 MCP 的包装关系成立
🔴 安全审计师:          ❌ 反对：ticket 表无上限（内存 DoS）。
                          条件：上限 1024 + LRU + 每会话 invoke 限速，且要有测试

结果: 3/4 → 多数通过 ✅（记录安全反对意见，作为进入 TASK 的前置条件）
```

### 带入 TASK 的强制条件

1. ticket 表上限 1024 + LRU + 每会话限速，并有测试（安全）
2. AC-3 / AC-5 / AC-10 的验证方式按需求门条件实现（架构 + 安全）
3. W1–W5 必须细化为 2–10 分钟粒度的原子任务，每个含可执行 `verify`（架构 + PM）
