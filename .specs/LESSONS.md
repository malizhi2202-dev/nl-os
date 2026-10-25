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
