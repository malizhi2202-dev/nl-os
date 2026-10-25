# ADR-012: 能力注册表与能力自发现

- **状态**: accepted
- **日期**: 2026-10-25
- **关联**: `ADR-007`（模块边界）、`ADR-013`（决策层消费注册表）、`ADR-014`（固化引用能力 ID）
- **前置条件来源**: G1 架构师条件（格式先冻结）、需求门架构师条件（`format_version` + schema 可验证）

## Context

一期 MVP 的可执行能力硬编码在 `rules_db.h` / `templates_db.h`。换机器即错。需要一个项目级模块，产出**本机能力清单**并成为能力的唯一真相；规则库与模板只引用能力 ID。

关键约束：
- 目标设备覆盖 OpenWrt（musl、无 systemd、16–128MB 内存），不能假设 systemd/apt/docker 存在
- 加载成本：1000 项能力时索引 ≤ 200KB，加载 ≤ 50ms
- 并发：`nlsh` 与 `nl-run` 可能同时启动；扫描可能由定时任务触发
- 安全：PATH 可能被污染（恶意同名二进制）；不可信任 PATH 顺序

## Decision

### 文件与格式

- 路径：`${XDG_STATE_HOME:-~/.local/state}/nl-os/capabilities.jsonl`，权限 **0600**
- 格式：**JSON Lines**，首行为元数据行：
  ```json
  {"format_version":1,"scanned_at":"2026-10-25T10:00:00Z","scanner":"nl-os/0.2","host":"aarch64","has_systemd":false}
  ```
  后续每行一项能力：
  ```json
  {"id":"ls","name":"ls","path":"/usr/bin/ls","inode":123456,"kind":"file","perm":"L1","slots":[],"source":"scan","trusted":true}
  ```
- **`format_version` 是格式冻结的标志**：任何不兼容变更必须升版本，读取方遇到更高版本必须拒绝加载并降级为"仅规则层"，不得猜测字段
- 必填字段（缺一即该项无效，跳过并记审计）：`id` / `path` / `perm` / `source` / `trusted`
- `perm` ∈ `L1`…`L4`，来自显式映射表（见下），**不由扫描推断语义**

### 发现策略

- 扫描目录白名单（硬编码常量，不可由环境变量扩展）：`/usr/bin /bin /usr/sbin /sbin /usr/local/bin /usr/local/sbin`，外加 `/usr/libexec`（仅记录不执行）
- `lstat` 判定：符号链接**不登记为可信**（`trusted:false`，`kind:"symlink"`），只在清单中列出供人查看；执行前必须 `realpath` 复校并命中白名单目录
- 不信任 PATH 顺序，不执行 PATH 中的相对路径
- systemd 单元发现：仅在 `has_systemd` 时尝试，失败静默降级（不视为错误）
- 权限等级来源：显式映射表 `capability_perm_map[]`（`rm`→L3、`ls`→L1、`systemctl`→L2 …），未在表中的能力一律 **L4（禁止）**，宁可不可用也不误放行

### 并发与写入

- 写：`flock(LOCK_EX)` 锁 `<state>/nl-os/capabilities.lock` → 写临时文件 → `fsync` → `rename` 原子替换 → 释放
- 读：原子 `rename` 保证读者看到旧版或新版，不看到半成品；读失败（含 JSON 解析失败）→ 降级仅规则层，不崩溃
- 增量：记录每个扫描目录的 `mtime` 与文件数；仅当 `--scan` 显式触发或 `mtime` 变化时重扫该目录

## Consequences

- ✅ 机器换环境后能力清单自动跟随；规则库/模板退化为"引用能力 ID"，消除双份真相
- ✅ 格式版本可验证 → 满足需求门架构师条件；段 2/段 3 可以按此格式开工
- ✅ 未在权限映射表中的能力默认 L4，安全默认成立
- ❌ 新增两个模块（`capability_registry.c` / `capability_scan.c`）与一个状态文件
- ❌ JSON 解析对 1000 项需 ~200KB 内存峰值；OpenWrt 档需分块流式解析（实现约束）
- ❌ 权限映射表需人工维护；新增命令默认 L4 会让用户觉得"什么都没法用"，需要用 `--scan --self-test` 报告引导用户显式放行

## 被否掉的备选

| 备选 | 否掉原因 |
|---|---|
| 二进制格式（自定义） | 不可读、不可 diff、调试成本高；200KB 级别不值得 |
| SQLite | 新依赖（违反 ADR-006 ≤3 依赖） |
| 扫描失败时回退到硬编码清单 | 双份真相，且会掩盖"这台机器真的没有该能力"的事实 |
| 由 `--help` 文本推断权限等级 | 不可靠且不可验证（G1 已定：不解析 help 语义） |
