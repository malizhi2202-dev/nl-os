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
#
# shell 内建（cd）为什么要绕一圈（ADR-020）：
#   bash 把 `command_not_found_handle` 放在**子 shell** 里跑，在里面 `cd` 影响不到你。
#   所以 handler 只让 nlsh 写下 `__NL_CD__ <dir>` 指令，再由 `PROMPT_COMMAND`（主 shell，
#   每次提示符前执行）读指令文件并 `cd -- "$dir"`。全程只传路径，不 eval。

if [ -z "${_NL_HOOK_LOADED:-}" ] && [ -n "${BASH_VERSION:-}" ]; then
_NL_HOOK_LOADED=1

# 每个 shell 一份指令文件：/run/user/<uid>（无则 /tmp/nl-os-hook-<uid>），0700
_NL_HOOK_DIR="${XDG_RUNTIME_DIR:-/tmp/nl-os-hook-$UID}"
_NL_HOOK_DIRECTIVE="$_NL_HOOK_DIR/nl-os-$$.directive"
if [ ! -d "$_NL_HOOK_DIR" ]; then
    mkdir -p "$_NL_HOOK_DIR" 2>/dev/null && chmod 700 "$_NL_HOOK_DIR" 2>/dev/null
fi

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

# 在**主 shell**（PROMPT_COMMAND）里应用指令。只认 `__NL_CD__ ` 前缀，
# 拿到的是一段路径文本，`cd -- "$dir"` 而不是 eval。
_nl_hook_apply_directive() {
    [ -s "$_NL_HOOK_DIRECTIVE" ] || return 0
    local dir
    while IFS= read -r dir; do
        case "$dir" in
            "__NL_CD__ "*)
                dir=${dir#__NL_CD__ }
                [ -d "$dir" ] && cd -- "$dir" ;;
        esac
    done < "$_NL_HOOK_DIRECTIVE"
    : > "$_NL_HOOK_DIRECTIVE"
}

command_not_found_handle() {
    local line="$*"
    if [ "${NL_SHELL_HOOK:-1}" != "0" ] \
       && _nl_hook_is_natural "$line" \
       && command -v nlsh >/dev/null 2>&1; then
        # 注意：这里跑在子 shell，cd 无效——指令文件交给 PROMPT_COMMAND 消费
        NL_SHELL_DIRECTIVE="$_NL_HOOK_DIRECTIVE" nlsh -c "$line"
        return $?          # 原样透传：0 已执行 / 1 没听懂或拒绝 / 3 取消
    fi
    _nl_hook_fallback "$1"
}

# 挂进 PROMPT_COMMAND（保留你原有的，数组/字符串两种形态都处理）
if declare -p PROMPT_COMMAND 2>/dev/null | grep -q '^declare -a'; then
    case " ${PROMPT_COMMAND[*]} " in
        *_nl_hook_apply_directive*) : ;;
        *) PROMPT_COMMAND=(_nl_hook_apply_directive "${PROMPT_COMMAND[@]}") ;;
    esac
else
    case "${PROMPT_COMMAND:-}" in
        *_nl_hook_apply_directive*) : ;;
        "") PROMPT_COMMAND="_nl_hook_apply_directive" ;;
        *)  PROMPT_COMMAND="_nl_hook_apply_directive;${PROMPT_COMMAND}" ;;
    esac
fi

fi
