# TEST: NL-OS 能力运行时 — AC 验收记录

- **Change ID**: `nl-capability-runtime`
- **验收时间**: 2026-10-25（段 2 收尾后补齐全量记录）
- **构建**: Debug（ASan）+ Release（`-O2`）双构建；下文数字标注构建类型
- **一键复现**:
  ```
  cmake -S . -B build && cmake --build build -j$(nproc) && (cd build && ctest --output-on-failure)
  ./scripts/verify_segment1.sh     # AC-1 ~ AC-5
  ./scripts/verify_segment2.sh     # AC-6 ~ AC-10（有 NL_MODEL_PATH 则测 decoder 档）
  ./scripts/verify_segment3.sh     # AC-11 ~ AC-16
  ```

## 0. 汇总

| 段 | 脚本 | 结果 |
|---|---|---|
| 段 1 | `verify_segment1.sh` | **12 / 12 通过** |
| 段 2 | `verify_segment2.sh`（规则档） | **17 / 17 通过** |
| 段 2 | `verify_segment2.sh`（decoder 档，真实 GGUF） | **18 / 18 通过** |
| 段 3 | `verify_segment3.sh` | **16 / 16 通过** |
| 单元/组件测试 | `ctest`（Debug） | **28 / 28 通过** |
| 单元/组件测试 | `ctest`（Release） | **28 / 28 通过** |

---

## 1. 功能验收矩阵

| AC | 要求 | 验证命令 | 实测结果 | 状态 |
|---|---|---|---|---|
| **AC-1** | 能力自发现：扫描零写副作用，注册表落盘 0600 | `nlsh --no-model --scan` + 扫描前后被扫目录 mtime 对比 | 新增 1890 条；被扫目录无写入；注册表 `0600` | ✅ |
| **AC-2** | 三档自检（usable / needs-privilege / missing） | `ctest -R capability_selftest` + `nlsh --self-test` | `usable=23 needs-privilege=1330 missing=537 total=1890`；单测 2/2 | ✅ |
| **AC-3** | 快路径不经模型且审计 `fast_path=true` | `NL_MODEL_PATH=/nonexistent nlsh -c 'ls -la'` + 审计断言 | 执行成功；审计行 `fast_path: true` | ✅ |
| **AC-4** | 能力缺失不猜命令 | `nlsh -c '重启 nginx'` | 输出"本机没有该能力"+ 语义最近 3 候选，**无命令输出** | ✅ |
| **AC-5** | 复杂度预算拒绝 + 维度 + 替代步骤 | `ctest -R budget` + `nlsh -c '把 /var/log 下所有 .log 按大小排序压缩后发到远程并清理'` | `refused`，含超出维度与可直接复制的替代步骤；退出码非 0 | ✅ |
| **AC-6** | Choice 只能选注册表里的能力 ID | `nlsh --eval models/benchmarks/nl-commands-zh-v2.json` | 77 条给出选择、43 条拒答；**所有 `capability_id` 都能在注册表查到**；准确率 85.7%（66/77） | ✅ |
| **AC-7** | Noul 三档与 L1–L4 一致 | `ctest -R 'decision\|verdict\|ticket'` + `--decide` 断言 | 单调性单测通过；`列出当前目录`→`cli:ls`(L1)、`创建…文件夹`→`cli:mkdir`(L2) | ✅（模型信号未接，见遗留） |
| **AC-8** | 低置信度/看不懂不执行，只列候选 | `nlsh --decide '弄一下那个'`；`NL_DECIDE_MIN_CONF=1.0 nlsh --decide '创建…文件夹'` | 前者 `refused` + `no-candidate`、无 `chosen`；后者 `refused` + `low-confidence` + 列候选 | ✅（默认阈值 0，理由见 DESIGN D19） |
| **AC-9** | 决策层不可用时降级不崩 | `NL_MODEL_PATH=/nonexistent nlsh --eval …`；`NL_TIER=1 nlsh --decide …` | 退出码 0、无 segfault、规则层照常执行；A1 档 `backend:"embedding"` 不加载 decoder | ✅ |
| **AC-10** | 约束解码 3 轮 × 120 条 100% 合法 | `verify_segment2.sh` 的 3 轮 `--eval` | 三轮 `schema_violations=0`、`fallbacks=0`（模型确实在跑） | ✅ |
| **AC-11** | `.tools` 固化含能力 ID + 校验和，幂等 | `nl-run --freeze good.tools` ×2 | 头部含 `# @format: 2` 与 `# @checksum: sha256:…`；两次 `diff` 为空 | ✅ |
| **AC-12** | 固化后离线无模型重放 | `NL_MODEL_PATH=/nonexistent nl-run good.tools` | 全部步骤成功，输出"使用固化脚本"标记 | ✅ |
| **AC-13** | 篡改被拒绝且无副作用 | `sed -i` 改一行后 `nl-run` | 退出码非 0、报校验和不匹配、**未执行任何步骤** | ✅ |
| **AC-14** | L2/L3 可撤销 | 建目录→删除→`nlsh --undo` | 目录及内容恢复；L1 场景提示"无需撤销" | ✅ |
| **AC-15** | 敏感路径不进快照 + 容量上限 | `$WORK/.ssh/id_rsa` 删除 → 快照 → undo | 快照目录 grep 不到私钥内容；undo 后私钥**未**被恢复；超限按 LRU 淘汰并打印记录 | ✅ |
| **AC-16** | 审计可回放 | `nlsh --audit --since 0 --limit N` | 输出 JSON 数组，含 `audit_id`/时间/能力/权限/`fast_path`/结果 | ✅ |

---

## 2. 非功能性验收（**含未达标项**）

| 指标 | 目标（REQUIREMENT / ADR-015） | 实测 | 状态 |
|---|---|---|---|
| 首次能力扫描 | ≤ 2s（树莓派 4） | **0.09s**（Release，1890 条，本机 2 vCPU） | ✅ |
| 自检 | ≤ 10s | **0.56s**（Release） | ✅ |
| 快路径额外开销 | ≤ 5ms（对比直接 `exec`） | **≈30ms 端到端**（Release）：进程启动 ~10ms + `nl_cap_load` **8.8ms** + exec；Debug(ASan) 为 90ms / 59ms | ❌ **未达标** |
| 决策层 P95 · A 档 | ≤ 150ms | 规则/embedding 路径 <1ms（未跑 decoder） | ✅（decoder 本就不上） |
| 决策层 P95 · B 档 | ≤ 1.5s | **2788ms**（0.5B q4_0，本机 2 vCPU + 负载 4.8 + 内存余量 300MB） | ❌ **未达标** |
| 决策层 P95 · C 档 | ≤ 1s | 未测（无 3B–7B GGUF） | ❌ 未验证 |
| 冷启动（模型常驻热启动） | ≤ 100ms | 现状是**每进程冷启动**：加载 428MB GGUF 约 4s | ❌ 未实现常驻 |
| 内存 · broker（不含模型） | ≤ 80MB | VmHWM 5.3MB（Release，`verify_all.sh`） | ✅ |
| 内存 · 含 0.5B 模型 | ≤ 2GB（B 档） | 进程 RSS ≈ 810MB（Debug 实测） | ✅ |

> 快路径 30ms 的构成已经定位：**每次进程都全量解析 1890 条注册表**（8.8ms）
> 加进程启动（~10ms）再加 exec。要达标需要"懒加载注册表"或"二进制缓存"，已列入遗留。

---

## 3. 段 2 决策层实测（T13）

| 档 | 后端 | 给出选择 | 准确率（已选择中） | P50 | P95 |
|---|---|---|---|---|---|
| A1（`NL_TIER=1`） | embedding | 77/120 | 79.2%（61/77） | <1ms | <1ms |
| B（本机 0.5B） | decoder | 77/120 | **85.7%（66/77）** | 1493ms | 2788ms |
| C | decoder | — | 未测 | — | — |

漏斗（100 条可判定输入）：**粗筛命中 67 / 漏救 33（33%）**，平均候选数 **0.85**；
命中 67 条内规则 top-1 为 61（91.0%），受约束 Choice 为 66（98.5%）。

---

## 4. 测试环境

| 项 | 值 |
|---|---|
| OS / libc | Linux x86_64 / glibc |
| CPU | AMD Ryzen 9 8945HX，但**仅分配 2 vCPU**，负载均值 4.3–4.8 |
| RAM | 8GB（测试时仅剩 ~300MB 空闲） |
| 编译器 | GCC，`-Wall -Wextra -Werror`，Debug 带 `-fsanitize=address` |
| llama.cpp | 系统安装 `/usr/local/lib/libllama.so`（ADR-006 零新依赖） |
| 模型 | Qwen2.5-0.5B-Instruct q4_0（428MB，ModelScope） |

**未覆盖**：ARM64 / ARMv7 / musl（无交叉编译链）、C 档硬件、真实用户口语句式。

---

## 5. 复现性说明

- 段 2 的 decoder 档需要 `NL_MODEL_PATH` 指向真实 GGUF；缺失时脚本自动按规则档跑（仍是 17 条断言）。
- `--eval` 的输出可被 `python3 -c 'import json'` 逐行解析（input 做了 JSON 转义，含引号的输入也安全）。
- 注册表/快照/审计路径均可用 `XDG_STATE_HOME` 重定向，验证脚本全部在临时目录里跑，不动用户真实状态。
