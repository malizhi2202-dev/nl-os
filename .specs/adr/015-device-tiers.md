# ADR-015: 端侧设备档位与模型预算（≤4GB → 32GB 自适应）

- **状态**: accepted
- **日期**: 2026-10-25
- **关联**: `ADR-006`（依赖上限）、`ADR-013`（类型化决策层）、`ADR-012`（注册表元数据）

## Context

"端侧"不是一个规格，而是一个跨度：

- 低端：树莓派 4 / 老旧工控机 / OpenWrt 路由 → **≤4GB RAM、纯 CPU、SD 卡存储**
- 中端：工控机 / NAS / 迷你主机 → 8–16GB、多核 CPU
- 高端：现代迷你主机与 ARM 笔记本 → **32GB、可能带 NPU/核显**

如果只选一个模型策略，必然在一端失效：0.5B decoder 在 4GB 上冷启动要读 400MB（SD 卡约 13 秒）且 decode 只有个位数 tok/s；而 32GB 机器上限制用 embedding 分类器又是浪费。

关键判断（本 ADR 定案）：**能力集与决策接口应当全档位一致，不同的只是"判断后端"。**

## Decision

### 三档设备画像与预算

| 档 | 判定条件 | 判断后端 | 常驻内存上限 | 决策延迟目标（P95） |
|---|---|---|---|---|
| **A · 受限** | RAM ≤ 4GB，无加速器（细分为 A1 128–768MB / A2 1–4GB，见下） | embedding（24–30M，≈25MB）+ C 分类头/原型；decoder **默认关闭**（A2 可选 0.35B） | ≤ 600MB（含模型）/ ≤ 80MB（不含） | ≤ 150ms |
| **B · 中等** | RAM 8–16GB | embedding 粗筛 + **0.5B–1.5B decoder 复核** | ≤ 2GB（含模型） | ≤ 1.5s |
| **C · 充裕** | RAM ≥ 32GB 或存在可用加速器 | 3B–7B decoder，允许 `n_gpu_layers` 卸载 | ≤ 8GB（含模型） | ≤ 1s（有加速器） |

硬性边界：
- **任何档位都不允许依赖 swap 达标**（SD 卡/eMMC 上 swap 既慢又损寿命）
- 任何档位都必须能在**模型完全缺席**时降级到规则层（ADR-013 已定）

### 支持边界（明确的地板与不支持）

**支持的地板**：`ARMv7-A / ARMv8-A + MMU + Linux + ≥128MB RAM`。
低于这条线的不在支持范围内——不是"性能差"，是**装不进去也跑不起来**。

**Tier A 细分为两个子档**（依据真实拆解数据，RAM 差距达 8–30 倍）：

| 子档 | 典型设备 | RAM | 允许的后端 |
|---|---|---|---|
| **A1** | 扫地机器人 SoC（全志 Cortex-A7 四核 + 512MB DDR3 + 4GB eMMC）、OpenWrt 路由、老电视盒子 | 128MB–768MB | **仅 embedding + 规则**；decoder 完全不上（0.35B 常驻约 250MB，512MB 机器会 OOM） |
| **A2** | 树莓派 4、入门工控机 | 1–4GB | embedding + 可选 0.35B decoder |

**手机（Android / iOS）：模型能跑，系统级 NL 操作不支持**

必须把两件事分开，否则容易误读成"手机上连模型都跑不了"：

| 事项 | 结论 |
|---|---|
| 在手机上跑模型 | ✅ **完全可行**，且是端侧最强一档（ARMv9 八核 / 12–16GB / NPU；0.6B Q4 可达数十 tok/s，24–30M embedding 单次前向仅数毫秒）。厂商内置（Gemini Nano / Apple 端侧模型）与 App 内自跑（llama.cpp / MLC 编入 APK）都在做 |
| 在手机上做"系统级 NL 操作" | ❌ 不支持：**问题不在算力，在没有可执行面**。Android 无 FHS（无 `/usr/bin`）、Bionic libc 与 glibc ABI 不兼容、Android 8+ 非 root 下静态可执行被 seccomp 拦截、App 沙箱（独立 uid + SELinux label）拿不到系统能力；"执行一条命令"这个对象本身不存在，可执行面是 Android API、权限模型由系统提供 |

手机上真实的"自然语言操作系统"形态是 **Agent 操作 UI**（截屏 + 视觉模型 + 无障碍服务点击，如 AutoGLM / Mobile-Agent / AppAgent），
与本项目"C + 模板填充 + L1–L4 + 命令执行"的技术栈没有交集，属**另一条产品线**，不进本 change。

**可复用到手机的部分**：仅 `embedding` 意图分类后端（可编为 `.so` 经 NDK 嵌入 App）。
`能力注册表 / L1–L4 / 模板填充 / fallback` 在手机上无对应对象。

> 依据：Termux 官方《Differences from Linux》（FHS 不兼容 / Bionic libc / seccomp 限制 / 单用户沙箱）。

**明确不支持（out of scope）**：MCU / RTOS 设备——如智能空调控制板
（Cortex-M0 @48MHz + 128KB Flash，Wi-Fi 为独立模组）、低端扫地机、绝大多数家电。
它们无 MMU、无 Linux、内存为 KB 级。这类设备的自然语言理解必须发生在**手机 App 或云端**，
本地只做词表映射。**不要把它们计入"端侧"**，否则范围无法收敛。

> 证据：全志 Cortex-A7 + 512MB DDR3L + 4GB eMMC 的扫地机拆解（elecfans，2023）；
> Cortex-M0 48MHz / 128KB Flash 的空调控制板方案（华芯微特，2022）。

### 探测与画像

`nlsh --scan` 在采集能力的同时采集宿主画像，写入注册表元数据行的**新增可选字段**（不升 `format_version`，缺失即按档 A 处理）：

```json
{"format_version":1,"scanned_at":"...","scanner":"nl-os/0.2",
 "has_systemd":false,"mem_total_mb":3900,"cores":4,
 "has_accel":false,"tier":"A"}
```

- `tier` 由 `mem_total_mb` 与 `has_accel` 推导，但**可被配置文件覆盖**（用户可能给 4GB 机器开 zram 或外挂加速器）
- 加速器探测只做存在性判断（`/dev/dri`、`/dev/rknpu`、`nvidia-smi` 是否存在），不做性能假设

### 后端抽象

决策层不再与"某个模型"绑定，而是与"后端"绑定：

```c
typedef struct NlDecideBackend {
    const char *name;                                  /* "rules" | "embedding" | "decoder" */
    int  (*init)(NlDecideBackend *b, const char *cfg);
    int  (*decide)(NlDecideBackend *b, const NlDecideRequest *req, NlDecideResult *out);
    void (*free)(NlDecideBackend *b);
} NlDecideBackend;
```

- 三个实现：`rules`（纯规则，永远可用）、`embedding`（llama.cpp embedding 模式 + C 分类头）、`decoder`（llama.cpp 生成 + GBNF Choice）
- `nlc_api` 按宿主画像与配置选择后端；**同一 `nl_decide()` 接口，上层无感**
- **运行期自降档**：若某后端的 P95 连续超过本档预算，自动降一档并写审计（`reason:"auto-downgrade"`），不得静默变慢

## Consequences

- ✅ 一套能力注册表覆盖 ≤4GB 到 32GB；换机器只是后端不同，不是重做
- ✅ 4GB 档的内存与延迟目标可达（25MB embedding + 常驻进程，无 decoder 冷启动问题）
- ✅ 32GB 档不被人为限制，可吃满硬件（含加速器卸载）
- ✅ 保持 ADR-006：三档后端都用同一个系统 llama.cpp，**零新依赖**
- ❌ 评测必须**分档跑**（同一批中文指令在 A/B/C 三档各出一份准确率与延迟），测试工作量约 3 倍
- ❌ 自降档逻辑本身会引入"同一输入在不同时刻结果不同"的现象，必须靠审计留痕解释
- ❌ embedding 后端需要标注样本/原型（数据工作），新增能力不能只靠提示词

## 被否掉的备选

| 备选 | 否掉原因 |
|---|---|
| 只支持一个 0.5B decoder（原策略） | 4GB 档冷启动 13s、decode 个位数 tok/s；32GB 档浪费硬件 |
| 按档位发布不同二进制 | 维护三份，能力集容易漂移 |
| 用 `size_t` 内存探测代替显式档位配置 | 探测会误判（容器、cgroup 限额），必须允许人工覆盖 |
| 把 embedding 模型也塞进 decoder 后端 | 两种推理模式参数不同（pooling vs 生成），混在一起必然出错 |
