# PROGRESS: nl-capability-runtime（暂停快照）

- **暂停时间**: 2026-10-25
- **暂停原因**: 用户决定转做新 change `capability-protocol`（统一调用协议 / 框架整合）
- **恢复条件**: `capability-protocol` 的协议字段与能力注册表格式对齐后，回来继续 T04–T08

---

## 已完成（均已 verify + 提交）

| Task | 内容 | verify |
|---|---|---|
| T01 | 四模块骨架 + CMake + 测试注册 | nm 4 符号，ctest 9/9 |
| T02 | 注册表 JSONL + `format_version` + flock + 原子写 0600 + 坏行跳过 | 5/5，ctest 10/10 |
| T03 | 能力扫描（白名单/lstat/同 inode 去重/ID 冲突 @hash8）+ 权限映射（未登记 L4） | 3/3，ctest 11/11 |

## 未完成

| Task | 内容 | 备注 |
|---|---|---|
| T04 | `capability_selftest` 三档分类 | ✅ 完成（2026-10-25，2/2） |
| T05 | `budget` 规则版复杂度预算 + refused + 替代 | ✅ 完成（2026-10-25，3/3） |
| T06 | `audit` JSONL + 快路径审计（fast_path=true） | 骨架已有，逻辑未实现 |
| T07 | 能力缺失 / 预算拒绝接入 `nlsh` + `--scan` / `--self-test` 接线 | 未开始 |
| T08 | 🔒 段 1 集成 + **注册表格式冻结签核**（auto=false，人审门） | 未开始，**新 change 的硬依赖** |

## 已排除方案（恢复时不要重试）

- 用 SQLite / 二进制存注册表 → 违反 ADR-006 依赖上限（见 ADR-012）
- 用 `cJSON_GetObjectItemCaseSensitive` / `item->valuestring` → vendor cJSON 是 API 子集，只有 25 个符号
- 用 `(int)` 写 inode → 截断，已改 double
- 为测试在扫描里开后门（环境变量扩展白名单）→ 改为 `dirs` 参数由调用方传入
- `mkdir(0700)` 当"不可读目录"测试 → 属主自己可读，等于没测；改 `0000` + root 下 SKIP

## 待确认假设（继承，未变）

- 模型进程形态（进程内 vs 常驻 `nl-infer`）未定，段 2 前必须定
- 中文 benchmark 仅 33 条，阈值合理性未知

## 恢复指令

1. 读本文件 + `.specs/nl-capability-runtime/TASK.md` 的 T04
2. 确认 `capability-protocol` 是否已改变注册表字段；若改了，先同步 `capability_registry.h/.c` 再继续
3. 从 T04 起按 `TASK.md` 顺序推进
