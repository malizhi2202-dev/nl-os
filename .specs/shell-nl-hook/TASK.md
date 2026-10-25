# TASK — shell-nl-hook

> 压缩路径（0→3→4→6）。总改动 ≈ 170 行（钩子 40 + 安装 60 + 验收 60 + 文档）。
> 每项都以"能跑出证据"的 verify 收口。

## 波次 1（顺序，无并行需求）

| ID | 任务 | write_files | verify | 状态 |
|---|---|---|---|---|
| T01 | 钩子实现：`command_not_found_handle` + 中文判定 + 系统回退 | `scripts/nl-hook.bash` | `bash -n scripts/nl-hook.bash` | ✅ |
| T02 | 安装器：`--hook-only` / `--no-hook` / `--uninstall-hook`，标记块幂等 + 备份 | `scripts/install_local.sh` | `bash scripts/install_local.sh --hook-only` 跑两次只留一段 | ✅ |
| T03 | 验收：AC-1~AC-9 共 25 条断言（假 nlsh 记录 argv，临时 HOME） | `scripts/verify_hook.sh` | `bash scripts/verify_hook.sh` → 25 通过 / 0 失败 | ✅ |
| T04 | 落真机：写进 `~/.bashrc` 并在真交互终端验证 | —（用户环境） | `bash -ic '列出当前目录'` 有输出 | ✅ |
| T05 | 文档：README / CHANGELOG / STATE / 本目录工件 | `README.md` `.specs/*` | 人工核对 | ✅ |

## 边界自查（提交前）

- `git diff --name-only` 必须是：`scripts/nl-hook.bash`、`scripts/install_local.sh`、
  `scripts/verify_hook.sh`、`README.md`、`.specs/shell-nl-hook/*`、`.specs/CHANGELOG.md`、`STATE.md`
- **没有**任何 C/C++ 源文件改动；`libnlc`/`nlsh` 二进制行为不变
- 没碰 `/etc/**`；`~/.bashrc` 只有一段标记块（5 行）

## 实现中撞到的两个 bash 真行为（已写进代码注释与 AC）

1. `case` 里 `[! -~]` 的空格**必须**转义成 `[!\ -~]`，否则直接语法错误；
   也不能用 `[![:print:]]`——UTF-8 locale 下中文算 printable，会把中文误判成 ASCII。
2. `command_not_found_handle` **对含 `/` 的命令名不触发**（bash 当路径去 exec）。
   所以"含绝对路径的中文句子"不会被接管；AC-7 的哨兵文件名也因此必须用相对名。
