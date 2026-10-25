# SUMMARY: T11a — Choice 的 GBNF 语法构造（模型无关部分）

- **Change ID**: `nl-capability-runtime` · **Task ID**: T11a · **完成时间**: 2026-10-25

## 做了什么

`nl_decide_grammar()`：只对候选集（≤8）枚举字面量，产生：

```
root ::= choice
choice ::= "\"cli:ls\"" | "\"cli:cat\"" | ... | "\"<refuse>\""
```

1. **不做全量枚举**（1000 能力会产生 ~12KB 语法）——这是 ADR-013 的核心决策
2. 末尾固定追加 `<refuse>` 兜底，允许模型拒绝而不是硬选
3. 候选为 0 时返回 -1（调用方必须走拒绝路径，不能构造空语法）

## verify

```
TEST grammar enumerates only candidates (+refuse) ... PASS
TEST grammar build cost well under 5ms ............. PASS
  [1000 次构造 2.466ms → 单次 0.0025ms]
```

**G2 架构师条件已满足**："段 2 开工前实测 GBNF 构造开销 ≤5ms" —— 实测单次 2.5 微秒。

## 尚未完成（T11b）

真正的 decoder 后端（加载模型 + 约束解码 + 宿主画像选后端）**未实现**：本机无 GGUF 模型。
已拆为 **T11b（blocked：需模型）**，接口与语法构造已就绪，模型到位后只需接 `llama_sampler_init_grammar`。
