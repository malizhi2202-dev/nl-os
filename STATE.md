# STATE — 跨会话项目状态

> 仓库根 `STATE.md`。AI 每次开始新会话先读这个，结束前更新。

---

## 当前位置

- **活跃 Change**: `nl-capability-runtime`
- **当前阶段**: DESIGN（DESIGN.md + ADR-012/013/014 已产出；G2 方案门 3/4 通过 → 进入 TASK）
- **当前 Task**: —
- **中断任务**（来自 R1.5 清窗）: 无
- **会话开始建议**:
  - 有活跃 change → 先读 `@.specs/nl-capability-runtime/CHANGE.md`
  - 恢复历史上下文 → `@.specs/CONTEXT.md`、`@.specs/nl-os/`（一期 MVP 已完成）

## 阻塞与待决策

| 项 | 类型 | 详情 | 待谁 | 自 |
|---|---|---|---|---|
| 注册表格式冻结 | 决策 | DESIGN 必须先冻结 `format_version` + schema，否则不得拆段 2 任务（架构师条件） | 设计 | 2026-10-25 |
| ARCHITECTURE.md 缺失 | 决策 | 架构级变更无沉淀处；建议 Phase 1 结束后跑一次 `A-architect` | 用户 | 2026-10-25 |
| 中文 benchmark 规模不足 | 决策 | 现 `models/benchmarks/nl-commands-zh.json` 仅 33 条，计划 1000 条 | 用户 | 2026-10-25 |
| 模型未部署 | bug | 本机无 `/usr/share/nl-os/model.gguf`，`nlsh` 只能走规则层 | 用户 | 2026-10-25 |

## 决策日志（最近 10 条，倒序）

- `[2026-10-25]` 永久放弃 `.nl` 完整编程语言，NL-C 收敛为 `.tools` — `@.specs/adr/011-drop-nl-language.md`
- `[2026-10-25]` `.specs/` 纳入版本控制（此前被 `.gitignore` 忽略，TASK/ADR/SUMMARY 均未入库）
- `[2026-10-25]` 决策层不走 Agent Loop，改为有限答案空间内的类型化判断（choice/score/noul） — `@.specs/nl-capability-runtime/CHANGE.md`
- `[2026-10-25]` 注册表 JSONL + format_version 冻结；决策两阶段（粗筛 K=8 → GBNF Choice）；模型不得 deny；内置 SHA-256 — `@.specs/adr/012~014`
- `[2026-07-01]` 一期 MVP 技术栈与十项架构决策 — `@.specs/adr/001~010`

## 已归档 Changes（最近 5 个，倒序）

| 日期 | Change | 摘要 | PR |
|---|---|---|---|
| 2026-07-02 | nl-os | 一期 MVP：四层管线 + L1–L4 + `.tools` | — |
| 2026-07-01 | nl-os-phase2 | 真实推理 + 安全强化（draft，未开工） | — |

---

## 横向命令状态

```yaml
ai_context_doc: .specs/CONTEXT.md
last_intel_scan: 2026-10-25     # 已按 I-intel-scan 摸底过（未生成新 CONTEXT，仅核对）
last_architect_at:              # 从未跑 A-architect
last_evolve_at:
last_evolve_promoted: []
last_health_at:
last_health_score:
```

---

## 健康检查（AI 每次会话开始前自检）

- [x] `.specs/` 目录存在
- [x] 当前活跃 change 的工件文件齐全（CHANGE 阶段：`CHANGE.md`）
- [x] 没有未归档但已合并的 change
- [ ] 没有 > 3 轮自动重试仍未解决的失败（暂无）
