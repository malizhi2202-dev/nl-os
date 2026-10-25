# TEST — nl-shell-runtime

## 1. 自动化结果（2026-10-25）

| 套件 | 数量 | 结果 |
|---|---|---|
| `ctest`（Debug） | 33 | **33/33 通过** |
| `scripts/verify_hook.sh` | 35 断言 | **35 通过 / 0 失败** |
| `scripts/verify_exec.sh` | 81 断言 | **81 通过 / 0 失败** |

新增用例：

- `tests/unit/shell_builtin_test.c`：内建判定、指令格式（`__NL_CD__ <绝对路径>`）、
  分号目标拒绝、`..`、非目录拒绝、无通道时报错原因、`~` 展开
- `tests/unit/parse_ipc_test.c`：`NlParseResult` 往返无损（命令/权限/确认/槽位/路径）、
  `ok:false` 与非法 JSON 视为失败、`nl_parse_rules_only` 与 `nl_parse` 判定一致
- `scripts/verify_hook.sh` AC-10~AC-13：
  - AC-10 交互式 `pwd` 证明父 shell 真的 cd 了
  - AC-11 用户原有 `PROMPT_COMMAND` 仍生效、重复 source 不重复挂载
  - AC-12 往指令通道塞 `touch` 命令一字节未执行（不 eval）
  - AC-13 有通道写指令 / 无通道诚实报错 / 非目录拒绝 / `cd ..` 模板可达

## 2. 真机实测

### 2.1 cd（交互式终端）

```
$ 进入 /tmp 目录
→ cd /tmp
✅ 已切换目录
malizhi@ubntu:/tmp$ pwd
/tmp
```

上级目录：

```
$ 切换到上级目录
→ cd ..
✅ 已切换目录
```

### 2.2 常驻解析进程（2 vCPU，裸 socket 请求）

| 请求 | 耗时 | 守护进程 RSS |
|---|---|---|
| 第 1 次（含模型加载） | 23.7s | 779 MB |
| 第 2 次（复用模型） | 21.9s | 796 MB（**没有重载**） |

客户端进程 RSS ≈ 26MB（模型不再在每个客户端进程里）。

**结论**：省掉的是加载（约 1.8s/次）与 780MB 的反复分配；**省不掉推理**（20s 量级，模型层瓶颈）。

### 2.3 非 TTY 不养常驻进程

```
$ XDG_RUNTIME_DIR=/tmp/nd2 build/nlsh -c '显示当前工作目录' < /dev/null
非 TTY rc=1
socket 文件: 0 个
守护进程: 0 个
```

### 2.4 模型路径不再 abort（本次修的真 bug）

修前：`GGML_ASSERT(n_tokens_all <= cparams.n_batch) failed` → `rc=134`（Release 与 Debug 都崩）。
修后：正常返回（本次输入模型判定"无法理解"，`rc=1`，但进程存活）。

## 3. 已知未覆盖

| 项 | 说明 |
|---|---|
| 推理耗时 | 单次 20s 量级属模型层（STATE「决策层延迟」），本次不解决也不假装解决 |
| zsh / 其它 shell | 未实现（本机无 zsh） |
| 多客户端并发 | 守护进程单线程，第二个请求排队；未测高压 |
| `export`/`alias` 等内建 | 范围外 |
