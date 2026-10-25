# UAT: 统一能力调用协议（CIP v1）+ 本地 broker

- **Change ID**: `capability-protocol`
- **验收依据**: `REQUIREMENT.md` 的 **10 条 AC**（不是演示脚本）
- **环境**: Linux x86_64 / glibc 2.35 / GCC 11 / `busctl` 可用 / system bus 存在
- **验收时间**: 2026-10-25

---

## 1. 总体验收结果

| AC | 验收方式 | 结果 |
|---|---|---|
| AC-1 discover 分页与命名空间 | `verify_all.sh`（limit/cursor/prefix） | ✅ |
| AC-2 describe 缺省从严 | `verify_all.sh` + `capability_registry_test` | ✅ |
| AC-3 invoke 只裁决不执行 | `verify_all.sh`（plan 空 + 审计无 commit） | ✅ |
| AC-4 confirm→ticket→commit | `verify_all.sh`（6 分支）+ `ticket_test` 8 例 | ✅ |
| AC-5 模型自报 effect 无效 | `verify_all.sh` 对抗用例 + `verdict_test` | ✅ |
| AC-6 MCP 门面 L2 上限 | `verify_all.sh`（门面子进程）+ `facade_mcp_test` | ✅ |
| AC-7 DBus 子进程桥 | `verify_all.sh`（真实 ListNames） | ✅ |
| AC-8 http only / https 拒绝 | `verify_all.sh` + `adapter_http_test` | ✅ |
| AC-9 审计可回放 | `verify_all.sh` + `audit_test` | ✅ |
| AC-10 端侧资源 | `verify_all.sh`（Release + `ulimit -v 524288`） | ✅ |

**汇总**：`bash scripts/verify_all.sh` → **31 条断言，0 失败**；`ctest` → **20/20**。

---

## 2. 关键验收证据（可复现）

### UAT-1 · 危险操作在 invoke 阶段被拒，且不产生可执行 plan

```
$ invoke {"capability":"cli:rm","args":{"paths":["/etc/passwd"]}}
→ {"ok":true,"result":{"verdict":"deny","reason":"目标路径 '/etc/passwd' 在保护名单中…",
   "plan":null,"alternatives":["cli:ls","cli:stat","cli:cat"]}}
```
审计侧：该 `invoke` 无对应 `commit` 记录（AC-3 双断言，不依赖 strace）。

### UAT-2 · 两阶段执行：未 commit 前无副作用

```
invoke cli:mkdir /tmp/nlbroker-w2-out → confirm + ticket，plan.executed=false
test -d /tmp/nlbroker-w2-out           → 不存在（PASS）
commit（带 ticket + 相同 args）        → status=ok
test -d /tmp/nlbroker-w2-out           → 存在（PASS）
```

### UAT-3 · 篡改 / 重放 / 伪造全部拒绝

| 场景 | 期望 | 实测 |
|---|---|---|
| 同一 ticket 二次 commit | `E_TICKET_MISMATCH` | ✅ |
| commit 时 args 与 invoke 不同 | `E_TICKET_MISMATCH` | ✅ |
| 伪造 ticket | `E_TICKET_INVALID` | ✅ |
| 超过 60s | `E_TICKET_EXPIRED` | ✅（单元测试） |

### UAT-4 · DBus 真实调用

```
$ nlbroker --scan-dbus --registry /tmp/w3-reg.jsonl
dbus 能力发现：新增 20 条
invoke  dbus:org.freedesktop.DBus|/org/freedesktop/DBus|org.freedesktop.DBus|ListNames
commit  → status=ok，输出含真实服务名列表
```

### UAT-5 · HTTP 与 Scheme 约束

```
invoke http:GET|https://example.com/x
→ {"ok":false,"error":"E_SCHEME_UNSUPPORTED","result":{"alternatives":["cli:curl"]}}
invoke/commit http:GET|http://127.0.0.1:18111/health → body "ac8-ok"
```

### UAT-6 · MCP 门面（独立进程，stdio）

```
tools/list            → 含 nl_cli_echo、nl_cli_mkdir；不含 nl_cli_rm（L3）
tools/call nl_cli_rm  → error（拒绝）+ 审计 reason=facade-not-exposed
tools/call nl_cli_echo→ 执行并返回 content
```

---

## 3. 打包/部署检查

| 项 | 命令 | 结果 |
|---|---|---|
| 二进制与库 | `ls build/{nlbroker,nlsh,libnlc.so}` | ✅ 589KB / 135KB / 1.0MB |
| 无新增 C 依赖 | `ldd build/nlbroker \| grep -cE 'libdbus\|libcurl\|libssl'` | **0** ✅ |
| socket 权限 | `stat -c '%a' /tmp/*.sock` | `600` ✅ |
| systemd 单元 | `systemd-analyze verify etc/nlbroker.service` | 语法可解析；仅报"未安装到 /usr/local/bin"（预期） |
| 命令行可用性 | `nlbroker --help` | 5 个选项齐备 ✅ |

---

## 4. 明确未满足 / 未验证项（不隐藏）

| 项 | 状态 | 说明 |
|---|---|---|
| ARM64 / musl / OpenWrt | ❌ **未验证** | 本机无交叉编译链；ADR-015 已把 A1/B/C 承诺降级为"未验证的设计目标" |
| 接入真实 Claude Code / Cursor | ❌ 未验证 | 门面用 fake MCP 客户端验证；未装真实客户端 |
| 多 MCP server | ❌ 未实现 | v1 仅单 server |
| POST body 透传 | ❌ 未实现 | v1 只 GET 空 body |
| SAST | ❌ 未跑 | 由 `-Werror` + ASan 补偿 |

## 5. 验收结论

**按需求文档（10 条 AC）验收通过**：`ctest 20/20` + `verify_all.sh` 31 断言 0 失败。
未验证项已逐条登记，且不满足项均已显式标注在发布材料（README/BUILD/ADR-015）中。
