#!/usr/bin/env bash
# nlsh-exec 验收（AC-1~AC-9）：拒绝优先 → 执行闭环 → 审计可查
set -u
ROOT=$(cd "$(dirname "$0")/.." && pwd)
BUILD="$ROOT/build"
STATE=$(mktemp -d); export XDG_STATE_HOME="$STATE"
WORK=$(mktemp -d)
NLSH="$BUILD/nlsh"
pass=0; fail=0
chk() { if eval "$2"; then echo "PASS $1"; pass=$((pass+1)); else echo "FAIL $1"; fail=$((fail+1)); fi; }
cd "$WORK" || exit 1

echo "=== 准备：扫描本机能力（临时状态目录，不动用户注册表）"
"$NLSH" --no-model --scan >/dev/null 2>&1
chk "准备 注册表非空" "[ -s '$STATE/nl-os/capabilities.jsonl' ]"

echo "=== AC-1 复合语义拒绝（UAT S4，绝不降级成单条破坏性命令）"
out=$("$NLSH" --no-model -c '把 /var/log 下所有 .log 按大小排序压缩后发到远程服务器并清理原文件' 2>&1); rc=$?
printf '%s\n' "$out" > "$WORK/out1.txt"
chk "AC-1 退出码非 0" "[ $rc -ne 0 ]"
chk "AC-1 说明超出能力边界" "grep -q '超出能力边界' '$WORK/out1.txt'"
chk "AC-1 给出维度" "grep -qE '能力不存在|跨能力过多|复合语义' '$WORK/out1.txt'"
chk "AC-1 给出替代步骤" "grep -q '替代步骤' '$WORK/out1.txt'"
chk "AC-1 不含任何可执行删除命令" "! grep -qE '(rm|mv|dd|shred)[[:space:]]' '$WORK/out1.txt'"

echo "=== AC-2 能力缺失：说清原因 + 候选"
out=$("$NLSH" --no-model -c '重启 nginx' 2>&1); rc=$?
printf '%s\n' "$out" > "$WORK/out2.txt"
chk "AC-2 退出码非 0" "[ $rc -ne 0 ]"
chk "AC-2 说明没有该能力/未支持" "grep -qE '没有该能力|未支持' '$WORK/out2.txt'"
chk "AC-2 给出注册表内的候选" "grep -q 'cli:' '$WORK/out2.txt'"
chk "AC-2 不生成命令" "! grep -qE '→|需确认' '$WORK/out2.txt'"

echo "=== AC-7 --dry-run 零副作用"
rm -rf dry__must_not_exist
out=$("$NLSH" --no-model --dry-run -c '创建一个叫 dry__must_not_exist 的文件夹' 2>&1); rc=$?
chk "AC-7 退出码 0" "[ $rc -eq 0 ]"
chk "AC-7 打印 dry-run" "printf '%s' '$out' | grep -q '\\[dry-run\\]'"
chk "AC-7 目录未创建" "[ ! -d dry__must_not_exist ]"

echo "=== AC-3 写操作真的执行"
rm -rf ac3_dir
out=$(NL_ASSUME_YES=1 "$NLSH" --no-model -c '创建一个叫 ac3_dir 的文件夹' 2>&1); rc=$?
printf '%s\n' "$out" > "$WORK/out3.txt"
chk "AC-3 退出码 0" "[ $rc -eq 0 ]"
chk "AC-3 目录已创建" "[ -d ac3_dir ]"
chk "AC-3 展示执行结果" "grep -q '完成' '$WORK/out3.txt'"

echo "=== AC-4 拒绝确认 = 不执行"
rm -rf ac4_dir
out=$(printf 'no\n' | "$NLSH" --no-model -c '创建一个叫 ac4_dir 的文件夹' 2>&1); rc=$?
chk "AC-4 退出码非 0" "[ $rc -ne 0 ]"
chk "AC-4 目录未创建" "[ ! -d ac4_dir ]"
chk "AC-4 输出含取消" "printf '%s' '$out' | grep -q '已取消'"

echo "=== AC-4b 无输入（EOF）= 取消（fail-closed）"
rm -rf ac4b_dir
out=$(printf '' | "$NLSH" --no-model -c '创建一个叫 ac4b_dir 的文件夹' 2>&1); rc=$?
chk "AC-4b 退出码非 0" "[ $rc -ne 0 ]"
chk "AC-4b 目录未创建" "[ ! -d ac4b_dir ]"

echo "=== AC-6 只读操作不问直接执行"
out=$("$NLSH" --no-model -c '列出当前目录' 2>&1); rc=$?
printf '%s\n' "$out" > "$WORK/out6.txt"
chk "AC-6 退出码 0" "[ $rc -eq 0 ]"
chk "AC-6 无确认提示" "! grep -q '需要确认' '$WORK/out6.txt'"
chk "AC-6 有真实列表输出" "grep -q 'ac3_dir' '$WORK/out6.txt'"

echo "=== AC-5 删除留快照且可撤销"
rm -rf ac5_dir && mkdir -p ac5_dir && echo important-ac5 > ac5_dir/f.txt
NL_ASSUME_YES=1 "$NLSH" --no-model -c '删除 ac5_dir' >/dev/null 2>&1
chk "AC-5 已删除" "[ ! -d ac5_dir ]"
"$NLSH" --no-model --undo >/dev/null 2>&1
chk "AC-5 撤销后文件恢复" "[ -f ac5_dir/f.txt ]"
chk "AC-5 内容一致" "grep -q 'important-ac5' ac5_dir/f.txt 2>/dev/null"

echo "=== AC-8 元字符不进 shell（单测覆盖，这里验端到端不落盘）"
rm -rf metachar_should_not_exist
NL_ASSUME_YES=1 "$NLSH" --no-model -c '创建一个叫 metachar_should_not_exist; touch hacked' >/dev/null 2>&1
chk "AC-8 未创建 hacked" "[ ! -f hacked ]"

echo "=== AC-9 审计可追溯（含结果与耗时，无输入明文）"
"$NLSH" --no-model --audit --since 0 --limit 200 > "$WORK/audit.json" 2>&1
chk "AC-9 审计为 JSON 数组" "head -c1 '$WORK/audit.json' | grep -q '\\['"
chk "AC-9 含 result 字段" "grep -q '\"result\":' '$WORK/audit.json'"
chk "AC-9 含 duration_us" "grep -q '\"duration_us\":' '$WORK/audit.json'"
chk "AC-9 含执行成功记录" "grep -q '\"result\":\"ok\"' '$WORK/audit.json'"
chk "AC-9 含取消记录" "grep -q '\"result\":\"cancelled\"' '$WORK/audit.json'"
chk "AC-9 不含输入明文" "! grep -q '创建一个叫 ac3_dir' '$WORK/audit.json'"

echo "=== nl-run 行为不回归（共享执行器 + 同一道预算闸门）"
rm -rf nr_dir && printf '创建一个叫 nr_dir 的文件夹\n' > nr.tools
"$BUILD/nl-run" --freeze nr.tools >/dev/null 2>&1
printf 'yes\n' | "$BUILD/nl-run" nr.tools >/dev/null 2>&1
chk "nl-run 仍能执行固化脚本" "[ -d nr_dir ]"
chk "nl-run 拒绝复合语义" "printf '每天备份并清理原文件\\n' > bad.tools && printf 'yes\\n' | '$BUILD/nl-run' bad.tools >/dev/null 2>&1; [ \$? -ne 0 ]"


echo "=== AC-11 确认策略统一（ADR-019）"
# 11a 回答词表：nl-run 也认 y（曾经 y = 取消）
rm -rf pol_dir && printf '创建一个叫 pol_dir 的文件夹\n' > pol.tools
"$BUILD/nl-run" --freeze pol.tools >/dev/null 2>&1
printf 'y\n' | "$BUILD/nl-run" pol.tools >/dev/null 2>&1
chk "AC-11a nl-run 接受 y" "[ -d pol_dir ]"

# 11b 取消退出码统一为 3
rm -rf pol_dir2 && printf '创建一个叫 pol_dir2 的文件夹\n' > pol2.tools
"$BUILD/nl-run" --freeze pol2.tools >/dev/null 2>&1
printf 'no\n' | "$BUILD/nl-run" pol2.tools >/dev/null 2>&1; rc=$?
chk "AC-11b nl-run 取消退出码 3" "[ $rc -eq 3 ]"
chk "AC-11b 未执行" "[ ! -d pol_dir2 ]"
printf 'no\n' | "$NLSH" --no-model -c '创建一个叫 pol_dir3 的文件夹' >/dev/null 2>&1; rc=$?
chk "AC-11b nlsh 取消退出码 3" "[ $rc -eq 3 ]"
chk "AC-11b nlsh 未执行" "[ ! -d pol_dir3 ]"

# 11c # @perm: allow-l2 只豁免 L2，不再豁免 L3 删除
rm -rf pol_keep && mkdir -p pol_keep && echo keep > pol_keep/f.txt
printf '# @perm: allow-l2\n删除 pol_keep\n' > pol3.tools
"$BUILD/nl-run" --freeze pol3.tools >/dev/null 2>&1
printf 'no\n' | "$BUILD/nl-run" pol3.tools >/dev/null 2>&1
chk "AC-11c L3 删除仍要确认（allow-l2 不豁免）" "[ -d pol_keep ]"
printf 'yes\n' | "$BUILD/nl-run" pol3.tools >/dev/null 2>&1
chk "AC-11c 确认后执行" "[ ! -d pol_keep ]"

# 11d --yes 与 NL_ASSUME_YES 等价
rm -rf pol_yes && "$BUILD/nl-run" --yes --freeze pol4.tools >/dev/null 2>&1 || true
printf '创建一个叫 pol_yes 的文件夹\n' > pol4.tools
"$BUILD/nl-run" --freeze pol4.tools >/dev/null 2>&1
"$BUILD/nl-run" --yes pol4.tools >/dev/null 2>&1
chk "AC-11d nl-run --yes 跳过确认" "[ -d pol_yes ]"
rm -rf pol_yes2 && "$NLSH" --no-model --yes -c '创建一个叫 pol_yes2 的文件夹' >/dev/null 2>&1
chk "AC-11d nlsh --yes 跳过确认" "[ -d pol_yes2 ]"

# 11e 快路径破坏性命令留快照，可撤销
rm -rf fast_keep && mkdir fast_keep && echo precious > fast_keep/v.txt
"$NLSH" --no-model -c 'rm -rf fast_keep' >/dev/null 2>&1
chk "AC-11e 快路径 rm 已执行（传统命令语义）" "[ ! -d fast_keep ]"
"$NLSH" --no-model --undo >/dev/null 2>&1
chk "AC-11e 撤销后恢复" "[ -f fast_keep/v.txt ] && grep -q precious fast_keep/v.txt"

# 11f nl-run 写审计
"$NLSH" --no-model --audit --since 0 --limit 300 > "$WORK/audit2.json" 2>&1
chk "AC-11f nl-run 步骤进审计" "grep -q '\"result\":\"ok\"' '$WORK/audit2.json'"
chk "AC-11f 取消进审计" "grep -q 'user-declined' '$WORK/audit2.json'"

# ── AC-11g 回答词表全量：五个词都算同意；空行 / EOF / 否定词算取消 ──
for w in y Y yes YES 是; do
    rm -rf "pol_w_$w"
    printf '%s\n' "$w" | "$NLSH" --no-model -c "创建一个叫 pol_w_$w 的文件夹" >/dev/null 2>&1
    chk "AC-11g nlsh 接受回答 [$w]" "[ -d pol_w_$w ]"
done
rm -rf pol_nr_zheng && printf '创建一个叫 pol_nr_zheng 的文件夹\n' > polz.tools
"$BUILD/nl-run" --freeze polz.tools >/dev/null 2>&1
printf '是\n' | "$BUILD/nl-run" polz.tools >/dev/null 2>&1
chk "AC-11g nl-run 接受中文「是」" "[ -d pol_nr_zheng ]"
rm -rf pol_blank && printf '\n' | "$NLSH" --no-model -c '创建一个叫 pol_blank 的文件夹' >/dev/null 2>&1; rc=$?
chk "AC-11g 空行=取消（退出码 3）" "[ $rc -eq 3 ]"
chk "AC-11g 空行未执行" "[ ! -d pol_blank ]"
rm -rf pol_eof && "$NLSH" --no-model -c '创建一个叫 pol_eof 的文件夹' </dev/null >/dev/null 2>&1; rc=$?
chk "AC-11g EOF=取消（nlsh 退出码 3）" "[ $rc -eq 3 ]"
chk "AC-11g EOF 未执行" "[ ! -d pol_eof ]"
rm -rf pol_eof2 && printf '创建一个叫 pol_eof2 的文件夹\n' > pole.tools
"$BUILD/nl-run" --freeze pole.tools >/dev/null 2>&1
"$BUILD/nl-run" pole.tools </dev/null >/dev/null 2>&1; rc=$?
chk "AC-11g EOF=取消（nl-run 退出码 3）" "[ $rc -eq 3 ]"
chk "AC-11g EOF 未执行（nl-run）" "[ ! -d pol_eof2 ]"
rm -rf pol_nope && printf 'nope\n' | "$NLSH" --no-model -c '创建一个叫 pol_nope 的文件夹' >/dev/null 2>&1; rc=$?
chk "AC-11g 否定词=取消" "[ $rc -eq 3 ] && [ ! -d pol_nope ]"

# ── AC-11h 触发条件：只读不问（P-1） ──
out=$(printf '' | "$NLSH" --no-model -c '列出所有文件' 2>&1); rc=$?
printf '%s\n' "$out" > "$WORK/ro.txt"
chk "AC-11h 只读命令直接执行（退出码 0）" "[ $rc -eq 0 ]"
chk "AC-11h 只读命令不弹确认" "! grep -q '需要确认' '$WORK/ro.txt'"

# ── AC-11i 绕过开关等价：NL_ASSUME_YES 对两个入口都生效；allow-l2 仍真能豁免 L2 ──
rm -rf pol_env && NL_ASSUME_YES=1 "$NLSH" --no-model -c '创建一个叫 pol_env 的文件夹' </dev/null >/dev/null 2>&1
chk "AC-11i NL_ASSUME_YES 对 nlsh 生效" "[ -d pol_env ]"
rm -rf pol_env2 && printf '创建一个叫 pol_env2 的文件夹\n' > pole2.tools
"$BUILD/nl-run" --freeze pole2.tools >/dev/null 2>&1
NL_ASSUME_YES=1 "$BUILD/nl-run" pole2.tools </dev/null >/dev/null 2>&1
chk "AC-11i NL_ASSUME_YES 对 nl-run 生效" "[ -d pol_env2 ]"
rm -rf pol_l2 && printf '# @perm: allow-l2\n创建一个叫 pol_l2 的文件夹\n' > polL2.tools
"$BUILD/nl-run" --freeze polL2.tools >/dev/null 2>&1
chk "AC-11i 固化后仍保留 @perm 头" "grep -q '^# @perm: allow-l2$' polL2.tools"
"$BUILD/nl-run" polL2.tools </dev/null >/dev/null 2>&1; rc=$?
chk "AC-11i allow-l2 无输入即执行 L2" "[ $rc -eq 0 ] && [ -d pol_l2 ]"

# ── AC-11j 退出码三态：0 成功 / 1 拒绝 / 3 取消（P-4） ──
rm -rf pol_ok && printf 'y\n' | "$NLSH" --no-model -c '创建一个叫 pol_ok 的文件夹' >/dev/null 2>&1; rc_ok=$?
"$NLSH" --no-model -c '重启 nginx' >/dev/null 2>&1; rc_refuse=$?
printf 'n\n' | "$NLSH" --no-model -c '创建一个叫 pol_never 的文件夹' >/dev/null 2>&1; rc_cancel=$?
chk "AC-11j 成功=0" "[ $rc_ok -eq 0 ]"
chk "AC-11j 拒绝=1（与取消可区分）" "[ $rc_refuse -eq 1 ]"
chk "AC-11j 取消=3" "[ $rc_cancel -eq 3 ]"

# ── AC-11k 快路径：不问，但破坏性命令留快照 + 审计可查（P-5/P-6） ──
snapdir="$XDG_STATE_HOME/nl-os/snapshots"
before=$(ls "$snapdir" 2>/dev/null | wc -l)
"$NLSH" --no-model -c 'ls' >/dev/null 2>&1
after=$(ls "$snapdir" 2>/dev/null | wc -l)
chk "AC-11k 非破坏性快路径不建快照" "[ $before -eq $after ]"
rm -rf pol_fast && mkdir -p pol_fast && echo keep > pol_fast/k.txt
"$NLSH" --no-model -c 'rm -rf pol_fast' >/dev/null 2>&1
after2=$(ls "$snapdir" 2>/dev/null | wc -l)
chk "AC-11k 破坏性快路径建快照" "[ $after2 -gt $after ]"
"$NLSH" --no-model --undo >/dev/null 2>&1
chk "AC-11k 快路径删除可撤销" "[ -f pol_fast/k.txt ] && grep -q keep pol_fast/k.txt"
"$NLSH" --no-model --audit --since 0 --limit 400 > "$WORK/audit3.json" 2>&1
chk "AC-11k 审计标出破坏性快路径" "grep -q '\"reason\":\"fast-path-destructive\"' '$WORK/audit3.json'"
chk "AC-11k 审计区分普通快路径" "grep -q '\"reason\":\"fast-path\"' '$WORK/audit3.json'"

echo
echo "=== 结果：$pass 通过 / $fail 失败"
rm -rf "$STATE" "$WORK"
[ "$fail" -eq 0 ]
