# SUMMARY: T06 — cli 适配器（argv plan + 超时 kill）

- **Change ID**: `capability-protocol` · **Task ID**: T06 · **完成时间**: 2026-10-25

## 做了什么

1. **`nl_cli_build_plan`**：`cli:<name>` → `argv[]`；支持 `paths[]` 与 `args[]` 两个数组；**永不拼 shell 字符串**
2. 拒绝：非 `cli:` 前缀、不可信能力、无 `path`、单个参数 ≥1KB、参数超 32 个
3. **`nl_cli_execute`**：`fork` + `execv`（用注册表的 realpath，不信 argv[0]）；stdout+stderr 合流；`poll` 超时后 `SIGKILL`；退出码透传
4. 输出上限 4KB

## verify

```
adapter_cli tests
  mkdir executes and creates dir ....... PASS
  timeout kills child .................. PASS
  non-cli capability rejected .......... PASS
  ';' stays literal (no shell) ......... PASS
  exit code passthrough ................ PASS
5/5 passed, 0 failed
```

其中 `';' stays literal` 是对 `plan=argv` 这条设计的直接验证：参数 `/tmp/x;rm -rf /tmp/victim` 被原样传给 `mkdir`，**victim 目录不存在** → 证明没有经过 shell。

## 边界 / 遗留

- 改动：`src/nlbroker/adapter_cli.{h,c}`、`tests/unit/adapter_cli_test.c`、`tests/CMakeLists.txt`
- 不支持管道/重定向（ADR-016 有意为之）；复杂操作需多能力组合
- 子进程未做资源限制（CPU/内存）——v2 可加 `setrlimit`
