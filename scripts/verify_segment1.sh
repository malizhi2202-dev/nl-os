#!/usr/bin/env bash
# 段 1 集成验收（nl-capability-runtime AC-1 ~ AC-5）
set -u
ROOT=$(cd "$(dirname "$0")/.." && pwd)
BUILD="$ROOT/build"
STATE=$(mktemp -d)
export XDG_STATE_HOME="$STATE"
REG="$STATE/caps.jsonl"
SMALL="$STATE/small.jsonl"
export NL_MODEL_PATH=/nonexistent/model.gguf

pass=0; fail=0
chk() { # chk <名称> <条件命令>
    if eval "$2"; then echo "PASS $1"; pass=$((pass+1)); else echo "FAIL $1"; fail=$((fail+1)); fi
}

echo "=== AC-1 能力自发现（扫描不写系统）"
mt_before=$(stat -c '%Y:%Z' /usr/bin /etc)
"$BUILD/nlsh" --scan --registry "$REG" | head -1
mt_after=$(stat -c '%Y:%Z' /usr/bin /etc)
chk "AC-1 注册表已生成且版本为 2" "grep -q '\"format_version\":2' '$REG'"
chk "AC-1 系统目录 mtime 未变" "[ '$mt_before' = '$mt_after' ]"
chk "AC-1 清单非空" "[ \$(grep -c '\"id\"' '$REG') -gt 10 ]"

echo "=== AC-2 三档自检"
out=$("$BUILD/nlsh" --self-test --registry "$REG")
echo "$out"
chk "AC-2 输出三档计数" "echo '$out' | grep -qE 'usable=[0-9]+ needs-privilege=[0-9]+ missing=[0-9]+ total=[0-9]+'"
chk "AC-2 total>0" "echo '$out' | grep -oE 'total=[0-9]+' | cut -d= -f2 | awk '{exit !(\$1>0)}'"

echo "=== AC-3 快路径不经模型"
out=$("$BUILD/nlsh" --registry "$REG" --no-model -c 'echo fastpath-ok' 2>/dev/null)
echo "$out"
chk "AC-3 输出命令结果" "echo '$out' | grep -q fastpath-ok"
chk "AC-3 审计标记 fast_path=true" "grep -q '\"fast_path\":true' '$STATE/nl-os/audit.jsonl'"

echo "=== AC-4 能力缺失不猜测"
printf '%s\n' '{"format_version":2,"scanner":"t08"}' \
  '{"id":"cli:cat","name":"cat","path":"/usr/bin/cat","perm":"L1","effect":"read","reversible":true,"source":"scan","trusted":true,"namespace":"cli"}' > "$SMALL"
out=$("$BUILD/nlsh" --registry "$SMALL" --no-model -c '列出当前目录' 2>/dev/null)
echo "$out"
chk "AC-4 明确说没有该能力" "echo '$out' | grep -q '本机没有该能力'"
chk "AC-4 给出候选" "echo '$out' | grep -q '候选：'"
chk "AC-4 不产生可执行命令" "! echo '$out' | grep -qE '^\[list_dir\]'"

echo "=== AC-5 预算拒绝"
out=$("$BUILD/nlsh" --registry "$REG" --no-model -c '每天凌晨三点把日志压缩后上传备份' 2>/dev/null)
echo "$out"
chk "AC-5 返回解析不了" "echo '$out' | grep -q '解析不了'"
chk "AC-5 给出替代步骤" "echo '$out' | grep -q '替代步骤'"

echo "---- 段 1 结果：pass=$pass fail=$fail"
rm -rf "$STATE"
[ "$fail" -eq 0 ]
