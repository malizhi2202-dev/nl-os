# T09 · 本地安装：`nlsh` 直接可用（无 root）

- **状态**: ✅ 完成
- **触发**: 用户 `nlsh` 得到 "Command 'nlsh' not found"，而产物在 `build/nlsh`
- **实现**:
  - `scripts/install_local.sh`：Release 构建 → `cmake --install --prefix ~/.local`（`-DNL_INSTALL_SYSTEM_FILES=OFF` 跳过 `/etc`、systemd）
  - `CMakeLists.txt`：`CMAKE_INSTALL_RPATH=$ORIGIN/../lib`（必须在 `add_library/add_executable` **之前**设置，否则目标属性为空——踩过一次）
  - `nl_model_default_path()`：`$NL_MODEL_PATH` → `/usr/share/nl-os` → `$XDG_DATA_HOME/nl-os` → `~/.local/share/nl-os` 自动找第一个 gguf；4 处调用点（nlsh/decide/eval/nl-run）统一
- **verify**:
  ```
  $ bash -lc 'command -v nlsh && nlsh --no-model --self-test | tail -1'
  /home/malizhi/.local/bin/nlsh
  自检完成：usable=23 needs-privilege=1330 missing=537 total=1890
  $ bash -lc 'nlsh --decide "创建一个叫 modeltest 的文件夹"'   # 未设 NL_MODEL_PATH
  {"status":"ok","chosen":"cli:mkdir","method":"choice-llm",...}   ← 模型被自动找到并解码
  ```
- **顺带修的真 bug**: 带 `-DBUILD_STUB_MODEL=ON` 构建时 `nl_model_default_path` 未定义（上一版把它插进了 `#ifdef GGML_USE_CPU` 分支），是 ARM64 交叉构建链接时暴露的
