# TASK: NL-OS — AI 时代自然语言操作系统

- **Change ID**: nl-os
- **关联**: `@.specs/nl-os/REQUIREMENT.md`、`@.specs/nl-os/DESIGN.md`

---

## 波次划分

```
Wave 1 (parallel · 基础设施):          T01[P]  T02[P]  T03[P]
                                        |       |       |
Wave 2 (parallel · 规则+模板):          T04[P]  T05[P]
         (depends on T01)                |       |
                                        └───┬───┘
                                            │
Wave 3 (模型推理):                       T06
         (depends on T03)                 |
                                            │
Wave 4 (parallel · 库组装+权限):   T07[P]  T08[P]
         (depends on T02,T04,T05,T06)      |       |
                                           │       |
Wave 5 (NL-Shell):                        T09
         (depends on T07,T08)              |
                                           │
                                   ┌───────┴───────┐
                                   │               │
Wave 6 (parallel · 收尾):    T10[P] T11[P] T12[P] T13[P]
         (depends on T09)       │      │      │      │
                                └──────┴──────┴──────┘
```

> 14 个任务，6 个波次。同 wave = 可并行（`[P]` 标记）。

---

## 任务清单

```xml
<task id="T01" parallel="true" status="pending">
  <name>项目骨架：CMake + 目录结构 + cJSON</name>
  <read_files>
    .specs/nl-os/DESIGN.md
    .specs/CONTEXT.md
    third_party/cJSON/*
  </read_files>
  <write_files>
    CMakeLists.txt
    cmake/toolchain-arm64.cmake
    src/CMakeLists.txt
    third_party/cJSON/cJSON.c
    third_party/cJSON/cJSON.h
    src/libnlc/json_helper.c
    src/libnlc/json_helper.h
    tests/CMakeLists.txt
    .gitignore
    .clang-format
  </write_files>
  <action>
    1. 创建顶层 CMakeLists.txt（C11, -Wall -Werror, 定义 libnlc/nlsh/nl-perm/nl-run 四个 target）
    2. 创建 cmake/toolchain-arm64.cmake（ARM64 交叉编译，glibc ≥ 2.28）
    3. 创建 src/ 和 tests/ 目录结构（按 DESIGN §2.4 模块依赖图）
    4. vendor cJSON v1.7.x 到 third_party/cJSON/
    5. 实现 json_helper.c/h（cJSON 封装：nl_json_parse / nl_json_get_string / nl_json_get_int / nl_json_get_float / nl_json_free）
    6. 配置 .clang-format（LLVM 风格，4 空格缩进）
  </action>
  <verify>mkdir -p build && cd build && cmake .. -DCMAKE_BUILD_TYPE=Debug && make -j$(nproc) && ./tests/test_json_helper</verify>
  <done>CMake 构建成功；json_helper 单元测试通过；ARM64 toolchain 文件存在</done>
  <depends_on></depends_on>
</task>

<task id="T02" parallel="true" status="pending">
  <name>安全网关 Layer 1：path_safety</name>
  <read_files>
    .specs/nl-os/DESIGN.md
    .specs/nl-os/REQUIREMENT.md
    .specs/adr/002-permission-model.md
    src/libnlc/json_helper.h
  </read_files>
  <write_files>
    src/libnlc/path_safety.c
    src/libnlc/path_safety.h
    tests/unit/path_safety_test.c
  </write_files>
  <action>
    实现路径安全模块（DESIGN §2.2 Layer 1），对应 AC-B4/B5/B6/B9：
    1. nl_path_normalize(path) → 调用 realpath() 解析符号链接和 ../
    2. nl_path_whitelist_check(path) → 仅 /home /tmp /var /opt /mnt 允许写操作
    3. nl_path_blacklist_check(path) → 拦截 / /etc /boot /sys /proc /dev/sd* /dev/nvme* /dev/mmcblk* /dev/dm-* /dev/loop* /dev/vd*
    4. nl_input_sanitize(input) → 剥离 Unicode 控制字符/零宽字符/bidi 字符/ANSI 转义序列
    5. 返回 NlPathResult {status: PASS/REJECT, reason: string}
    白名单和黑名单用编译期常量数组定义（可被 perm_rules.conf 覆盖的设计预留注释）
  </action>
  <verify>cd build && make && ./tests/test_path_safety</verify>
  <done>path_safety 单元测试通过；覆盖：正常路径/符号链接/../遍历/零宽字符/黑名单路径/白名单边界 6 类 case</done>
  <depends_on></depends_on>
</task>

<task id="T03" parallel="true" status="pending">
  <name>ggml vendor + model_loader stub</name>
  <read_files>
    .specs/nl-os/DESIGN.md
    .specs/adr/006-dependency-strategy.md
    .specs/adr/010-c11-gguf-lock.md
  </read_files>
  <write_files>
    third_party/ggml/ggml.c
    third_party/ggml/ggml.h
    third_party/ggml/ggml-cpu.c
    third_party/ggml/ggml-cpu.h
    third_party/ggml/ggml-impl.h
    third_party/ggml/gguf.c
    third_party/ggml/gguf.h
    third_party/ggml/README.nl-os
    src/libnlc/model_loader.c
    src/libnlc/model_loader.h
    tests/unit/model_loader_test.c
  </write_files>
  <action>
    1. 从 ggml 上游锁定一个稳定 commit → vendor 核心文件到 third_party/ggml/
    2. 创建 third_party/ggml/README.nl-os 记录锁定 commit hash + 日期 + 升级流程
    3. 实现 model_loader.c/stub 版本（MVP 先不链接 ggml，用 stub 返回固定值）：
       - nl_model_init(path) → 返回成功 stub（真实加载待 T06）
       - nl_model_get_info() → 返回模型元信息 stub
       - nl_model_free()
    4. stub 版本的目的是验证构建系统和 API 设计，真实推理在 T06 接入
  </action>
  <verify>cd build && cmake .. -DBUILD_STUB_MODEL=ON && make -j$(nproc) && ./tests/test_model_loader</verify>
  <done>ggml 文件已 vendor；model_loader stub 编译通过且测试通过</done>
  <depends_on></depends_on>
</task>

<task id="T04" parallel="true" status="pending">
  <name>规则引擎 Layer 2：rule_engine</name>
  <read_files>
    .specs/nl-os/DESIGN.md
    .specs/nl-os/REQUIREMENT.md (AC-A1~A8, AC-A11)
    .specs/adr/001-four-layer-pipeline.md
  </read_files>
  <write_files>
    src/libnlc/rule_engine.c
    src/libnlc/rule_engine.h
    src/libnlc/rules_db.h
    tests/unit/rule_engine_test.c
    tests/component/rule_coverage_test.c
  </write_files>
  <action>
    实现规则引擎（DESIGN §2.2 Layer 2），对应 AC-A8（规则命中率 ≥ 80%）：
    1. 定义规则结构体 NlRule {pattern, intent, template_id, slots[]}
    2. 在 rules_db.h 中预定义 100+ 条高频规则，覆盖 12 种 intent：
       - 文件创建/删除/移动/复制/查看（~40 条）
       - 目录导航/列表/切换（~25 条）
       - 进程查看/搜索（~15 条）
       - 其他常用操作（~20 条，如 find/tar/chmod/grep）
    3. nl_rule_match(input) → NlRuleResult {matched: bool, rule_id, intent, slots}
    4. 匹配算法：分词 → 关键词 hash 查找 → 模板槽位提取
    5. 目标：O(1) 查找，< 1ms 延迟
    规则格式示例：
    {"创建" ("一个"|"个")? "叫" {name:word} "的" ("文件夹"|"目录")} → intent=create_dir, slot={name}
  </action>
  <verify>cd build && make && ./tests/test_rule_engine && ./tests/test_rule_coverage</verify>
  <done>规则引擎单元测试通过；规则覆盖率测试 ≥ 80/100 命中</done>
  <depends_on>T01</depends_on>
</task>

<task id="T05" parallel="true" status="pending">
  <name>模板填充 Layer 4：template_fill</name>
  <read_files>
    .specs/nl-os/DESIGN.md
    .specs/nl-os/REQUIREMENT.md (AC-D2 结构化输出 Schema)
    src/libnlc/rule_engine.h
  </read_files>
  <write_files>
    src/libnlc/template_fill.c
    src/libnlc/template_fill.h
    src/libnlc/templates_db.h
    tests/unit/template_fill_test.c
  </write_files>
  <action>
    实现模板填充模块（DESIGN §2.2 Layer 4）：
    1. 在 templates_db.h 中定义 12 种 intent 各 1~3 个命令模板：
       create_file: "touch {path}/{name}"
       create_dir:  "mkdir -p {path}/{name}"
       delete:      "rm -rf {path}"（需权限审核标注）
       move:        "mv {source} {dest}"
       list_dir:    "ls -la {path}"
       change_dir:  "cd {path}"
       show_process:"ps aux | grep {name}" / "ps aux --sort=-%mem | head -{n}"
       find_files:  "find {path} -name '{pattern}' -{filter} {value}"
       view_file:   "cat {path}" / "less {path}"
       search_content: "grep -rn '{pattern}' {path}"
       run_cmd:     "{command}"（透传）
       other:       返回 NULL → 触发"无法理解"提示
    2. nl_template_fill(intent, entities) → NlCommand {cmd_string, perm_level, paths[]}
    3. 填充后再次调用 path_safety 对 paths[] 做最终安全检查
    4. 返回的 perm_level 供权限守护使用
  </action>
  <verify>cd build && make && ./tests/test_template_fill</verify>
  <done>template_fill 单元测试通过；12 种 intent 模板全部覆盖；路径安全检查二次调用正常</done>
  <depends_on>T01</depends_on>
</task>

<task id="T06" parallel="false" status="pending">
  <name>模型推理 Layer 3：intent_extract + few_shot</name>
  <read_files>
    .specs/nl-os/DESIGN.md
    .specs/nl-os/REQUIREMENT.md (AC-D2, AC-D3, AC-D4)
    .specs/adr/001-four-layer-pipeline.md
    .specs/adr/004-model-selection.md
    src/libnlc/model_loader.h
    src/libnlc/json_helper.h
  </read_files>
  <write_files>
    src/libnlc/intent_extract.c
    src/libnlc/intent_extract.h
    src/libnlc/few_shot.c
    src/libnlc/few_shot.h
    tests/unit/intent_extract_test.c
    tests/model/schema_validation_test.c
  </write_files>
  <action>
    实现模型推理模块（DESIGN §2.2 Layer 3），对应 AC-D2/D3/D4：
    1. 接入 ggml（替换 T03 的 stub）：
       - nl_model_load(path) → 加载 GGUF v3，mmap，返回 NlModel
       - nl_model_infer(model, prompt) → 推理，返回 token 序列
    2. intent_extract.c：
       - 构建 prompt（system prompt + few-shot examples + user input）
       - 调用 ggml 推理 → 采样 → 解析输出为 JSON
       - JSON Schema 校验（必须符合 AC-D2 定义的 12 种 intent + entities + confidence）
       - 校验失败 → 重试（最多 3 次）
       - confidence < 0.6 → 返回 NlIntentResult{status: LOW_CONFIDENCE}
    3. few_shot.c（MVP stub，二期实现动态检索）：
       - 静态返回 3 条固定 few-shot 示例
       - 接口预留：nl_few_shot_select(input, k) → 返回 top-k 相似样本
    4. 模型文件路径从环境变量 NL_MODEL_PATH 读取，未设置则提示用户
  </action>
  <verify>cd build && cmake .. -DBUILD_STUB_MODEL=OFF && make -j$(nproc) && ./tests/test_intent_extract && ./tests/test_schema_validation</verify>
  <done>intent_extract 编译通过；schema 校验 100 条测试 100% 符合 JSON Schema；低置信度拒绝逻辑正常</done>
  <depends_on>T03</depends_on>
</task>

<task id="T07" parallel="true" status="pending">
  <name>libnlc 公共 API + 四层管线组装</name>
  <read_files>
    .specs/nl-os/DESIGN.md
    .specs/adr/008-agent-architecture.md
    src/libnlc/path_safety.h
    src/libnlc/rule_engine.h
    src/libnlc/intent_extract.h
    src/libnlc/template_fill.h
    src/libnlc/model_loader.h
  </read_files>
  <write_files>
    src/libnlc/nlc_api.h
    src/libnlc/nlc_api.c
    tests/unit/nlc_api_test.c
  </write_files>
  <action>
    实现 libnlc 公共 API（DESIGN §2.4），对应 AC-D1/D5：
    1. nlc_api.h 公共接口（7 个函数）：
       - nl_init(model_path) → 加载模型，返回 handle
       - nl_parse(handle, input, &result) → 完整四层管线
       - nl_plan(handle, goal, &plan) → 步骤序列（MVP 返回 1 步）
       - nl_execute_step(step, &result) → 执行单步
       - nl_observe(result, &obs) → 观察结果（MVP 返回 DONE）
       - nl_get_last_error() → 错误信息
       - nl_free(handle) → 释放资源
    2. nlc_api.c 中组装四层管线：
       input → nl_input_sanitize → nl_rule_match
         → (未命中) nl_intent_extract → nl_template_fill
         → NlParseResult {intent, entities, command, perm_level, confidence, source_layer}
    3. NlParseResult 结构体：
       - source_layer: RULE_ENGINE | MODEL_INFERENCE → 供 verbose 模式显示
    4. nl_plan/nl_execute_step/nl_observe MVP 实现为单步退化版本（ADR-008）
  </action>
  <verify>cd build && make && ./tests/test_nlc_api</verify>
  <done>nlc_api 编译为 libnlc.so；四层管线组装正确；nl_parse 输入→输出端到端跑通（含规则命中+模型兜底两条路径）</done>
  <depends_on>T02,T04,T05,T06</depends_on>
</task>

<task id="T08" parallel="true" status="pending">
  <name>权限守护 nl-perm 完整实现</name>
  <read_files>
    .specs/nl-os/DESIGN.md
    .specs/nl-os/REQUIREMENT.md (AC-B1~B9)
    .specs/adr/002-permission-model.md
    .specs/adr/007-module-boundaries.md
    src/libnlc/path_safety.h
  </read_files>
  <write_files>
    src/nl-perm/main.c
    src/nl-perm/perm_engine.c
    src/nl-perm/perm_engine.h
    src/nl-perm/ipc_server.c
    src/nl-perm/ipc_server.h
    src/nl-perm/audit_log.c
    src/nl-perm/audit_log.h
    src/nl-perm/seccomp_filter.c
    src/nl-perm/seccomp_filter.h
    etc/perm_rules.conf
    etc/nl-perm.service
    tests/unit/perm_engine_test.c
    tests/component/perm_ipc_test.c
  </write_files>
  <action>
    实现权限守护完整功能（DESIGN §2.1 右半），对应 AC-B1~B9：
    1. main.c：daemon 化（fork, setsid, chdir /, 关闭 stdin/stdout），信号处理（SIGHUP 重载配置, SIGTERM 优雅退出）
    2. perm_engine.c：
       - nl_perm_check(request) → NlPermResult {decision, level, reason, audit_id}
       - L1 判定：command_type 为只读 → allow
       - L2 判定：write/create → confirm
       - L3 判定：delete/overwrite → confirm（需 yes）
       - L4 判定：paths 命中黑名单 → deny（不生成命令）
       - 白名单检查：paths 必须在 /home /tmp /var /opt /mnt 内
    3. ipc_server.c：Unix domain socket (/run/nl-os/perm.sock)，SOCK_STREAM，JSON 协议（DESIGN §2.3）
    4. audit_log.c：syslog 写入（LOG_AUTH facility），NL→命令映射 + 结果
    5. seccomp_filter.c：限制自身系统调用（open/read/write/close/socket/accept/send/recv/syslog 白名单）
    6. etc/perm_rules.conf：可配置 L4 黑名单 + 白名单路径，格式：# 注释 + path /pattern L4
    7. etc/nl-perm.service：systemd unit（Type=notify, Restart=always, ProtectSystem=strict）
  </action>
  <verify>cd build && make && ./tests/test_perm_engine && ./tests/test_perm_ipc</verify>
  <done>nl-perm 编译通过；perm_engine 单元测试通过（覆盖 L1/L2/L3/L4 判定）；IPC 通信测试通过；systemd unit 文件语法正确</done>
  <depends_on>T01</depends_on>
</task>

<task id="T09" parallel="false" status="pending">
  <name>NL-Shell 完整实现 + IPC 客户端联调</name>
  <read_files>
    .specs/nl-os/DESIGN.md
    .specs/nl-os/REQUIREMENT.md (AC-A1~A13)
    .specs/adr/007-module-boundaries.md
    src/libnlc/nlc_api.h
    src/nl-perm/ipc_server.h
  </read_files>
  <write_files>
    src/nlsh/main.c
    src/nlsh/dispatch.c
    src/nlsh/dispatch.h
    src/nlsh/history.c
    src/nlsh/history.h
    src/nlsh/line_edit.c
    src/nlsh/line_edit.h
    src/nlsh/ipc_client.c
    src/nlsh/ipc_client.h
    etc/nlsh.conf
    tests/integration/nlsh_basic_test.sh
    tests/integration/nlsh_perm_test.sh
  </write_files>
  <action>
    实现 NL-Shell REPL 完整功能（DESIGN §2.1 左半），对应 AC-A1~A13：
    1. main.c：
       - 解析命令行参数：--dry-run / --verbose / --no-model / --debug / -c "cmd"
       - 初始化：dlopen libnlc.so → nl_init() → 连接权限守护 socket
       - REPL 循环：显示 prompt → 读取输入 → dispatch → 显示结果
       - 信号处理：SIGINT (Ctrl+C) 中断推理，SIGQUIT 忽略，SIGTERM 优雅退出
    2. dispatch.c（AC-A4/A11）：
       - nl_dispatch(input) → 判断传统命令 or NL 输入
       - 传统命令判定：以已知命令名开头（ls/cd/ps/echo/find/grep/...）或有 pipe/redirect 语法
       - 传统命令 → popen("/bin/sh -c", input)
       - NL 输入 → nl_parse() → ipc_client 请求权限 → 根据 decision 执行
    3. history.c：内存环形缓冲（默认 1000 条）+ 文件持久化（~/.nlsh_history）
    4. line_edit.c：简易行编辑器（AC-A9）：
       - 支持 Backspace/Delete/Left/Right/Home/End
       - 支持 Ctrl+C 中断 / Ctrl+D 退出（空输入时）
       - 输入长度上限 4096 字节（AC-A10）
       - 不依赖 GNU Readline（避免 GPL 传染）
    5. ipc_client.c：连接权限守护，发送 REQUEST → 接收 RESPONSE（DESIGN §2.3）
    6. etc/nlsh.conf：默认配置（prompt 样式/历史文件路径/权限守护 socket 路径）
    7. 集成测试脚本验证基本功能
  </action>
  <verify>cd build && make && ./tests/integration/nlsh_basic_test.sh && ./tests/integration/nlsh_perm_test.sh</verify>
  <done>NL-Shell 编译通过；基本 NL 指令（创建/删除/列表/导航）端到端可用；传统命令透传正常；Ctrl+C/Ctrl+D 响应正确；IPC 权限审核联调通过</done>
  <depends_on>T07,T08</depends_on>
</task>

<task id="T10" parallel="true" status="pending">
  <name>nl-run：.tools 脚本执行器</name>
  <read_files>
    .specs/nl-os/DESIGN.md
    .specs/nl-os/REQUIREMENT.md (AC-C1~C7)
    .specs/adr/003-nl-c-syntax.md
    src/libnlc/nlc_api.h
  </read_files>
  <write_files>
    src/nl-run/main.c
    src/nl-run/script_parser.c
    src/nl-run/script_parser.h
    tests/component/tools_test.sh
    tests/component/tools_failfast_test.sh
    tests/component/tools_context_test.sh
  </write_files>
  <action>
    实现 .tools 脚本执行器（DESIGN §3.3），对应 AC-C1~C7：
    1. main.c：
       - 解析参数：nl-run [--freeze] [--dry-run] <file.tools>
       - 步骤 1：script_parser 解析文件 → N 条 NL 指令
       - 步骤 2：步骤预览（展示所有步骤 + 权限级别）
       - 步骤 3：逐条执行（nl_parse → ipc_client 权限审核 → 执行）
       - fail-fast：任一步骤失败 → 输出错误 + exit code = 步骤编号
       - 全部成功 → exit code = 0
       - 文件不存在/为空 → exit code = -1
    2. script_parser.c：
       - 逐行读取 .tools 文件（UTF-8）
       - 跳过空行和 # 注释行
       - 解析头部声明：# @perm: allow-l2（预授权标记）
       - 每行作为一条 NL 指令存入 NlScript
    3. --freeze（二期）：首次执行后保存翻译结果
    4. 步骤间上下文：维护 cwd 状态（cd 命令更新 cwd，后续命令使用更新后的 cwd）
  </action>
  <verify>cd build && make && ./tests/component/tools_test.sh && ./tests/component/tools_failfast_test.sh && ./tests/component/tools_context_test.sh</verify>
  <done>nl-run 编译通过；.tools 基本执行测试通过；fail-fast 测试通过（第 K 步失败 exit code = K）；步骤间上下文传递测试通过</done>
  <depends_on>T09</depends_on>
</task>

<task id="T11" parallel="true" status="pending">
  <name>Benchmark 建设：1000 条评测集 + 评估脚本</name>
  <read_files>
    .specs/nl-os/REQUIREMENT.md (AC-A8, AC-D2, AC-D4, AC-D6)
    .specs/adr/004-model-selection.md
    src/libnlc/nlc_api.h
  </read_files>
  <write_files>
    models/benchmarks/nl-commands-zh.json
    models/benchmarks/adversarial.json
    models/benchmarks/regression.json
    models/eval/run_benchmark.sh
    models/eval/validate_benchmark.py
    models/eval/results/.gitkeep
    models/prompts/intent_classify.txt
    models/prompts/entity_extract.txt
    models/prompts/system.txt
  </write_files>
  <action>
    建设模型评测基准（DESIGN §1 D10，ADR-004），对应 AC-A8/D6：
    1. nl-commands-zh.json（1000 条）：
       - 覆盖 12 种 intent，每个 ≥ 30 条
       - 难度分布：easy 40% / medium 40% / hard 20%（标注 difficulty 字段）
       - 每条含：id / input / expected_intent / expected_entities / difficulty / category
       - 来源：人工编写（MVP）+ 后续从真实用户日志补充
    2. adversarial.json（100+ 条对抗性样本）：
       - 路径遍历（"删除 ../../../etc/passwd"）
       - Unicode 混淆（零宽字符、bidi、同形异义字）
       - 多义表达（"清理一下系统"→可能指 /tmp 或 /）
       - 否定句式（"除了 /tmp 之外的所有目录都删掉"）
    3. regression.json（500 条回归测试子集，从 nl-commands-zh 中抽取）
    4. prompt 模板（models/prompts/）：
       - system.txt：角色定义 + 输出 JSON Schema + 约束
       - intent_classify.txt：few-shot 示例（3 条）
       - entity_extract.txt：实体提取指令
    5. run_benchmark.sh：指定模型路径 → 跑 benchmark → 输出准确率/F1/延迟
    6. validate_benchmark.py：检查 benchmark 格式/覆盖率/数量
  </action>
  <verify>python3 models/eval/validate_benchmark.py && bash models/eval/run_benchmark.sh --dry-run</verify>
  <done>benchmark 1000 条格式验证通过；12 种 intent 全覆盖（每个 ≥ 30 条）；难度分布符合 4:4:2；对抗性样本 ≥ 100 条</done>
  <depends_on>T09</depends_on>
</task>

<task id="T12" parallel="true" status="pending">
  <name>单元测试 + 组件测试全覆盖</name>
  <read_files>
    .specs/nl-os/REQUIREMENT.md (全部 AC)
    .specs/adr/009-testing-strategy.md
    src/libnlc/*.h
    src/nl-perm/*.h
    tests/unit/*
    tests/component/*
  </read_files>
  <write_files>
    tests/unit/rule_engine_test.c (补充)
    tests/unit/template_fill_test.c (补充)
    tests/unit/dispatch_test.c
    tests/component/pipeline_test.c
    tests/component/perm_rules_test.c
    tests/component/fail_closed_test.c
    tests/safety/path_traversal_test.c
    tests/safety/injection_test.c
    tests/safety/l4_bypass_test.c
  </write_files>
  <action>
    补充测试覆盖到 Layer 1-3 全绿（ADR-009 五层金字塔），对应所有 v1 AC：
    1. 补充单元测试（已有测试的增强 + 新增）：
       - dispatch_test.c：传统命令判定边界 50 条 case（AC-A11）
       - 各模块边界 case（空输入/超长输入/Unicode 特殊字符）
    2. 组件测试：
       - pipeline_test.c：四层管线端到端（NL 输入→命令输出），30 条 case
       - perm_rules_test.c：权限规则加载 + 重载 + 配置错误处理（AC-B7）
       - fail_closed_test.c：权限守护不可达 → NL-Shell 拒绝操作（AC-B1-B4 安全边界）
    3. 安全测试（Layer 3）：
       - path_traversal_test.c：所有路径遍历变体（AC-B5/B6）
       - injection_test.c：命令注入/Unicode 注入/ANSI 注入（AC-B9）
       - l4_bypass_test.c：尝试绕过 L4 的对抗性输入（AC-B4 所有等价表述）
    目标：Layer 1-2 测试全部通过 CI（make test 一键运行）
  </action>
  <verify>cd build && cmake .. -DBUILD_TESTING=ON && make && ctest --output-on-failure</verify>
  <done>所有单元测试 + 组件测试 + 安全测试通过；ctest 输出全绿；安全测试零误放行</done>
  <depends_on>T09</depends_on>
</task>

<task id="T13" parallel="true" status="pending">
  <name>端到端测试 + Docker 环境 + README</name>
  <read_files>
    .specs/nl-os/REQUIREMENT.md (AC 1-30)
    .specs/nl-os/DESIGN.md
    tests/e2e/*
    tests/integration/*
  </read_files>
  <write_files>
    tests/e2e/Dockerfile
    tests/e2e/docker-compose.yml
    tests/e2e/e2e_full_test.sh
    tests/e2e/scenarios/create_delete_test.sh
    tests/e2e/scenarios/perm_workflow_test.sh
    tests/e2e/scenarios/tools_pipeline_test.sh
    tests/e2e/scenarios/safety_guard_test.sh
    README.md
    docs/BUILD.md
    docs/CONFIG.md
    docs/ARCHITECTURE.md
  </write_files>
  <action>
    端到端测试 + 文档（ADR-009 Layer 5），完成 MVP 收尾：
    1. Dockerfile：基于 debian:stable-slim，安装 build-essential/cmake，编译 NL-OS
    2. e2e 场景测试（Docker 隔离，独立 filesystem snapshot 每次测试）：
       - create_delete_test.sh：文件创建/删除 NL 指令端到端
       - perm_workflow_test.sh：L1→L4 完整权限流程（输入→审核→确认→执行）
       - tools_pipeline_test.sh：.tools 编写→执行→验证结果
       - safety_guard_test.sh：L4 硬拦截 + 路径遍历 + 符号链接攻击
    3. README.md：项目介绍 + 快速开始 + 架构概述 + 许可证
    4. docs/BUILD.md：构建说明（x86_64 + ARM64 交叉编译）
    5. docs/CONFIG.md：配置说明（nlsh.conf + perm_rules.conf + systemd）
    6. docs/ARCHITECTURE.md：指向 DESIGN.md 的架构文档（简化版供外部贡献者阅读）
  </action>
  <verify>cd tests/e2e && docker-compose build && docker-compose up --abort-on-container-exit && docker-compose down</verify>
  <done>Docker e2e 测试全部通过；4 个场景覆盖核心 AC；README 和文档齐全</done>
  <depends_on>T09</depends_on>
</task>
```

---

## 状态字段说明

- `status="pending"` — 未开始
- `status="in_progress"` — 进行中
- `status="done"` — 已完成（verify 通过）
- `status="blocked"` — 阻塞

---

## AC 覆盖矩阵

| AC | 覆盖任务 | 验证方式 |
|---|---|---|
| AC-A1~A3, A6 | T09 (nlsh) | 集成测试 |
| AC-A4, A11 | T09 (dispatch) | dispatch_test |
| AC-A5 | T09 (nlsh) | 集成测试 |
| AC-A7 | T04+T09 | 规则引擎 + 降级测试 |
| AC-A8 | T04 (rule_engine) | rule_coverage_test |
| AC-A9 | T09 (line_edit) | 集成测试 |
| AC-A10, A12, A13 | T09 (nlsh) | 集成测试 |
| AC-B1~B4, B8 | T08 (nl-perm) | perm_engine_test |
| AC-B5~B6 | T02 (path_safety) | path_safety_test |
| AC-B7 | T08 (perm_rules.conf) | perm_rules_test |
| AC-B9 | T02 (path_safety) | injection_test |
| AC-C1~C4, C7 | T10 (nl-run) | tools_test |
| AC-C5~C6 | T10 (script_parser) | tools_failfast_test |
| AC-D1 | T07 (libnlc) | nlc_api_test |
| AC-D2 | T06+T11 | schema_validation + benchmark |
| AC-D3 | T06+T11 | benchmark 跨架构 |
| AC-D4 | T06 | intent_extract_test |
| AC-D5 | T07 (nlc_api.h) | grep 验证 |
| AC-D6 | T11 (benchmark) | validate_benchmark.py |

---

> `read_files`/`write_files` 是 R7.3 强约束。4-dev 步骤 5 提交前会 verify diff 边界。
