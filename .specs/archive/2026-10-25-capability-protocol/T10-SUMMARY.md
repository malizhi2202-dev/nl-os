# SUMMARY: T10 — dbus 能力发现

- **Change ID**: `capability-protocol` · **Task ID**: T10 · **完成时间**: 2026-10-25

## 做了什么

1. `nl_dbus_discover(reg)`：`busctl --no-pager introspect org.freedesktop.DBus /org/freedesktop/DBus org.freedesktop.DBus`
2. 解析 `method` 行 → 取成员名（**去掉 busctl 输出的前导 `.`**）
3. 登记为 `dbus:org.freedesktop.DBus|/org/freedesktop/DBus|org.freedesktop.DBus|<method>`
   - `perm=L2`、`effect=network`、`effect_defaulted=1`（推断而非人工标注）、`reversible=1`、`source=bus`
4. `nlbroker --scan-dbus --registry PATH`：发现后写回注册表（原子写）

## 实现与 TASK 原描述的偏差（已记录）

TASK 原写"`busctl list` 服务名 → `dbus:<service>|*`"。**但只登记服务名产生的能力不可调用**（没有方法、没有 path/interface），
违反"能力必须可执行"的前提。改为**自省 `org.freedesktop.DBus` 的方法**，登记后即可真实 invoke/commit。
服务级枚举留作 v2（会产出大量不可调用项，需要先设计"能力可用性"标记）。

## verify（端到端）

```
$ nlbroker --scan-dbus --registry /tmp/w3-reg.jsonl
dbus 能力发现：新增 20 条 → /tmp/w3-reg.jsonl

$ discover {prefix:"dbus:"} → 20 条，含 ...|ListNames
$ invoke  → confirm, plan.executed=False
$ commit  → ok, exit=0, 输出含 org.freedesktop.DBus
```

## 边界 / 遗留

- 只自省一个已知服务（`org.freedesktop.DBus`）；全总线自省会产生数百条且耗时长
- 方法签名未映射 → 带参方法调用仍不可用（与 T09 的限制一致）
