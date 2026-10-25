# SUMMARY: T09 — dbus 适配器（busctl 子进程桥）

- **Change ID**: `capability-protocol` · **Task ID**: T09 · **完成时间**: 2026-10-25

## 做了什么

1. `nl_dbus_tool_path()`：在 `/usr/bin` `/bin` `/usr/local/bin` 里找 `busctl`（不引 libdbus）
2. `nl_dbus_build_plan()`：解析两种 ID 形态
   - `dbus:<service>|<method>` → path=`/`、interface=`service`
   - `dbus:<service>|<path>|<interface>|<method>`（4 段）
   - 每段做**字符白名单**校验（字母数字 + `_.-`，path 额外允许 `/`），`;` `$()` 等一律拒绝
   - v1 仅支持**无参方法调用**；带 args 直接拒绝
3. 复用 `NlPlan` 与 `nl_cli_execute`（超时/杀进程/退出码），不重复造执行器
4. dispatch 增加按命名空间选择适配器（`cli:` / `dbus:`）

## verify

```
adapter_dbus tests
  two-part id -> busctl argv ......... PASS
  four-part id accepted .............. PASS
  shell metacharacters rejected ...... PASS   ← ';' 与 '$()' 都被拒
  non-empty args rejected ............ PASS
  live call ...ListNames ............. PASS   ← 真实 system bus 调用
  discover registers methods ......... PASS
6/6 passed, 0 failed
```

## 边界 / 遗留

- 改动：`src/nlbroker/adapter_dbus.{h,c}`、`src/nlbroker/dispatch.c`、`src/nlbroker/main.c`、`CMakeLists.txt`、`tests/CMakeLists.txt`
- 仅支持 `busctl`；`gdbus` 语法不同，未实现（若目标设备只有 gdbus 则该命名空间不可用）
- 仅无参方法（v1）；带参调用需要 D-Bus 签名映射，记入 v2
