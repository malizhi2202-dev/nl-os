# SUMMARY: T02 — broker 骨架（Unix socket + 行分帧 + 分发）

- **Change ID**: `capability-protocol`
- **Task ID**: T02
- **完成时间**: 2026-10-25
- **AI 角色**: Dev

---

## 做了什么

`src/nlbroker/{server.h,server.c,main.c}`（约 250 行）+ `etc/nlbroker.service`：

1. **Unix socket 服务端**：`AF_UNIX` + `SOCK_STREAM`；`bind` 前 `unlink` 残留；创建后 `chmod 0600`；backlog 8
2. **行分帧**：1MB 上限；一个连接内可连续处理多行（已实测一次连接发 3 行 → 3 条响应）；超长且无 `\n` 则断开
3. **分发**：`nl_server_handle_line` 走 `nl_cip_parse` → 渲染；T02 只实现 `discover`（空能力表），其余 op 返回 `E_BAD_REQ`（T08 换成 `dispatch.c`）
4. **优雅退出**：`sigaction` **不设 `SA_RESTART`**，`accept` 可被信号中断；退出时 `close` + `unlink` socket
5. **`--dry-run`**：只打印 socket 路径不监听（T14 资源门限验证要用）
6. **systemd 单元**：`RuntimeDirectory` / `StateDirectory` / `NoNewPrivileges` / `ProtectSystem=strict` / `ProtectHome=read-only`

## 失败响应回带请求 id（实现中修的小问题）

首版解析失败时统一返回 `"id":"-"`，客户端无法关联是哪个请求失败了。改为：**解析已取到信封 id 就回带**，未取到才用 `-`。

## 改动文件

| 文件 | 性质 |
|---|---|
| `src/nlbroker/server.h` / `server.c` / `main.c` | 新增 |
| `etc/nlbroker.service` | 新增 |
| `CMakeLists.txt` | 修改（新增 `nlbroker` target，链接 `nlc`） |

## verify 输出

```
=== socket 权限
600
=== 一个连接内连发 3 行（测分帧）
{"v":1,"id":"a","ok":true,"result":{"tier":"A","capabilities":[],"cursor":null}}
{"v":1,"id":"b","ok":false,"error":"E_BAD_REQ"}
{"v":1,"id":"c","ok":true,"result":{"tier":"A","capabilities":[],"cursor":null}}
=== SIGTERM 优雅退出
socket 已删除 ✅
=== 全量回归
100% tests passed, 0 tests failed out of 12
```

## 边界检查

5 个改动项全部在 T02 的 `write_files` 内，无越界。

## 遗留

- T02 没有单测（TASK 未列测试文件）；分帧逻辑目前由本次手工 verify 覆盖，**正式回归在 T08 的 `verify_w2.sh`**（需补一条"单连接多请求"用例）
- `discover` 的 payload 是硬编码空表；`tier` 固定 `"A"`，等 T03/T08 接注册表与画像后改为真实值
- 单线程：一个慢连接会阻塞后续 accept（ADR-017 已接受，v2 再并发）
