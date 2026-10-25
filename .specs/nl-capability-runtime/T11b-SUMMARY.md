# SUMMARY: T11b — decoder 后端接线（加载模型 + 受约束解码 + 按画像选后端）

- **Change ID**: `nl-capability-runtime` · **Task ID**: T11b · **完成时间**: 2026-10-25
- **前置**: T11a（GBNF 语法构造，2.5µs/次）

## 做了什么

### 1. 新模块 `libnlc/decision_model.{h,c}`（把"模型相关"隔离出来）

| 能力 | 接口 | 说明 |
|---|---|---|
| 档位 → 后端 | `nl_tier_from_bytes()`, `nl_backend_for_tier()` | A1(≤1GB)→`embedding`（decoder 完全不上，避免 512MB 机器 OOM）；A2/B/C→`decoder`；未知档位→`rules` |
| 内存探测 | `nl_total_ram_bytes()` | 读 `/proc/meminfo`；`NL_TIER` 环境变量可覆盖（容器/cgroup 会误判，ADR-015 要求可覆盖） |
| prompt 构造 | `nl_choose_user_prompt()`, `nl_choose_prompt()` | user 段纯字符串（可无模型单测）；完整 prompt 经 `nl_model_format_chat` 渲染模型自带 chat 模板 |
| 受约束解码 | `nl_choose_with_model()` | GBNF 只枚举候选 + `<refuse>`，模型**无从生成候选之外的能力 id** |
| 输出解析 | `nl_choose_parse()` | 容忍引号/空白；只接受候选集内 id 或 `<refuse>`；候选外一律拒绝 |
| 低置信度 | `nl_choose_min_conf()` | 默认 0.5，`NL_DECIDE_MIN_CONF` 可覆盖（AC-8） |

**安全性质**（比"能用"更重要）：
- 语法构造失败时 `infer_impl` 退回贪心，但输出仍要过 `nl_choose_parse`——不在候选集内即 `ERR`，**不会失败即放行**
- `<refuse>` 是一等输出，走拒答路径而不是硬选一个
- 实测 67 次选择中"输出候选外 id"的次数为 **0**

### 2. `model_loader` 补齐（T11b 依赖）

- `nl_model_infer_grammar()`：挂 `llama_sampler_init_grammar` → 贪心，`root` 规则名固定
- `nl_model_format_chat()`：`llama_model_chat_template` + `llama_chat_apply_template`，无模板回退 Qwen ChatML
- `nl_model_last_confidence()`：grammar 掩码后各 token 概率的几何平均（AC-6 的 `probability` 字段）
- `n_batch=256`（原来 1 会把 prefill 拖成逐 token）、每次推理前 `llama_memory_clear`（REPL 不能复用上次 KV）
- `n_threads` 按实际核数（上限 8）：默认值在 2 核机器上过订阅，反而更慢

### 3. `nlsh` 接线

- `nlsh --decide <自然语言>`：只决策不执行，输出
  `{"status":"ok","chosen":"cli:mkdir","method":"choice-llm","tier":3,"backend":"decoder","candidates":[...]}`
- `nlsh --eval <评测集>`：逐条决策 + 汇总（`schema_violations` / `fallbacks` / `accuracy` / `p50` / `p95`），见 T13
- 启动时按档位自降档：A1 档直接不加载 decoder（打印"decoder 未启用"）；模型加载失败 → `rules`，不阻塞
- 每次决策落审计（`method` 字段区分 `choice-llm` / `rule-fallback` / `rule-narrowed` / `low-confidence` / `model-refuse` / `no-candidate`）

## verify

```
$ NL_MODEL_PATH=~/.local/share/nl-os/qwen2.5-0.5b-q4_0.gguf ./build/tests/test_decision_model
decision_model tests
  TEST tier and backend mapping follows ADR-015 ... PASS
  TEST choice prompt lists every candidate id ... PASS
  TEST choice parse accepts only candidates or refuse ... PASS
  TEST constrained choice accuracy and latency (needs NL_MODEL_PATH) ...
  [decidable 100/120 · 粗筛命中 67 · 漏救 33 (33.0%) · 平均候选 0.85
   · rule top-1 61/67 = 91.0% · choice 66/67 = 98.5% (refuse 0 err 0)
   · P50 2453ms P95 5050ms]
 PASS
```

单次烟测（Qwen2.5-0.5B-Instruct q4_0，`n_ctx=512`）：

```
输入「创建一个叫 test 的文件夹」 → 候选 ["cli:mkdir"] → 输出 "cli:mkdir"（受约束解码，11 字节）
```

## 关键实测结论

| 指标 | 数值 | 说明 |
|---|---|---|
| 受约束解码准确率 | **66/67 = 98.5%** | 仅统计"正确能力已进候选"的 67 条 |
| 规则 top-1（同口径） | 61/67 = 91.0% | 模型在候选集内**修正了 5 条排序错误**（+7.5pp） |
| 候选外输出 | 0 | 语法 + 解析双重保证 |
| 解码失败 / 拒答 | 0 / 0 | 67 次全部成功 |
| 延迟 P50 / P95 | 2453ms / 5050ms | **本机是 2 vCPU + 负载 4.8 的共享 VM，8GB RAM 只剩 300MB 空闲**，模型 428MB 反复换页；不代表 B 档硬件性能 |
| 冷启动 | ~4s（首次读模型），常驻后每次推理仍需 2.5s | 见下方遗留 |

## 边界 / 遗留

1. **Noul 的模型信号仍未接**（T12 只做了规则的单调性组合）：模型只能参与 Choice，不能参与 allow/confirm/deny 抬升。理由是接一次 Noul 就要再跑一次 decode，P95 立刻翻倍；在延迟没解决前不接。
2. **Score 仍由规则算**（D14 不变），未做 embedding 版本。
3. **延迟远超 ADR-015 的 B 档目标（P95 ≤1.5s）**：本机 2 vCPU 太弱 + 内存压力大。要么在小模型上做 KV/prefix cache 复用（候选列表与 system 段固定，只变 user 输入），要么把 A2 档的 decoder 默认关掉只做 embedding。**这是待办，不是已达标**。
4. 每次推理 `clear_kv` 丢掉缓存：正确但慢，prefix cache 是下一个优化点。
5. 只有 x86_64 + glibc 实测；ARM64/musl 仍未验证（沿用 G4 架构师的降级标注）。
