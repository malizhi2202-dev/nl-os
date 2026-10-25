# CHANGELOG

## 2026-10-25 · 统一确认门落地测试 + 修 `--freeze` 丢策略头（change `nlsh-exec` 续）

**修复**

- `--freeze` 不再丢 `# @perm: allow-l2`：策略头写进固化文件（`# @checksum` 之后、正文之前），
  并纳入校验和（改策略头 = `TAMPERED`）。此前"固化后的 L2 免确认"在最常用路径上**静默失效**，
  ADR-019 的 P-3 等于没落地。格式仍是 v2（对旧读取方只是正文多一行注释）
- `nl_freeze_write` 新增 `policy_line` 形参；`nl_script_parse` 保留 `perm_line`；
  非注释行会被拒绝写入策略头

**测试补齐（确认策略全矩阵，40 条）**

| 组 | 覆盖 |
|---|---|
| AC-11g | 词表五个词（`y/Y/yes/YES/是`）× 两个入口；空行 / EOF / 否定词 = 取消 |
| AC-11h | 只读不问（P-1 触发条件） |
| AC-11i | `NL_ASSUME_YES` 对两入口等价；固化后保留 `@perm`；`allow-l2` 真的豁免 L2 且只到 L2 |
| AC-11j | 退出码三态：成功 0 / 拒绝 1 / 取消 3 |
| AC-11k | 快路径非破坏性不建快照；破坏性建快照且可 `--undo`；审计 reason 区分 `fast-path` / `fast-path-destructive` |
| 单测 | `freeze_test` 新增"策略头保留 + 改头算篡改 + 拒绝非注释行" |

**文档**

- ADR-019 增 §6 落地复盘（缺陷成因 + 修复 + 教训）；UAT 增场景 5（统一确认门手工自检，已实跑）
- `LESSONS.md`：解析出的元数据必须检查所有写回路径；负向用例抓不到"全丢"类缺陷

**回归**：`ctest` 31/31（Debug+Release）、`verify_exec.sh` **76/76**、段 1/2/3 = 12/18/16、`verify_all.sh` 0 失败


## 2026-10-25 · 统一确认门 + 本地安装 + 三项目标实测（change `nlsh-exec` 续）

**新增（ADR-019 统一确认门）**

- 三套确认策略收敛为一套：`nl_confirm_required` / `nl_confirm_yes` / `nl_confirm_assume_yes` / `nl_confirm_ask`
- **行为变化**：`nl-run` 也接受 `y`（原来 `y` = 取消）；两处取消统一退出码 **3**；
  `# @perm: allow-l2` 收窄为"只豁免 L2"（原来实际会豁免 L3 删除）；
  `nl-run` 每步写审计（之前完全没有）；`nlsh` 快路径的破坏性命令（rm/mv/dd/…）执行前留快照，
  审计 `reason=fast-path-destructive`——手滑 `rm -rf` 现在能 `--undo`
- 新增开关 `nlsh --yes` / `nl-run --yes`（等价 `NL_ASSUME_YES=1`）
- 专家团讨论记录与决策：`.specs/adr/019-unified-confirm-gate.md`

**可用性**

- `bash scripts/install_local.sh`：无 root 装到 `~/.local`，装完直接 `nlsh`（不再只认 `./build/nlsh`）
- 模型路径自动发现：`$NL_MODEL_PATH` → `/usr/share/nl-os` → `$XDG_DATA_HOME/nl-os` → `~/.local/share/nl-os`
- 修 `BUILD_STUB_MODEL=ON` 下 `nl_model_default_path` 未定义（交叉构建链接时暴露）

**验证补齐（此前记为"未验证"的三项）**

| 项 | 结论 |
|---|---|
| IME / TTY 分支 | ✅ 用 `pty.fork()` 驱动真终端：中文回显、命令正确、目录落盘；退格删一个完整汉字无残字 |
| ARM64 | ✅ 无 root 装交叉工具链 → 构建通过 → `qemu-aarch64-static` 下 scan/eval 跑通，评测数值与 x86_64 完全一致（stub 模型；真机需交叉编 llama） |
| C 档 3B–7B | ❌ 仍不可测：可用内存 545MB，3B q4 需 ~3GB；档位映射已测（`NL_TIER=4` → backend decoder） |

**回归**：`ctest` 31/31（Debug+Release）、`verify_exec.sh` 49/49、段 1/2/3 = 12/18/16、`verify_all.sh` 0 失败

## 2026-10-25 · nlsh 执行接线：说一句就生效，且先说清边界（change `nlsh-exec`）

**修复（安全优先）**

- UAT S4：`把 /var/log 下所有 .log 按大小排序压缩后发到远程服务器并清理原文件` 曾被规则层降级成
  `rm -rf 原文件`。预算层新增两个拒绝来源：**多动作连接词**（`并/然后/接着/最后/之后/同时`，阈值 3→1）
  与**无对应能力的操作词**（压缩/排序/发送/远程/下载/定时/循环…）→ `NL_DIM_UNKNOWN_CAP`
- 子命令注入：`创建一个叫 x; touch hacked` 会被解析成 `touch hacked` 并执行（生成命令本身干净，
  命令级 guard 拦不住）→ dispatch 入口加**输入级闸门**（`; | & $ \` > <` 直接拒绝）
- 行编辑器只能输入 ASCII：`printf '中文…' | nlsh` 得到残缺字符串，交互式中文同样打不进去 →
  非 TTY 走 `fgets`，TTY 接受 UTF-8 多字节，退格按完整字符删

**变更**

- `nlsh` 真正执行自然语言结果：确认门（`needs_confirm || perm>=2`，`NL_ASSUME_YES=1` 可跳过，
  EOF = 取消）→ 权限 → 快照 → `execvp`（不经 shell）→ 审计（result/duration）
- 执行器从 `nl-run` 抽到 `libnlc/exec.c`，两处共用；`nl-run` 每步也过同一道预算闸门
- `--dry-run` 贯通 `-c` 与 REPL；解析失败时按关键词给候选（`重启 nginx` → `cli:systemctl`）
- `--decide` 的方法名如实化：规则已确定、模型没解码时输出 `rule-decisive`，不再谎称 `choice-llm`

**实测**

- `scripts/verify_exec.sh` 36/36；`ctest` 30/30（Debug/Release）；段 1/2/3 验收 12/12、18/18、16/16；
  `verify_all.sh` 0 失败
- 120 条评测集不变（93 ok / 27 refused / 91.4%），新维度未误伤合法句子
- Release 端到端：只读 0.05–0.07s，写操作（含确认门+快照+执行）0.03–0.06s
- 快路径（T07）：传统命令不再加载能力注册表 —— `nlsh -c 'echo hi'` **30ms → 0–10ms**
  （根因：满表 1891 条/356KB 的 JSONL 全量解析占了大头，而快路径不需要注册表）

## 2026-10-25 · 规则层与决策层打通 + 槽位剥离（修 `mkdir -p demo 的文件夹`）

**修复**

- `rule_engine.c::extract_after_key_long`：剥离句尾修饰词（`的文件夹`/`的目录`/`的文件`/`的内容`/
  `X 目录下`）与前导量词（`一个`/`叫`/`名为`），但**名词类后缀只在其前有空白时才剥**，
  保证 `删除 旧文件` 不会被啃成 `旧`。新增回归 `tests/unit/rule_slot_test.c`（6 组 27 条断言，含反向守卫）
- REPL `--eval` 的 JSON 输出已转义（含引号的输入不再产生非法 JSON）

**变更**

- `nl_decide_candidates`：先跑规则层，命中意图给对应能力 **+50 强提示**（原来只有 20 条词典 hint，
  导致"能执行的说法决策层选不出来"）
- `nl_choose_with_model`：**规则有把握就不问模型**——单候选直接采纳；top-1 领先次优 ≥20 分
  （`NL_DECIDE_MARGIN` 可调）直接采纳；其余才跑 GBNF 受约束解码
- 规则库补 13 条自然说法（`建立个叫 X 的文件夹`、`查看 X 的内容`、`进入 X`、`到 X 目录下`、
  `列出进程`…）

**实测（120 条中文集）**

- 规则层 intent 68.3% → **81.7%**；覆盖率 73.3% → **86.7%**
- 候选召回 67% → **86%**（漏救 33% → 14%）；规则 top-1 91.0% → **98.8%**
- 决策层准确率（`--eval` 规则档）79.2% → **91.4%**
- 结论：候选变好后 decoder 边际价值为负（强制解码 96.5% vs 规则 98.8%，且多花 3.4s），
  故加"规则有把握不问模型"闸门；本集 P50/P95 = 0ms

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
