#!/usr/bin/env bash
# 无 root 本地安装：把 nlsh / nl-run / nl-perm / libnlc.so 装进 ~/.local，
# 并把「终端里直接说人话」的 bash 钩子写进 ~/.bashrc 的标记块。
#
#   bash scripts/install_local.sh                 # → $HOME/.local（含钩子）
#   bash scripts/install_local.sh /opt/nl         # → 自定义前缀
#   bash scripts/install_local.sh --hook-only     # 只装/更新钩子，不编译（快）
#   bash scripts/install_local.sh --no-hook       # 装程序，不碰 ~/.bashrc
#   bash scripts/install_local.sh --uninstall-hook # 摘掉标记块，其它行一个字节不动
#
# 装完直接 `nlsh` 就能用（前提：$PREFIX/bin 在 PATH 里，~/.local/bin 在多数发行版
# 的 ~/.bashrc / ~/.profile 里已默认加入）。钩子生效需新开一个终端。
set -euo pipefail
ROOT=$(cd "$(dirname "$0")/.." && pwd)
BUILD="$ROOT/build-install"
PREFIX="$HOME/.local"
HOOK=install

for arg in "$@"; do
    case "$arg" in
        --hook-only)      HOOK=install; SKIP_BUILD=1 ;;
        --no-hook)        HOOK=skip ;;
        --uninstall-hook) HOOK=uninstall ;;
        -*)               echo "未知选项：$arg" >&2; exit 2 ;;
        *)                PREFIX="$arg" ;;
    esac
done
SKIP_BUILD="${SKIP_BUILD:-0}"

# ── bash 钩子：~/.bashrc 标记块 ────────────────────────────────
RC="$HOME/.bashrc"
HOOK_SRC="$ROOT/scripts/nl-hook.bash"
HOOK_DST="$PREFIX/share/nl-os/nl-hook.bash"
BEGIN="# >>> nl-os shell hook >>>"
END="# <<< nl-os shell hook <<<"

_hook_strip() {   # 摘掉标记块（含两端标记行），其余内容逐行保留
    [ -f "$RC" ] || return 0
    awk -v b="$BEGIN" -v e="$END" '
        $0 == b {skip=1; next}
        $0 == e {skip=0; next}
        !skip   {print}
    ' "$RC" > "$RC.nl-os.tmp"
    mv "$RC.nl-os.tmp" "$RC"
}

_hook_install() {
    mkdir -p "$(dirname "$HOOK_DST")"
    install -m 644 "$HOOK_SRC" "$HOOK_DST"
    local had_block=0
    grep -qxF "$BEGIN" "$RC" 2>/dev/null && had_block=1
    if [ "$had_block" = 1 ]; then
        _hook_strip                       # 幂等：先摘旧块再写新块
    elif [ -f "$RC" ]; then
        cp "$RC" "$RC.bak.nl-os"          # 首次写入前备份
        echo "   已备份：$RC.bak.nl-os"
    fi
    {
        echo ""
        echo "$BEGIN"
        echo "# 终端里直接说人话；关闭：export NL_SHELL_HOOK=0；卸载：install_local.sh --uninstall-hook"
        echo "[ -f \"$HOOK_DST\" ] && . \"$HOOK_DST\""
        echo "$END"
    } >> "$RC"
    echo "== 钩子已写入 ~/.bashrc（新开终端生效）：$HOOK_DST"
}

_hook_uninstall() {
    if grep -qxF "$BEGIN" "$RC" 2>/dev/null; then
        _hook_strip
        echo "== 已从 ~/.bashrc 摘掉钩子标记块（备份仍在：$RC.bak.nl-os）"
    else
        echo "== ~/.bashrc 里没有钩子标记块，无需卸载"
    fi
    rm -f "$HOOK_DST"
}

case "$HOOK" in
    uninstall) _hook_uninstall; exit 0 ;;
esac

if [ "$SKIP_BUILD" = 0 ]; then
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
fi

[ "$HOOK" = install ] && _hook_install

if [ "$SKIP_BUILD" = 0 ]; then
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
fi
echo "✅ 完成：直接用 \`nlsh\`，或在新终端里直接说中文（钩子已装）"
