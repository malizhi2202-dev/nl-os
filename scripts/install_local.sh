#!/usr/bin/env bash
# 无 root 本地安装：把 nlsh / nl-run / nl-perm / libnlc.so 装进 ~/.local
#
#   bash scripts/install_local.sh            # → $HOME/.local
#   bash scripts/install_local.sh /opt/nl    # → 自定义前缀
#
# 装完直接 `nlsh` 就能用（前提：$PREFIX/bin 在 PATH 里，~/.local/bin 在多数发行版
# 的 ~/.bashrc / ~/.profile 里已默认加入）。
set -euo pipefail
ROOT=$(cd "$(dirname "$0")/.." && pwd)
PREFIX="${1:-$HOME/.local}"
BUILD="$ROOT/build-install"

echo "== 配置（Release，无 root，系统级文件跳过）"
cmake -S "$ROOT" -B "$BUILD" \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_INSTALL_PREFIX="$PREFIX" \
    -DNL_INSTALL_SYSTEM_FILES=OFF \
    -DBUILD_TESTING=OFF >/dev/null

echo "== 编译"
cmake --build "$BUILD" -j"$(nproc 2>/dev/null || echo 2)" 2>&1 | tail -3

echo "== 安装到 $PREFIX"
cmake --install "$BUILD" >/dev/null
ls -l "$PREFIX/bin/nlsh" "$PREFIX/lib/libnlc.so"

echo "== 自检"
if command -v nlsh >/dev/null 2>&1; then
    nlsh --no-model --self-test | tail -1
else
    # 新开一个登录 shell 才能看到 ~/.bashrc / ~/.profile 里加的 PATH
    if bash -lc 'command -v nlsh' >/dev/null 2>&1; then
        echo "（当前 shell 的 PATH 还没刷新；新开终端或 bash -lc 里可直接用 nlsh）"
    else
        echo "⚠️ $PREFIX/bin 不在 PATH 里，先执行："
        echo "   export PATH=\"$PREFIX/bin:\$PATH\""
    fi
fi
"$PREFIX/bin/nlsh" --no-model --self-test | tail -1
echo "✅ 完成：直接用 \`nlsh\`（模型会自动从 ~/.local/share/nl-os/*.gguf 找）"
