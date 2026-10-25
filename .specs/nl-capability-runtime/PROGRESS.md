# PROGRESS: nl-capability-runtime

- **状态**: 段 1 / 段 2 / 段 3 全部完成（T11b、T13 已于 2026-10-25 补齐）
- **前置**: `capability-protocol` 已冻结并把注册表格式定为 v2（本 change 的段 1 格式冻结复用该结论）

---

## 已完成（均已 verify + 提交）

| Task | 内容 | verify |
|---|---|---|
| T01 | 四模块骨架 + CMake + 测试注册 | nm 4 符号，ctest 9/9 |
| T02 | 注册表 JSONL + `format_version` + flock + 原子写 0600 + 坏行跳过 | 5/5，ctest 10/10 |
| T03 | 能力扫描（白名单/lstat/同 inode 去重/ID 冲突 @hash8）+ 权限映射（未登记 L4） | 3/3，ctest 11/11 |
| T04 | `capability_selftest` 三档分类 | 2/2 |
| T05 | `budget` 规则版复杂度预算 + refused + 替代步骤 | 3/3 |
| T06 | 快路径判定 + 审计 `fast_path` | 含在段 1 集成脚本 |
| T07 | 能力缺失/预算拒绝接入 nlsh + `--scan` / `--self-test` | 含在段 1 集成脚本 |
| T08 | 段 1 集成验证 + 冻结引用（人审门已签核） | `verify_segment1.sh` 12/12 |
| T10 | 候选收缩（**规则意图 +50 强提示** + 词典，top-K=8，L4 不进候选） | `ctest -R decision` 3/3；召回 67→86 |
| T11a | GBNF Choice 语法构造 + 开销实测（2.5µs/次） | 语法只枚举候选 + `<refuse>`；构建开销断言 |
| **T11b** | **decoder 后端接线**：档位→后端、受约束解码、置信度阈值、`--decide` | `ctest -R decision_model` 4/4（有模型时含实测） |
| T12 | Noul 三档 + 单调性（模型只能更严格） | `ctest -R decision` 单调性断言 |
| **T13** | **后端自降档 + 分档评测 + 中文集 120 条** | `verify_segment2.sh` 17/17 |
| T14 | `.tools` 固化 + 校验（SHA-256 链式） | 含在 `verify_segment3.sh` |
| T15 | 快照（8MB/敏感/200MB/LRU）+ `--undo` | 含在 `verify_segment3.sh` |
| T16 | `nlsh --audit` 回放 | `verify_segment3.sh` 16/16 |

## 文档产出（段 2 收尾补齐）

| 文件 | 内容 |
|---|---|
| `TEST.md` | AC-1~AC-16 的验证矩阵 + **非功能指标实测（含 3 项未达标）** + 测试环境 |
| `REVIEW.md` | 段 2 出口四角色评审（4/4 条件通过，2 项条件未闭环）+ 本轮发现并修复的 5 个问题 |
| `T11b-SUMMARY.md` / `T13-SUMMARY.md` | 实现细节与实测数字 |
| `DESIGN.md` D19/D20 | 置信度阈值默认 0 的依据、档位→后端映射与降档边界 |
| `adr/015` 补记 | 后端映射落地 + 分档实测 + 未实现的"P95 自动降档" |

## 本轮顺带修复的既有缺陷

| 缺陷 | 影响 | 修复 |
|---|---|---|
| Release 全量构建编不出来（`-Wformat-truncation` ×2、`-Wunused-result` ×3） | 发布配置实际不可用 | `snapshot.c` 精度限定符 + 两处测试返回值检查；`verify_all.sh` 改为构建全量 Release |
| `--eval` 的 input 未转义 | 含引号输入产生非法 JSON | `json_escape()` |
| `download_model.sh` 失败退出 0 且会误删已下载模型 | 模型丢失/误判成功 | 幂等校验 + `.part` 临时文件 + 失败退出非 0 |

## 当前测试规模

```
ctest: 28/28 全绿
scripts/verify_segment1.sh: 12/12
scripts/verify_segment2.sh: 17/17（规则档）/ 18/18（decoder 档，三轮 schema_violations=0、fallbacks=0）
scripts/verify_segment3.sh: 16/16
scripts/verify_all.sh（capability-protocol 的全量 AC）: 31/31
```

## 已知遗留（诚实记录，不是"已达标"）

| 项 | 现状 | 影响 |
|---|---|---|
| 自然语言命令不会被执行 | `nlsh` 只展示命令 + 确认标志；执行要走 `.tools`/`nl-run` | DESIGN §2 的"确认→权限→快照→执行→审计"后半段未接线；**下一个 change 首项** |
| 决策层延迟 | 修复后本集 P50/P95 = 0ms（规则有把握就不问模型）；强制解码时 P95 3393ms | 达标只是因为没触发 decoder；真上 decoder 的硬件指标仍未验证 |
| 粗筛召回 | 漏救已从 33% 降到 **14%**（规则层并入候选收缩）；平均候选 1.06 | 剩余 14 条是"把字句/双槽位"等硬句式 |
| C 档评测 | 无 3B+ GGUF，未实测 | ADR-015 的 C 档承诺仍是"未验证设计目标" |
| Noul 模型信号 | 未接（只用规则单调性） | 模型只能影响 Choice，不能抬升 allow→confirm |
| 规则槽位 `after_key` | 仍会吃进句尾修饰词（详见 LESSONS） | 影响规则层，非本 change 范围 |
| ARM64 / musl | 无交叉编译链，未验证 | 沿用 G4 架构师降级标注 |
| 置信度阈值 | 默认 0（关闭）：实测该指标无区分度（0.5 会丢 53% 正确答案） | AC-8 的数值语义默认不生效，靠"候选为空/模型 <refuse>"兜底 |
| 规则层基线 | 68.3% → **81.7%**（intent），覆盖率 73.3% → 86.7% | 槽位剥离 + 13 条自然说法规则 |

## 已排除方案（恢复时不要重试）

- 用 SQLite / 二进制存注册表 → 违反 ADR-006 依赖上限（见 ADR-012）
- 用 `cJSON_GetObjectItemCaseSensitive` / `item->valuestring` → vendor cJSON 是 API 子集，只有 25 个符号
- 用 `(int)` 写 inode → 截断，已改 double
- 为测试在扫描里开后门（环境变量扩展白名单）→ 改为 `dirs` 参数由调用方传入
- `mkdir(0700)` 当"不可读目录"测试 → 属主自己可读，等于没测；改 `0000` + root 下 SKIP
- 用全量枚举的 GBNF 语法覆盖全部能力 → 1000 条会产生 ~12KB 语法，ADR-013 已否
- 让模型直接输出命令行 → 只允许输出候选 id（GBNF + 解析双重过滤）

## 恢复指令（若需继续）

1. 读本文件 + `.specs/nl-capability-runtime/T13-SUMMARY.md` 的"遗留"
2. 延迟问题优先：prefix cache / KV 复用 / 换硬件复测，任选其一再谈"B 档达标"
3. 召回问题其次：扩词典并用 `nlsh --eval` 看 `refused` 下降
