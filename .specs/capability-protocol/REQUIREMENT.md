# REQUIREMENT: 统一能力调用协议（CIP v1）+ 本地 broker

- **Change ID**: `capability-protocol`
- **关联**: `@.specs/capability-protocol/CHANGE.md`、`@.specs/CONTEXT.md`、`@.specs/discovery/claim-verification.md`

---

## 用户故事

- **US-1**：作为应用开发者，我想让 Claude Code / Cursor 通过 MCP 调用我的本机能力，以便不必重写客户端就能用上这些能力。
- **US-2**：作为安全责任人，我想所有写/删/特权操作在执行前被裁决，以便模型不能直接毁掉机器。
- **US-3**：作为框架使用者，我想同一套协议调用 CLI / DBus / MCP / HTTP 四类能力，以便不用为每种写一套集成。
- **US-4**：作为排障/合规人员，我想回放每一次调用与裁决，以便复现问题或举证。
- **US-5**：作为端侧用户，我想在 ≤4GB（下沿 512MB）设备上跑同一套东西，以便同一框架覆盖从路由器到迷你主机。

## 验收准则（AC）

### AC-1 · discover 分页与命名空间

- **Given** broker 已加载四个命名空间的能力
- **When** 发送 `{"op":"discover","limit":10}`
- **Then** 返回 ≤10 条，每条含 `id`（含命名空间前缀）、`perm`、`effect`、`reversible`；带 `cursor` 可继续翻页；`discover {prefix:"dbus:"}` 只返回 DBus 能力
- **验证方式**: `echo '{"op":"discover","limit":10}' | nc -U /run/nl-os/broker.sock`，断言条数与 `cursor` 字段

### AC-2 · describe 元数据完整，缺省从严

- **Given** 能力 `cli:rm` 未在映射表中标注 `effect`
- **When** 发送 `{"op":"describe","ids":["cli:rm"]}`
- **Then** 返回 `effect:"delete"`（**未标注一律按最严**）、`reversible:false`、`slots[]`、`timeout_ms`；响应中有 `effect_defaulted:true` 标记
- **验证方式**: 同上，断言 `effect=="delete"` 且 `effect_defaulted==true`

### AC-3 · invoke 只裁决，不执行

- **Given** 一个危险请求 `{"op":"invoke","id":"r1","capability":"cli:rm","args":{"paths":["/etc"]}}`
- **When** broker 收到该请求
- **Then** 返回 `verdict:"deny"`、`reason` 命中规则、**响应中不出现可执行的 `plan`**；且用 `strace -f -e trace=execve` 观察 broker，**无任何 `execve` 发生**
- **验证方式**: `strace -f -e trace=execve -p $(pidof nlbroker) &` + 发送请求，断言无 execve 且 verdict=deny

### AC-4 · confirm → ticket → commit

- **Given** 一个 L2 写操作（`cli:mkdir`），`invoke` 返回 `verdict:"confirm"` 与 `ticket`
- **When** 客户端带该 ticket 发 `commit`
- **Then** 执行成功且返回 `audit_id`；**无 ticket / 伪造 ticket / 超过 TTL / 参数与 invoke 时不符** 四种情况一律拒绝，且拒绝原因可区分
- **验证方式**: 脚本跑 5 个分支（正常 + 4 种非法），断言返回码分别为 `ok / E_TICKET_INVALID / E_TICKET_EXPIRED / E_TICKET_REPLAY(参数不符)`

### AC-5 · 模型自报 effect 无效（对抗用例）

- **Given** 调用方在 `invoke` 中额外传 `{"effect":"read"}`，试图把删除操作伪装成只读
- **When** broker 处理该请求
- **Then** **忽略请求中的 effect 字段**，仍按注册表判定 `delete` 并 `deny`；审计中记录 `untrusted_effect_attempt:true`
- **验证方式**: 构造该请求，断言 verdict=deny 且审计字段为 true

### AC-6 · MCP 门面只暴露低危能力

- **Given** MCP 门面启动
- **When** MCP 客户端发送 `tools/list`
- **Then** 列表**只含 `perm ≤ L2`** 的能力；对 `perm ≥ L3` 的能力发 `tools/call` 一律返回错误并写审计
- **验证方式**: MCP 客户端（或手写 JSON-RPC）分别 list 与 call 一个 L3 能力，断言前者不含、后者被拒

### AC-7 · DBus 适配器（子进程桥）

- **Given** 本机存在可用的 system bus 服务（如 `org.freedesktop.DBus` 的只读方法）
- **When** 调用 `dbus:org.freedesktop.DBus|ListNames`
- **Then** 通过 `busctl`/`gdbus` 子进程完成调用并返回结果；**不引入 libdbus**（`ldd` 输出中无 `libdbus`）
- **验证方式**: `ldd build/nlbroker | grep -c dbus` 应为 0；功能调用成功

### AC-8 · HTTP 适配器仅 http，https 明确报错

- **Given** 能力 `http:GET` 指向 `http://127.0.0.1:PORT/health`
- **When** 调用该能力
- **Then** 返回响应体与状态码；**当 URL 是 `https://` 时**返回 `E_SCHEME_UNSUPPORTED`，并在 `alternatives` 中建议 `cli:curl`
- **验证方式**: 本地起一个 http server + 构造 https URL，分别断言成功与错误码

### AC-9 · 审计可回放

- **Given** 一段时间内的调用
- **When** 发送 `{"op":"audit","since":<ts>}`
- **Then** 返回每条记录的 `audit_id / ts / capability_id / verdict / effect / duration_us / reason`；`invoke` 与 `commit` 各一条、可用 `audit_id` 关联；不含明文敏感参数
- **验证方式**: `python3 -c 'json.loads(...)'` 校验字段齐全，并 grep 确认无原始路径参数

### AC-10 · 端侧资源占用

- **Given** A1 档设备（模拟：限制 RSS 512MB）
- **When** broker 启动并加载注册表（不含模型）
- **Then** 常驻 RSS ≤ 80MB，冷启动 ≤ 100ms（注册表 1000 项）
- **验证方式**: `/usr/bin/time -v build/nlbroker --dry-run` 读 `Maximum resident set size`；`time` 读启动耗时

---

## 范围切分

### v1（本次必做）

- CIP v1 五条报文 + 版本协商 + 错误码
- broker 独立进程 + Unix socket（0600）
- 四个适配器：`cli`（完整）/ `dbus`（子进程桥）/ `mcp`（stdio JSON-RPC 子进程）/ `http`（**beta，http:// only**）
- 裁决：effect 从严缺省、权限分级、ticket 两阶段、fail-closed
- MCP 门面（仅 `perm ≤ L2`）
- JSONL 审计 + `audit` 查询

### v2（下一轮考虑，本次不做）

- 原生 libdbus 绑定（消除子进程开销）
- 原生 TLS / libcurl（https 直连；需修订 ADR-006）
- 远程 broker（TCP + 双向认证）
- 能力热插拔（无需重启 broker）
- `describe` 批量 + 能力变更订阅（subscribe）

### out（永远不做）

- Android UI / 视觉执行面
- Windows / macOS 执行面
- 云端模型调用与遥测
- 多用户鉴权体系（v1 依赖文件权限与 socket 权限）
- 以 YAML/Rego 作为策略语言（本项目的策略是编译期表 + 运行期规则，不引入解释器）

---

## 非功能性需求

- **性能**：`invoke` 裁决本身 ≤ 5ms（不含执行）；`discover` 100 条 ≤ 10ms；子进程桥（dbus/http）额外 ≤ 15ms
- **内存**：broker 常驻 ≤ 80MB（不含模型），符合 ADR-015 A1 档
- **安全**：socket 权限 0600；ticket 一次性 + TTL 60s + 绑定参数哈希；`plan` 用 **argv 数组**而非命令字符串（消除 shell 注入面）；MCP 门面 L2 上限
- **兼容性**：glibc ≥ 2.28 与 musl（OpenWrt）；x86_64 / ARM64
- **可观测性**：JSONL 审计，字段固定；不写明文敏感参数
- **可访问性**：无 GUI，不涉及

## 依赖与假设

- 依赖：系统 llama.cpp（推理，可选）、cJSON、POSIX（socket/fork/exec）
- 假设：`busctl` 或 `gdbus` 至少存在其一（否则 DBus 适配器不可用，降级为"该命名空间为空"）
- 假设：MCP 客户端通过 stdio 启动门面（不假设它有 UI 做 L3 确认）
- **待确认**：D-Bus 方法调用的超时与信号语义在子进程桥下如何映射（DESIGN 需定）

---

> AC 是 TEST 阶段派生用例的唯一来源。

---

## 🛡️ 需求门投票记录（2026-10-25，三轮）

**第 1 轮**：PM 质疑 AC-10（端侧资源）是否该进 v1 → 结论：进，因为 ADR-015 已把 A1 档定为支持范围，资源是硬指标。用户评测员质疑 ticket TTL 60s 会不会打断用户 → 澄清：ttl 只覆盖"invoke 到用户按确认"的间隔，`nlsh` 内部会重新 invoke，不暴露给用户。安全审计师要求 AC-5 必须有可执行攻击脚本，而不是描述。

**第 2 轮**：AC-8 的 `alternatives` 必须是可直接复制执行的（继承此前用户评测员条件）。AC-10 的 80MB 在 x86 开发机上无法直接验证 → 改为"在 512MB cgroup 限制下运行并通过 `VmHWM` 读取"。

**第 3 轮**：安全 → 架构：AC-3 用 `strace` 验证不可移植（需 ptrace 权限，容器内常被禁）→ 改为两条可移植断言：① 响应中 `plan` 字段存在但 `executed:false`；② 审计中该 `invoke` 无对应 `commit` 记录，且 `/proc/<pid>/stat` 的 `utime` 无显著增长。架构 → PM：10 条 AC 可控。

```
🟫 高级产品经理:        ✅ 覆盖四个执行面与两个门面；v1/v2/out 切分合理
🟦 资深用户评测员:      ✅ AC 均为可感知外部行为；拒绝路径有 alternatives（可执行）
🟩 架构师:             ❌ 反对 AC-10 的验证方式（x86 开发机无法代表 A1 档）。
                          条件：改为 cgroup 限制 + VmHWM；并在 TASK 里标注"需目标档硬件复测"
🔴 安全审计师:          ✅ 条件通过：AC-5 的对抗脚本必须入 TEST 矩阵；ticket 四条非法分支都要有断言

结果: 3/4 → 多数通过 ✅（记录架构师反对意见与条件，进入 DESIGN）
```

### 带入 DESIGN 的条件

1. AC-10 改为 cgroup 512MB + `VmHWM` 验证，并在 TASK 标注目标硬件复测（架构师）
2. AC-3 改为两条可移植断言，不依赖 `strace`（第三轮结论）
3. AC-5 对抗脚本必须进 TEST 矩阵（安全）
