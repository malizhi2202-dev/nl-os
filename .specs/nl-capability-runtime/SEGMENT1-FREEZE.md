# SEGMENT1-FREEZE — 段 1 集成与注册表格式冻结

- **Change ID**: `nl-capability-runtime` · **Task ID**: T08 · **完成时间**: 2026-10-25
- **说明**：原计划在本任务"冻结注册表格式"。实际该格式已由 `capability-protocol` 提前冻结为 **v2**，
  本文件只做**引用与验收留痕**，不再重复冻结（避免两处真相）。

## 1. 冻结的 schema 来源

| 项 | 值 |
|---|---|
| 格式 | JSON Lines，首行元数据 |
| 版本 | `format_version = 2` |
| 冻结依据 | `@.specs/adr/012-capability-registry.md`（v2 字段：`namespace` / `effect` / `reversible` / `effect_defaulted`） |
| 必填字段 | `id` / `perm` / `source` / `trusted`；非 `cli` 命名空间 `path` 可缺省 |
| 缺省从严 | `effect` 缺省 = `delete` 且 `effect_defaulted=1` |
| ID 规范 | 命名空间前缀：`cli:` / `dbus:` / `mcp:` / `http:` |

**格式变更需升 `format_version` 并提供迁移说明。**

## 2. 段 1 验收（`scripts/verify_segment1.sh`）

```
=== AC-1 能力自发现（扫描不写系统）
扫描完成：新增 1890 条，总计 1890 条
PASS AC-1 注册表已生成且版本为 2
PASS AC-1 系统目录 mtime 未变
PASS AC-1 清单非空

=== AC-2 三档自检
自检完成：usable=23 needs-privilege=1330 missing=537 total=1890
PASS AC-2 输出三档计数
PASS AC-2 total>0

=== AC-3 快路径不经模型
fastpath-ok
PASS AC-3 输出命令结果
PASS AC-3 审计标记 fast_path=true

=== AC-4 能力缺失不猜测
本机没有该能力：cli:ls
候选：cli:cat
PASS AC-4 明确说没有该能力 / 给出候选 / 不产生可执行命令

=== AC-5 预算拒绝
解析不了（超出能力边界）：复合语义
替代步骤：cli:ls、cli:stat、cli:cat
PASS AC-5 返回解析不了 / 给出替代步骤

---- 段 1 结果：pass=12 fail=0
```

## 3. 段 1 完成判定

| 判定项 | 结果 |
|---|---|
| AC-1~AC-5 全绿 | ✅ 12/12 |
| `ctest` 回归 | ✅ 23/23 |
| 注册表版本 | ✅ v2（与 broker 共用） |
| 系统零写操作 | ✅ `/usr/bin`、`/etc` mtime 未变 |

> 段 1 交付：能力可发现、可自检、可被拒绝（能力缺失/预算超限），且所有动作有审计。
> 段 2（决策层：embedding/decoder 后端）与段 3（固化/回滚）仍未开始。
