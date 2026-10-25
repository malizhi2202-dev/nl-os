# TEST: nlsh 执行接线

- **Change ID**: `nlsh-exec`
- **日期**: 2026-10-25
- **结论**: 验收脚本 **76/76** 通过；`ctest` **31/31**（Debug+Release）；段 1/2/3 验收 12/12、18/18、16/16；`verify_all.sh` 0 失败

---

## 1. 自动化结果（本轮实跑）

| 套件 | 命令 | 结果 |
|---|---|---|
| 单元测试 | `ctest`（Debug, ASan/UBSan） | **31/31 passed**（新增 `exec`、`confirm`） |
| 单元测试 | `ctest`（Release） | **31/31 passed** |
| 本 change 验收 | `scripts/verify_exec.sh` | **76 通过 / 0 失败**（含 AC-11 统一确认门 40 条） |
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
| AC-11 确认策略统一（ADR-019） | `confirm_test`（4 组）+ `freeze_test`（策略头保留/防篡改）+ `verify_exec.sh` AC-11a~11k（40 条） | ✅ |
| AC-12 本地安装可用 | 登录 shell `command -v nlsh` + 无 `NL_MODEL_PATH` 时模型自动发现 | ✅ |
| AC-13 ARM64 交叉构建与运行 | 交叉构建 + `qemu-aarch64-static` 跑 scan/eval，数值与 x86_64 一致 | ✅（stub 模型） |

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

### AC-11（确认策略统一，ADR-019 全矩阵实跑）

```
PASS AC-11g nlsh 接受回答 [y] / [Y] / [yes] / [YES] / [是]
PASS AC-11g nl-run 接受中文「是」
PASS AC-11g 空行=取消（退出码 3）         PASS AC-11g 空行未执行
PASS AC-11g EOF=取消（nlsh 退出码 3）     PASS AC-11g EOF 未执行
PASS AC-11g EOF=取消（nl-run 退出码 3）   PASS AC-11g EOF 未执行（nl-run）
PASS AC-11g 否定词=取消
PASS AC-11h 只读命令直接执行（退出码 0）  PASS AC-11h 只读命令不弹确认
PASS AC-11i NL_ASSUME_YES 对 nlsh 生效    PASS AC-11i NL_ASSUME_YES 对 nl-run 生效
PASS AC-11i 固化后仍保留 @perm 头         PASS AC-11i allow-l2 无输入即执行 L2
PASS AC-11j 成功=0  PASS AC-11j 拒绝=1（与取消可区分）  PASS AC-11j 取消=3
PASS AC-11k 非破坏性快路径不建快照        PASS AC-11k 破坏性快路径建快照
PASS AC-11k 快路径删除可撤销              PASS AC-11k 审计标出破坏性快路径
PASS AC-11k 审计区分普通快路径
```

落地测试时撞出并修掉的缺陷（AC-11i 就是为了锁住它）：

```
$ printf '# @perm: allow-l2\n创建一个叫 l2f 的文件夹\n' > f.tools
$ build/nl-run --freeze f.tools          # 改前：固化后 # @perm 头消失
$ build/nl-run f.tools </dev/null        # → 仍然问一次 → 退出码 3（开关静默失效）
# 改后固化文件：
# @format: 2
# @checksum: sha256:5dfd514a…c6ed
# @perm: allow-l2
创建一个叫 l2f 的文件夹
$ build/nl-run f.tools </dev/null        # → mkdir -p l2f ✅ 退出码 0
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
| `nlsh -c 'echo hi'`（纯快路径，改前 20–50ms） | **0.00–0.01s**（T07 懒加载后） |
| `nlsh -c '列出当前目录'`（注册表加载 + 解析 + 执行） | 0.07s / 0.05s / 0.05s |
| `NL_ASSUME_YES=1 nlsh -c '创建一个叫 perfN 的文件夹'`（含确认门 + 快照 + 执行） | 0.06s / 0.03s / 0.04s |
| 规则档决策 `--decide` | P50/P95 = 0ms（未触发解码） |

> 执行链路本身是 fork/exec（毫秒级）＋`clock_gettime` 计时；30–70ms 里主要是进程启动与
> 注册表全量加载（既有已知项，见 UAT「快路径开销」）。

## 6. 未覆盖 / 已知欠缺

| 项 | 说明 |
|---|---|
| 快照对 `mkdir` | 目标不存在时 `NL_SNAP_SKIPPED`（L2 不提示，L3+ 才提示） |
| 交互式 UTF-8 | 已支持多字节输入，但未做输入法组合串（IME pending string）的光标处理 |
