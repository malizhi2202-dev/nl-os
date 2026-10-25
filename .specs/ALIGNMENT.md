# ALIGNMENT — 五层对齐报告

- **本轮基线**: `e410142`（上轮对齐终点）→ 本轮覆盖 `e410142..f1bd0f2`
- **当前对齐到**: `f1bd0f2`（change `nlsh-exec`）
- **五层状态（当前）**: L1 ✔ / L2 N/A（无 UI）/ L3 ✔ / L4 ✔ / L5 ✔
- **上轮基线**: `75e911e` → `e410142`（capability-protocol）

## 1. 覆盖提交（20 条，`75e911e..e410142`）

`fbd7777 0-change` · `194a832 REQUIREMENT+DESIGN+ADR` · `729a96b T01` · `76f643d STATE 修正` ·
`9f8c4aa Task 门+CONTEXT` · `9c65a76 方案确认` · `6ad62ea T02` · `9600f39 T03` · `90d62de T04` ·
`a7f4b96 T05` · `d57edb2 T06` · `04a5981 T07` · `bde029f T08` · `566ab82 T09+T10` · `c204292 T11` ·
`d187054 T12` · `fd51d71 T13` · `ad77c75 T14 脚本` · `9d0b751 T14 全绿` · `5bdfe72 TEST` · `b4366ad REVIEW+T-FIX`

## 2. 提交归类映射

| commit | 主题 | L1 | L2 | L3 | L4 | L5 |
|---|---|---|---|---|---|---|
| `fbd7777`–`194a832` | CHANGE / REQUIREMENT / DESIGN / ADR | | | ✔ | ✔ | |
| `729a96b`–`fd51d71` | T01–T13 实现（代码 + 单测） | ✔ | | | | ✔ |
| `ad77c75`–`9d0b751` | T14 验证脚本与全绿 | ✔ | | | | ✔ |
| `5bdfe72` `b4366ad` | TEST / REVIEW + T-FIX | ✔ | | ✔ | | ✔ |
| `e410142` | 本轮对齐（README/BUILD/CONTEXT/ADR-015） | | | ✔ | ✔ | |

## 3. 漂移检测

| # | 规则 | 漂移 | 证据 | 判定 |
|---|---|---|---|---|
| D1-1 | 代码变了、活文档没变 | README 组件表**没有 `nlbroker`**，也没有 CIP 使用说明 | 代码 `fbd7777..b4366ad` 新增 broker；`README.md` 仍只有 4 个组件 | ✔ 已合并（`e410142`） |
| D1-2 | 同上 | `docs/BUILD.md` 未提 `verify_all.sh`、未标注平台验证状态 | `BUILD.md` 只有 ctest；`TEST.md` 记录 ARM64/musl 未验证 | ✔ 已合并（`e410142`） |
| D4-1 | 执行留痕缺口 | `STATE.md` 曾出现"活跃 Change 为 CHANGE 阶段、当前阶段却是 DEV" | commit `76f643d` | ✔ 已修（`76f643d`） |
| D5-1 | 需求失配 | AC-4 的 `commit` 契约在实现时新增 `capability`+`args` | `REQUIREMENT.md` AC-4 与 `DESIGN.md` §3.2 已同步 | ✔ 已留痕（同 change 内实现前修正，无需新 CHANGE） |
| D2-1 | 决策与代码矛盾（疑似） | ADR-013 的类型化决策层（Choice/Score/Noul）**尚未实现** | 代码中无模型决策；当前只有规则裁决 | ⚠️ **非本 change 漂移**：该部分属暂停的 `nl-capability-runtime`（段 2） |
| D6-1 | 孤儿产物 | `.specs/discovery/*` 是否仍被引用 | `CHANGE.md` / `ADR-015` / `ADR-016` 均引用 | ✔ 无孤儿 |

## 4. 拉齐动作

**可自动合并（已执行，`e410142`）**
1. `README.md`：组件表补 `nlbroker`；新增「本地 broker（CIP v1）」小节；架构指引补归档路径
2. `docs/BUILD.md`：测试命令补 `verify_all.sh`；补构建产物与**平台支持状态表**
3. `.specs/CONTEXT.md`：补平台验证状态、broker 三种运行形态
4. `.specs/adr/015-device-tiers.md`：补「验证状态」段，**显式把 A1/A2/B/C 的内存与延迟承诺降级为"未验证的设计目标"**

**需人工裁决**：无（G3 已按 G4 条件以"显式标注"方式处置）

**建议删除**：无

## 5. 已对齐记录

| 漂移 | 对应 commit | 合并 commit |
|---|---|---|
| D1-1 / D1-2 | `fbd7777..b4366ad` | `e410142` |
| D4-1 | `6ad62ea` | `76f643d` |
| D5-1 | `bde029f` | 同 change 文档同步（`bde029f`） |
| G3 平台承诺 | `75e911e..b4366ad` | `e410142`（ADR-015 验证状态段） |

> 基线已更新至 `e410142`。下轮对齐从此处起算。

---

## 附录 · 历史轮次

### 2026-10-25 · nlsh-exec 轮（`e410142..f1bd0f2`）

| # | 规则 | 漂移 | 处置 |
|---|---|---|---|
| N1-1 | 代码变了、活文档没变 | `README.md` 只有一个"组件表"，未说明 nlsh **会执行**、`NL_ASSUME_YES`、`--dry-run` | ✔ 已补「用自然语言干活」小节 |
| N1-2 | 同上 | `nl-capability-runtime/UAT.md` 的场景 2/3/4/6 描述的是"只展示不执行"，已被本 change 改变 | ✔ 已重写场景 2，修正 3/4 预期，补 `rule-decisive` 方法名 |
| N3-1 | 设计与实现不符 | 旧 `DESIGN.md` §2 管线图画到"展示命令"为止，实际已贯通执行 | ✔ 已在旧 DESIGN 顶部加"后续变更"指引，实现细节落在 `nlsh-exec/DESIGN.md` |
| N5-1 | 需求失配 | UAT S4 期望"拒绝"，实现曾生成 `rm -rf 原文件` | ✔ 预算层修复 + 回归（`budget_test`） |
| N6-1 | 孤儿产物 | 新脚本 `scripts/verify_exec.sh` 是否被引用 | ✔ 已写入 `nlsh-exec/TEST.md`、`STATE.md`、`CHANGELOG.md` |
| N2-1 | 决策与代码矛盾 | `--decide` 把"规则已确定、没解码"标成 `choice-llm` | ✔ 改 `rule-decisive`；旧 change 的 T11b/T13 SUMMARY 是历史记录，不改写，差异在此登记 |

**拉齐动作**：README / 旧 UAT / 旧 DESIGN / STATE / CHANGELOG / LESSONS 均已更新，无待人工裁决项。
**对齐到**：`f1bd0f2`。
