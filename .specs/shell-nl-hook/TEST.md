# TEST — shell-nl-hook

## 1. 自动化

```bash
bash scripts/verify_hook.sh      # 25 通过 / 0 失败
```

全部用**假 nlsh**（PATH 里注入，记录 argv）+ **临时 HOME**，不碰真实 `~/.bashrc`，
不需要模型，可在满载机器上稳定跑（无时序断言）。

| AC | 断言 | 结果 |
|---|---|---|
| AC-1 中文接管 | 1a 调用 1 次 / 1b 整行原样 / 1c 退出码透传 3 / 1d 多词整行拼回 | 4/4 |
| AC-2 英文不接管 | 2a `git statsu` 未调 nlsh / 2b `nosuch_command_zzz` 未调 / 2c 仍给系统提示且非 0 | 3/3 |
| AC-3 真命令零影响 | `ls /tmp` 后钩子零调用 | 1/1 |
| AC-4 开关 | 4a `NL_SHELL_HOOK=0` 关 / 4b `NL_SHELL_HOOK_ALL=1` 接管英文 | 2/2 |
| AC-5 安装幂等可卸载 | 5a 一段标记块 / 5b 有备份 / 5c 钩子落 prefix / 5d 重复装仍一段 / 5e 其它行未改 / 5f 卸载后消失 / 5g 卸载后自定义行仍在 / 5h 钩子文件删除 | 8/8 |
| AC-6 不动系统文件 | `/etc/bash.bashrc` md5 安装前后一致 | 1/1 |
| AC-7 钩子不 eval | 7a 含 `;` 的哨兵文件一字节未创建 / 7b 整行原样传入（未拆分） | 2/2 |
| AC-8 语法 | 8a/8b `bash -n` 通过 / 8c 重复 source 不报错 | 3/3 |
| AC-9 真终端 | `bash -i` 经 `~/.bashrc` → 钩子 → nlsh 被调用 | 1/1 |

## 2. 真机端到端（人工实跑，2026-10-25）

装进真实 `~/.bashrc` 后，在交互式 bash 里：

| 场景 | 命令 | 期望 | 实际 |
|---|---|---|---|
| 中文只读命令 | `bash -ic '列出当前目录'` | 列目录 + 完成 | ✅ 列出 `/tmp` 内容，`✅ 完成（10 ms）`，整次 1.84s / 17MB |
| 确认门在钩子里仍生效 | `bash -ic '创建一个叫 nlhook_demo 的文件夹' </dev/null` | 要求确认，EOF 取消 | ✅ `⚠️ 需要确认 (L2): mkdir -p nlhook_demo` → `已取消（无输入）`，目录**未**创建 |
| 显式同意后执行 | 同一句加 `NL_ASSUME_YES=1` | 真建目录 | ✅ 目录出现 |
| 英文未知命令 | `bash -ic 'nosuchcmd_zzz'` | 系统 command-not-found | ✅ `nosuchcmd_zzz: command not found` |
| 关闭开关 | `NL_SHELL_HOOK=0 bash -ic '列出当前目录'` | 退化为系统行为 | ✅ `列出当前目录: command not found` |

## 3. 回归

- 未改任何 C/C++ 源：`ctest` 与 `verify_exec.sh` 的既有基线不受影响（本 change 未触碰 `src/`）
- `verify_hook.sh` 独立于 `verify_all.sh`（后者是 capability 协议 AC-1~AC-10，互不覆盖）

## 4. 已知未覆盖

| 项 | 说明 |
|---|---|
| 真实 IME 组合输入 | 与既有 bug 项一致，需真机输入法验证 |
| `cd` 等 shell 内建 | 范围外（见 CHANGE §3），钩子会显示"无 cli:cd 能力" |
| zsh | 本机无 zsh，未实现也未测 |
