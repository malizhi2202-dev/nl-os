# REQUIREMENT: NL-OS — AI 时代自然语言操作系统

- **Change ID**: nl-os
- **关联**: `@.specs/nl-os/CHANGE.md`、`@.specs/CONTEXT.md`
- **路径**: 完整

---

## 用户故事

### 模块 A：NL-Shell（自然语言终端交互）

- **US-A1**：作为 **Linux 用户**，我想在终端中用自然语言描述文件操作（创建/删除/移动/查看），以便**不需要记忆 `mkdir`/`find`/`tar` 等命令的具体 flag**。
- **US-A2**：作为 **Linux 用户**，我想在终端中用自然语言描述目录导航（进入某个目录/列出内容/查找项目），以便**快速在项目间切换而不需要手动 `cd` + `ls` + `find` 组合**。
- **US-A3**：作为 **Linux 用户**，我想在终端中用自然语言查看和管理进程（查看负载/搜索进程/发送信号），以便**不需要记忆 `ps`/`top`/`kill` 的参数组合**。
- **US-A4**：作为 **Linux 用户**，我想在 NL-Shell 中**混用自然语言和传统命令**（比如 `ls -la | grep nl` 和"找到最大的 3 个文件"可以在同一个会话中交替使用），以便**逐步过渡到 NL 交互方式而不丢失已有效率**。
- **US-A5**：作为 **Linux 用户**，我想在执行前看到 NL-Shell 把我的自然语言**翻译成了什么命令**（`--dry-run` 模式），以便**我可以确认理解正确，如果不正确可以手动编辑后再执行**。
- **US-A6**：作为 **Linux 用户**，当模型未加载或推理失败时，我想**规则引擎仍能处理高频命令**，以便**系统基本可用，不会因为模型问题而完全无法工作**。

### 模块 B：权限分级系统

- **US-B1**：作为 **Linux 用户**，当我执行危险的自然语言操作（如删除文件/目录）时，系统应该**明确警告并显示将要执行的命令**，要求我显式确认（输入 `yes`），以便**防止 NL 理解错误导致的数据丢失**。
- **US-B2**：作为 **Linux 用户**，某些极度危险的操作（如删除系统目录/裸写设备）应该被**硬拦截**——无论在 NL-Shell 中怎么描述，都不可能执行，以便**即使模型严重误解析，也不会造成系统级破坏**。
- **US-B3**：作为 **系统管理员**，我想配置哪些路径/命令属于 L1（安全）/L2（确认）/L3（高危）/L4（禁止），以便**根据组织安全策略调整权限级别**。
- **US-B4**：作为 **安全审计员**，我想所有 NL→命令的映射和执行结果都**记录到 syslog**，以便**事后追溯谁在什么时候通过 NL 执行了什么操作**。

### 模块 C：.tools 自然语言脚本

- **US-C1**：作为 **开发者/运维**，我想编写 `.tools` 文件（包含自然语言指令序列），并通过 `nl-run` 执行它，以便**用自然语言编写自动化脚本，替代繁琐的 bash 脚本**。
- **US-C2**：作为 **开发者**，我想在 `.tools` 首次执行后**锁定（freeze）**翻译结果，以便**后续执行不再经过模型推理，保证确定性和速度**。
- **US-C3**：作为 **开发者**，当 `.tools` 脚本中包含危险操作时，我想**在执行前看到完整的步骤预览**，并逐条确认或拒绝。

### 模块 D：推理引擎（支撑 A/B/C 的基础能力）

- **US-D1**：作为 **系统集成者**，推理引擎（`libnlc`）应该作为共享库提供，可以被 NL-Shell、权限守护、nl-run 复用，以便**模型只加载一次，节省内存**。
- **US-D2**：作为 **运维人员**，推理引擎在 ARM64 和 x86_64 上应该**相同的 API 和相同的行为**，以便**开发和部署在不同架构上一致**。

---

## 验收准则（AC）

### 模块 A · NL-Shell

#### AC-A1 · 基本文件创建

- **Given** 用户在 NL-Shell 中，当前目录为 `/tmp/test-nl`
- **When** 用户输入「创建一个叫 test 的文件夹」
- **Then** 系统执行 `mkdir test`，`/tmp/test-nl/test/` 目录被创建，终端显示成功信息
- **验证方式**: `echo '创建一个叫 test 的文件夹' | nlsh --no-model 2>&1 | grep -q "已创建目录" && test -d /tmp/test-nl/test`

#### AC-A2 · 文件删除（带确认）

- **Given** 用户在 NL-Shell 中，`/tmp/test-nl/test/` 存在
- **When** 用户输入「删除 test 目录」
- **Then** 系统判定为 L3 高危操作，显示警告和将要执行的命令 `rm -rf /tmp/test-nl/test`，等待用户输入 `yes` 确认。用户输入 `yes` 后执行删除。用户输入其他内容则取消。
- **验证方式**: 模拟终端输入，验证提示包含 `⚠️` 和 `rm` 命令内容，验证非 `yes` 输入不执行删除

#### AC-A3 · 安全路径硬拦截（L4）

- **Given** 用户在 NL-Shell 中
- **When** 用户输入「删掉根目录」
- **Then** 系统在 Layer 1 安全网关拦截，**不生成任何命令**，输出「❌ 此操作被禁止。原因：目标路径 `/` 在保护名单中。如需执行此操作，请使用原生 shell。」
- **验证方式**: `echo '删掉根目录' | nlsh 2>&1 | grep -q "禁止" && echo '删掉根目录' | nlsh 2>&1 | grep -qv "rm"`

#### AC-A4 · 传统命令透传

- **Given** 用户在 NL-Shell 中，传入 `-c` 参数
- **When** 用户输入 `ls -la /tmp`
- **Then** 系统识别为传统 shell 命令，直接透传给 `/bin/sh -c`，**输出与 `/bin/sh -c "ls -la /tmp"` 一致**（不含 NL-Shell 的 prompt 或提示信息）
- **验证方式**: `nlsh -c "ls -la /tmp" 2>/dev/null` 与 `/bin/sh -c "ls -la /tmp"` 的 stdout 输出 diff 为空

#### AC-A5 · 混合模式（NL + 传统命令 pipe）

- **Given** 用户在 NL-Shell 中，当前目录包含 `test.txt` 和 `other.txt`
- **When** 用户输入「列出当前目录的文件 | grep test」（在同一行中 NL 部分 + pipe + 传统命令）
- **Then** NL 部分「列出当前目录的文件」翻译为 `ls`，整体命令 `ls | grep test` 执行，输出只包含 `test.txt`
- **验证方式**: `nlsh -c "列出当前目录的文件 | grep test" 2>&1` 输出为 `test.txt`

#### AC-A5b · NL 翻译结果 pipe 到文件

- **Given** 用户在 NL-Shell 中
- **When** 用户输入「列出所有 python 文件 > py_list.txt」
- **Then** NL 部分翻译为 `find . -name "*.py"`，重定向正确执行，`py_list.txt` 包含结果
- **验证方式**: 执行后验证 `py_list.txt` 存在且内容正确

#### AC-A6 · Dry-run 模式

- **Given** 用户在 NL-Shell 中，启用了 `--dry-run`
- **When** 用户输入「创建一个叫 dryrun-test 的文件夹」
- **Then** 系统显示将执行的命令 `mkdir dryrun-test`，但**不实际创建目录**
- **验证方式**: `nlsh --dry-run -c "创建一个叫 dryrun-test 的文件夹" 2>&1 | grep "mkdir" && test ! -d dryrun-test`

#### AC-A7 · 降级模式（模型未加载）

- **Given** 模型文件不存在或加载失败
- **When** 用户输入「显示当前目录的文件列表」
- **Then** 规则引擎命中 → 翻译为 `ls` → 正常执行。同时终端提示「⚠️ 模型未加载，复杂指令可能无法理解。」
- **验证方式**: 删除模型文件后启动 nlsh，执行规则覆盖的命令，验证正常执行 + 提示存在

#### AC-A8 · 规则引擎命中率

- **Given** 100 条预设高频命令测试集（覆盖文件创建/删除/查看/移动、目录导航/列表、进程查看/搜索 8 个子类）
- **When** 在 `--no-model` 模式下逐条输入
- **Then** 至少 80 条正确映射为预期命令（规则命中率 ≥ 80%）
- **验证方式**: `nl-run benchmark/rule-coverage.test --no-model` 输出 `passed: >=80/100`

#### AC-A9 · 信号处理（Ctrl+C 中断 / Ctrl+D 退出）

- **Given** 用户在 NL-Shell REPL 中，模型推理正在运行（耗时操作）
- **When** 用户按下 Ctrl+C
- **Then** 推理立即中断，显示「⚠️ 操作已取消」，返回 REPL 提示符，不留僵尸进程
- **When** 用户按下 Ctrl+D（空输入行）
- **Then** NL-Shell 正常退出（exit code 0），不残留临时文件
- **验证方式**: 启动长时间推理（如 sleep 替代），Ctrl+C 验证返回 prompt；Ctrl+D 验证进程退出

#### AC-A10 · 输入验证与长度限制

- **Given** 用户在 NL-Shell REPL 中
- **When** 用户输入超过 4096 字节的内容
- **Then** 系统提示「⚠️ 输入过长（最大 4096 字符）」，拒绝处理，返回 prompt
- **When** 用户输入空行（仅回车）
- **Then** 不执行任何操作，返回新 prompt
- **When** 用户输入仅空白字符（空格/制表符）
- **Then** 同上，返回 prompt
- **验证方式**: 管道输入超长字符串验证截断行为；空输入验证不报错

#### AC-A11 · 传统命令判定边界

- **Given** 用户在 NL-Shell REPL 中
- **When** 用户输入以已知命令名开头的字符串（`ls -la`、`ps aux`、`cd /tmp`、`echo hello`）
- **Then** 判定为传统命令，直接透传执行，不经过 NL 处理管线
- **When** 用户输入以自然语言短语开头（「帮我...」「显示...」「创建...」）
- **Then** 判定为 NL 输入，进入四层管线处理
- **When** 用户输入单个词既是命令名又是自然语言词汇（如 `find`、`kill`）
- **Then** 如果有参数（`find . -name "*.c"`）→ 传统命令透传；如果无参数（`find`）→ 传统命令透传（保守策略：优先当命令）
- **验证方式**: 准备 50 条边界 case 测试集，验证分类正确率 100%

#### AC-A12 · 双失败处理（规则 + 模型均失败）

- **Given** 规则引擎未命中（无匹配模板），且模型推理也失败（模型崩溃/超时/返回无效 JSON）
- **When** 用户输入「把那个东西弄一下」（高度模糊的 NL 输入）
- **Then** 系统输出「❌ 无法理解你的意图。请用更具体的方式重新描述，或使用传统命令格式。」返回 prompt。**不执行任何命令，不猜测。**
- **验证方式**: 模拟规则引擎返回空 + 模型返回无效 JSON，验证系统拒绝执行且不崩溃

#### AC-A13 · Verbose 模式

- **Given** 用户以 `nlsh --verbose` 启动
- **When** 用户输入「删除超过 30 天的日志文件」
- **Then** 终端输出**每一层的处理结果**：
  - `[安全网关] 路径检查通过: /var/log/*.log`
  - `[规则引擎] 未命中，进入模型推理`
  - `[模型推理] intent=delete, confidence=0.87, latency=320ms`
  - `[模板填充] find /var/log -name "*.log" -mtime +30 -delete`
  - `[权限守护] 判定 L3，需要用户确认`
- **验证方式**: `nlsh --verbose -c "..."` 输出包含 `[安全网关]`/`[规则引擎]`/`[模型推理]`/`[模板填充]` 四个标签

### 模块 B · 权限分级

#### AC-B1 · L1 读操作自动放行

- **Given** 权限配置为默认（L1 自动放行）
- **When** 用户输入 NL 指令映射为 `ls`/`cat`/`ps`/`find`（只读操作）
- **Then** 命令直接执行，**无任何确认提示**
- **验证方式**: 对 L1 命令列表逐条验证，无一触发确认提示

#### AC-B2 · L2 修改操作需回车确认

- **Given** 权限配置为默认（L2 需确认）
- **When** 用户输入 NL 指令映射为 `mkdir`/`touch`/`echo > file`（修改操作）
- **Then** 显示将执行的命令 + 提示「按 Enter 确认执行，输入其他取消」
- **验证方式**: 验证确实显示了命令内容并要求确认

#### AC-B3 · L3 高危操作需输入 yes

- **Given** 权限配置为默认（L3 高危）
- **When** 用户输入 NL 指令映射为 `rm`/`rmdir`/`mv`（覆盖目标）
- **Then** 红色警告 `⚠️ 危险操作` + 显示命令 + 要求输入 `yes` 确认
- **验证方式**: 输入 `no` 后验证未执行；输入 `yes` 后验证已执行

#### AC-B4 · L4 禁止执行硬拦截

- **Given** 默认 L4 黑名单包含：
  - 关键系统目录：`/`, `/etc`, `/boot`, `/sys`, `/proc`, `/lib`, `/lib64`, `/usr/lib`, `/usr/lib64`
  - 块设备：`/dev/sd*`, `/dev/nvme*`, `/dev/mmcblk*`, `/dev/dm-*`, `/dev/loop*`, `/dev/vd*`
  - 关键系统文件：`/etc/passwd`, `/etc/shadow`, `/etc/sudoers`, `/boot/vmlinuz*`
- **When** 用户输入 NL 指令，其映射的命令涉及 L4 路径（包括通过符号链接、`../` 等间接方式）
- **Then** 硬拦截，显示 `❌ 此操作被禁止`，**不生成也不显示任何命令**
- **验证方式**: 遍历 L4 黑名单路径（共 20+ 条），逐一验证不生成命令；遍历 3 种等价 NL 表述（删掉/清空/格式化+路径），验证全拦截

#### AC-B5 · 符号链接规范化

- **Given** 创建符号链接 `/tmp/test-nl/link → /etc`
- **When** 用户输入「删除 /tmp/test-nl/link 下的内容」
- **Then** `realpath()` 解析后目标为 `/etc`，触发 L4 硬拦截
- **验证方式**: 验证符号链接被正确解析，L4 拦截生效

#### AC-B6 · `../` 路径遍历检测

- **Given** 当前目录为 `/home/user/projects`
- **When** 用户输入「删除 ../../../etc 下的配置文件」
- **Then** `realpath("../../../etc")` = `/etc`，触发 L4 硬拦截
- **验证方式**: 验证路径遍历被规范化后正确拦截

#### AC-B7 · 管理员可配置权限规则

- **Given** 用户编辑 `/etc/nl-os/perm_rules.conf`，添加 L4 路径 `/mnt/backup`
- **When** 权限守护重新加载配置（`kill -HUP` 或重启）
- **Then** 对 `/mnt/backup` 的操作触发 L4 拦截
- **验证方式**: 修改配置 → 重载 → 验证新规则生效

#### AC-B8 · 审计日志

- **Given** 权限守护正常运行
- **When** 用户通过 NL-Shell 执行了任何操作（L1/L2/L3/L4）
- **Then** syslog 记录包含：时间戳、用户名、原始 NL 输入、生成的命令、权限级别、执行结果（通过/拒绝/用户取消）
- **验证方式**: `journalctl -t nl-perm` 或 `grep nl-perm /var/log/syslog`

#### AC-B9 · Unicode 与特殊字符注入防护

- **Given** 用户在 NL-Shell 中
- **When** 用户输入包含以下任一内容：
  - Unicode 零宽字符（`​`, `‌`, `‍`, `﻿`）
  - Unicode bidi 控制字符（`‪`, `‫`, `‬`, `‭`, `‮`, `⁦`, `⁧`, `⁨`, `⁩`）
  - ANSI 转义序列（`\x1b[...`）
  - 终端控制字符（`\x00`-`\x08`, `\x0b`, `\x0c`, `\x0e`-`\x1f`, `\x7f`）
- **Then** 系统**剥离**不安全字符后处理 NL 输入，或拒绝含控制字符的输入并提示「⚠️ 输入包含不可见字符，已清理」。
  - 零宽字符和 bidi 控制字符：**剥离后处理**
  - ANSI 转义序列：**剥离后处理**
  - 终端控制字符（NULL, BEL, BS 等）：**拒绝并提示**
- **验证方式**: 构造含上述字符的 10 条测试输入，验证不被注入或导致未定义行为

### 模块 C · .tools 脚本

#### AC-C1 · 基本 .tools 执行

- **Given** 创建文件 `test.tools`，内容为：
  ```
  创建一个叫 build 的目录
  在 build 目录里创建一个叫 README.md 的空文件
  列出 build 目录的内容
  ```
- **When** 执行 `nl-run test.tools`
- **Then** 三条 NL 指令依次翻译并执行：`mkdir build` → `touch build/README.md` → `ls build/`，输出包含 `README.md`
- **验证方式**: 执行 `.tools` 后验证目录和文件存在

#### AC-C2 · .tools 执行前预览

- **Given** 创建 `danger.tools`，包含一条 L3 高危操作「删除 build 目录」
- **When** 执行 `nl-run danger.tools`
- **Then** 在执行到高危步骤前，显示步骤预览，要求用户确认（`yes`），其他步骤不受影响
- **验证方式**: 验证高危步骤确实触发了确认提示

#### AC-C3 · .tools Freeze 锁定

- **Given** 执行过 `nl-run test.tools` 并成功
- **When** 执行 `nl-run --freeze test.tools`
- **Then** 生成 `test.tools.frozen` 文件（或同目录下的缓存），包含已翻译的具体命令。之后执行 `nl-run test.tools` 时直接使用 frozen 版本，**不经过模型推理**。
- **验证方式**: frozen 文件存在 → 删除模型 → 执行 .tools 仍然成功（使用 frozen 版本）

#### AC-C4 · .tools 权限预授权

- **Given** `.tools` 文件头部包含 `# @perm: allow-l2`
- **When** 执行该 `.tools`
- **Then** L2 级别操作自动确认（不需要用户逐条按 Enter），L3 仍需要输入 `yes`
- **验证方式**: 验证 L2 自动跳过确认，L3 仍需确认

#### AC-C5 · .tools 步骤失败处理

- **Given** 创建 `fail.tools`，共 3 条指令，第 2 条故意失败（如「删除一个不存在的目录」）
- **When** 执行 `nl-run fail.tools`
- **Then** 第 1 条正常执行，第 2 条失败时输出 `❌ 步骤 2 失败: <错误信息>`，**第 3 条不再执行**（fail-fast）。`nl-run` 返回非零 exit code（具体值 = 失败的步骤编号）。
- **验证方式**: 执行 fail.tools 后验证第 3 条未被执行的副作用不存在

#### AC-C6 · nl-run exit code

- **Given** .tools 脚本包含 N 条指令
- **When** 全部成功执行
- **Then** `nl-run` exit code = 0
- **When** 第 K 条指令失败（K ≥ 1）
- **Then** `nl-run` exit code = K（失败的步骤编号）
- **When** .tools 脚本为空或不存在
- **Then** exit code = -1，stderr 输出错误原因
- **验证方式**: `nl-run test.tools && echo "PASS" || echo "FAIL, exit=$?"`

#### AC-C7 · .tools 步骤间上下文传递

- **Given** 创建 `context.tools`：
  ```
  创建一个叫 project 的目录
  进入 project 目录
  创建一个叫 main.c 的文件
  ```
- **When** 执行 `nl-run context.tools`
- **Then** 第 2 步「进入 project 目录」翻译为 `cd project`，第 3 步文件创建在 `project/` 目录内（非当前工作目录）。**步骤间的上下文（当前工作目录）在 .tools 执行器内部维护和传递。**
- **验证方式**: 执行后验证 `project/main.c` 存在，而非 `./main.c`

### 模块 D · 推理引擎

#### AC-D1 · 模型共享加载

- **Given** 两个进程（NL-Shell 和 nl-run）同时运行
- **When** 两个进程各自初始化 `libnlc`
- **Then** 模型文件只被 mmap 一次（共享内存），两个进程的内存 RSS 之和 ≈ 模型大小 + 进程自身开销（而非 2 × 模型大小）
- **验证方式**: `/proc/<pid1>/smaps` + `/proc/<pid2>/smaps` 验证共享内存页

#### AC-D2 · 结构化输出（JSON Schema 约束）

- **Given** 模型推理输入为「把 downloads 里大于 100M 的视频删掉」
- **When** 推理完成
- **Then** 输出符合 JSON Schema：
  ```json
  {
    "intent": "string (enum: create_file|create_dir|delete|move|copy|list_dir|view_file|find_files|change_dir|show_process|search_content|run_cmd|other)",
    "entities": {
      "target": "string | array<string>",
      "location": "string (optional)",
      "condition": "string (optional)",
      "filter_type": "string (optional, enum: size_greater|size_less|mtime_before|mtime_after|name_pattern|none)",
      "filter_value": "string (optional)"
    },
    "confidence": "float (0.0-1.0)"
  }
  ```
- **验证方式**: 对 100 条测试输入，100% 输出符合 Schema（不符合则重试，最多 3 次）

#### AC-D3 · 跨架构一致性

- **Given** 同一份 GGUF 模型文件
- **When** 分别在 x86_64 和 ARM64 上，对 100 条相同输入做推理
- **Then** intent 分类结果**完全一致**（100/100），实体提取结果一致率 ≥ 98%（允许浮点精度差异导致的极少量不一致）。任何不一致的 case 必须人工审查。
- **验证方式**: 两个架构跑同一 benchmark，diff 对比结果。不一致条目 < 2 条，且每条不一致条目必须符合"实体提取中的可选字段差异，不影响命令语义"的标准。

#### AC-D4 · 低置信度拒绝

- **Given** 模型推理完成，返回 JSON 含 `confidence` 字段
- **When** `confidence < 0.6`（低置信度阈值，可配置）
- **Then** 系统**拒绝执行**，输出「⚠️ 对以下意图理解不确定（置信度: X%），请用更具体的方式重新描述。」显示模型猜测的 intent 供用户参考，但不执行。
- **When** `confidence ≥ 0.6`
- **Then** 正常进入模板填充阶段
- **验证方式**: 准备 10 条故意模糊的输入，验证低置信度时拒绝执行率 ≥ 90%

#### AC-D5 · API 预留 Agent 扩展点

- **Given** `src/libnlc/nlc_api.h` 头文件
- **When** 审查 API 定义
- **Then** 必须存在以下函数声明（或等效的结构体定义）：
  - `nl_plan()` 或 `NlPlan` 结构体（支持返回步骤序列，MVP 返回单步）
  - `nl_execute_step()` 或等效的单步执行接口
  - `nl_observe()` 或等效的结果观察接口
  - 当前 MVP 实现单步退化版本（`nl_plan` 永远返回 1 步）
- **验证方式**: `grep -E "nl_plan|nl_execute_step|nl_observe" src/libnlc/nlc_api.h` 有输出

#### AC-D6 · Benchmark 建设标准

- **Given** 需要建立模型评测基准
- **When** 编写 `models/benchmarks/nl-commands-zh.json`
- **Then** 测试集满足以下标准：
  - **总量**: ≥ 1000 条中文 NL→结构化输出对
  - **覆盖**: 所有 12 个 intent 类型，每个 ≥ 30 条
  - **难度分布**: easy 40% / medium 40% / hard 20%
  - **对抗性样本**: ≥ 100 条（含路径遍历、同义词替换、多义表达、否定句式）
  - **格式**: 每条含 `input`（NL 文本）, `expected_intent`, `expected_entities`, `difficulty`, `category`
  - **来源**: 人工标注 + 真实用户日志（MVP 发布后补充）
- **验证方式**: `python3 models/eval/validate_benchmark.py` 检查覆盖率、格式、数量

---

## 范围切分

### v1（本次必做 · MVP）

- ✅ NL-Shell REPL：自然语言输入 → 命令执行（文件/目录/进程 3 类）
- ✅ 规则引擎：覆盖 80% 高频命令（100+ 条规则模板）
- ✅ 模型推理兜底：意图分类 + 实体提取（结构化输出）
- ✅ 4 级权限（L1-L4）+ 安全网关前置
- ✅ 传统命令透传
- ✅ `--dry-run` + `--verbose` 模式
- ✅ `.tools` 脚本执行（基本版，不含 freeze）
- ✅ 审计日志（syslog）
- ✅ Benchmark 测试集（1000 条）+ 评估脚本
- ✅ `libnlc` 共享库 API（预留 Agent 扩展点）
- ✅ `nl-perm` 权限守护进程（systemd 管理）
- ✅ CMake 构建系统 + ARM64/x86_64 交叉编译
- ✅ 降级模式（模型未加载时规则引擎独立运行）

### v2（下一轮考虑）

- 🔲 `.tools` freeze 锁定机制
- 🔲 `.tools` 预授权声明（`# @perm:`）
- 🔲 NL-C 完整编程语言（`.nl` 格式，函数/包/导入）
- 🔲 NL-C 包系统（文件操作包/网络操作包/系统管理包）
- 🔲 动态 few-shot 检索（相似度匹配增强推理准确率）
- 🔲 模型 OTA 更新（签名校验/增量更新/回滚）
- 🔲 eBPF 内核权限拦截
- 🔲 会话上下文（`$参照上一次`）
- 🔲 Agent Loop（Planner → Executor → Observer 多步推理）

### out（永远不做 · 或无限期推迟）

- ❌ GUI 界面（NL-OS 是命令行工具）
- ❌ 语音输入/输出
- ❌ 真正的内核模块（.ko）—— eBPF 是内核算力的安全边界，不越过这条线
- ❌ 模型训练/微调管线（NL-OS 是推理运行时，不是训练框架）
- ❌ Windows/macOS 支持（NL-OS 深度依赖 Linux 内核特性）
- ❌ 云端模型调用（NL-OS 的核心价值 = 本地运行）
- ❌ 包市场/应用商店（保持核心精简，生态交给发行版）

---

## 非功能性需求

- **性能**:
  - NL-Shell 冷启动（不含模型加载）< 100ms
  - 模型首次加载 < 2s（ARM64, Q4_0 量化, 0.5B 模型）
  - 规则匹配延迟 < 1ms（P99）
  - 模型推理延迟 < 500ms（P99, 单次意图分类 + 实体提取）
  - NL-Shell 进程空闲内存 < 50MB（不含模型）, < 500MB（含模型）
  - 权限守护进程内存 < 10MB

- **安全**:
  - L4 硬拦截：涉及的路径/命令**在任何情况下都不能通过 NL-Shell 执行**（即使模型输出恶意 JSON）
  - 路径白名单：仅 `/home /tmp /var /opt /mnt` 允许写操作，系统路径默认只读
  - 权限守护 fail-closed：守护进程不可达时，NL-Shell 拒绝所有 NL 操作（传统命令透传不受影响）
  - 权限守护自身 seccomp 沙箱：限制可用的系统调用白名单
  - 不依赖外部网络（模型推理纯本地，不发送任何数据出去）

- **兼容性**:
  - 架构：Linux x86_64（glibc ≥ 2.28）、ARM64（aarch64, glibc ≥ 2.28）
  - 编译器：GCC ≥ 9、Clang ≥ 14
  - C 标准：C11（确保 ARM 交叉编译链兼容性）
  - 内核：Linux ≥ 5.10（seccomp 支持）

- **可观测性**:
  - 所有 NL→命令映射写入 syslog（`nl-perm` facility）
  - NL-Shell 提供 `--verbose` 模式：输出每一层的处理结果（安全网关 → 规则引擎 → 模型推理 → 模板填充）
  - 权限守护提供 `--stats` 模式：输出各级别操作计数（L1 N次/L2 N次/L3 N次/L4 N次）
  - 模型推理提供 `--debug` 模式：输出原始 prompt + 原始模型输出 + 解析后的 JSON

- **二进制体积**（`strip` 后）:
  - `libnlc.so` < 2MB（不含模型数据）
  - `nlsh` < 500KB
  - `nl-perm` < 300KB
  - `nl-run` < 200KB

---

## 依赖与假设

### 外部依赖

| 依赖 | 版本 | 用途 | 管理方式 |
|---|---|---|---|
| ggml | 锁定 commit（TBD） | 张量运算 + GGUF 加载 | vendor 到 `third_party/ggml/` |
| cJSON | v1.7.x | JSON 解析（模型输出） | vendor 到 `third_party/cJSON/` |
| greatest / cmocka | latest | C 单元测试框架 | 测试依赖，不进入发布 |
| pthread | POSIX | 多线程（模型推理） | 系统库 |
| CMake | ≥ 3.16 | 构建系统 | 系统包管理器 |

### 关键假设

1. **用户已有一个可用的 Linux 终端环境**（bash/zsh/fish）——NL-Shell 替换的是 shell 前端，不替换内核
2. **模型文件由发行版或用户自行提供**——NL-OS 不捆绑模型，提供下载引导
3. **用户具有一定的中文自然语言表达能力**——系统对高度模糊/歧义的输入可能拒绝执行并反问
4. **权限守护运行在 systemd 管理的 Linux 上**——非 systemd 发行版需要手动管理守护进程
5. **目标硬件至少有 2GB 可用 RAM**（树莓派 4/工控机/普通 PC）
6. **MVP 阶段模型不做微调**——使用开源预训练模型 + prompt 工程达到可用准确率

---

> AC 是 TEST 阶段派生用例的唯一来源，禁止在 TEST 阶段引入新 AC。
