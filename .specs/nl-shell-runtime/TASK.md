# TASK — nl-shell-runtime

> 压缩路径（0→3→4→6）。每项都有能跑出证据的 verify。

| ID | 任务 | write_files | verify | 状态 |
|---|---|---|---|---|
| T01 | `cd` 指令通道（内建判定 + 路径展开 + 目录校验 + 无通道报错） | `src/libnlc/shell_builtin.{c,h}` | `ctest -R shell_builtin` | ✅ |
| T02 | dispatch 内建分支：跳过注册表、写指令、诚实的失败路径 | `src/nlsh/dispatch.c` | `build/nlsh -c '进入 /tmp 目录'`（有/无通道各一次） | ✅ |
| T03 | 修死模板：上级/父目录/上一级 → template 901 | `src/libnlc/rules_db.h` | `--dry-run '切换到上级目录'` → `[dry-run] cd ..` | ✅ |
| T04 | 钩子改为"handler 写指令 + PROMPT_COMMAND 应用" | `scripts/nl-hook.bash` | 交互式 `pwd` 验证 | ✅ |
| T05 | 常驻守护进程（socket + 惰性加载 + 空闲卸载 + 单连接） | `src/nlsh/serve.{c,h}`, `src/nlsh/main.c` | `nlsh --serve` + 裸 socket 请求 | ✅ |
| T06 | 客户端与编解码（自动拉起/回退/超时） | `src/libnlc/parse_ipc.{c,h}` | `ctest -R parse_ipc` | ✅ |
| T07 | 三级解析接线 + `--no-model` 同时关远程 | `src/libnlc/nlc_api.c`, `src/nlsh/dispatch.c`, `src/nlsh/main.c` | 非 TTY 不拉起 / 守护不可用仍能跑 | ✅ |
| T08 | 修模型路径 abort（分块按 `n_batch`） | `src/libnlc/model_loader.c` | `nlsh -c '显示当前工作目录'` 不再 `rc=134` | ✅ |
| T09 | 验收：hook 35 断言 + 2 个单测 + 回归 | `scripts/verify_hook.sh`, `tests/unit/*` | 25→**35 通过**；`ctest 33/33` | ✅ |
| T10 | 落真机 + 文档 | 本目录、ADR-020/021、README、STATE、CHANGELOG | 重装后交互式实测 | ✅ |

## 实现中撞到的三件事（都写进了注释/ADR，不是"顺便修"）

1. **`command_not_found_handle` 跑在子 shell**：`cd` 在 handler 里无效 → 必须靠 `PROMPT_COMMAND`。
2. **`cd ..` 模板是死的**：没有任何规则选得到 template 901，`切换到上级目录` 被吃成 `cd 上级目录`。
3. **模型路径从来没成功过**：分块按 `n_ctx` 而非 `n_batch`，prompt 超 256 token 就 `GGML_ASSERT` abort。
   AC-14 只断言了"RSS 涨了"，没断言"解析成功"，所以这个 abort 一直没被发现。

## 边界自查

`git diff --name-only` 只应包含：`src/libnlc/{shell_builtin,parse_ipc,nlc_api,model_loader,json_helper,rules_db}*`、
`src/nlsh/{serve,main,dispatch}*`、`scripts/{nl-hook.bash,verify_hook.sh}`、`tests/unit/{shell_builtin,parse_ipc}_test.c`、
`tests/CMakeLists.txt`、`CMakeLists.txt`、`.specs/**`、README/STATE/CHANGELOG。
