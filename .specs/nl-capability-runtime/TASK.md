# TASK: NL-OS 能力运行时

- **Change ID**: `nl-capability-runtime`
- **关联**: `@.specs/nl-capability-runtime/REQUIREMENT.md`、`@.specs/nl-capability-runtime/DESIGN.md`、`@.specs/CONTEXT.md`
- **拆解范围**: 段 1（能力层）拆到可执行粒度；**段 2 / 段 3 只登记占位**——按 G1 架构师硬条件，段 1 的注册表格式冻结（T08）通过后才允许拆段 2 任务
- **技术栈**: 沿用 C11 / CMake / cJSON / 系统 llama.cpp；**不新增外部依赖**

---

## 波次划分

```
Wave 1:                   T01 (骨架，其余全部依赖它)
                            │
Wave 2 (parallel):        T02[P]  T03[P]  T06a[P]      (registry / scan / audit 骨架)
                            │       │       │
Wave 3 (parallel):        T04[P]  T05[P]                (self-test / budget)
                            │       │
Wave 4 (parallel):        T06[P]  T07[P]                (快路径+审计接线 / 能力缺失拒绝)
                            │       │
Wave 5:                   T08   ← 🔒 段 1 集成 + 注册表格式冻结（auto=false，人审门）
                            │
                            ▼ 冻结通过后才拆解
段 2 (T10–T13)            段 3 (T14–T16)      ← 本文件仅占位
```

---

## 任务清单（段 1 · 能力层）

```xml
<task id="T01" parallel="false" status="done" auto="true">
  <name>编译骨架：新增 4 个模块占位 + 测试注册</name>
  <read_files>
    .specs/nl-capability-runtime/DESIGN.md
    .specs/adr/012-capability-registry.md
    CMakeLists.txt
    src/libnlc/nlc_api.h
    tests/CMakeLists.txt
  </read_files>
  <write_files>
    CMakeLists.txt
    src/libnlc/capability_registry.c
    src/libnlc/capability_registry.h
    src/libnlc/capability_scan.c
    src/libnlc/capability_scan.h
    src/libnlc/audit.c
    src/libnlc/audit.h
    src/libnlc/budget.c
    src/libnlc/budget.h
    tests/CMakeLists.txt
  </write_files>
  <action>
    1. 四个模块建 .c/.h 骨架，全部函数返回 -1（未实现），头文件写出完整签名：
       - capability_registry.h：`NlCapRegistry *nl_cap_load(const char *path, NlCapLoadErr *err)` /
         `int nl_cap_save_atomic(NlCapRegistry *r, const char *path)` /
         `const NlCapability *nl_cap_find(const NlCapRegistry *r, const char *id)` /
         `int nl_cap_count(const NlCapRegistry *r)` / `void nl_cap_free(NlCapRegistry *r)`
       - capability_scan.h：`int nl_cap_scan_dirs(const char *const *dirs, int n, NlCapRegistry *out)` /
         `int nl_cap_selftest(NlCapRegistry *r, NlSelfTestReport *rep)`
         （**dirs 由调用方传入**：CLI 传硬编码白名单，测试传临时目录——避免为测试开后门）
       - audit.h：`int nl_audit_emit(const NlAuditRec *rec)`
       - budget.h：`NlBudgetVerdict nl_budget_check(const char *input, const NlCapRegistry *r, NlBudgetDetail *d)`
    2. libnlc target 加入这 4 个 .c（保持既有源文件不变）
    3. tests/CMakeLists.txt 按既有 `if(EXISTS ...)` 模式注册 4 个测试名：
       capability_registry / capability_scan / capability_selftest / budget
    4. 不实现任何逻辑，不写测试断言（下一波各任务自己写）
  </action>
  <verify>cd build && cmake .. -DCMAKE_BUILD_TYPE=Debug -DBUILD_TESTING=ON && make -j$(nproc) && nm -D libnlc.so | grep -E 'nl_cap_load|nl_cap_scan_dirs|nl_audit_emit|nl_budget_check' | wc -l</verify>
  <done>构建通过；nm 输出为 4（四个骨架符号已导出）；既有 9 个测试仍全绿</done>
  <depends_on></depends_on>
</task>

<task id="T02" parallel="true" status="done" auto="true">
  <name>capability_registry：JSONL 读写 + format_version + flock + 原子写 0600</name>
  <read_files>
    .specs/adr/012-capability-registry.md
    src/libnlc/json_helper.h
    src/libnlc/capability_registry.h
    src/libnlc/capability_registry.c
  </read_files>
  <write_files>
    src/libnlc/capability_registry.c
    src/libnlc/capability_registry.h   <!-- 扩范围：须暴露 nl_cap_skipped() 供坏行计数（见 T02-SUMMARY） -->
    tests/unit/capability_registry_test.c
  </write_files>
  <action>
    按 ADR-012 实现：
    1. 首行元数据解析，校验 `format_version == 1`；更高版本 → 返回 NL_CAP_ERR_VERSION，调用方降级
    2. 逐行解析能力项，必填字段缺失（id/path/perm/source/trusted）→ 跳过该项并计数（不整体失败）
    3. 写：`flock(LOCK_EX)` → 临时文件 → 全量写 + `fsync` → `rename` → 释放；文件与目录 0600
    4. 锁等待超时（默认 200ms）→ 返回 NL_CAP_ERR_LOCK，不阻塞启动
    5. 流式解析：不把整文件一次性读入，按行 buffer 读取（OpenWrt 内存约束）
    6. perm 只接受 L1–L4 字面量，非法值 → 该项按 L4 处理并计数
  </action>
  <verify>cd build && cmake .. -DBUILD_TESTING=ON && make -j$(nproc) && ctest -R capability_registry --output-on-failure</verify>
  <done>4 类 case 通过：往返读写一致 / format_version=2 被拒绝 / 并发两进程写不产生半成品 / 生成文件权限为 0600</done>
  <depends_on>T01</depends_on>
</task>

<task id="T03" parallel="true" status="done" auto="true">
  <name>capability_scan：白名单目录扫描 + lstat/symlink 标记 + 权限映射（未登记 L4）</name>
  <read_files>
    .specs/adr/012-capability-registry.md
    src/libnlc/capability_registry.h
    src/libnlc/capability_scan.h
    src/libnlc/capability_scan.c
  </read_files>
  <write_files>
    src/libnlc/capability_scan.c
    src/libnlc/capability_perm_map.h
    src/libnlc/capability_registry.c   <!-- 扩范围：修 T02 遗留的 inode int 截断 -->
    src/libnlc/capability_registry.h   <!-- 扩范围：新增 nl_cap_at() 只读遍历（冲突 id 无法 find 反查） -->
    tests/unit/capability_scan_test.c
  </write_files>
  <action>
    1. 白名单常量表：`/usr/bin /bin /usr/sbin /sbin /usr/local/bin /usr/local/sbin`（记为默认集，CLI 引用）
    2. 遍历调用方传入的目录；每项 `lstat`：
       - 普通文件 → `kind:"file"`, `trusted:true`，记录 `realpath` 与 `inode`
       - 符号链接 → `kind:"symlink"`, `trusted:false`（只列不执行）
    3. 权限等级查 `capability_perm_map.h`（显式映射：ls/cat/find/ps→L1，mkdir/touch/cp/mv→L2，rm/rmdir→L3，sudo/passwd/mount→L4）
       表里没有的一律 **L4**
    4. 去重：同 inode 只记一次；ID 冲突（同名不同路径）追加 `@<hash8>` 后缀
    5. 目录不可读 → 记 warning 继续，不整体失败
  </action>
  <verify>cd build && make -j$(nproc) && ctest -R capability_scan --output-on-failure</verify>
  <done>case 通过：临时目录里的普通文件被登记为 trusted / 符号链接为 untrusted / 未登记命令 perm=L4 / 同名不同路径产生两个不同 id</done>
  <depends_on>T01</depends_on>
</task>

<task id="T04" parallel="true" status="done" auto="true">
  <name>capability_selftest：三档分类（usable / needs-privilege / missing）</name>
  <read_files>
    .specs/nl-capability-runtime/REQUIREMENT.md
    src/libnlc/capability_scan.h
    src/libnlc/capability_registry.h
  </read_files>
  <write_files>
    src/libnlc/capability_scan.c
    tests/unit/capability_selftest_test.c
  </write_files>
  <action>
    1. 对注册表每项做**只读**探测：`fork` + `execv(path, {"--version"})`（无参则 `--help`），捕获退出码，超时 200ms 强杀
    2. 分类：能执行且退出码可接受 → `usable`；perm ≥ L2（或目录不可写）→ `needs-privilege`；路径不存在/不可执行 → `missing`
    3. 探测过程不写任何文件，不改变系统状态（测试里用 `stat` 前后对比 mtime）
    4. 输出计数汇总结构 `NlSelfTestReport {usable, needs_privilege, missing, total}`
  </action>
  <verify>cd build && make -j$(nproc) && ctest -R capability_selftest --output-on-failure</verify>
  <done>case 通过：`/bin/ls` 判 usable / 构造的不可执行文件判 missing / L3 能力判 needs-privilege / 探测前后目标目录 mtime 不变</done>
  <depends_on>T03</depends_on>
</task>

<task id="T05" parallel="true" status="done" auto="true">
  <name>budget：规则版复杂度预算 + refused 输出结构</name>
  <read_files>
    .specs/nl-capability-runtime/REQUIREMENT.md
    src/libnlc/budget.h
    src/libnlc/capability_registry.h
    src/libnlc/rule_engine.h
  </read_files>
  <write_files>
    src/libnlc/budget.c
    src/libnlc/alternatives_db.h
    tests/unit/budget_test.c
  </write_files>
  <action>
    1. 结构化判定（不调模型）：跨能力数 > 3 / 命中"每天/定时/循环/条件/并发/上传/下载"等复合词 / 需要特权 / 目标能力不在注册表 → 记维度
    2. 任一维度超限 → `NL_BUDGET_REFUSE`，并填 `NlBudgetDetail.dimensions[]`
    3. `alternatives_db.h`：静态映射表 `能力或场景 → 替代能力 ID 列表`（如"上传备份"→`scp`/`rsync`/`tar`），按注册表过滤不存在的项
    4. 替代命令由现有 `template_fill` 产出（**可直接复制执行**，来源 = 注册表内能力）
    5. 全部阈值用 `#define` 常量集中声明，禁止散落魔法数
  </action>
  <verify>cd build && make -j$(nproc) && ctest -R budget --output-on-failure</verify>
  <done>case 通过：5 条超范围输入全部 refused 且带维度与替代 / 3 条简单输入 pass / 替代建议中的能力 ID 全部存在于注入的注册表</done>
  <depends_on>T02,T03</depends_on>
</task>

<task id="T06" parallel="true" status="done" auto="true">
  <name>audit_log + 快路径审计（fast_path=true）</name>
  <read_files>
    .specs/nl-capability-runtime/DESIGN.md
    src/libnlc/audit.h
    src/nlsh/dispatch.c
    src/nl-perm/audit_log.c
  </read_files>
  <write_files>
    src/libnlc/audit.c
    src/nlsh/dispatch.c
    tests/unit/audit_log_test.c
  </write_files>
  <action>
    1. JSONL 追加写 `${XDG_STATE_HOME:-~/.local/state}/nl-os/audit.jsonl`，0600，`O_APPEND`+`flock`
    2. 记录字段：`ts / input_hash / capability_id / perm_level / fast_path / probability / result / reason`；
       **不写明文参数**（input 只存 sha256 前 16 hex + 长度），路径中的用户名可保留
    3. `dispatch.c`：传统命令判定命中 → 走快路径，写 `fast_path:true`，**不调用 `nl_parse`**
    4. 与 `nl-perm/audit_log.c`（syslog，daemon 侧）互不替代，各自独立
  </action>
  <verify>cd build && make -j$(nproc) && ctest -R audit_log --output-on-failure && NL_MODEL_PATH=/nonexistent ./nlsh -c 'echo fastpath-probe' && tail -1 ~/.local/state/nl-os/audit.jsonl | python3 -c 'import json,sys; r=json.loads(sys.stdin.read()); assert r["fast_path"] is True and r["result"]=="ok"; print("audit ok")'</verify>
  <done>JSONL 可被 python json 解析；快路径记录 fast_path=true；日志权限 0600；无明文输入</done>
  <depends_on>T01</depends_on>
</task>

<task id="T07" parallel="true" status="done" auto="true">
  <name>能力缺失 / 快路径判定接入 nlsh，拒绝输出替代步骤</name>
  <read_files>
    .specs/nl-capability-runtime/REQUIREMENT.md
    src/nlsh/main.c
    src/nlsh/dispatch.c
    src/libnlc/budget.h
    src/libnlc/capability_registry.h
  </read_files>
  <write_files>
    src/nlsh/main.c
    src/nlsh/cmd_capability.c
    tests/unit/capability_refuse_test.c
  </write_files>
  <action>
    1. `nlsh` 启动加载注册表；加载失败 → 降级"仅规则层"并打印一次警告（不阻塞）
    2. 规则层解析出能力 ID 后，若该 ID 不在注册表 → 输出"本机没有该能力" + 语义最近 3 个候选（字符串相似度，不调模型），**不生成命令**
    3. 预算 refused → 输出维度 + 替代步骤（T05 产出），退出码非 0
    4. 新增 `nlsh --scan` / `nlsh --self-test` 参数解析，走 T03/T04（本任务只接线，不改扫描逻辑）
  </action>
  <verify>cd build && make -j$(nproc) && ctest -R capability_refuse --output-on-failure && ./nlsh --scan --dry-run | head -3</verify>
  <done>case 通过：缺失能力不产生命令且给出 3 候选 / refused 输出含维度与替代 / `--scan --dry-run` 退出码 0 且不写索引</done>
  <depends_on>T03,T05,T06</depends_on>
</task>

<task id="T08" parallel="false" status="done" auto="false">
  <name>🔒 段 1 集成验证 + 注册表格式冻结签核</name>
  <read_files>
    .specs/nl-capability-runtime/REQUIREMENT.md
    .specs/adr/012-capability-registry.md
    .specs/nl-capability-runtime/TASK.md
  </read_files>
  <write_files>
    .specs/nl-capability-runtime/SEGMENT1-FREEZE.md
    scripts/verify_segment1.sh
    src/libnlc/capability_scan.c   <!-- 扩范围：统一 cli: 前缀（ID 不一致的集成缺陷） -->
    tests/unit/capability_scan_test.c
  </write_files>
  <action>
    1. 写 `scripts/verify_segment1.sh`：一键跑 AC-1~AC-5 的 5 条验证（扫描零写、三档分类、快路径、能力缺失、预算拒绝）
    2. 全量 `ctest` + 脚本全绿后，写 `SEGMENT1-FREEZE.md`：
       - 冻结的字段清单与 `format_version=1` 的完整 schema
       - AC-1~AC-5 的实跑输出（贴命令与结果）
       - 明确"格式变更需升 version + 迁移说明"
    3. **人审门**：冻结需用户确认后才允许拆段 2 任务
  </action>
  <verify>bash scripts/verify_segment1.sh && cd build && ctest --output-on-failure</verify>
  <done>5 条 AC 脚本全绿；ctest 全绿；SEGMENT1-FREEZE.md 含 schema 与实跑输出；用户已签核</done>
  <depends_on>T02,T04,T05,T07</depends_on>
</task>
```

---

## 段 2 / 段 3（占位 · 待段 1 冻结后拆解）

| id | 名称 | 对应 AC | 状态 |
|---|---|---|---|
| T10 | 候选收缩：**embedding 语义粗筛**（A 档主力）+ 规则兜底 → top-K=8，统计粗筛命中率/漏救率 | AC-6 前置 | blocked（待 T08 冻结） |
| T11a | GBNF Choice 语法构造 + 开销实测（2.5µs/次） | AC-6, AC-10 | ✅ done |
| T11b | decoder 后端接线（加载模型 + 约束解码 + 按画像选后端） | AC-6, AC-10 | ✅ done |
| T12 | Noul 三档 + 单调性（模型只能更严格） | AC-7, AC-8 | ✅ done |
| T13 | 后端自降档 + **分档评测**（A/B/C 各一份准确率与 P95 延迟）+ 中文集扩到 100+ | AC-9, AC-6 | ✅ done（C 档无硬件，显式标注未验证） |
| T14 | `.tools` 固化与校验（SHA-256 链式哈希 + 篡改拒绝） | AC-11~AC-13 | ✅ done |
| T15 | 快照（8MB/敏感排除/200MB/LRU）+ `--undo` | AC-14, AC-15 | ✅ done |
| T16 | `nlsh --audit`（回放，复用 nl_audit_query） | AC-16 | ✅ done |

---

## AC 覆盖矩阵（段 1）

| AC | 覆盖任务 | 验证方式 |
|---|---|---|
| AC-1 能力自发现 | T03 + T07 | `nlsh --scan` + 扫描前后 mtime 对比 |
| AC-2 三档自检 | T04 | `ctest -R capability_selftest` |
| AC-3 快路径 | T06 | `NL_MODEL_PATH=/nonexistent nlsh -c 'ls -la'` + 审计字段 |
| AC-4 能力缺失不猜 | T07 | `nlsh -c '重启 nginx'` 断言无命令输出 |
| AC-5 预算拒绝 | T05 + T07 | `ctest -R budget` + 脚本断言 `refused` |
| AC-6~AC-10 | T10–T13 | `scripts/verify_segment2.sh`（17 断言） |
| AC-11~AC-16 | T14–T16 | 待段 3 |

---

## 强制条件（来自门禁，逐条对应任务）

| 条件 | 落地任务 |
|---|---|
| 段 1 格式冻结后才拆段 2（G1 架构师） | T08（auto=false）+ 段 2 全部 blocked |
| 段 2 开工前实测 GBNF 构造开销（G2 架构师） | T11 action 第 1 条 |
| benchmark 统计粗筛命中率/漏救率（G2 PM） | T10、T13 |
| 0600 作实现断言 / SHA-256 NIST 向量 / 审计无明文（G2 安全） | T02、T06、T14 |
| 替代步骤可直接复制执行（需求门用户评测员） | T05 + T07 |

---

## 状态字段说明

- `status="pending"` 未开始 · `in_progress` 进行中 · `done` verify 通过 · `blocked` 阻塞
- `auto="true"` 可按门禁自动推进 · `auto="false"` 执行前必须人工确认（T08）

---

> `read_files` / `write_files` 是 R7.3 强约束；4-dev 提交前会 `git diff --name-only` 比对边界。

---

## 扩范围记录（R7.3 · 超出原 write_files 的改动）

| 日期 | 文件 | 原因 | 关联任务 |
|---|---|---|---|
| 2026-10-25 | `src/libnlc/rule_engine.c`、`src/libnlc/rules_db.h`、`tests/unit/rule_slot_test.c` | 写 UAT 时发现主路径缺陷：`创建一个叫 uat 的文件夹` → `mkdir -p uat 的文件夹`（必然失败）。修槽位剥离 + 补 13 条自然说法规则 + 补回归 | T13 复测（B5 结论的直接落点） |
| 2026-10-25 | `src/libnlc/decision.c` | 候选收缩与执行路径脱节（决策层用独立 20 条词典）。改为先跑规则层、命中意图 +50 强提示 | T10/T13 |
| 2026-10-25 | `src/nlsh/nl_eval.{h,c}`、`src/nlsh/main.c` | `--eval` 与 `--decide` 是 T13 的评测入口，未在原 `write_files` 中 | T13 |
| 2026-10-25 | `models/download_model.sh` | 失败仍退出 0、会在完整文件上误删模型 | T11b（模型就位的前提） |
| 2026-10-25 | `src/libnlc/snapshot.c`、`tests/unit/{capability_scan,adapter_http}_test.c`、`scripts/verify_all.sh` | Release 全量构建无法通过（`-O2` 才触发的 `-Werror`），验证链只编 `nlbroker` 掩盖了问题 | 段 2 出口评审（架构师条件） |
