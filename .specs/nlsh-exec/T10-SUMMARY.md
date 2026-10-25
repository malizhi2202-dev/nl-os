# T10 · 三项"未验证目标"的实测（IME / C 档 / ARM64）

- **状态**: ✅ 结论已出（2 项从"未验证"变"已验证"，1 项仍不可测）

## 10.1 IME / TTY 分支 —— 已验证（用 pty 代替人工敲）

用 `pty.fork()` 开伪终端驱动 `nlsh`，注入 UTF-8 字节（等价于输入法 commit 之后终端收到的字节）：

```
nl> 创建一个叫 pty_dir 的文件夹
⚠️ 需要确认 (L2): mkdir -p pty_dir
执行? [y/N] y
→ mkdir -p pty_dir
✅ 完成（11 ms）
判定：中文被回显 True / 命令正确 True / 目录存在 True
```
退格用例（多打一个"错"字再按一次退格）：`mkdir -p pty_bs`，无残字、无替换字符 `U+FFFD`。

**仍未测**：输入法"正在组合"（preedit）时的光标/渲染——那段发生在输入法/终端侧，不在本仓库代码里；
要验需要在有 fcitx/ibus 的真机上由人操作。

## 10.2 C 档（3B–7B）—— 仍不可测，有硬数字

```
Mem: total 8467MB, available 545MB
磁盘：12G 可用
模型：只有 qwen2.5-0.5b-q4_0.gguf（428MB），无 3B–7B 权重
```
3B q4 约 2GB、运行时需 ~3–4GB，本机可用内存 545MB → 装不下也跑不动。
能测的部分已测：`NL_TIER=4` 时后端选 `decoder`、档位不被误判（`{"tier":4,"backend":"decoder"}`），
即档位映射在做，缺的只是"大模型真机延迟"这一个数据点。

## 10.3 ARM64 —— 已交叉编译并在 qemu 下跑通

用 `apt-get download` + `dpkg -x` 在 `/tmp` 装了一套 aarch64 交叉工具链（无需 root）：
binutils/gcc-11/libc6-dev-arm64-cross/linux-libc-dev-arm64-cross + qemu-user-static。

```
$ cmake -DCMAKE_TOOLCHAIN_FILE=/tmp/toolchain-arm64-local.cmake -DBUILD_STUB_MODEL=ON
$ cmake --build build-arm64      → 成功
$ file build-arm64/nlsh
ELF 64-bit LSB pie executable, ARM aarch64, ...
$ qemu-aarch64-static -L <sysroot> build-arm64/nlsh --no-model --scan
扫描完成：新增 1952 条，总计 1952 条
$ qemu-aarch64-static -L <sysroot> build-arm64/nlsh --eval models/benchmarks/nl-commands-zh-v2.json | tail -1
{"summary":{"total":120,"ok":93,"refused":27,"schema_violations":0,"accuracy":91.4,...}}
```
**结论**：ARM64 上规则层/注册表扫描/评测/审计的数值与 x86_64 **完全一致**。
**边界**：① 这是 **stub 模型** 构建——真机还需交叉编译 llama.cpp（本机 `/usr/local/lib` 只有 x86_64 的 libllama）；
② 执行路径（fork/exec 主机二进制）在 qemu 下无意义，未测；③ arm64-musl 未测
（musl 侧只验证到"x86_64 musl 静态 hello 可编"，本仓库 `libnlc` 是 SHARED、需同时满足 `-specs` 与 `-static` 的组合，需先加 `BUILD_SHARED` 开关，未做）。
