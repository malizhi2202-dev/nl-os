# TEST: nlsh 执行接线

- **Change ID**: `nlsh-exec`
- **日期**: 2026-10-25
- **结论**: 验收脚本 36/36 通过；`ctest` 30/30；段 1/2/3 验收 12/12、18/18、16/16；`verify_all.sh` 0 失败

---

## 1. 自动化结果（本轮实跑）

| 套件 | 命令 | 结果 |
|---|---|---|
| 单元测试 | `ctest`（Debug, ASan/UBSan） | **30/30 passed**（新增 `exec`，`budget` 扩到 5 组） |
| 单元测试 | `ctest`（Release） | **30/30 passed** |
| 本 change 验收 | `scripts/verify_exec.sh` | **36 通过 / 0 失败** |
| 段 1 | `scripts/verify_segment1.sh` | pass=12 fail=0 |
| 段 2 | `scripts/verify_segment2.sh` | 18 通过 / 0 失败（decoder 档） |
| 段 3 | `scripts/verify_segment3.sh` | pass=16 fail=0 |
| 全量 | `scripts/verify_all.sh` | **0 项失败** |

## 2. AC ↔ 测试映射

| AC | 断言位置 | 实测 |
|---|---|---|
| AC-1 复合语义拒绝 | `verify_exec.sh` AC-1（5 条）+ `budget_test` `test_refuse_uat_s4` | ✅ |
| AC-2 能力缺失给候选 | `verify_exec.sh` AC-2（4 条） | ✅ |
| AC-3 写操作执行 | `verify_exec.sh` AC-3（3 条） | ✅ |
| AC-4 拒绝确认/无输入不执行 | `verify_exec.sh` AC-4 + AC-4b（5 条） | ✅ |
| AC-5 快照与撤销 | `verify_exec.sh` AC-5（3 条） | ✅ |
| AC-6 只读不问 | `verify_exec.sh` AC-6（3 条） | ✅ |
| AC-7 dry-run 零副作用 | `verify_exec.sh` AC-7（3 条） | ✅ |
| AC-8 无 shell 注入 | `exec_test`（9 反例 + 4 正例）+ `verify_exec.sh` AC-8 端到端 | ✅ |
| AC-9 审计可追溯 | `verify_exec.sh` AC-9（6 条） | ✅ |
| AC-10 不回归 | 上面的全套件 | ✅ |

## 3. 证据（实跑输出摘录）

### AC-1（UAT S4 原句，改前是 `rm -rf 原文件`）

```
$ ./build/nlsh --no-model -c '把 /var/log 下所有 .log 按大小排序压缩后发到远程服务器并清理原文件'
解析不了（超出能力边界）：能力不存在、跨能力过多
替代步骤：cli:ls、cli:stat、cli:cat
exit=1
```

### AC-2（`重启 nginx`）

```
$ ./build/nlsh --no-model -c '重启 nginx'
无法理解这句话；本机没有该能力或未支持该说法。
相近能力：cli:systemctl
exit=1
```

### AC-3 / AC-4 / AC-6 / AC-7

```
$ NL_ASSUME_YES=1 ./build/nlsh --no-model -c '创建一个叫 uat-x 的文件夹'
→ mkdir -p uat-x
✅ 完成（12 ms）
$ test -d uat-x && echo OK          # OK

$ printf 'no\n' | ./build/nlsh --no-model -c '创建一个叫 uat-y 的文件夹'
⚠️ 需要确认 (L2): mkdir -p uat-y
执行? [y/N] 已取消（未执行）        # exit=1，目录不存在

$ ./build/nlsh --no-model -c '列出当前目录'
→ ls -la
total 88
…（真实列表）
✅ 完成（11 ms）

$ ./build/nlsh --no-model --dry-run -c '创建一个叫 uat-z 的文件夹'
[dry-run] mkdir -p uat-z            # exit=0，目录未创建
```

### AC-5（删除 → 撤销）

```
$ NL_ASSUME_YES=1 ./build/nlsh --no-model -c '删除 keepme'
→ rm -rf keepme
✅ 完成（11 ms）
$ ./build/nlsh --no-model --undo
✅ 已撤销第 1 次快照
$ cat keepme/f.txt
important
```

### AC-8（输入级闸门，UAT 第二轮新发现）

改动过程中发现：`创建一个叫 x; touch hacked` 会被规则层解析成子命令 `touch hacked`，
而**生成命令本身是干净的**（不含元字符），只有在输入层能拦：

```
$ NL_ASSUME_YES=1 ./build/nlsh -c '创建一个叫 metachar_should_not_exist; touch hacked'
拒绝执行：输入含 shell 元字符 ';'，自然语言模式不支持 shell 语法   # 未创建 hacked
```

### AC-9（审计）

```
$ ./build/nlsh --no-model --audit --since 0 --limit 200
[{"audit_id":"…","capability_id":"cli:mkdir","verdict":"confirm","result":"ok",
  "duration_us":11700,"args_hash":"…","reason":"executed",…},
 {…"result":"cancelled","reason":"user-declined"…}]
```

## 4. 评测集未回归

```
$ NL_MODEL_PATH=/nonexistent ./build/nlsh --eval models/benchmarks/nl-commands-zh-v2.json | tail -1
{"summary":{"total":120,"ok":93,"refused":27,"schema_violations":0,"accuracy":91.4,
 "intent_checked":93,"intent_ok":85,"tier":3,"backend":"rules"}}
```
与改动前完全一致：新增的两个拒绝维度没有误伤评测集里的合法句子。

## 5. 性能（本机 2 vCPU，Release 语义）

Release 构建（`build-rel`），同一临时状态目录，各跑 3 次：

| 场景 | 耗时 |
|---|---|
| `nlsh -c '列出当前目录'`（注册表加载 + 解析 + 执行） | 0.07s / 0.05s / 0.05s |
| `NL_ASSUME_YES=1 nlsh -c '创建一个叫 perfN 的文件夹'`（含确认门 + 快照 + 执行） | 0.06s / 0.03s / 0.04s |
| 规则档决策 `--decide` | P50/P95 = 0ms（未触发解码） |

> 执行链路本身是 fork/exec（毫秒级）＋`clock_gettime` 计时；30–70ms 里主要是进程启动与
> 注册表全量加载（既有已知项，见 UAT「快路径开销」）。

## 6. 未覆盖 / 已知欠缺

| 项 | 说明 |
|---|---|
| `nl-run` 的 L2 自动放行 | `nl-run` 沿用既有 `allow_l2`（固化脚本里 L2 不问），`nlsh` 走"问一次"，两条路径的确认策略尚未统一 |
| 快照对 `mkdir` | 目标不存在时 `NL_SNAP_SKIPPED`（L2 不提示，L3+ 才提示） |
| 交互式 UTF-8 | 已支持多字节输入，但未做输入法组合串（IME pending string）的光标处理 |
