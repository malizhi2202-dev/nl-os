#!/usr/bin/env bash
# shell-nl-hook 验收（AC-1~AC-9）：只接管中文 → 真命令零影响 → 安装幂等可卸载
#
# 全程用 PATH 里的假 nlsh（记录 argv）与临时 HOME，不动真实的 ~/.bashrc。
set -u
ROOT=$(cd "$(dirname "$0")/.." && pwd)
HOOK="$ROOT/scripts/nl-hook.bash"
pass=0; fail=0
chk() { if eval "$2"; then echo "PASS $1"; pass=$((pass+1)); else echo "FAIL $1"; fail=$((fail+1)); fi; }

T=$(mktemp -d); trap 'rm -rf "$T"' EXIT
cd "$T" || exit 1   # AC-7 用相对哨兵文件名：含 "/" 的命令名 bash 不会触发 command_not_found_handle
FAKEBIN="$T/bin"; mkdir -p "$FAKEBIN"
LOG="$T/nlsh.log"; : > "$LOG"

# 假 nlsh：把 argv 写进日志（一条调用一行），按 FAKE_NLSH_RC 退出
cat > "$FAKEBIN/nlsh" <<EOF
#!/usr/bin/env bash
printf '%s|' "\$@" >> "$LOG"
echo >> "$LOG"
exit "\${FAKE_NLSH_RC:-0}"
EOF
chmod +x "$FAKEBIN/nlsh"
reset_log() { : > "$LOG"; }
calls() { wc -l < "$LOG" | tr -d ' '; }

export PATH="$FAKEBIN:$PATH"
export XDG_STATE_HOME="$T/state"

echo "=== AC-1 中文未知输入 → 交给 nlsh，退出码透传"
reset_log
bash -c ". '$HOOK'; 创建测试目录" >/dev/null 2>&1
chk "AC-1a 中文被接管（调用 1 次）" "[ $(calls) -eq 1 ]"
chk "AC-1b 整行原样传入" "grep -qF -- '-c|创建测试目录|' '$LOG'"
reset_log
FAKE_NLSH_RC=3 bash -c ". '$HOOK'; 删除所有文件" >/dev/null 2>&1
rc=$?
chk "AC-1c 退出码原样透传（3）" "[ $rc -eq 3 ]"
reset_log
bash -c ". '$HOOK'; 列出 当前 目录" >/dev/null 2>&1
chk "AC-1d 多词中文整行拼回" "grep -qF -- '-c|列出 当前 目录|' '$LOG'"

echo "=== AC-2 英文未知命令不被接管"
reset_log
bash -c ". '$HOOK'; git statsu" >/dev/null 2>&1
chk "AC-2a 未调用 nlsh（git 存在，子命令拼错由 git 自己报，到不了钩子）" "[ $(calls) -eq 0 ]"
reset_log
bash -c ". '$HOOK'; nosuch_command_zzz" >/dev/null 2>&1
chk "AC-2b 未调用 nlsh" "[ $(calls) -eq 0 ]"
out=$(bash -c ". '$HOOK'; nosuch_command_zzz" 2>&1); rc=$?
chk "AC-2c 仍给出系统提示且非 0" "[ $rc -ne 0 ] && printf '%s' \"\$out\" | grep -qiE 'command not found|找不到|未找到|apt|install'"

echo "=== AC-3 真命令零影响"
reset_log
bash -c ". '$HOOK'; ls /tmp >/dev/null; echo ok" >/dev/null 2>&1
chk "AC-3 真命令不触发钩子" "[ $(calls) -eq 0 ]"

echo "=== AC-4 开关"
reset_log
NL_SHELL_HOOK=0 bash -c ". '$HOOK'; 创建测试目录" >/dev/null 2>&1
chk "AC-4a NL_SHELL_HOOK=0 完全关闭" "[ $(calls) -eq 0 ]"
reset_log
NL_SHELL_HOOK_ALL=1 bash -c ". '$HOOK'; nosuch_command_zzz" >/dev/null 2>&1
chk "AC-4b NL_SHELL_HOOK_ALL=1 接管英文" "[ $(calls) -eq 1 ]"

echo "=== AC-5 安装幂等 / 可卸载 / 有备份"
H="$T/home"; mkdir -p "$H"
printf '# 我自己的配置\n' > "$H/.bashrc"
CNF_BEFORE=$(md5sum /etc/bash.bashrc 2>/dev/null | awk '{print $1}')
HOME="$H" bash "$ROOT/scripts/install_local.sh" --hook-only >/dev/null 2>&1
chk "AC-5a 标记块写入一次" "[ $(grep -cxF '# >>> nl-os shell hook >>>' "$H/.bashrc") -eq 1 ]"
chk "AC-5b 首次写入有备份" "[ -f '$H/.bashrc.bak.nl-os' ]"
chk "AC-5c 钩子文件落到 prefix" "[ -f '$H/.local/share/nl-os/nl-hook.bash' ]"
HOME="$H" bash "$ROOT/scripts/install_local.sh" --hook-only >/dev/null 2>&1
chk "AC-5d 重复安装仍只有一段" "[ $(grep -cxF '# >>> nl-os shell hook >>>' "$H/.bashrc") -eq 1 ]"
chk "AC-5e 其它行未被改" "grep -qxF '# 我自己的配置' '$H/.bashrc'"
CNF_AFTER=$(md5sum /etc/bash.bashrc 2>/dev/null | awk '{print $1}')
chk "AC-6 系统 /etc/bash.bashrc 未被改" "[ '$CNF_BEFORE' = '$CNF_AFTER' ]"
HOME="$H" bash "$ROOT/scripts/install_local.sh" --uninstall-hook >/dev/null 2>&1
chk "AC-5f 卸载后标记块消失" "[ $(grep -cxF '# >>> nl-os shell hook >>>' "$H/.bashrc") -eq 0 ]"
chk "AC-5g 卸载后自定义行仍在" "grep -qxF '# 我自己的配置' '$H/.bashrc'"
chk "AC-5h 卸载删掉钩子文件" "[ ! -f '$H/.local/share/nl-os/nl-hook.bash' ]"

echo "=== AC-7 钩子不 eval（危险输入原样交给 nlsh）"
reset_log
SENT="PWNED"; rm -f "$T/$SENT"
bash -c ". '$HOOK'; '创建 a; touch $SENT 的文件夹'" >/dev/null 2>&1
chk "AC-7a 危险内容一字节未执行" "[ ! -f '$T/$SENT' ]"
chk "AC-7b 原样传入（含分号，整行未拆分）" "grep -qF -- 'a; touch' '$LOG' && grep -qF -- '的文件夹|' '$LOG'"

echo "=== AC-8 语法 / 重复 source"
chk "AC-8a hook 语法正确" "bash -n '$HOOK'"
chk "AC-8b install 脚本语法正确" "bash -n '$ROOT/scripts/install_local.sh'"
chk "AC-8c 重复 source 不报错" "bash -c \". '$HOOK'; . '$HOOK'; declare -F command_not_found_handle >/dev/null\""

echo "=== AC-9 真终端（bash -i 走 ~/.bashrc → 钩子 → nlsh）"
H2="$T/home2"; mkdir -p "$H2"; : > "$H2/.bashrc"
HOME="$H2" bash "$ROOT/scripts/install_local.sh" --hook-only >/dev/null 2>&1
reset_log
HOME="$H2" bash -ic '查询磁盘占用情况' >/dev/null 2>&1
chk "AC-9 交互式终端里中文未知命令被接管" "[ $(calls) -eq 1 ]"

echo "=== AC-10 shell 内建 cd：handler 只写指令，PROMPT_COMMAND 在父 shell 应用"
# 假 nlsh 改成：收到 -c 时把 __NL_CD__ /tmp 写进指令通道
cat > "$FAKEBIN/nlsh" <<EOF
#!/usr/bin/env bash
printf '%s|' "\$@" >> "$LOG"
echo >> "$LOG"
[ -n "\${NL_SHELL_DIRECTIVE:-}" ] && printf '__NL_CD__ /tmp\\n' > "\$NL_SHELL_DIRECTIVE"
exit 0
EOF
chmod +x "$FAKEBIN/nlsh"
out=$(cd / && printf '进入 /tmp 目录\npwd\nexit\n' | PATH="$FAKEBIN:$PATH" bash -i 2>&1)
chk "AC-10a 父 shell 真的 cd 到 /tmp" "printf '%s' \"\$out\" | grep -q '^/tmp$'"
chk "AC-10b handler 内 cd 不算数（子 shell），由 PROMPT_COMMAND 补上" "printf '%s' \"\$out\" | grep -q 'nl-os-.*\.directive\|/tmp'"

echo "=== AC-11 PROMPT_COMMAND 不被覆盖 / 不重复挂载"
out=$(printf 'exit\n' | PATH="$FAKEBIN:$PATH" bash -i -c 'true' 2>/dev/null; true)
h3="$T/home3"; mkdir -p "$h3"
{ echo 'PROMPT_COMMAND="echo USER_PROMPT_CMD"'; } > "$h3/.bashrc"
HOME="$h3" bash "$ROOT/scripts/install_local.sh" --hook-only >/dev/null 2>&1
chk "AC-11a 原有 PROMPT_COMMAND 仍在" "grep -q 'USER_PROMPT_CMD' '$h3/.bashrc'"
out=$(cd / && printf 'exit\n' | HOME="$h3" PATH="$FAKEBIN:$PATH" bash -i 2>&1)
chk "AC-11b 用户原有 PROMPT_COMMAND 仍被执行" "printf '%s' \"\$out\" | grep -q USER_PROMPT_CMD"
n=$(printf 'source %s/share/nl-os/nl-hook.bash; source %s/share/nl-os/nl-hook.bash; printf "%%s" "$PROMPT_COMMAND"' \
      "$h3/.local" "$h3/.local" | bash 2>/dev/null | grep -o _nl_hook_apply_directive | wc -l | tr -d ' ')
chk "AC-11c 重复 source 不会重复挂载（1 次）" "[ '$n' -eq 1 ]"

echo "=== AC-12 内建命令仍不 eval（指令通道里塞命令不会被执行）"
SENT2="$T/PWNED2"; rm -f "$SENT2"
cat > "$FAKEBIN/nlsh" <<EOF
#!/usr/bin/env bash
[ -n "\${NL_SHELL_DIRECTIVE:-}" ] && printf '__NL_CD__ /tmp; touch $SENT2\\n' > "\$NL_SHELL_DIRECTIVE"
exit 0
EOF
chmod +x "$FAKEBIN/nlsh"
(cd / && printf '进入某个地方\nexit\n' | PATH="$FAKEBIN:$PATH" bash -i >/dev/null 2>&1)
chk "AC-12 注入内容一字节未执行" "[ ! -f '$SENT2' ]"

echo "=== AC-13 nlsh 侧指令通道（真二进制）"
NLSH="$ROOT/build/nlsh"
if [ -x "$NLSH" ]; then
    rm -f "$T/d1"
    out=$("$NLSH" --no-model -c '进入 /tmp 目录' <<<'') ; rc=$?
    chk "AC-13a 无指令通道时诚实报错（不假装成功）" "[ $rc -ne 0 ] && printf '%s' \"\$out\" | grep -q '需要在终端钩子下运行'"
    rm -f "$T/d1"
    NL_SHELL_DIRECTIVE="$T/d1" "$NLSH" --no-model -c '进入 /tmp 目录' >/dev/null 2>&1; rc=$?
    chk "AC-13b 有通道时写 __NL_CD__ 并 rc=0" "[ $rc -eq 0 ] && grep -qxF '__NL_CD__ /tmp' '$T/d1'"
    rm -f "$T/d2"
    NL_SHELL_DIRECTIVE="$T/d2" "$NLSH" --no-model -c '进入 /no_such_dir_zzz 目录' >/dev/null 2>&1; rc=$?
    chk "AC-13c 目标不是目录 → 拒绝且不写指令" "[ $rc -ne 0 ] && [ ! -s '$T/d2' ]"
    out=$("$NLSH" --no-model --dry-run -c '切换到上级目录' 2>&1)
    chk "AC-13d cd .. 模板可达（死模板已修）" "printf '%s' \"\$out\" | grep -qF '[dry-run] cd ..'"
else
    echo "SKIP AC-13（未构建 build/nlsh）"
fi

echo
echo "=== 合计：$pass 通过 / $fail 失败"
[ "$fail" -eq 0 ] || exit 1
