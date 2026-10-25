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

echo
echo "=== 合计：$pass 通过 / $fail 失败"
[ "$fail" -eq 0 ] || exit 1
