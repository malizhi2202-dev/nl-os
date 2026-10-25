# STATE — 跨会话项目状态

> 仓库根 `STATE.md`。AI 每次开始新会话先读这个，结束前更新。

---

## 当前位置

- **活跃 Change**: `general-not-enumeration`（**已完成**：通用层改造 + ADR-024）
- **当前阶段**: 7-integration 通过 —— `ctest 36/36`、`verify_exec.sh 84/84`、`verify_hook.sh 35/35`、
  语义簇 121/121 + 旧留出集 55/55；留出集 A 34/35 → B 20/23 → C 19/19 → D 19/19 → E 11/11 → F 8/8
  （每批跑完即作废）；**安全网关已接入主解析路径**（写操作路径过黑名单/白名单，此前只在 broker 生效）
- **通用层（ADR-024）**: 规则 = 高频缓存；决策层 = 语义提示词认能力 + 结构性判别
  （句中有合法域名 → `dig`，否则 → `ip`）；参数只收"审查过的字面量"或"形状校验过的域名"；
  `ip`/`dig` 能登记 L1、`systemctl` 的只读形态能按 L1 跑，前提都是 exec 层的只读形态白名单；
  名词侧做归一（文件夹≡目录）+ 容器名词×标记词组合，不再逐句加词
- **已可安装使用**: `bash scripts/install_local.sh` → `~/.local/bin/nlsh`（模型自动发现，无需 export）
- **已装版本**: 01:12 的 Release（`当前工作路径` = 0.04s / 7.5MB；需要模型的句子 1.3s 内如实拒绝）
- **终端钩子已装**: `~/.bashrc` 标记块 → `~/.local/share/nl-os/nl-hook.bash`（卸载 `install_local.sh --uninstall-hook`）
- **当前 Task**: 无未完成项。剩下的是**需要路径参数**的说法（仍走模型或如实拒绝）与模型推理速度
- **模型已就位**: `~/.local/share/nl-os/qwen2.5-0.5b-q4_0.gguf`（429MB / 409MiB）
- **本机内存现实**: 机器已扩到 16GB（`MemAvailable` 约 7.8GB），资源闸门（ADR-023）现在放行，
  模型可以加载；但本机能用毫秒级跑完的句子**不该**去付 20s 的模型代价 —— 先把规则/决策层用满
- **中断任务**（来自 R1.5 清窗）: 无
- **会话开始建议**:
  - 恢复历史 → `@.specs/archive/2026-10-25-capability-protocol/`
  - 暂停中的 change → `@.specs/nl-capability-runtime/PROGRESS.md`
  - 本次改动 → `@.specs/adr/023-spawn-detach-and-mem-gate.md`、`@.specs/paraphrase-coverage/TEST.md`
  - 恢复历史上下文 → `@.specs/CONTEXT.md`、`@.specs/nl-os/`

## 流程欠账（2026-10-25 自查发现，待补）

| 欠账 | 阶段要求 | 现状 | 补救 |
|---|---|---|---|
| ~~Task 门未跑~~ | 3-task 出口门 | ✅ 已补跑（3/4，含 verify fallback 条件，已落地） | 完成 2026-10-25 |
| ~~CONTEXT.md 未更新域语言~~ | 1-requirement「提取域语言」 | ✅ 已补 13 条术语 + 4 条决策 | 完成 2026-10-25 |
| ~~方案未经用户确认~~ | 人机边界④把关 | ✅ 2026-10-25 用户逐条确认 10 条关键决策（记录见 CHANGE.md 末尾） | 完成 |
| ARCHITECTURE.md 缺失 | 0.4 架构级变更沉淀 | 仍不存在 | 评估是否跑 A-architect |

## 阻塞与待决策

| 项 | 类型 | 详情 | 待谁 | 自 |
|---|---|---|---|---|
| 注册表格式冻结 | 决策 | DESIGN 必须先冻结 `format_version` + schema，否则不得拆段 2 任务（架构师条件） | 设计 | 2026-10-25 |
| ~~ARCHITECTURE.md 缺失~~ | 决策 | ✅ 已裁定：以 ADR-001~018 作为架构文档，不单独维护 | 用户 | 2026-10-25 |
| ~~shell 内建（`cd`）~~ | 决策 | ✅ 已落地（ADR-020）：指令通道 + `PROMPT_COMMAND`，不 eval；`export`/`alias` 仍未做 | 用户 | 2026-10-25 |
| ~~模型常驻进程~~ | 决策 | ✅ 已落地（ADR-021）：`nlsh --serve` + 三级回退。**但单次推理仍约 20s**（128 token 生成 × 最多 3 次重试），规则未命中的句子交互上仍不可用——这是下一个要解决的问题 | 用户 | 2026-10-25 |
| 中文 benchmark 规模不足 | 决策 | 现 `models/benchmarks/nl-commands-zh.json` 仅 33 条，计划 1000 条 | 用户 | 2026-10-25 |
| ~~模型未部署~~ | bug | ✅ 已部署到 `~/.local/share/nl-os/`（`NL_MODEL_PATH` 指定）；`download_model.sh` 默认写 `/usr/share/nl-os` 会权限失败且**仍退出 0**（已知缺陷） | 用户 | 2026-10-25 |
| ~~自然语言命令不执行~~ | bug | ✅ 已修（change `nlsh-exec`）：`nlsh` 内贯通 确认→权限→快照→执行→审计，执行器与 `nl-run` 共用 | 用户 | 2026-10-25 |
| ~~确认策略不统一~~ | 决策 | ✅ 已统一（ADR-019）：一套触发条件/词表/绕过开关/取消码；`allow-l2` 收窄为只豁免 L2；`nl-run` 补审计；快路径破坏性命令留快照 | 用户 | 2026-10-25 |
| ~~固化丢策略头~~ | bug | ✅ 已修：`--freeze` 只写 `@format/@checksum/正文`，`# @perm: allow-l2` 被丢 → 固化后开关静默失效；现策略头随固化保留并纳入校验和（AC-11i 锁住） | 用户 | 2026-10-25 |
| IME 组合串（preedit） | bug | TTY 分支已用 pty 验证（中文/退格正确）；**输入法正在组合时**的光标渲染发生在 IME/终端侧，需真机 + 输入法验证 | 用户 | 2026-10-25 |
| ~~候选漏救 33%~~ | bug | ✅ 已修：规则层并入候选收缩后漏救降到 **14%**（召回 67%→86%），决策层准确率 79.2%→91.4% | 用户 | 2026-10-25 |
| 决策层延迟 | bug | 加"规则有把握不问模型"闸门后本集 P50/P95 = 0ms；强制解码仍 3393ms（0.5B/2 vCPU） | 用户 | 2026-10-25 |
| ~~REPL 非 TTY 输入中文"无法理解"~~ | bug | ✅ 已修：`line_edit.c` 只接受 ASCII 字节 —— 非 TTY 改走 `fgets`，TTY 接受 UTF-8 多字节（同一缺陷也让交互式中文打不进去） | 用户 | 2026-10-25 |
| C 档未验证 | 决策 | 无 3B–7B GGUF；本机可用内存 545MB（3B q4 需 ~3GB），ADR-015 的 C 档延迟承诺仍为"未验证设计目标"；档位映射已测 | 用户 | 2026-10-25 |

## 决策日志（最近 10 条，倒序）

- `[2026-10-26]` **通用性代替穷举（ADR-024）**：用户报两句 → 我补两条规则被点破「这不就成了穷举了吗」。
  正解是分层：规则只当高频缓存；决策层做**语义认能力**（提示词→候选）+ **结构判别**
  （句中有合法域名 → `dig`，否则 → `ip`）；参数只收审查过的字面量或**形状校验过的域名**
  （`nl_domain_check` 挡 `dig -f /etc/passwd`）；`ip`/`dig` 登记 L1 的前提是 exec 的**只读形态白名单**；
  权限表升为权威（装载时纠正注册表里的过期 `perm`）、空表自动扫。留出集 A 34/35 → B 20/23 → C 19/19，
  每次失败都修的是**类级**缺陷（大小写、虚词提示词、判别力权重）—— `@.specs/adr/024-general-not-enumeration.md`
- `[2026-10-26]` **修"输入后卡死/变慢"（ADR-023）**：①`spawn_daemon` 单 fork + `waitpid(守护进程)`
  → 终端无限挂起（实测 2 分 21 秒），改双 fork + `spawn_detached_test` 锁 500ms；②加载模型前过
  `resource_gate`（`文件×2 + 512MB` 余量），内存不够就降级而不是拖垮整机；③召回缺口用**语义簇覆盖测试**发现
  （`tests/paraphrase/`，调参集 73%→100%、留出集 **84%** 揭示过拟合）—— `@.specs/adr/023-spawn-detach-and-mem-gate.md`
- `[2026-10-26]` **ASCII 关键词必须词边界**：`{"cp"}` 子串命中「占**CPU**」→ `cp -r`（提问变拷贝）
- `[2026-10-26]` **`template_id == 0` 是通配符**：给 `NL_INTENT_OTHER` 加第一个模板会静默改变
  `{"怎么"}→OTHER,tmpl 0` 的规则（「怎么样」曾变成查端口）—— 新模板改挂 `SHOW_PROCESS`
- `[2026-10-25]` **决策层兜底（ADR-022）**：规则未命中 → 候选收缩认出工具 → 用**审查过的只读默认参数**执行，不加载模型（`显示当前工作目录` 由 20s/"无法理解" 变成 `pwd` 11ms）；只对 L1 生效、参数为表内字面量、表外不猜——`@.specs/decision-fallback/`
- `[2026-10-25]` **常驻解析进程 `nlsh --serve`**：模型只加载一次、空闲卸载；只在 TTY 自动拉起；三级回退（规则→常驻→进程内）——`@.specs/adr/021-inferd-daemon.md`。实测省 1.8s/次的加载，但推理 20s 仍是瓶颈
- `[2026-10-25]` **`cd` 由父 shell 执行**：`bash` 的 `command_not_found_handle` 在子 shell 里跑，钩子 cd 无效 → `nlsh` 写 `__NL_CD__ <dir>`，`PROMPT_COMMAND` 执行 `cd -- "$dir"`（不 eval）——`@.specs/adr/020-shell-builtin-parent-exec.md`
- `[2026-10-25]` **修模型路径 abort**：decode 分块按 `n_ctx`(512) 而断言要求 `n_batch`(256)，prompt 超 256 token 就 `GGML_ASSERT` 崩（每次调用 `rc=134`）；AC-14 只断言了"加载"没断言"成功"，所以一直没发现
- `[2026-10-25]` **终端直接说人话（shell 钩子）**：bash `command_not_found_handle`，只接管含中文的未知命令，英文与真命令零影响；钩子不 eval、原样交 `nlsh -c`（确认门/审计不变）——`@.specs/shell-nl-hook/`
- `[2026-10-25]` **模型惰性加载**：规则能决定的命令不再启动即加载 428MB 模型（3.57s/779MB → 0.2s/19MB）；规则未命中仍加载（AC-14 5 条）——回应"占用 CPU 很大很快就满了"
- `[2026-10-25]` **统一确认门完成落地测试**：40 条断言（词表全量/空行与 EOF 取消/只读不问/绕过等价/退出码 0-1-3/快路径快照与审计）；测试撞出并修掉"`--freeze` 丢 `# @perm:` 头"
- `[2026-10-25]` **确认策略统一为 ADR-019**：一套触发条件/词表（`y|yes|是`）/开关（`NL_ASSUME_YES`/`--yes`）/取消码（3）；`allow-l2` 只豁免 L2；`nl-run` 补审计；快路径破坏性命令留快照可撤销
- `[2026-10-25]` **ARM64 已验证**：无 root 交叉工具链 + qemu 下 scan/eval 与 x86_64 数值一致（stub 模型；真机需交叉编 llama）
- `[2026-10-25]` **`nlsh` 真正执行自然语言结果**：确认门 + 权限 + 快照 + `execvp` + 审计；`NL_ASSUME_YES=1` 供脚本、EOF = 取消（fail-closed）——`@.specs/nlsh-exec/REVIEW.md`
- `[2026-10-25]` **拒绝优先**：多动作连接词与"无对应能力操作词"在解析前拒绝，绝不降级成单条破坏性命令（UAT S4 曾生成 `rm -rf 原文件`）
- `[2026-10-25]` **输入级闸门**：NL 输入含 shell 元字符直接拒绝——子命令注入发生在解析前，命令级 guard 拦不住
- `[2026-10-25]` **UAT 首轮实跑 10/12**：场景 3（能力缺失无解释）与场景 4（复合句降级成 `rm -rf`）不过，均由 `nlsh-exec` 修复后复跑 12/12
- `[2026-10-25]` **T11b 完成**：decoder 后端接线（档位→后端、GBNF 受约束 Choice、输出只认候选集内 id）；模型在候选集内把准确率从规则 top-1 的 91.0% 提到 **98.5%**（66/67）
- `[2026-10-25]` **置信度阈值默认改为 0（关闭）**：实测 0.5 会丢掉 53% 的正确回答且准确率反而更低（85.7%→80.6%）——该指标无区分度，见 T13-SUMMARY B6
- `[2026-10-25]` **T13 分档评测**：A1(模拟) 79.2% / B(本机 0.5B) 85.7% / C 未测；瓶颈确认在候选召回（漏救 33%）而非模型选择

- `[2026-10-25]` 永久放弃 `.nl` 完整编程语言，NL-C 收敛为 `.tools` — `@.specs/adr/011-drop-nl-language.md`
- `[2026-10-25]` `.specs/` 纳入版本控制（此前被 `.gitignore` 忽略，TASK/ADR/SUMMARY 均未入库）
- `[2026-10-25]` 决策层不走 Agent Loop，改为有限答案空间内的类型化判断（choice/score/noul） — `@.specs/nl-capability-runtime/CHANGE.md`
- `[2026-10-25]` **CIP v1 定案**：五报文 / 两阶段 invoke-commit / plan 用 argv 数组 / effect 五类缺省 delete / 威胁模型"模型不可信、系统用户可信" — `@.specs/adr/016-cip-protocol.md`
- `[2026-10-25]` **broker 独立进程 + fail-closed + ticket 一次性(TTL60s,args 哈希)** — `@.specs/adr/017-broker-process.md`
- `[2026-10-25]` **执行面用子进程桥/最小实现，不引 libdbus/libcurl/TLS** — `@.specs/adr/018-capability-adapters.md`
- `[2026-10-25]` **段 3 完成**：`.tools` 固化+校验、快照/撤销、审计回放；`verify_segment3.sh` 16 断言 0 失败
- `[2026-10-25]` **规则槽位缺陷（已登记未修）**：`after_key` 会吃进句尾修饰词（`README.md 文件`、`demo 的文件夹`），影响命中率；改动需配套回归，见 LESSONS
- `[2026-10-25]` **capability-protocol 归档**：CIP v1 全链路验收通过（`ctest 20/20`、`verify_all.sh` 31 断言），产物移至 `.specs/archive/2026-10-25-capability-protocol/`
- `[2026-10-25]` **差异化假设被推翻并收窄**：安全执行层非空白（Belay/Leash/Pipelock 等 7 个项目）；新定位 = 端侧档位 + 零运行时 + 中文 — `@.specs/discovery/claim-verification.md`
- `[2026-10-25]` **ADR-015 修正**：支持地板 = ARMv7-A/ARMv8-A + MMU + Linux + ≥128MB RAM；Tier A 细分 A1(128–768MB，仅 embedding)/A2(1–4GB)；**MCU/RTOS 家电（空调 Cortex-M0 128KB Flash）明确 out of scope**
- `[2026-10-25]` **ADR-015 设备分档**：A(≤4GB) embedding 为主 / B(8-16GB) +0.5B decoder / C(≥32GB) +大模型；判断后端抽象、运行期自降档 — `@.specs/adr/015-device-tiers.md`
- `[2026-10-25]` 注册表 JSONL + format_version 冻结；决策两阶段（粗筛 K=8 → GBNF Choice）；模型不得 deny；内置 SHA-256 — `@.specs/adr/012~014`
- `[2026-07-01]` 一期 MVP 技术栈与十项架构决策 — `@.specs/adr/001~010`

## 已归档 Changes（最近 5 个，倒序）

| 日期 | Change | 摘要 | PR |
|---|---|---|---|
| 2026-10-25 | nl-capability-runtime | 能力自发现/注册表/预算 + 决策层 + 固化撤销审计（T01–T16） | — |
| 2026-10-25 | capability-protocol | CIP v1 协议 + broker + 四执行面 + MCP 门面（AC 10/10） | — |
| 2026-07-02 | nl-os | 一期 MVP：四层管线 + L1–L4 + `.tools` | — |
| 2026-07-01 | nl-os-phase2 | 真实推理 + 安全强化（draft，未开工） | — |

---

## 横向命令状态

```yaml
ai_context_doc: .specs/CONTEXT.md
last_intel_scan: 2026-10-25     # 已按 I-intel-scan 摸底过（未生成新 CONTEXT，仅核对）
last_architect_at:              # 从未跑 A-architect
last_evolve_at:
last_evolve_promoted: []
last_health_at:
last_health_score:
```

---

## 健康检查（AI 每次会话开始前自检）

- [x] `.specs/` 目录存在
- [x] 当前活跃 change 的工件文件齐全（CHANGE 阶段：`CHANGE.md`）
- [x] 没有未归档但已合并的 change
- [ ] 没有 > 3 轮自动重试仍未解决的失败（暂无）
