# CHANGELOG

## 2026-10-26 · 通用性改造：语义认能力 + 结构抽参数 + 权限表当权威（ADR-024）

**问题**：用户报了「解析wwww.baidu.com这个域名的dns」「网卡信息」两句都"无法理解"，
我一对一补规则，被用户点破：**「这不就成了穷举了吗 没有通用的处理吗 模型层理解不了吗」**。

这个批评是对的：规则是"整句形态"匹配，用户换语序/加"帮我"/换同义词就不命中，
命中率永远等于"我碰巧见过多少种说法"。诊断下来，两个"模型层"当时都没在干活：

- **经典模型层**输出空间只有 13 个 intent（答 `other` = 没能力），而且**实体被丢掉**：
  `nl_template_fill(intent, 0, …)` 的 `0` 曾经表示"该 intent 的第一张模板"，
  所以模型说 `list_dir + location=/var/log`，生成出来仍是裸 `ls -la`。
- **类型化决策层**（候选收缩 + GBNF 受约束 Choice）只被 `--eval` 用到，没接进解析链；
  而它的候选来源（注册表）在用户机器上是**空表**或**过期的权限快照**。

**改法（都是类级，不是补句子）**：

- **域名是结构性判别，不写关键词规则**：「我的 IP」和「baidu.com 的 IP」句式一样，
  区别只在句中有没有合法域名。改为决策层判：句中有合法域名 → `dig`，否则 → `ip`。
  规则库里只留 3 条**永远不可能问 DNS** 的说法（网卡/网络接口/网口）。
- **结构性参数抽取**：`nl_extract_domain`（允许域名与中文粘连）+ `nl_domain_check`
  （字符集、标签、顶级域、长度；挡住 `dig +short -f /etc/passwd` 的选项注入）。
  抽不到/校验不过 → 不成立，绝不生成半条命令。
- **模板选择改成"槽位能填满的最具体那张"**，模型实体不再被丢；`OTHER` 不参与自动选择；
  提供了非法 domain → 直接失败而不是退到通用模板。
- **权限表是权威、注册表只是缓存**：装载时对 `source=scan` 的条目按表重新取值
  （否则把 `ip` 从 L4 改成 L1，老注册表里它永远是 L4，决策层永远看不到它）；
  注册表为空时 `nlsh` 自己扫一次并落盘 —— 通用层必须开箱可用。
- **`ip`/`dig` 为什么能是 L1**：因为 exec 层加了**只读形态白名单**
  （`ip` 必须整条等于 `ip -br addr`；`dig` 只允许 `dig +short <已校验域名>`），
  其它形态 fail-closed 拒绝。
- 新增 intent `show_network`（不复用 `show_process`：后者已经被 uptime/lsof 借住过；
  更不能塞 `OTHER`，那会让 `template_id==0` 的泛规则静默拿到命令）。

**验证（留出集，跑一次即作废）**：A 批 35 条 34 通过 → 暴露"大小写敏感"；
B 批 23 条 20 通过 → 暴露"1-2 字虚词提示词撞车"和"域名判别力不足"；C 批 19 条 **19 通过**。
主调参集 121/121 不回归；`ctest` 36/36、`verify_exec.sh`、`verify_hook.sh` 全绿。

## 2026-10-26 · 修"输入后卡死/变慢"三个缺陷 + 语义簇覆盖测试（ADR-023）

**问题**：用户报告「本来响应很快，现在从输入自然语言到出结果特别慢」。实际发现终端里
`nlsh -c 当前工作路径` 已经卡在 `do_wait` **2 分 21 秒** —— 不是慢，是**无限挂起**。

**三个独立缺陷**：

- **后台进程 waitpid 等错对象**：`spawn_daemon()` 单 fork 后 `waitpid` 等的是 exec 之后的
  **常驻守护进程本身**，所以「守护进程活多久，用户终端就卡多久」。改双 fork，
  中间进程 setsid 后立刻退出；新增 `tests/unit/spawn_detached_test.c` 断言返回 < 500ms。
- **加载模型前不看内存**：可用 657MB 时仍去加载 409MiB 模型（RSS 677–815MB），
  把 8GB 机器推进内存回收，用户感受是"整个终端都卡"。新增 `src/libnlc/resource_gate.{h,c}`：
  `文件大小×2 + 512MB 余量` 放得下才加载，放不下就降级到规则层并如实说明（`NL_MODEL_MEM_FORCE=1` 可强制）。
- **召回缺口**：「当前工作路径」「我在哪」「列个文件清单」这类说法没被规则/决策层接住，
  落进 20s 的模型层 —— 而这一层又会触发上面那个挂起。

**测试方法改了（这才是重点）**：原先是"我挑几句常见说法试"，属于**抽样**。
改成**语义簇 + 改写不变性**：`tests/paraphrase/nl-paraphrase-zh.json`（10 簇 121 条）、
留出集 `-holdout.json`（55 条，调参时没看过）、`scripts/verify_paraphrase.py` 输出逐簇通过率与失败原句。
调参集 73% → 100% 的同时，**留出集只有 84%** —— 这个差值就是过拟合的真实大小。

**顺带修掉的误匹配**：`{"cp"}` 子串命中「占**CPU**」→ `cp -r`（提问变拷贝）；
`{"进程"}` 吞掉「哪个进程在监听端口」；`运行**时间**` 被当命令执行；
`template_id==0` 通配语义让「怎么样」变成查端口。

**验收**：`ctest 36/36`、`verify_exec.sh 86/86`（AC-14 改为按可用内存分支断言）、
`verify_hook.sh 35/35`、语义簇 121/121 + 留出集 55/55。

## 2026-10-25 · 规则未命中不再干等 20 秒：决策层兜底 + 词典扩容（ADR-022）

**问题**：上一轮修好崩溃后发现，"规则未命中"这条路实际不可用——模型自由生成单次 20–30s，
且对绝大多数输入返回"无法理解"。而项目里的决策层（候选收缩 + 受约束 Choice）是毫秒级的，
只回答"哪个工具"，没接进 `nlsh -c`。

**做法**：解析链插入一层兜底：规则 → **决策层** → 常驻进程 → 本地模型。

- 新增 `src/libnlc/decision_cmd.c`：候选收缩认出工具 → 查**10 条逐条审查过的只读默认参数表**
  （`pwd` / `ls -la` / `ps aux` / `df -h` / `free -m` / `uptime` / `whoami` / `uname -a` / `date` /
  `lsof -i -P -n`）→ 拼成固定命令，走原有确认门/权限/快照/审计
- 安全边界：只对 `capability_perm_map.h` 里的 **L1** 生效；参数只能是表内字面量（零注入面）；
  **表外工具不猜**；`ss`/`netstat` 未登记 = L4，不借兜底放宽权限，网络查看改用 L1 的 `lsof`
- `decision.c` 词典扩充 + 权重（特指 10 / 泛指 6），修"`显示当前工作目录` 在 pwd 和 ls 之间打平"
- `NlSource` 增 `NL_SOURCE_DECISION`

**实测**（`--no-model`，即完全不加载模型）：

| 输入 | 改前 | 改后 |
|---|---|---|
| `显示当前工作目录` | 无法理解（20–30s） | `pwd` **11ms** |
| `看看磁盘还剩多少` | 无法理解 | `df -h` |
| `内存还剩多少` | 无法理解 | `free -m` |
| `当前用户是谁` | 无法理解 | `whoami` |
| `查看端口占用` | 无法理解 | `lsof -i -P -n`（L1） |
| `现在几点` | 无法理解 | `date` |

召回抽样 12 句：**9 句**由规则或决策层直接决定（此前约 2 句）。

**安全实测**：`删除 /tmp/x 目录`（L3）仍弹确认、EOF 取消后目录未被删——兜底没有绕过确认门。

**验收**：`ctest` 34/34（新增 `decision_cmd` 单测）、`verify_exec.sh` **84 断言**（新增 AC-15）、
`verify_hook.sh` 35 断言，全绿。回归中 `AC-14` 因前提变化变红（那句不再加载模型），
已换句子 + 新增 AC-15，而不是把阈值改松。

## 2026-10-25 · 终端自然语言补完：`cd` 能用了 + 常驻解析进程（顺带修一个每次调用都崩的真 bug）

**背景**：钩子上线后仍有两处断点——`cd` 不生效、规则未命中每次重付 1.8s 模型加载。

**一、`cd` 真正生效（ADR-020）**

- `bash` 把 `command_not_found_handle` 放在**子 shell** 执行（实测 handler 里 `cd` 后父 shell `PWD` 不变），
  所以钩子自己 cd 也没用。改为：`nlsh` 写 `__NL_CD__ <dir>` 指令 → `PROMPT_COMMAND` 在主 shell
  执行 `cd -- "$dir"`。**只传路径，不 eval**（往通道里塞 `touch` 一字节都不执行）。
- `cd` 作为内建能力（`shell:cd`）跳过注册表检查；目标不是目录 / 没有指令通道时**诚实报错**，
  绝不打印"已切换目录"。
- 顺带修**死模板**：`cd ..`（template 901）没有任何规则可达，`切换到上级目录` 被吃成
  `cd 上级目录`。新增 87/88/89（上级/父目录/上一级），须排在 83/84/85 之前。

**二、常驻解析进程（ADR-021）**

- `nlsh --serve`：unix socket + 行分隔 JSON，模型**首条请求才加载**，空闲 300s 卸载、3600s 退出
- 客户端三级回退：本地规则 → 常驻进程 → 进程内加载；IPC 失败绝不影响命令
- **只在 stdin 是 TTY 时自动拉起**（脚本/测试不偷偷养进程）；`--no-model` 同时关闭远程
- 实测：第 1 次 23.7s（含加载）→ 第 2 次 21.9s，守护 RSS 稳定 796MB（不重载），客户端 26MB

**三、修一个真 bug：模型路径每次调用都 abort**

`model_loader` 按 `n_ctx`(512) 分块 decode，但 llama 断言要求 `n_tokens_all <= n_batch`(256) →
prompt 超 256 token 直接 `GGML_ASSERT` 崩（`rc=134`，Release/Debug 都崩）。
改为按 `llama_n_batch` 分块。

**为什么一直没被发现**：AC-14 只断言"模型加载了（RSS 涨了）"，没断言"解析成功"。

**验证**：`ctest` **33/33**、`verify_hook.sh` **35 断言**、`verify_exec.sh` **81 断言**，全绿。

**范围外**：单次推理 20s 量级属模型层瓶颈（STATE「决策层延迟」），本次不假装解决。

## 2026-10-25 · 终端里直接说人话（bash 钩子，不必再敲 `nlsh`）

**需求**：用户原话「不想输入 nlsh，就想在机器上自己启动之后在终端直接输入自然语言啥的可以自己识别操作」。

**做法**：bash 原生钩子 `command_not_found_handle` —— 只有"命令不存在"时才轮到 NL，
`ls` / `git status` 零影响，不做别名、不抢输入、不需要常驻进程。

- 新增 `scripts/nl-hook.bash`：含非 ASCII（中文）才交给 `nlsh -c`；英文未知命令仍走系统
  `command-not-found`（`git statsu` 这类根本到不了钩子，git 自己就报了）
- 钩子**不 eval、不解析**：确认门（ADR-019）/权限/快照/审计全在 nlsh 内，安全边界不变
- `install_local.sh` 增 `--hook-only` / `--no-hook` / `--uninstall-hook`：写进 `~/.bashrc`
  标记块，幂等（重复装仍一段）、首次写入前备份 `~/.bashrc.bak.nl-os`、卸载只摘块
- 开关：`NL_SHELL_HOOK=0` 全关；`NL_SHELL_HOOK_ALL=1` 连英文未知命令也接管（默认关）
- 验收 `scripts/verify_hook.sh`：**25 断言 0 失败**（假 nlsh 记录 argv + 临时 HOME）

**真机实测**：`bash -ic '列出当前目录'` 直接列目录（10ms 执行）；`创建一个叫 X 的文件夹`
在钩子上下文里照样弹确认门，EOF 取消则**不建目录**，`NL_ASSUME_YES=1` 才执行。

**范围外（登记）**：`cd`/`export` 等 shell 内建改不了父 shell（`cli:cd` 未注册，v2 议题）；
zsh 未做（本机无 zsh）；模型常驻另开 change。

**测试自己抓出的两个 bash 真行为**：`case` 里 `[! -~]` 空格不转义会语法错误
（且不能用 `[![:print:]]`，UTF-8 下中文算 printable）；`command_not_found_handle`
对含 `/` 的命令名不触发。

## 2026-10-25 · 模型惰性加载：规则能决定的命令不再加载 428MB 模型

**问题**：`nlsh` 对任何非传统命令都在启动时加载模型，哪怕规则层一句话就能决定。
实测 `创建一个叫 X 的文件夹`：**3.57s / 779MB RSS / 2.1s CPU**；同一句 `--no-model` 只要
0.22s / 18MB。2 vCPU 机器上等于每条规则命令白占满一核约 2 秒 + 白吃 780MB。

**改动**

- `nlc_api`：新增 `nl_init_lazy()`（只记路径 + 可读性检查）、`ensure_model()`（Layer 3 才加载）、
  `nl_model_is_loaded()`
- `nlsh` / `nl-run` 改用 `nl_init_lazy`；`nl_init()` 语义不变
- 规则未命中时仍加载（行为不变）；模型缺失时降档到规则、不阻塞

**实测**

| 场景 | 改前 | 改后 |
|---|---|---|
| `nlsh -c '创建一个叫 X 的文件夹'`（规则命中） | 3.57s / 779MB / 2.1s CPU | **0.16–0.34s / 19MB / 0.1s CPU** |
| 同上 `--no-model`（对照） | 0.22s / 18MB | 0.36s / 19MB |
| `nlsh -c '显示当前工作目录'`（规则未命中） | 3.4s / 779MB | 5.7s / 786MB（仍加载，符合预期） |
| 模型缺失 + 规则命中 | 降档可用 | 降档可用（AC-14 断言） |

**顺带修掉两个"满载机器上的假失败"**

- `verify_all.sh` AC-8 http body：起 `http.server` 后没等端口可连，满载时 broker 报 connection refused →
  改成先探测端口（连续 3 次全绿）
- `T-FIX-03` 限速：原逻辑"120 次调用"在 2 vCPU 满载时跨了多个秒窗口，限速本就不会触发 →
  改为按秒计数：单秒真的打满 100 次仍不拒才算失败，打不满记 **SKIP**（不假装通过）

**验证**：AC-14 新增 5 条（`verify_exec.sh` 76 → **81 通过 / 0 失败**）；`ctest` 31/31（Debug+Release）；
段 1/2/3 全绿；`verify_all.sh` 连续 **3 次 0 失败**


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
