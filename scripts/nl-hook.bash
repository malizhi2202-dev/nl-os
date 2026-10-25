# nl-os shell hook —— 在终端里直接说人话（bash）
#
# 由 `bash scripts/install_local.sh` 写进 ~/.bashrc 的标记块；也可手动 `. <本文件>`。
# 原理：bash 原生钩子 `command_not_found_handle` —— 只有"命令不存在"时才轮到我，
#       `ls` / `git status` 等真命令完全不受影响（不做别名、不抢输入）。
#
# 关闭：      export NL_SHELL_HOOK=0      （完全退化为系统行为）
# 英文也接管：export NL_SHELL_HOOK_ALL=1  （默认关：英文拼错不让模型去"猜"）
#
# 边界：本文件**不 eval、不解析**自然语言，一律原样交给 `nlsh -c`；
#       确认门（ADR-019）/权限/快照/审计/校验都在 nlsh 里，安全边界不变。

if [ -z "${_NL_HOOK_LOADED:-}" ] && [ -n "${BASH_VERSION:-}" ]; then
_NL_HOOK_LOADED=1

# 判定：含非 ASCII 字节（中文等）即视为自然语言；纯 ASCII 只在显式开启时接管。
# `[!\ -~]` 里的空格**必须**转义：写成 `[! -~]` 会让 case 直接语法错误；
# 也不能用 `[![:print:]]`——UTF-8 locale 下中文算 printable，会把中文误判成 ASCII。
_nl_hook_is_natural() {
    [ -n "$1" ] || return 1
    case "$1" in
        *[!\ -~]*) return 0 ;;
    esac
    [ "${NL_SHELL_HOOK_ALL:-0}" = "1" ]
}

# 回退：与 /etc/bash.bashrc 里 Ubuntu 的 command-not-found 等价。
_nl_hook_fallback() {
    if [ -x /usr/lib/command-not-found ]; then
        /usr/lib/command-not-found -- "$1"
    elif [ -x /usr/share/command-not-found/command-not-found ]; then
        /usr/share/command-not-found/command-not-found -- "$1"
    else
        printf '%s: command not found\n' "$1" >&2
        return 127
    fi
}

command_not_found_handle() {
    local line="$*"
    if [ "${NL_SHELL_HOOK:-1}" != "0" ] \
       && _nl_hook_is_natural "$line" \
       && command -v nlsh >/dev/null 2>&1; then
        nlsh -c "$line"
        return $?          # 原样透传：0 已执行 / 1 没听懂或拒绝 / 3 取消
    fi
    _nl_hook_fallback "$1"
}

fi
