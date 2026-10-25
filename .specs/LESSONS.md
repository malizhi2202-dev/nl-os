# LESSONS — 跨 change 失败教训库（常驻，不归档）

> 只在**踩过坑并已修**之后追加；每条给可复现的症状与正确做法。

## 2026-10-25 · vendor cJSON 是 API 子集，不是上游

**症状**：编译报 `implicit declaration of function 'cJSON_GetObjectItemCaseSensitive'`；
或运行期 `cJSON_GetArraySize(obj)` 对**对象**恒返回 0，导致递归遍历永远进不去。

**根因**：`third_party/cJSON/` 是自实现的兼容子集（25 个符号）：
- 没有 `cJSON_GetObjectItemCaseSensitive`，只能 `cJSON_GetObjectItem`
- 没有 `item->valuestring` 直连的推荐用法，应走 `cJSON_GetStringValue/GetNumberValue`
- **`cJSON_GetArraySize` 对 object 返回 0**（只对 array 有效）

**正确做法**：
1. 写 cJSON 代码前先 `grep '^cJSON' third_party/cJSON/cJSON.h`
2. 需要遍历 object/array 的子节点时，手动走 `node->child` / `it->next`（结构体字段是公开的）

**代价**：`verdict.c` 的路径扫描曾因此完全失效（`/etc/passwd` 未被拦截），
由 T04 的"保护路径必须 deny"测试抓出。**这类漏洞靠代码审查很难发现，必须有断言。**

## 2026-10-25 · `-Werror=format-truncation` 会连撞多轮

**症状**：`snprintf(dst, sizeof(dst), "%s", src)` 被 GCC 判为可能截断，`-Werror` 编译失败。

**正确做法**：变长字符串进定长缓冲，一律写精度说明符（`"%.127s"`），并单独处理"NUL 也要有位置"。
不要用 `-Wno-format-truncation` 关掉——超长文件名/ID 被静默截断是**数据正确性问题**（两个不同能力可能撞成同一个 ID）。

## 2026-10-25 · 测试通过 ≠ 测试有效

**症状**：`capability_scan` 的"不可读目录"用例一直通过，但什么都没测到。

**根因**：用 `mkdir(path, 0700)` 构造"不可读目录"——**属主自己依然可读**（root 更是）。

**正确做法**：不可读用 `0000`，且 `geteuid()==0` 时显式打印 `SKIP`，不要假装通过。
同理：任何"权限/存在性"类断言，先问一句"这个测试在什么条件下会真的失败"。

## 2026-10-25 · 端侧真实下沿是 512MB，不是 4GB

扫地机器人 SoC（全志 Cortex-A7 四核 + 512MB DDR3 + 4GB eMMC）是"能跑 Linux 的最小现实样本"。
0.35B decoder 常驻约 250MB，在这类机器上会 OOM → A1 档只能上 25MB 的 embedding。
**设计任何"端侧常驻内存"预算时，用 512MB 而不是 4GB 兜底。**

## 2026-10-25 · 只在 Release 才报的 `-Werror=unused-result`

**症状**：Debug 构建全绿，第一次 `-DCMAKE_BUILD_TYPE=Release` 构建失败：
`ignoring return value of 'write' declared with attribute 'warn_unused_result'`。

**根因**：Release 默认启用 `_FORTIFY_SOURCE`，`write`/`read` 等被标记 `warn_unused_result`；
`(void)write(...)` 不能消除该属性告警。

**正确做法**：消费返回值（`if (write(...) < 0) { /* ignore */ }`）。
**推广**：任何"只在 Debug 跑"的项目，交付前必须跑一次 Release 构建——否则会集中爆雷。

## 2026-10-25 · 失败响应必须先渲染 error

**症状**：`https` 拒绝返回 `{"ok":false,"result":{"alternatives":[...]}}`——**错误码消失**。

**根因**：`nl_cip_render` 先判 `payload` 非空就输出 `ok:false + result`，而 `error` 只在
"无 payload 的失败"分支里写。于是所有"拒绝但附替代方案"的响应都丢错误码。

**正确做法**：渲染顺序 = **先判 `!ok`**（带/不带 payload 两分支），再判 `ok + payload`。
**推广**：任何"状态 + 附加数据"的序列化，先处理异常态。

## 2026-10-25 · 实现了但没接线 ≈ 没实现

**症状**：ticket 限速 `nl_ticket_rate_ok()` 写好了、也有单测，但**从没有调用点**；
ADR 里承诺的 DoS 缓解实际不生效。

**根因**：单元测试直接调这个函数 → 通过；但端到端路径没接。

**正确做法**：安全类断言必须打在**端到端路径**上（`verify_all.sh` 连发 120 次 invoke，
断言出现 `E_POLICY_DENY`）。单测只能证明"函数对"，不能证明"系统在用"。

## 2026-10-25 · 审计/日志截断要丢最旧的，不是最新的

**症状**：`audit` 查询返回的最老记录，新写入的事件（含刚发生的拒绝）看不到。

**根因**：按行顺序填充到缓冲区，超长时截断尾部。

**正确做法**：环形保留"最近 N 条"，字节不足时**从最旧开始丢弃**。
排障场景要的是"刚刚发生了什么"。

## 2026-10-25 · 规则槽位 `after_key` 会吃进句尾修饰词

**症状**：`查看 README.md 文件` → 目标被抽成 `README.md 文件`；`创建一个叫 demo 的文件夹` → `demo 的文件夹`。
生成命令变成 `cat README.md 文件` / `mkdir -p demo 的文件夹`，**必然失败**。

**根因**：`rule_engine.c` 的 `extract_after_key_long()` 取"关键词之后到句尾"，
既不剥中文修饰后缀（文件/目录/文件夹/的内容），也不剥引号。

**影响**：评测集里 `查看 {x} 的内容` 这类句式直接"无法理解"（规则要求 查看+文件 同时出现），
是规则层 intent 准确率 68.3% 的主要损耗来源之一。

**正确做法（待办）**：对 `after_key` 槽位做后缀剥离 + 引号提取；并把该规则纳入评测集回归。
**不要**在没有回归的情况下改 `extract_after_key_long`——它被大量规则共用。

## 2026-10-25 · 敏感路径清单要按"路径段"匹配，不是按字面

**症状**：敏感清单写 `/.ssh/`，实际路径是相对 `.ssh` 或以 `.ssh` 结尾 → 判不出来，**私钥被快照进磁盘**。

**正确做法**：把清单存成目录名（`.ssh`），匹配时检查 `/name/`、结尾 `/name`、以及整体等于 `name`。
**推广**：任何"路径黑名单"都不要假设调用方传的是绝对路径且带尾斜杠。

## 2026-10-25 · 快照/备份必须存绝对路径

**症状**：快照 manifest 存了相对路径，`--undo` 在另一个 cwd 执行时"成功"但文件没回来。

**正确做法**：落盘前 `realpath` 绝对化；恢复时不再依赖 cwd。
