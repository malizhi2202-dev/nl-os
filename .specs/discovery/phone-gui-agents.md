# DISCOVERY: 手机端本地 GUI Agent —— 模型、运行层、控制层、框架层

- **类型**: 横向调研（D-discovery），不进入任何 change 阶段
- **日期**: 2026-10-25
- **动机**: 确认"是否有可以操作手机的本地模型 + 方案 + 成熟框架"，并判断与本项目（Linux 端侧 NL-OS）的关系
- **结论先行**: 有，且已到"个人可复现"的成熟度；但它是**另一条产品线**，可复用的只有 embedding 分类器与决策三原语

---

## 1. 模型层（能跑在手机上的 GUI agent）

| 模型 | 规模 | 量化后体积 | 许可 / 来源 |
|---|---|---|---|
| **GUI-Owl 1.5**（Qwen3-VL 微调，mPLUG/阿里） | 2B（+ ~0.4B ViT） | **约 1.5GB**（LM 1.05GB + 视觉塔 445MB，4bit） | 已被第三方完整打包进手机 App（见 §2） |
| **UI-TARS-2B-SFT**（字节） | 2B | ~1.2GB（Q4） | apache-2.0，HF 公开 |
| **AutoGLM-Phone-9B**（智谱） | 9B | ~5GB（Q4） | 已有 Multilingual-GGUF，需 8GB+ 手机 |
| **BlueLM-GUI**（vivo AI Lab） | 未核 | 未核 | 官方实现已开源 |
| **MAI-UI**（技术报告 2512.22047） | 未核 | 未核 | 新发布，标注"real-world centric" |

> 量级结论：**2B 是手机端 GUI agent 的甜点位**，Q4 后约 1.2–1.5GB，8GB 手机可跑。

## 2. 运行层（真的在手机上跑了，有实测数字）

**Opposable**（Show HN，2026-10-21）—— Android App，端侧跑 2B 多模态 agent，**经无障碍服务读屏 + 跨 App 点击/输入**：

| 指标 | 实测值 |
|---|---|
| 模型 | GUI-Owl 1.5（Qwen3-VL 微调）打包成 **llama.cpp** 格式，4bit |
| 体积 | LM 1.05GB + 视觉塔 445MB ≈ **1.5GB** |
| 视觉编码 | 24 层 ViT（≈0.4B），416×960，ONNX → **NPU**；Galaxy S25 NPU fp16 **~236ms**（对比 M2 CPU ~3.9s） |
| 单步耗时（M2 CPU，含优化） | **~7.3s**（编码 3.6s + prefill 2.5s + decode 1.1s） |
| 单屏判断 | 36 屏分级测试 **27–28/36** |
| 端到端成功率 | 未见任务 **4/8、3/8**；给子目标 + 屏上验证后 **6/8** |
| 已知限制 | NPU 只编了 Snapdragon；首次 NPU 图准备 **72s**（之后 0.39s）；部分输入仍需 CPU 编码 |

## 3. 控制层（不需要 root）

| 方案 | 机制 | 备注 |
|---|---|---|
| **无障碍服务（AccessibilityService）** | 读控件树 + 注入点击/文本 | Opposable 用的就是这条；国产 ROM 碎片化严重 |
| **Shizuku** | 借 ADB 权限执行 shell 级操作 | 无需 root，用户手动授权一次 |
| **MCP over Shizuku** | 把 Android 控制暴露成 MCP 工具 | `jav` 项目：非 root 控制运行时 + 原生遥测 + 双层级 AI（**Jev + director**） |
| **无头虚拟屏** | 在隐藏虚拟显示上自动化，不碰真实屏幕（display 0） | `headless-android-sdk`（Shizuku 授权） |
| ADB / uiautomator2 / Appium | 经典自动化 | 需连接电脑或无线调试，非纯端侧 |

## 4. 框架层

| 项目 | 定位 |
|---|---|
| **Mobile-Agent-v3.5**（arXiv 2602.16855） | 多平台通用 GUI agent 框架 |
| **MobileRL**（THUDM） | 移动 GUI agent 的在线强化学习 |
| **AgentPaw** | Android 手机上的 agent |
| **jav** | 非 root Android 控制运行时（MCP）+ 双层决策 |
| **OpenHands open-operator** | 通用 operator，含 UI-TARS 接入 |
| **BlueLM-GUI**（vivo） | 厂商官方开源实现 |
| 数据集/基准 | AITW、AndroidControl、Mobile-Bench、GUI-Odyssey、**AndroidWorld**（有公开榜） |

## 5. 关键判断（对本项目）

**可用性**：完全成立。模型（2B/9B）、运行层（llama.cpp + ONNX/NPU）、控制层（无障碍/Shizuku/MCP）、框架（Mobile-Agent/jav）四层都齐，且已有人做成免费 App 并公开实测。

**但仍然不是同一条产品线**，五项差异不变（执行面 / 模型量级 / 语言 / 权限裁决权 / 失败模式），详见 `@.specs/adr/015-device-tiers.md`。

**可复用的两样**：
1. `embedding` 意图分类后端（手机端可 NDK 嵌入）
2. **决策三原语 Choice/Score/Noul** —— `jav` 用 "Jev + director" 双层级在手机控制上验证了该抽象可行，候选集从"命令"换"UI 动作"即可

**一个重要的反向启发**：GUI agent 解决的是"没有 API 的能力就点屏幕"。Linux 上同样存在无 CLI 的能力（DBus 服务、GUI 应用），而 **DBus 是结构化、可审计、零模型**的等价物——比屏幕点击稳定得多。建议作为段 1 之后的扩展方向（已登记 STATE）。

## 6. 未核实项（不写成结论）

- AndroidWorld 的具体分数排名（榜页内容未取到有效数据）
- BlueLM-GUI / MAI-UI 的参数量与许可细节
- 国产 ROM（MIUI/HyperOS、ColorOS、HarmonyOS）上无障碍自动化的实际成功率
- `jav` / `headless-android-sdk` 的成熟度与维护活跃度（仅见仓库描述）

## 7. 对 ADR-013 的旁证与警示

- 旁证：`jav` 把 Jev 作为决策层用在手机控制上，说明"类型化决策"是可跨执行面复用的抽象
- 警示：同期有《The Rise and Fall of Jev》一类文章（HN 84 分）在讨论 Jev 的热度退潮——**架构判断（有限答案空间 + 代码持有政策）仍然成立，但不要把产品押在 Jev 这个具体供应商上**
