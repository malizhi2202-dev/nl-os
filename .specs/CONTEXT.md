# CONTEXT — NL-OS 项目共享上下文

> 本文件**跨 change 长期累积**。每个 change 在 REQUIREMENT 阶段会向这里追加术语和决策。
> 目标：为 AI 提供项目级的「域语言 + 默认偏好」，省去重复解释。

---

## 项目概要

NL-OS 是一个**自然语言驱动的操作系统交互层**。目标是把「用自然语言操作计算机」的能力变成 Linux 的第 5 个基础抽象（继进程、文件、内存、网络之后）。核心交付：
- **NL-Shell**：替换 bash 前端的自然语言终端
- **NL-C**：以 C 语言为底层解释器的自然语言编程范式（`.tools` 脚本 + `.nl` 源文件）
- **权限分级系统**：4 级操作权限（L1 读安全 / L2 修改确认 / L3 删除高危 / L4 禁止执行）

用户：Linux 运维/开发者、边缘设备集成者、嵌入式/IoT 场景。

---

## 技术栈（团队级默认 / 已锁定）

- **语言**: C11（不是 C17，确保 ARM 旧交叉编译链兼容）
- **构建系统**: CMake ≥ 3.16
- **核心依赖**: ggml（vendor，锁定 commit）、cJSON（vendor, v1.7.x）、pthread（系统库）
- **测试框架**: greatest.h（header-only, C 单元测试）
- **模型格式**: GGUF v3
- **目标架构**: Linux x86_64 + ARM64（aarch64），glibc ≥ 2.28，内核 ≥ 5.10
- **栈卡片编号**: 无（此项目不属于 flow-kit 预定义的 8 个技术栈——它是 C 语言系统级项目）

---

## 域语言（术语表）

| 术语 | 定义 |
|---|---|
| **NL-Shell** | 自然语言终端 REPL。接收自然语言输入，翻译为 Linux 命令并执行。可执行文件名 `nlsh`。 |
| **NL-C** | 自然语言编程范式。分两层：`.tools`（指令序列脚本）和 `.nl`（完整编程语言，含函数/包/导入）。 |
| **.tools** | NL-C 的脚本格式。纯自然语言指令组成的可执行文件，类比 bash 脚本。 |
| **.nl** | NL-C 的源文件格式。包含函数、包导入、类型等完整编程语言特性。二期交付。 |
| **NL 推理引擎 (`libnlc`)** | 核心共享库。包含四层处理管线：安全网关 → 规则引擎 → 模型推理 → 模板填充。 |
| **权限守护 (`nl-perm`)** | 独立进程。接收 NL-Shell 的操作请求，返回 allow/deny/confirm。fail-closed 原则。 |
| **L1 · 读安全** | 只读操作（`ls`/`cat`/`find`/`ps`），自动放行，无需确认。 |
| **L2 · 修改确认** | 写/修改操作（`mkdir`/`touch`/`echo > file`），显示命令后按 Enter 确认。 |
| **L3 · 删除高危** | 删除/不可逆操作（`rm`/`rmdir`/`mv`覆盖），红色警告 + 需输入 `yes`。 |
| **L4 · 禁止执行** | 硬拦截。涉及保护路径（`/`, `/etc`, `/boot`, `/sys`, `/proc`, `/dev/sda*`）的操作，不生成命令。不可绕过。 |
| **安全网关** | 四层管线的第一层。在所有 NL 处理之前运行，执行路径规范化和黑/白名单检查。 |
| **规则引擎** | 四层管线的第二层。关键词 + 模板匹配，覆盖 80% 高频命令，延迟 < 1ms。 |
| **模板填充** | 四层管线的第四层。根据 intent + entities 填充命令模板（如 `mkdir {name}`），模型不直接生成命令文本。 |
| **dry-run** | NL-Shell 模式，只显示生成的命令但不执行。 |
| **freeze** | `.tools` 文件的编译操作——首次翻译后保存具体命令，后续直接执行，保证确定性。 |
| **fail-closed** | 安全原则——权限守护不可达时，NL-Shell 拒绝所有 NL 操作，而非放行。 |
| **结构化输出** | 模型推理的输出格式（JSON Schema 约束），而非自由文本。包含 intent + entities + confidence。 |
| **降级模式** | 模型未加载或推理失败时，规则引擎独立运行的状态。复杂指令可能无法处理。 |
| **透传** | 用户输入被识别为传统 shell 命令时，直接传给 `/bin/sh -c` 执行，不经过 NL 处理管线。 |

---

## 已锁决策

- `[2026-07-01]` **ADR-001** 四层混合架构（安全网关 → 规则引擎 → 模型推理 → 模板填充）。安全网关在模型前运行。来自 `@.specs/nl-os/CHANGE.md`
- `[2026-07-01]` **ADR-002** 权限分级模型 L1-L4 + 安全网关前置 + 路径白名单（仅 `/home /tmp /var /opt /mnt` 允许写操作）。来自 `@.specs/nl-os/CHANGE.md`
- `[2026-07-01]` **ADR-003** NL-C 语法规范分两层：`.tools` v1（MVP，指令序列）→ `.nl` v1（二期，完整编程语言）。来自 `@.specs/nl-os/CHANGE.md`
- `[2026-07-01]` **ADR-004** 模型选型流程：先建 benchmark（1000 条中文 NL→命令测试集）→ 跑分对比 → 数据驱动选型。对比维度：准确率/延迟/内存。来自 `@.specs/nl-os/CHANGE.md`
- `[2026-07-01]` **ADR-005** 内核集成路线：Phase 1 用户态权限守护 → Phase 2 eBPF → Phase 3 评估 .ko 必要性。来自 `@.specs/nl-os/CHANGE.md`
- `[2026-07-01]` **ADR-006** 依赖策略：fork+vendor ggml（锁定 commit）+ cJSON（v1.7.x），总外部依赖 ≤ 3（ggml + cJSON + 测试框架）。来自 `@.specs/nl-os/CHANGE.md` 多角色对抗
- `[2026-07-01]` **ADR-007** 模块边界：`libnlc.so`（共享库，模型 mmap 一次）+ `nl-perm`（独立进程, Unix socket IPC, fail-closed）+ `nlsh`（REPL）+ `nl-run`（.tools 执行器）。来自 `@.specs/nl-os/CHANGE.md` 多角色对抗
- `[2026-07-01]` **ADR-008** Agent 架构：MVP 单步模式（NL 输入 → 一条命令），`libnlc` API 预留 Planner/Executor/Observer 接口供二期 Agent Loop 扩展。来自 `@.specs/nl-os/CHANGE.md` 多角色对抗
- `[2026-07-01]` **ADR-009** 测试策略：五层金字塔（单元 → 组件 → 模型推理 → 集成 → 端到端）。模型层用准确率分布而非二元判定。Layer 1-3 CI 每次跑，Layer 4-5 PR merge 跑。来自 `@.specs/nl-os/CHANGE.md` 多角色对抗
- `[2026-07-01]` **ADR-010** C11 标准（非 C17）+ GGUF v3 锁定 + CMake 构建。来自 `@.specs/nl-os/CHANGE.md` 多角色对抗

---

## 默认偏好（AI 在缺省时按此决策）

- **命名风格**:
  - 文件/目录：`snake_case`（`rule_engine.c`, `path_safety.h`）
  - 函数：`nl_` 前缀 + `snake_case`（`nl_parse_input`, `nl_check_permission`）
  - 类型/结构体：`Nl` 前缀 + `PascalCase`（`NlIntent`, `NlPermLevel`）
  - 宏/常量：`UPPER_SNAKE_CASE`（`NL_MAX_INPUT_LEN`, `NL_PERM_L4`）
  - 测试文件：`<name>_test.c` 紧邻源码
- **错误处理**：函数返回 `int`（0 成功, <0 错误码），错误信息通过 `nl_error_t *` 输出参数传递。不使用全局 errno。
- **内存管理**：调用者分配，被调用者填充。避免在被调用者内部 malloc 不释放。模型内存（mmap）由 libnlc 管理。
- **日志**：NL-Shell 使用 `stderr` 输出提示/警告，`stdout` 输出命令执行结果。权限守护使用 syslog。
- **提交格式**: `<type>(<change-id>): <subject>` — 例如 `feat(nl-os): add path safety whitelist checker`
- **注释语言**：代码注释使用中文，标识符（变量/函数/类型）使用英文。
- **安全默认**：任何不确定的操作默认拒绝（deny-by-default），而非放行。

---

## 既有抽象索引

> greenfield 项目，暂无既有抽象。以下为 MVP 阶段规划的抽象（4-dev 阶段写代码时必查此清单，避免重复实现）：

### 核心库 (libnlc)

| 模块 | 路径 | 职责 |
|---|---|---|
| 公共 API | `src/libnlc/nlc_api.h` | 外部调用的唯一入口（`nl_parse()`, `nl_init()`, `nl_free()`） |
| 规则引擎 | `src/libnlc/rule_engine.c` | 关键词匹配 + 模板选择 |
| 模型加载 | `src/libnlc/model_loader.c` | GGUF v3 解析 + mmap |
| 意图提取 | `src/libnlc/intent_extract.c` | 模型推理 → JSON 结构化输出 |
| 模板填充 | `src/libnlc/template_fill.c` | intent + entity → 命令字符串 |
| 路径安全 | `src/libnlc/path_safety.c` | realpath + 白名单 + 黑名单 |
| Few-shot | `src/libnlc/few_shot.c` | 相似度检索（二期） |
| JSON 辅助 | `src/libnlc/json_helper.c` | cJSON 封装 |

### 权限守护 (nl-perm)

| 模块 | 路径 | 职责 |
|---|---|---|
| 权限引擎 | `src/nl-perm/perm_engine.c` | L1-L4 判定 |
| IPC 服务端 | `src/nl-perm/ipc_server.c` | Unix domain socket |
| 审计日志 | `src/nl-perm/audit_log.c` | syslog 写入 |
| Seccomp | `src/nl-perm/seccomp_filter.c` | 自身沙箱 |

---

## 禁动清单

> greenfield 项目，暂无禁动项。以下为规划中的高风险模块：

- `src/libnlc/path_safety.c` — 安全关键路径，任何修改需额外安全审查
- `src/nl-perm/perm_engine.c` — L4 判定逻辑，修改需附对抗性测试
- `third_party/ggml/` — vendor 锁定版本，不允许随意升级

---

## intel-scan 元数据

- **last_intel_scan**: N/A（greenfield 项目，首次扫描将在项目代码建立后执行）
- **scanner**: `prompts/I-intel-scan.md`
- **下次重扫建议**: MVP Phase 1 代码完成（约 5000 行 C 代码）后首次扫描

---

> 此文件长度建议 ≤ 300 行；超出时把陈旧条目归档到 `.specs/archive/CONTEXT-history.md`。
