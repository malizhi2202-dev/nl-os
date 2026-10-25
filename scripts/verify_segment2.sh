#!/usr/bin/env bash
# 段 2 验收（AC-6 Choice / AC-7 Noul / AC-8 低置信度 / AC-9 降级 / AC-10 约束解码）
#
# 说明：
#   - 有 GGUF 模型（NL_MODEL_PATH 指向可读文件）时测 decoder 档；
#     没有时整段按规则档跑，AC-6/AC-10 仍须成立（AC-9 即"模型缺席不崩"）。
#   - 注册表扫到临时 XDG_STATE_HOME，不动用户真实状态目录。
set -u
ROOT=$(cd "$(dirname "$0")/.." && pwd)
BUILD="$ROOT/build"
BENCH="$ROOT/models/benchmarks/nl-commands-zh-v2.json"
STATE=$(mktemp -d); export XDG_STATE_HOME="$STATE"
WORK=$(mktemp -d)
pass=0; fail=0
chk() { if eval "$2"; then echo "PASS $1"; pass=$((pass+1)); else echo "FAIL $1"; fail=$((fail+1)); fi; }
trap 'rm -rf "$STATE" "$WORK"' EXIT

MODEL="${NL_MODEL_PATH:-$HOME/.local/share/nl-os/qwen2.5-0.5b-q4_0.gguf}"
if [ -r "$MODEL" ]; then
    export NL_MODEL_PATH="$MODEL"
    MODE="decoder"
else
    export NL_MODEL_PATH=/nonexistent/model.gguf
    MODE="rules"
fi
echo "=== 模式：$MODE（模型：$NL_MODEL_PATH）"

echo "=== 准备：扫描能力注册表（不依赖模型）"
"$BUILD/nlsh" --no-model --scan >"$WORK/scan.txt" 2>&1 || true
tail -1 "$WORK/scan.txt"
REG="$STATE/nl-os/capabilities.jsonl"
chk "准备 注册表已生成" "[ -s '$REG' ]"

echo "=== AC-6 Choice 只输出注册表里的能力 ID"
"$BUILD/nlsh" --eval "$BENCH" >"$WORK/eval1.jsonl" 2>"$WORK/eval1.err"; rc=$?
head -2 "$WORK/eval1.jsonl"
tail -1 "$WORK/eval1.jsonl"
chk "AC-6 退出码 0" "[ $rc -eq 0 ]"
chk "AC-6 有 summary 行" "grep -q '\"summary\"' '$WORK/eval1.jsonl'"
chk "AC-6 所有能力 ID 都在注册表内" \
  "python3 -c \"
import json,sys
ids={json.loads(l)['id'] for l in open('$REG') if l.startswith('{\\\"id\\\"')}
bad=[r['input'] for r in (json.loads(l) for l in open('$WORK/eval1.jsonl') if l.startswith('{\\\"input\\\"')) if r['capability_id'] and r['capability_id'] not in ids]
sys.exit(1 if bad else 0)\""
chk "AC-6 含糊输入被拒绝" \
  "python3 -c \"
import json,sys
rows=[json.loads(l) for l in open('$WORK/eval1.jsonl') if l.startswith('{\\\"input\\\"')]
sys.exit(0 if any(r['refused'] and not r['capability_id'] for r in rows) else 1)\""

echo "=== AC-7 Noul 三档（与 L1-L4 一致）"
( cd "$ROOT/build" && ctest -R 'decision|verdict|ticket' --output-on-failure >"$WORK/ctest_noul.txt" 2>&1 )
chk "AC-7 三档单测通过" "grep -q '100% tests passed' '$WORK/ctest_noul.txt'"
"$BUILD/nlsh" --no-model --decide '列出当前目录' >"$WORK/d_ls.txt" 2>/dev/null
"$BUILD/nlsh" --no-model --decide '创建一个叫 ac7 的文件夹' >"$WORK/d_mk.txt" 2>/dev/null
chk "AC-7 L1 能力被选中（ls）" "grep -q 'cli:ls' '$WORK/d_ls.txt'"
chk "AC-7 L2 能力被选中（mkdir）" "grep -q 'cli:mkdir' '$WORK/d_mk.txt'"

echo "=== AC-8 低置信度只列候选、不生成命令"
"$BUILD/nlsh" --no-model --decide '弄一下那个' >"$WORK/amb.txt" 2>/dev/null; rc=$?
cat "$WORK/amb.txt"
chk "AC-8 含糊输入被拒答" "grep -q '\"status\":\"refused\"' '$WORK/amb.txt'"
chk "AC-8 未给出任何能力" "! grep -q '\"chosen\":\"cli:' '$WORK/amb.txt'"
# 与 AC-4/AC-5 一致：拒答即非 0（脚本可据此区分"决定了"和"拒答了"）
chk "AC-8 拒答退出码非 0" "[ $rc -ne 0 ]"
# 高阈值下即使模型给出选择也必须降级为 uncertain（仅 decoder 档可测）
if [ "$MODE" = "decoder" ]; then
    # 单个候选时规则层already决定、模型不解码，闸门无从生效；强制 solo 解码才真正测到阈值
    NL_DECIDE_MIN_CONF=1.0 NL_DECIDE_SOLO_DECODE=1 "$BUILD/nlsh" --decide '创建一个叫 ac8 的文件夹' >"$WORK/conf.txt" 2>/dev/null
    cat "$WORK/conf.txt"
    chk "AC-8 阈值 1.0 时不再执行选择" "grep -qE '\"method\":\"(low-confidence|rule-fallback)\"' '$WORK/conf.txt'"
fi

echo "=== AC-9 决策层不可用时降级"
out=$("$BUILD/nlsh" --eval "$BENCH" 2>&1 <<<""); rc=$?
printf '%s\n' "$out" >"$WORK/ac9.txt"
chk "AC-9 --no-model 语义下退出码 0" "[ $rc -eq 0 ]"
chk "AC-9 无 segfault/超时" "! grep -qi 'segmentation\\|core dumped' '$WORK/ac9.txt'"
"$BUILD/nlsh" --no-model -c 'echo ac9-probe' >"$WORK/ac9b.txt" 2>&1
chk "AC-9 规则层仍可正常执行" "grep -q 'ac9-probe' '$WORK/ac9b.txt'"
NL_TIER=1 "$BUILD/nlsh" --decide '列出当前目录' >"$WORK/a1.txt" 2>/dev/null
cat "$WORK/a1.txt"
chk "AC-9 A1 档不上 decoder" "grep -q '\"backend\":\"embedding\"' '$WORK/a1.txt'"

echo "=== AC-10 约束解码 3 轮输出 100% 合法"
viol=0
for i in 1 2 3; do
    "$BUILD/nlsh" --eval "$BENCH" >"$WORK/round$i.jsonl" 2>/dev/null
    v=$(python3 -c "
import json
print([json.loads(l)['summary']['schema_violations'] for l in open('$WORK/round$i.jsonl') if l.startswith('{\"summary\"')][0])")
    fb=$(python3 -c "
import json
print([json.loads(l)['summary']['fallbacks'] for l in open('$WORK/round$i.jsonl') if l.startswith('{\"summary\"')][0])")
    echo "  第 $i 轮：schema_violations=$v fallbacks=$fb"
    viol=$((viol + v))
done
chk "AC-10 三轮 schema_violations 全为 0" "[ $viol -eq 0 ]"
chk "AC-10 回退次数被单独计数" "grep -q '\"fallbacks\"' '$WORK/round1.jsonl'"

echo
echo "段 2 验收：$pass 通过，$fail 失败"
[ $fail -eq 0 ]
