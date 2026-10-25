#!/usr/bin/env bash
# 段 3 验收（AC-11 固化 / AC-12 离线重放 / AC-13 篡改拒绝 / AC-16 审计回放）
set -u
ROOT=$(cd "$(dirname "$0")/.." && pwd)
BUILD="$ROOT/build"
STATE=$(mktemp -d); export XDG_STATE_HOME="$STATE"
WORK=$(mktemp -d)
pass=0; fail=0
chk() { if eval "$2"; then echo "PASS $1"; pass=$((pass+1)); else echo "FAIL $1"; fail=$((fail+1)); fi; }

echo "=== AC-11 固化生成 .tools（含能力引用 + 校验和，幂等）"
printf 'readme-content-for-ac12\n' > "$WORK/README.md"
cat > "$WORK/good.tools" <<'EOF'
列出当前目录
cat README.md
EOF
( cd "$WORK" && "$BUILD/nl-run" --freeze good.tools >/dev/null )
chk "AC-11 头部含 format" "grep -q '^# @format: 2$' '$WORK/good.tools'"
chk "AC-11 头部含 sha256 校验和" "grep -qE '^# @checksum: sha256:[0-9a-f]{64}$' '$WORK/good.tools'"
cp "$WORK/good.tools" "$WORK/good.copy"
( cd "$WORK" && "$BUILD/nl-run" --freeze good.tools >/dev/null )
chk "AC-11 两次固化内容一致" "diff -q '$WORK/good.tools' '$WORK/good.copy' >/dev/null"

echo "=== AC-12 固化后离线无模型重放"
out=$(cd "$WORK" && NL_MODEL_PATH=/nonexistent/model.gguf "$BUILD/nl-run" good.tools 2>&1)
printf '%s\n' "$out" > "$WORK/out12.txt"
head -5 "$WORK/out12.txt"
chk "AC-12 提示使用固化脚本" "grep -q '使用固化脚本' '$WORK/out12.txt'"
chk "AC-12 步骤全部执行成功" "grep -q '全部完成' '$WORK/out12.txt'"

echo "=== AC-13 篡改被拒绝且无副作用"
cat > "$WORK/tamper.tools" <<'EOF'
创建一个叫 ac13_must_not_exist 的文件夹
EOF
( cd "$WORK" && "$BUILD/nl-run" --freeze tamper.tools >/dev/null )
sed -i 's/ac13_must_not_exist/ac13_TAMPERED/' "$WORK/tamper.tools"
( cd "$WORK" && rm -rf ac13_must_not_exist ac13_TAMPERED )
out=$(cd "$WORK" && "$BUILD/nl-run" tamper.tools 2>&1); rc=$?
printf '%s\n' "$out" > "$WORK/out13.txt"
head -3 "$WORK/out13.txt"
chk "AC-13 退出码非 0" "[ $rc -ne 0 ]"
chk "AC-13 报告校验失败" "grep -q '校验和不匹配' '$WORK/out13.txt'"
chk "AC-13 未执行任何步骤" "[ ! -d '$WORK/ac13_must_not_exist' ] && [ ! -d '$WORK/ac13_TAMPERED' ]"

echo "=== AC-16 审计可回放"
"$BUILD/nlsh" --no-model -c 'echo audit-probe' >/dev/null 2>&1 || true
"$BUILD/nlsh" --audit --since 0 --limit 20 > "$WORK/out16.txt" 2>&1
head -c 160 "$WORK/out16.txt"; echo
chk "AC-16 输出 JSON 数组" "head -c 1 '$WORK/out16.txt' | grep -q '\['"
chk "AC-16 含 audit_id 字段" "grep -q 'audit_id' '$WORK/out16.txt'"

echo "---- 段 3 结果：pass=$pass fail=$fail"
rm -rf "$STATE" "$WORK"
[ "$fail" -eq 0 ]
