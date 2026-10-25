# TASK: nlsh 执行接线

- **Change ID**: `nlsh-exec`
- **依据**: `REQUIREMENT.md` AC-1~AC-10、`DESIGN.md` D1~D8

---

## 波次 1 · 拒绝优先（先止血，可独立验收）

### T01 · 预算层新增两个拒绝来源
- **write_files**: `src/libnlc/budget.c`、`src/libnlc/budget.h`
- **做什么**: 多动作连接词（并/然后/接着/最后/之后/同时）≥1 即 `TOO_MANY_CAPS`；无对应能力操作词 → `UNKNOWN_CAP`
- **verify**: `./build/test_budget`（新）5 拒绝 + 6 放行；`nlsh -c '把 /var/log 下所有 .log 按大小排序压缩后发到远程服务器并清理原文件'` 输出拒绝、无 `rm -rf`
- **auto**: true

### T02 · 解析失败给原因与候选
- **write_files**: `src/nlsh/dispatch.c`
- **做什么**: 关键词→argv0 小表，命中且在注册表则输出候选；否则回退"无法理解"
- **verify**: `nlsh -c '重启 nginx'` 输出含 `本机没有该能力` 与 ≥1 候选，exit≠0
- **auto**: true

## 波次 2 · 执行器共享

### T03 · 抽出 `exec.c`，`nl-run` 改用
- **write_files**: `src/libnlc/exec.c`、`src/libnlc/exec.h`、`src/libnlc/CMakeLists.txt`、`src/nl-run/main.c`
- **做什么**: `nl_exec_has_metachars` / `nl_exec_execute` / `nl_exec_collect_paths`；`nl-run` 删掉本地副本
- **verify**: `verify_segment3.sh` 16/16 不变；`ctest -R freeze|snapshot`
- **auto**: true

## 波次 3 · nlsh 接执行

### T04 · dispatch 接 确认→权限→快照→执行→审计
- **write_files**: `src/nlsh/dispatch.c`、`src/nlsh/dispatch.h`、`src/nlsh/main.c`
- **做什么**: 确认门（`NL_ASSUME_YES`）、快照、执行、审计、`--dry-run` 贯通 `-c`
- **verify**: `verify_exec.sh` AC-3/AC-4/AC-5/AC-6/AC-7 全绿
- **auto**: true

## 波次 4 · 验收与收口

### T05 · 验收脚本 + 单测
- **write_files**: `scripts/verify_exec.sh`、`tests/unit/budget_test.c`、`tests/unit/exec_test.c`、`tests/unit/CMakeLists.txt`
- **verify**: 脚本 0 失败；`ctest` 全绿
- **auto**: true

### T07 · 快路径懒加载（用户追问后追加）
- **write_files**: `src/nlsh/main.c`
- **verify**: `nlsh -c 'echo hi'` 30ms → 0–10ms；ctest/verify_exec/段1/段3 全绿
- **auto**: true

### T08 · 统一确认门（ADR-019，专家团讨论后追加）
- **write_files**: `src/libnlc/confirm.c/.h`（新）、`src/nlsh/dispatch.c`、`src/nlsh/main.c`、`src/nl-run/main.c`、`tests/unit/confirm_test.c`、`scripts/verify_exec.sh`
- **verify**: `test_confirm` 4/4；`verify_exec.sh` AC-11a~11k 全绿（40 条）
- **auto**: true
- **落地复盘**: 扩测试时发现 `--freeze` 会丢掉 `# @perm:` 策略头（`allow-l2` 固化后静默失效），
  已修（策略头随固化保留并纳入校验和）；`freeze_test` + AC-11i 锁住

### T09 · 本地安装与模型自动发现（用户反馈 `nlsh: command not found` 后追加）
- **write_files**: `scripts/install_local.sh`（新）、`CMakeLists.txt`（RPATH + `NL_INSTALL_SYSTEM_FILES`）、`src/libnlc/model_loader.c/.h`、4 处调用点
- **verify**: 登录 shell 里 `command -v nlsh` 命中；未设 `NL_MODEL_PATH` 时 `--decide` 走出 `choice-llm`
- **auto**: true

### T10 · 三项"未验证目标"实测
- **write_files**: 无代码（仅验证记录与本 SUMMARY）
- **verify**: pty 驱动 TTY 中文/退格、ARM64 交叉构建 + qemu 跑评测、C 档内存硬数字
- **auto**: true

### T06 · 文档与 STATE
- **write_files**: `.specs/nlsh-exec/{TEST,REVIEW}^*.md`、`.specs/nl-capability-runtime/UAT.md`、`.specs/CHANGELOG.md`、`.specs/LESSONS.md`、`STATE.md`、`README.md`
- **verify**: 文档与实现一致（命令贴的是实跑输出）
- **auto**: true

---

## 边界

- 不改 `rule_engine.c` / `rules_db.h`（槽位问题不在本 change）
- 不改 `perm.sock` 协议、不改模型/档位
- 不实现链式执行

## 扩范围记录

| 日期 | 项 | 原因 | 处理 |
|---|---|---|---|
| 2026-10-25 | `src/nlsh/line_edit.c`（T04b） | 接执行后验 REPL 路径，发现行编辑器只吃 ASCII，中文输入被整段丢弃（交互式同样受影响） | 非 TTY 走 `fgets`；TTY 接受 UTF-8；退格按字符删。已写 T04b SUMMARY |
| 2026-10-25 | `src/nlsh/dispatch.c` 输入级闸门 | AC-8 用例撞出子命令注入（`; touch hacked` 被执行），命令级 guard 拦不住 | 解析前拒绝 `; \| & $ `` > <`；记入 LESSONS |
| 2026-10-25 | `src/nlsh/main.c` 方法名 `rule-decisive` | `--decide` 在"规则已确定、模型未解码"时谎称 `choice-llm`，可解释性失真 | 按 `nl_model_last_confidence() < 0` 区分 |
| 2026-10-25 | `scripts/verify_segment2.sh` AC-8 用例 | 单候选时短路，阈值闸门测不到；原断言靠环境偶然通过 | 用例加 `NL_DECIDE_SOLO_DECODE=1` 强制解码 |
| 2026-10-25 | `tests/unit/budget_test.c`（改扩写而非新建） | 仓库已有同名测试，新建会变成两份 | 直接扩写（ponytail 阶梯 2） |
| 2026-10-25 | `src/nlsh/main.c` 快路径懒加载（T07） | 用户追问"快路径 ≤5ms 为啥不动"；实测满表 20–50ms、空表 0–10ms，耗时几乎全在注册表全量解析，而快路径不需要注册表 | 传统命令时不加载注册表；实测 30ms → 0–10ms，NL 路径不变 |

### T11 · 模型惰性加载（用户反馈"占用 CPU 很大很快就满了"后追加）
- **write_files**: `src/libnlc/nlc_api.c/.h`（`nl_init_lazy` / `ensure_model` / `nl_model_is_loaded`）、`src/nlsh/main.c`、`src/nl-run/main.c`、`scripts/verify_exec.sh`
- **verify**: 规则可决定命令 RSS < 200MB 且 0.1–0.3s（改前 779MB / 3.5s）；规则未命中仍加载（RSS > 300MB）；模型缺失降档不阻塞。AC-14 5 条
- **auto**: true
