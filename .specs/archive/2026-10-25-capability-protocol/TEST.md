# TEST: 统一能力调用协议（CIP v1）+ 本地 broker

- **Change ID**: `capability-protocol`
- **关联**: `@.specs/archive/2026-10-25-capability-protocol/{REQUIREMENT,DESIGN,TASK}.md`
- **测试基线**: `ctest 20/20`、`scripts/verify_all.sh`（AC-1~AC-10 全绿）、`build-cov`（`--coverage`）

---

## 0. 本次测试范围声明（5 轮金字塔）

| 轮次 | 状态 | 范围 | 理由 |
|---|---|---|---|
| 第 1 轮 · 功能 | ✅ 必跑 | AC-1~AC-10 全部 | 30 个断言 + 20 个 ctest |
| 第 2 轮 · 性能 | ✅ 必跑 | invoke / discover 延迟 + broker RSS | REQUIREMENT 有明确预算 |
| 第 3 轮 · 安全 | ⚠️ 部分 | 秘钥扫描 + OWASP 映射 + 对抗用例；**SAST 未跑** | 无 clang-analyzer/cppcheck 环境，记为缺口 |
| 第 4 轮 · 兼容 | ⚠️ 部分 | x86_64 + glibc 实测；**ARM64/musl/OpenWrt 未验** | 本机无交叉编译链与 ARM 设备 |
| 第 5 轮 · 可观测 | ⚠️ 部分 | 审计 JSONL 全量验证；**无指标/追踪** | CLI+daemon，无 metrics 端点（DESIGN 未要求） |

---

## 第 1 轮 · 功能测试

### 1.1 测试矩阵（AC → 用例）

| AC | 类型 | 用例 / 脚本 | 状态 |
|---|---|---|---|
| AC-1 discover 分页与命名空间 | integration | `verify_all.sh`（limit/cursor/prefix） + `cip` 单元 | ✅ |
| AC-2 describe 缺省从严 | unit + integration | `capability_registry_test`（v1 缺省）+ `verify_all.sh` | ✅ |
| AC-3 invoke 只裁决不执行 | integration | `verify_all.sh`（plan 为空 + 审计无 commit） | ✅ |
| AC-4 confirm→ticket→commit | unit + integration | `ticket_test` 8 例 + `verify_all.sh` 6 分支 | ✅ |
| AC-5 模型自报 effect 无效 | unit + integration | `verdict_test` + `verify_all.sh` 对抗用例 | ✅ |
| AC-6 MCP 门面 L2 上限 | unit | `facade_mcp_test` 5 例 | ✅ |
| AC-7 DBus 子进程桥 | unit + integration | `adapter_dbus_test`（含真实总线） | ✅ |
| AC-8 http only / https 拒绝 | unit + integration | `adapter_http_test` 4 例 + `verify_all.sh` | ✅ |
| AC-9 审计可回放 | unit + integration | `audit_test` 4 例 + `verify_all.sh` | ✅ |
| AC-10 端侧资源 | integration | `verify_all.sh`（Release + `ulimit -v`） | ✅ |

**覆盖完整度**：10/10 AC 有自动化覆盖，**无 UAT-only 项**。

### 1.2 UAT 脚本

无。所有 AC 均可自动化验证（这是本 change 的设计目标之一）。

### 1.3 覆盖率（`build-cov`，`--coverage`）

| 文件（本 change 新增） | 行覆盖 | 判定（门槛 80%） |
|---|---|---|
| `libnlc/sha256.c` | 100.00% | ✅ |
| `nlbroker/adapter_dbus.c` | 96.43% | ✅ |
| `nlbroker/adapter_http.c` | 94.23% | ✅ |
| `libnlc/audit.c` | 93.91% | ✅ |
| `nlbroker/ticket.c` | 88.16% | ✅ |
| `libnlc/capability_scan.c` | 86.57% | ✅ |
| `nlbroker/verdict.c` | 87.14% | ✅ |
| `libnlc/capability_registry.c` | 81.74% | ✅ |
| `nlbroker/adapter_mcp.c` | 81.99% | ✅ |
| `nlbroker/facade_mcp.c` | 77.13% | ⚠️ 略低 |
| `nlbroker/adapter_cli.c` | 77.78% | ⚠️ 略低 |
| `libnlc/cip.c` | **68.52%** | ❌ 不达标 |
| `nlbroker/dispatch.c` | **32.76%** | ❌ 不达标（详见下） |

**测量方法限制**：broker 侧模块被多个测试 `#include`，各测试二进制独立产生 `.gcda`；本表取**单文件最高值**，非跨二进制合并（本机无 gcovr/lcov）。

**两个真实缺口**：

1. **`cip.c` 68.5%**：错误码分支（`E_TICKET_*`、`E_SCHEME_*` 在 cip 层不可达）与 `describe` 空数组/超长 id 等边界未覆盖。
   → **T-FIX-01**
2. **`dispatch.c` 32.8%**：socket 路径（`discover` / `describe` / `audit`）只在 `verify_all.sh` 里被跑到，**不在 ctest 里**；单元侧只覆盖了 `invoke/commit`。
   → **T-FIX-02**

### 1.4 边界 / 错误路径用例

| 类别 | 用例 | 结果 |
|---|---|---|
| 非法 JSON / 空行 / 缺 id / 未知 op / v=2 | `cip_test` 5 例 | ✅ |
| ticket 伪造 / 过期 / 重放 / 参数篡改 / 表满淘汰 | `ticket_test` 5 例 | ✅ |
| shell 元字符（`;` `$()`） | `adapter_dbus_test` / `adapter_cli_test` | ✅ |
| 路径穿越 + 保护路径 + `~` 展开 | `verdict_test` + `verify_all.sh` | ✅ |
| https 拒绝 + 替代建议 | `adapter_http_test` + `verify_all.sh` | ✅ |
| 超长能力 ID（溢出风险） | dbus/mcp 构造侧显式长度检查（代码审查 + 单测间接覆盖） | ⚠️ 无直接用例 |

### 1.5 测试质量自检（6 维衰退风险）

| 维度 | 自查结论 |
|---|---|
| 过度 mock | 无 mock；全部真实子进程/真实 socket/真实 D-Bus（无 mock 导致的假绿） |
| 断言强度 | 断言**外部可观测行为**（响应字段、目录是否创建、审计记录），未断言内部调用 |
| 测试独立性 | 每个用例自建临时注册表/临时 state 目录；`ticket_test` 显式 `reset` |
| 环境依赖 | 依赖 `busctl`、`python3`、`/bin/echo` 等；缺失时 **SKIP 而非假通过**（已实测 SKIP 分支） |
| 速度 | 全量 `ctest` 约 2.1s，可每次提交跑 |
| 稳定性 | 均为确定性断言，无 sleep 轮询（唯一 `usleep(700ms)` 在 http 端口等待，失败转 SKIP） |

**发现并已修的测试缺陷**：`capability_scan` 的"不可读目录"用例曾用 `mkdir(0700)`，
属主自己可读 → 等于没测；改 `0000` + root 下 SKIP。已记入 `LESSONS.md`。

---

## 第 2 轮 · 性能测试

### 2.1 性能预算（来自 REQUIREMENT.md）

| 指标 | 预算 | 实测 | 结论 |
|---|---|---|---|
| `invoke` 裁决本身 | ≤ 5ms | **p50 0.21ms / p95 0.50ms / max 0.94ms** | ✅ 10 倍余量 |
| `discover` 100 条 | ≤ 10ms | **p50 0.07ms / p95 0.18ms**（50 条注册表） | ✅ |
| broker 常驻内存 | ≤ 80MB（不含模型） | **VmHWM 5.3MB**（Release） | ✅ 15 倍余量 |
| 子进程桥额外开销 | ≤ 15ms | 未单独测量（dbus/http 端到端含在网络/进程创建中） | ⚠️ 缺数据 |

**测量条件**：x86_64 / glibc，Release 构建，50 次 discover + 100 次 invoke，
Unix socket 往返计入（真实客户端视角）。

### 2.2 退步项处理

无基线可比（首次建立）。**本表即为后续基线**。

---

## 第 3 轮 · 安全测试

### 3.1 依赖漏洞

- 外部依赖：vendor `cJSON`（自实现子集）、系统 `llama.cpp`（本 change 未使用）
- **无包管理器**（无 npm/pip/go mod），不存在自动 CVE 扫描路径 → **记为缺口**（T-FIX-03 观察项）
- 依赖数量核对：`ldd build/nlbroker` 无 `libdbus`/`libcurl`/`libssl`（ADR-006 守住）

### 3.2 秘钥扫描

`grep -rniE '(api[_-]?key|secret|passwd|password|token)\s*[:=]' src/ etc/ scripts/` → 3 处命中，
**全部为 `strtok` 变量名 `token`**（`path_safety.c`），非密钥。✅

### 3.3 SAST

**未执行**（本机无 clang-analyzer / cppcheck）。补偿措施：
- `-Wall -Wextra -Wpedantic -Werror` 全量开启（Debug + Release 双构建均通过）
- Debug 构建带 `-fsanitize=address,undefined`，全部测试在 ASan 下跑通（含泄漏检测）

### 3.4 OWASP 映射（CLI/daemon 场景裁剪）

| 项 | 本项目的对应 | 状态 |
|---|---|---|
| 注入（命令） | `plan` 为 argv 数组、不经 shell；`';'` 字面量用例 | ✅ |
| 注入（DBus/HTTP 参数） | 服务/路径/方法白名单；URL 解析只允许 http:// | ✅ |
| 越权 | `effect` 只能由注册表给出；L4 deny-by-default | ✅ |
| 认证 | v1 单用户本地，socket 0600 | ✅（范围外：多用户） |
| 敏感数据 | 审计不落明文参数（只落 hash）；快照排除清单（段 3 未实现） | ✅ / ⚠️ |
| 重放 | ticket 一次性 + TTL + args 哈希 | ✅ |
| DoS | ticket 表 1024 + LRU；限速 100/s | ✅ |

---

## 第 4 轮 · 兼容性测试

| 项 | 状态 | 说明 |
|---|---|---|
| x86_64 + glibc | ✅ 实测 | 全部测试与 E2E |
| ARM64 / aarch64 | ❌ 未验证 | 本机无交叉编译链；`cmake/toolchain-arm64.cmake` 存在但未跑 |
| musl / OpenWrt | ❌ 未验证 | 同上；代码未使用 glibc 专有接口（除 `getline`/`strtok_r`/`strdup` 均为 POSIX） |
| 跨内核版本 | ❌ 未验证 | 未在 5.10 以下验证 |
| `.tools`/注册表向后兼容 | ✅ | v1 文件可读（`capability_registry_test`） |

**结论**：兼容性只覆盖了开发机平台。**这是本 change 最大的未验证面**，且直接影响 ADR-015 的 A1 档承诺。
→ **T-FIX-03**（需目标硬件/交叉编译环境，本机无法闭环）

---

## 第 5 轮 · 可观测性验证

| 项 | 状态 | 说明 |
|---|---|---|
| 结构化日志 | ✅ | 审计 JSONL 12 字段，`audit` 报文可查询 |
| 日志不含敏感数据 | ✅ | 单测 + E2E 双断言（`/etc/passwd` 不出现） |
| 拒绝可解释 | ✅ | 响应带 `reason`（人类可读）+ 审计带原因码 |
| 指标（metrics） | ❌ 无 | DESIGN 未要求；端侧无 Prometheus 需求 |
| 链路追踪 | ❌ 无 | 单进程 + 单跳，无分布式链路 |
| 告警 | ❌ 无 | 由 systemd `Restart=on-failure` 承担 |

---

## 新增测试登记

| 测试文件 | 用例数 | 覆盖对象 |
|---|---|---|
| `tests/unit/cip_test.c` | 7 | 协议编解码 |
| `tests/unit/capability_registry_test.c` | 8 | 注册表 v1/v2 |
| `tests/unit/verdict_test.c` | 6 | 裁决规则 + 对抗 |
| `tests/unit/ticket_test.c` | 8 | SHA-256 向量 + ticket 全生命周期 |
| `tests/unit/audit_test.c` | 4 | 审计写入/查询/脱敏 |
| `tests/unit/adapter_cli_test.c` | 5 | argv 执行/超时/无 shell |
| `tests/unit/adapter_dbus_test.c` | 6 | dbus 桥 + 真实调用 |
| `tests/unit/adapter_mcp_test.c` | 3 | MCP 客户端 + fake server |
| `tests/unit/adapter_http_test.c` | 4 | URL/仅 http/真实请求 |
| `tests/unit/facade_mcp_test.c` | 5 | 门面 L2 上限 |
| `scripts/verify_w2.sh` / `verify_all.sh` | 16 / 28 断言 | 端到端 |

---

## 回归保护

- 每次提交跑 `ctest`（2.1s，20 个测试）
- 端到端 `scripts/verify_all.sh`（约 15s，含 Release 构建）作为合并前基线
- 修复任务（T-FIX-01/02）完成后，需把覆盖率补到门槛并保持 20/20 不退化

---

## 缺口汇总（移交 6-review）

| # | 缺口 | 严重度 | 建议 |
|---|---|---|---|
| G1 | `cip.c` 68.5% < 80% | 🟡 | T-FIX-01：补错误码与边界分支 |
| G2 | `dispatch.c` 32.8%；socket 路径只由 shell 脚本覆盖 | 🟡 | T-FIX-02：把 `verify_all.sh` 的关键断言移入 ctest |
| G3 | ARM64/musl 完全未验证 | 🔴 | T-FIX-03：需要目标硬件或交叉编译链（本机不可闭环） |
| G4 | 无 SAST | 🟢 | 由 `-Werror` + ASan 补偿；可在 CI 加 cppcheck |
| G5 | 子进程桥开销未单独测量 | 🟢 | 在目标设备上补 |

## 🛡️ 测试门投票记录（2026-10-25）

```
🟫 高级产品经理:      ✅ 10/10 AC 有自动化覆盖，无 UAT-only 项；缺口都已登记且影响面清晰
🟦 资深用户评测员:    ✅ 拒绝可解释（reason + 原因码）、替代方案可执行（cli:curl / cli:ls）
🟩 架构师:           ❌ 反对：compatibility 只覆盖 x86_64+glibc，而 ADR-015 承诺了 A1 档（ARMv7/musl）。
                        条件：G3 必须在交付前由目标硬件闭环，或在 CHANGE 中显式降级承诺
🔴 安全审计师:        ✅ 条件通过：对抗用例齐、无明文、无 shell 注入面；建议补 SAST 到 CI（G4）

结果: 3/4 → 多数通过 ✅（记录架构师反对意见 → 进入 6-review）
```

### 带入 6-review 的强制条件

1. **G3（ARM64/musl）必须有明确处置**：闭环或在 CHANGE 里显式降级 ADR-015 的 A1 承诺（架构师）
2. G1/G2 需在 6-review 出具修复任务或显式接受（测试门隐含）
3. G4/G5 可登记为观察项，不阻塞
