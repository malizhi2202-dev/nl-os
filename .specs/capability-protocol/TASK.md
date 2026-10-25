# TASK: 统一能力调用协议（CIP v1）+ 本地 broker

- **Change ID**: `capability-protocol`
- **关联**: `@.specs/capability-protocol/{REQUIREMENT,DESIGN}.md`、`@.specs/adr/016~018`
- **拆解依据**: DESIGN §6 的 W1–W5；每条任务对应 REQUIREMENT 的 AC
- **纪律**: 每个任务 2–10 分钟可完成、含可执行 `verify`；**不允许一个任务跨两个适配器**

---

## 波次划分

```
W1 协议与骨架   T01[P] → T02[P] → T03[P]
                                    │
W2 最小闭环     T04 → T05 → T06 → T07 → T08   ← 分水岭：端到端可用
                                              │
W3 DBus         T09[P] → T10[P]               │
W4 MCP          T11[P] → T12[P]               │
W5 HTTP+收口    T13[P] → T14                  │
```

---

## 任务清单

```xml
<task id="T01" parallel="false" status="done" auto="true">
  <name>cip：协议编解码（信封 + 5 报文 + 9 错误码）</name>
  <read_files>
    .specs/capability-protocol/DESIGN.md      <!-- §3 契约 -->
    .specs/adr/016-cip-protocol.md
    src/libnlc/json_helper.h
    third_party/cJSON/cJSON.h
  </read_files>
  <write_files>
    src/libnlc/cip.h
    src/libnlc/cip.c
    tests/unit/cip_test.c
    tests/CMakeLists.txt
    CMakeLists.txt
  </write_files>
  <action>
    1. 定义 `NlCipReq { v, id, op, ... }` 与 `NlCipResp { v, id, ok, error, ... }` 结构
    2. `nl_cip_parse(const char *line, NlCipReq *out)` —— JSON Lines 单行解析，非法报文返回 E_BAD_REQ
    3. `nl_cip_render(const NlCipResp *r, char *buf, size_t cap)` —— 序列化
    4. 9 个错误码字符串常量与 `nl_cip_error_str()`
    5. 版本校验：`v != 1` → E_VERSION
    6. 注意：vendor cJSON 是 API 子集（无 `GetObjectItemCaseSensitive`），只用 `cJSON_GetObjectItem` / `cJSON_GetStringValue` / `cJSON_GetNumberValue`
  </action>
  <verify>cd build && cmake .. -DBUILD_TESTING=ON && make -j$(nproc) && ctest -R cip --output-on-failure</verify>
  <done>6 类 case 通过：合法 discover / 未知 op → E_BAD_REQ / v=2 → E_VERSION / 缺 id → E_BAD_REQ / 空行 → E_BAD_REQ / render 后可被 reparse</done>
  <depends_on></depends_on>
</task>

<task id="T02" parallel="false" status="done" auto="true">
  <name>broker 骨架：Unix socket + 行分帧 + 分发</name>
  <read_files>
    src/libnlc/cip.h
    src/nl-perm/ipc_server.c          <!-- 复用 Unix socket 既有写法 -->
    .specs/adr/017-broker-process.md
  </read_files>
  <write_files>
    src/nlbroker/main.c
    src/nlbroker/server.c
    src/nlbroker/server.h
    CMakeLists.txt
    etc/nlbroker.service
  </write_files>
  <action>
    1. `nlbroker [--socket PATH] [--dry-run]`；默认 `/run/nl-os/broker.sock`
    2. `AF_UNIX` + `SOCK_STREAM`；`bind` 前 `unlink` 旧文件；**创建后 chmod 0600**
    3. accept 循环（v1 单线程，一次处理一个连接直到 EOF）
    4. 按 `\n` 分帧（单行上限 1MB，超限断开）；每行交 `nl_cip_parse` → 分发
    5. `discover` 先返回空能力表（注册表在 T03 接）
    6. SIGTERM/SIGINT 优雅退出并删除 socket
    7. **verify fallback**：`nc` 不存在时改用 `python3 -c 'import socket,sys;s=socket.socket(socket.AF_UNIX);s.connect("/tmp/t.sock");s.sendall(b"{...}\n");print(s.recv(4096).decode())'`
  </action>
  <verify>cd build && make -j$(nproc) && ./src/nlbroker/nlbroker --socket /tmp/t.sock & sleep 0.3; printf '{"v":1,"id":"1","op":"discover"}\n' | nc -U /tmp/t.sock; stat -c '%a' /tmp/t.sock</verify>
  <done>discover 返回 ok=true 且 capabilities 为数组；socket 权限为 600；SIGTERM 后 socket 文件被删除</done>
  <depends_on>T01</depends_on>
</task>

<task id="T03" parallel="false" status="done" auto="true">
  <name>注册表 v2：namespace / effect / reversible（兼容读 v1）</name>
  <read_files>
    src/libnlc/capability_registry.c
    src/libnlc/capability_registry.h
    .specs/adr/012-capability-registry.md
    .specs/capability-protocol/DESIGN.md      <!-- §1 / AC-2 -->
  </read_files>
  <write_files>
    src/libnlc/capability_registry.c
    src/libnlc/capability_registry.h
    tests/unit/capability_registry_test.c
  </write_files>
  <action>
    1. `NlCapability` 增加 `namespace` / `effect` / `reversible`
    2. `NL_CAP_FORMAT_VERSION` 升为 2；**读 v1 文件时**：namespace 由 id 前缀推断，effect 缺省 `delete`，reversible 缺省 0
    3. 写出时必带三字段；`discover` / `describe` 依赖这些字段
    4. `effect_defaulted` 标记：读取时若走了缺省，置 1
  </action>
  <verify>cd build && make -j$(nproc) && ctest -R capability_registry --output-on-failure</verify>
  <done>新增 3 类 case：v2 往返保留三字段 / v1 文件读入后 effect=delete 且 effect_defaulted=1 / 非法 effect 字符串 → 按 delete 处理</done>
  <depends_on>T01</depends_on>
</task>

<task id="T04" parallel="false" status="done" auto="true">
  <name>裁决器：effect 从严 + 权限分级 + 保护路径（AC-3, AC-5）</name>
  <read_files>
    src/libnlc/path_safety.h
    src/libnlc/capability_registry.h
    src/libnlc/cip.h
    .specs/capability-protocol/DESIGN.md      <!-- §4 威胁模型 -->
  </read_files>
  <write_files>
    src/nlbroker/verdict.c
    src/nlbroker/verdict.h
    tests/unit/verdict_test.c
  </write_files>
  <action>
    1. `nl_verdict_eval(cap, args, out)` → `allow | confirm | deny`，**永不产生"执行"**
    2. effect=delete 或 reversible=0 → 至少 confirm；命中保护路径 → deny（复用 `path_safety`）
    3. perm=L4 → deny；L3 → confirm（需二次确认文本）
    4. **忽略请求中出现的 `effect` 字段**，并在结果中置 `untrusted_effect_attempt`
    5. deny 时 `plan=NULL` 且给 `alternatives`（来自 `alternatives_db`，可执行）
  </action>
  <verify>cd build && make -j$(nproc) && ctest -R verdict --output-on-failure</verify>
  <done>6 类 case：L1 allow / L2 confirm / L3 confirm / L4 deny / 保护路径 deny 且 plan 为空 / 伪造 effect=read 的 rm 仍 deny 且 untrusted_effect_attempt=1</done>
  <depends_on>T03</depends_on>
</task>

<task id="T05" parallel="false" status="done" auto="true">
  <name>ticket：签发 / 校验 / 一次性 / TTL / args 哈希 / 上限 1024+LRU</name>
  <read_files>
    src/nlbroker/verdict.h
    src/libnlc/cip.h
    .specs/adr/017-broker-process.md
  </read_files>
  <write_files>
    src/nlbroker/ticket.c
    src/nlbroker/ticket.h
    tests/unit/ticket_test.c
  </write_files>
  <action>
    1. `nl_ticket_issue(cap_id, args, effect, out_token)`：128bit CSPRNG，绑定 args 的 SHA-256
    2. `nl_ticket_check(token, cap_id, args, now)` → `OK | INVALID | EXPIRED | MISMATCH(含重放)`
    3. TTL 默认 60s；用完即删 + nonce 进短时黑名单
    4. **表上限 1024 + LRU 淘汰**（G2 安全条件）；淘汰与超限写审计
    5. 每会话 invoke 限速（默认 100/s）——超限返回 `E_POLICY_DENY`
  </action>
  <verify>cd build && make -j$(nproc) && ctest -R ticket --output-on-failure</verify>
  <done>6 类 case：正常通过 / 伪造 / 过期 / 参数不符 / 二次使用 / 插入 1025 个后最旧的被淘汰且表大小=1024</done>
  <depends_on>T04</depends_on>
</task>

<task id="T06" parallel="false" status="done" auto="true">
  <name>cli 适配器：argv plan + commit 执行（不经 shell）</name>
  <read_files>
    src/libnlc/template_fill.h
    src/libnlc/capability_registry.h
    src/nlbroker/verdict.h
  </read_files>
  <write_files>
    src/nlbroker/adapter_cli.c
    src/nlbroker/adapter_cli.h
    tests/unit/adapter_cli_test.c
  </write_files>
  <action>
    1. `nl_cli_build_plan(cap, args, plan)` → `char *argv[]`（**绝不拼 shell 字符串**）
    2. 参数逐项校验类型与长度；路径过 `path_safety`
    3. `nl_cli_execute(plan, timeout_ms, out)`：`fork` + `execvp` + 超时 kill（SIGKILL）+ 捕获 stdout/stderr + 退出码
    4. 明确不支持管道/重定向（返回 `E_BAD_REQ` 并提示拆成多能力）
  </action>
  <verify>cd build && make -j$(nproc) && ctest -R adapter_cli --output-on-failure</verify>
  <done>5 类 case：正常 mkdir 成功 / 超时被 kill / 非法字符拒绝 / 含 `;` 的参数被当作字面量（不执行第二段）/ 退出码透传</done>
  <depends_on>T05</depends_on>
</task>

<task id="T07" parallel="false" status="done" auto="true">
  <name>审计：JSONL 写入 + audit 查询 + 脱敏（AC-9）</name>
  <read_files>
    src/libnlc/audit.h          <!-- T06 骨架已建 -->
    .specs/capability-protocol/REQUIREMENT.md   <!-- AC-9 -->
  </read_files>
  <write_files>
    src/libnlc/audit.c
    src/nlbroker/audit_query.c
    tests/unit/audit_test.c
  </write_files>
  <action>
    1. 追加写 `${XDG_STATE_HOME}/nl-os/audit.jsonl`，0600，O_APPEND + flock
    2. 字段：`audit_id / ts / capability_id / verdict / effect / duration_us / reason / args_hash`（**不落明文参数**）
    3. `audit` 报文：按 `since` 过滤 + `limit` 上限（默认 50、最大 500）
    4. `invoke` 与 `commit` 各记一条，可用 `audit_id` 关联
  </action>
  <verify>cd build && make -j$(nproc) && ctest -R audit --output-on-failure</verify>
  <done>4 类 case：写入可被 python json 解析 / 含 `; rm -rf` 的参数不出现明文 / since 过滤有效 / limit 超上限被截断</done>
  <depends_on>T04</depends_on>
</task>

<task id="T08" parallel="false" status="done" auto="true">
  <name>W2 集成：discover→describe→invoke→commit→audit 全链路（AC-1,2,3,4,9）</name>
  <read_files>
    src/nlbroker/*
    src/libnlc/cip.h
  </read_files>
  <write_files>
    src/nlbroker/main.c
    src/nlbroker/dispatch.c
    src/nlbroker/dispatch.h
    src/nlbroker/server.c          <!-- 扩范围：handler 注入 -->
    src/nlbroker/server.h          <!-- 扩范围：NlLineHandler -->
    src/libnlc/cip.c               <!-- 扩范围：commit 补 capability/args（契约缺陷） -->
    tests/unit/cip_test.c          <!-- 扩范围：同步 commit 契约 -->
    scripts/verify_w2.sh
  </write_files>
  <action>
    1. `discover` 接注册表（支持 prefix/limit/cursor）
    2. `describe` 返回 slots/effect/reversible/effect_defaulted
    3. `invoke` 走裁决 → 生成 plan → 签发 ticket（confirm）或直接执行（allow）
    4. `commit` 校验 ticket → 执行 → 审计
    5. `scripts/verify_w2.sh` 覆盖 AC-1/2/3/4/9 五条
  </action>
  <verify>bash scripts/verify_w2.sh</verify>
  <done>脚本五条断言全绿；AC-3 用"响应 plan.executed=false + 审计无对应 commit"双断言（不依赖 strace）</done>
  <depends_on>T02,T03,T04,T05,T06,T07</depends_on>
</task>

<task id="T09" parallel="true" status="done" auto="true">
  <name>dbus 适配器：busctl/gdbus 子进程桥（AC-7）</name>
  <read_files>
    src/nlbroker/adapter_cli.h
    .specs/adr/018-capability-adapters.md
  </read_files>
  <write_files>
    src/nlbroker/adapter_dbus.c
    src/nlbroker/adapter_dbus.h
    src/nlbroker/dispatch.c        <!-- 扩范围：按命名空间路由 -->
    src/nlbroker/main.c            <!-- 扩范围：--scan-dbus -->
    tests/unit/adapter_dbus_test.c
  </write_files>
  <action>
    1. 探测 `busctl` / `gdbus` 是否存在（缺失则该命名空间为空，不报错）
    2. `dbus:<service>|<method>` 解析；服务名/方法名白名单校验（仅 `[A-Za-z0-9_.|]`）
    3. 通过 argv 调 `busctl call`，**不拼 shell**；超时映射为进程超时
    4. 返回值原样回传（JSON 或纯文本）
  </action>
  <verify>cd build && make -j$(nproc) && ctest -R adapter_dbus --output-on-failure && ldd build/nlbroker | grep -c dbus</verify>
  <done>调用 `dbus:org.freedesktop.DBus|ListNames` 成功；`ldd | grep -c dbus` 为 0</done>
  <depends_on>T08</depends_on>
</task>

<task id="T10" parallel="true" status="done" auto="true">
  <name>dbus 能力发现：busctl list → 注册表</name>
  <read_files>
    src/nlbroker/adapter_dbus.h
    src/libnlc/capability_registry.h
  </read_files>
  <write_files>
    src/nlbroker/adapter_dbus.c
    tests/unit/adapter_dbus_test.c
  </write_files>
  <action>
    1. `nl_dbus_discover(registry)`：**改为自省 `org.freedesktop.DBus` 的方法**（原方案登记服务名会产生不可调用的能力，见 T10-SUMMARY）
    2. effect 缺省 `network`（DBus 服务多与网络/系统状态相关），perm 缺省 L2
    3. 标记 `effect_defaulted=1`
  </action>
  <verify>cd build && make -j$(nproc) && ctest -R adapter_dbus --output-on-failure</verify>
  <done>`discover {prefix:"dbus:"}` 返回 ≥1 条；每条 effect 为 network 且 effect_defaulted=1</done>
  <depends_on>T09</depends_on>
</task>

<task id="T11" parallel="true" status="pending" auto="true">
  <name>mcp 客户端适配器：stdio JSON-RPC（反向吸收）</name>
  <read_files>
    src/nlbroker/adapter_cli.h
    .specs/adr/018-capability-adapters.md
  </read_files>
  <write_files>
    src/nlbroker/adapter_mcp.c
    src/nlbroker/adapter_mcp.h
    tests/unit/adapter_mcp_test.c
    tests/fixtures/fake_mcp_server.py
  </write_files>
  <action>
    1. 启动配置的 MCP server 子进程；`initialize` → `tools/list` → `tools/call`
    2. 工具名映射为 `mcp:<server>/<tool>`；`inputSchema` 转成 slots
    3. effect 缺省 `write`（工具可能改状态），perm 缺省 L2
    4. 子进程崩溃 → 该命名空间不可用，不拖垮 broker
  </action>
  <verify>cd build && make -j$(nproc) && ctest -R adapter_mcp --output-on-failure</verify>
  <done>用 fake server 完成 list + call；工具可被 `discover {prefix:"mcp:"}` 看到；server 崩溃后 discover 不再返回其能力</done>
  <depends_on>T08</depends_on>
</task>

<task id="T12" parallel="true" status="pending" auto="true">
  <name>MCP 门面：仅暴露 perm ≤ L2（AC-6）</name>
  <read_files>
    src/nlbroker/adapter_mcp.h
    .specs/capability-protocol/REQUIREMENT.md   <!-- AC-6 -->
  </read_files>
  <write_files>
    src/nlbroker/facade_mcp.c
    src/nlbroker/facade_mcp.h
    tests/unit/facade_mcp_test.c
  </write_files>
  <action>
    1. stdio JSON-RPC server：实现 `initialize` / `tools/list` / `tools/call`
    2. `tools/list` **只返回 perm ≤ L2**；工具名统一前缀 `nl_`
    3. 对 L3/L4 的 `tools/call` 返回错误并写审计
    4. 内部转调 broker 的 CIP（同一进程内函数调用，不经 socket）
  </action>
  <verify>cd build && make -j$(nproc) && ctest -R facade_mcp --output-on-failure</verify>
  <done>list 不含 L3/L4 能力；直接 call 一个 L3 工具被拒且审计有记录</done>
  <depends_on>T08</depends_on>
</task>

<task id="T13" parallel="true" status="pending" auto="true">
  <name>http 适配器（beta）：最小 HTTP/1.1，仅 http://（AC-8）</name>
  <read_files>
    src/nlbroker/adapter_cli.h
    .specs/capability-protocol/DESIGN.md   <!-- §1 http 行 -->
  </read_files>
  <write_files>
    src/nlbroker/adapter_http.c
    src/nlbroker/adapter_http.h
    tests/unit/adapter_http_test.c
  </write_files>
  <action>
    1. 能力 ID `http:GET|<url>` / `http:POST|<url>`；URL 解析只支持 host:port + path
    2. `socket` + `connect` + 手写请求行/头 + 读响应；**只支持 http://**
    3. `https://` → `E_SCHEME_UNSUPPORTED` + `alternatives:["cli:curl"]`
    4. 超时 5s；响应体上限 1MB
  </action>
  <verify>cd build && make -j$(nproc) && ctest -R adapter_http --output-on-failure</verify>
  <done>对本地 http server 的 GET 成功；https URL 返回 E_SCHEME_UNSUPPORTED 且 alternatives 含 cli:curl</done>
  <depends_on>T08</depends_on>
</task>

<task id="T14" parallel="false" status="pending" auto="false">
  <name>🔒 收口：AC 全量验证 + 对抗用例 + 资源门限</name>
  <read_files>
    .specs/capability-protocol/REQUIREMENT.md
    scripts/verify_w2.sh
  </read_files>
  <write_files>
    scripts/verify_all.sh
    .specs/capability-protocol/UAT.md
  </write_files>
  <action>
    1. `scripts/verify_all.sh` 覆盖 AC-1~AC-10
    2. **AC-5 对抗脚本**：构造 `{"effect":"read"}` 伪装的 `rm`，必须 deny 且审计标记
    3. **AC-10 资源**：在 `systemd-run --scope -p MemoryMax=512M` 下跑 broker，读 `VmHWM` ≤ 80MB
    4. 全量 `ctest` + 脚本全绿后写 `UAT.md`
    5. **verify fallback**：无 `systemd-run` 时改用 `bash -c 'ulimit -v 524288; exec build/nlbroker --dry-run'`，并在 `UAT.md` 标注"该门限需在 A1 档目标硬件复测"
  </action>
  <verify>bash scripts/verify_all.sh && cd build && ctest --output-on-failure</verify>
  <done>10 条 AC 全绿；ctest 全绿；UAT.md 贴出每条 AC 的实跑输出</done>
  <depends_on>T08,T09,T10,T11,T12,T13</depends_on>
</task>
```

---

## 边界补充（Task 门后追加）

为使每个任务可独立 verify，`src/nlbroker/*` 与 `tests/unit/*` 的新增任务**允许修改** `CMakeLists.txt`（接入 target）
与 `tests/CMakeLists.txt`（注册测试）。除此之外仍严格按各任务 `write_files` 执行。

## AC 覆盖矩阵

| AC | 覆盖任务 | 验证方式 |
|---|---|---|
| AC-1 discover 分页与命名空间 | T03 + T08 | `verify_w2.sh` |
| AC-2 describe 缺省从严 | T03 + T04 + T08 | `ctest -R capability_registry,verdict` |
| AC-3 invoke 只裁决不执行 | T04 + T08 | 响应 `plan.executed=false` + 审计无 commit |
| AC-4 confirm→ticket→commit | T05 + T08 | 5 分支脚本 |
| AC-5 模型自报 effect 无效 | T04 + T14 | 对抗脚本 |
| AC-6 MCP 门面 L2 上限 | T12 | `ctest -R facade_mcp` |
| AC-7 DBus 子进程桥 | T09 | 调用 + `ldd | grep -c dbus`=0 |
| AC-8 http only / https 拒绝 | T13 | `ctest -R adapter_http` |
| AC-9 审计可回放 | T07 + T08 | `ctest -R audit` + jq 字段 |
| AC-10 端侧资源 | T14 | cgroup 512M + `VmHWM` |

## 门禁强制条件 → 任务映射

| 条件 | 落在 |
|---|---|
| ticket 上限 1024 + LRU + 限速（G2 安全） | T05 |
| AC-10 用 cgroup + VmHWM，不用 x86 直测（需求门架构师） | T14 |
| AC-3 不用 strace，用 plan 标记 + 审计（需求门第三轮） | T08 |
| AC-5 对抗脚本必须进测试矩阵（需求门安全） | T14 |
| 每个适配器各自独立 verify（G1 PM） | T09 / T11 / T13 各自 ctest |
| TASK 必须 2–10 分钟粒度（G2 架构师） | 本文件全部任务 |

---

> `read_files` / `write_files` 是 R7.3 强约束；4-dev 提交前 `git diff --name-only` 比对边界。

---

## 🛡️ Task 门投票记录（2026-10-25，三轮 · 补跑）

> 背景：T01 完成后自查发现 3-task 的出口门遗漏，现补跑。结论对 T01 的粒度校准仍然有效。

**第 1 轮 · 行业对照**：MCP 规范的演进经验是"适配器模式 + 每个适配器独立契约测试"；Rust/Go 生态的同类项目（Napaxi、PhoneBuddySDK）也都是"核心引擎 + 各后端适配器"。当前拆分（W1 核心 → W2 端到端 → W3/W4/W5 各自适配器）与该模式一致。

**第 2 轮 · 粒度与可执行性核查**：

- T01 实际耗时**超过 10 分钟**（编解码 + 7 个测试 + CMake 接入）——粒度校准：后续适配器任务预计同样接近上限，**不允许再合并两个适配器到一个任务**。
- **T02 的 verify 依赖 `nc`**，最小系统（OpenWrt 默认镜像）不一定有。
- **T14 的 verify 依赖 `systemd-run`**，容器/无 systemd 环境不可用。

**第 3 轮 · 交叉验证**：

- 安全 → 架构：`T04`（裁决）与 `T05`（ticket）顺序正确——先有 verdict 才有 ticket；但要注意 fail-closed 不能在 T04 未就绪时被绕过，已由 T08 集成负责。
- 架构 → PM：14 个任务对 10 条 AC 的覆盖矩阵已逐条对齐，无遗漏。
- PM → 架构：T08 同时承担"集成"与"AC 脚本"，失败时会阻塞 W3–W5；接受该风险（因为 W3–W5 本就依赖 T08 的 dispatch）。

```
🟫 高级产品经理:  ✅ 覆盖矩阵逐条对齐 AC，无遗漏
🟦 资深用户评测员: ✅ 每个任务都有用户可感知的验证点（能跑、能拒、能回放）
🟩 架构师:        ❌ 反对验证命令的可移植性：T02 依赖 nc、T14 依赖 systemd-run。
                     条件：两处 verify 必须给出 fallback（python3 socket / ulimit -v 或无 root 方案）
🔴 安全审计师:    ✅ T04→T05 顺序正确；T14 的对抗用例与资源门限都在

结果: 3/4 → 多数通过 ✅（记录架构师反对意见，条件：补 verify fallback）
```

### 补跑后立即执行的条件（已在本文件落地）

1. **T02 verify 增加 fallback**：`nc` 不存在时用 `python3 -c` 的 socket 客户端（见 T02 action 第 7 条）
2. **T14 verify 增加 fallback**：无 `systemd-run` 时改用 `ulimit -v` 或标注"需目标档硬件复测"
3. **粒度锁定**：T06/T09/T11/T13 每个只做一个适配器，不得合并
