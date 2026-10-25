#!/usr/bin/env bash
# W2 端到端验收：AC-1 / AC-2 / AC-3 / AC-4 / AC-9
set -u
ROOT=$(cd "$(dirname "$0")/.." && pwd)
BUILD="$ROOT/build"
SOCK=/tmp/nlbroker-w2.sock
REG=/tmp/nlbroker-w2-reg.jsonl
STATE=$(mktemp -d)
export XDG_STATE_HOME="$STATE"
export NL_STATE_DIR="$STATE"

cat > "$REG" <<JSON
{"format_version":2,"scanner":"w2"}
{"id":"cli:mkdir","name":"mkdir","path":"/bin/mkdir","perm":"L2","effect":"write","reversible":true,"source":"scan","trusted":true,"namespace":"cli"}
{"id":"cli:echo","name":"echo","path":"/bin/echo","perm":"L1","effect":"read","reversible":true,"source":"scan","trusted":true,"namespace":"cli"}
{"id":"cli:rm","name":"rm","path":"/bin/rm","perm":"L3","effect":"delete","reversible":false,"source":"scan","trusted":true,"namespace":"cli"}
{"id":"cli:mystery","name":"mystery","path":"/bin/true","perm":"L1","source":"scan","trusted":true,"namespace":"cli"}
JSON

rm -f "$SOCK"
"$BUILD/nlbroker" --socket "$SOCK" --registry "$REG" &
PID=$!
cleanup() {
    kill -TERM "$PID" 2>/dev/null
    wait "$PID" 2>/dev/null
    rm -rf "$STATE" /tmp/nlbroker-w2-out /tmp/nlbroker-w2-b
}
trap cleanup EXIT
for _ in $(seq 1 50); do [ -S "$SOCK" ] && break; sleep 0.05; done

python3 - "$SOCK" <<'PY'
import json, os, socket, sys

sock_path = sys.argv[1]
fails = []

def call(req):
    s = socket.socket(socket.AF_UNIX)
    s.connect(sock_path)
    s.sendall((json.dumps(req) + "\n").encode())
    buf = b""
    while b"\n" not in buf:
        c = s.recv(65536)
        if not c:
            break
        buf += c
    s.close()
    return json.loads(buf.decode().strip())

def check(name, cond, detail=""):
    print(("PASS " if cond else "FAIL ") + name + ("" if cond else " :: " + str(detail)))
    if not cond:
        fails.append(name)

# AC-1 discover 分页与命名空间
r = call({"v":1,"id":"1","op":"discover","limit":2})
res = r.get("result", {})
check("AC-1 discover returns limit items", bool(r.get("ok")) and len(res.get("capabilities", [])) == 2, r)
check("AC-1 cursor present", res.get("cursor") is not None, res)
r2 = call({"v":1,"id":"2","op":"discover","prefix":"cli:rm"})
check("AC-1 prefix filter", len(r2.get("result", {}).get("capabilities", [])) == 1, r2)

# AC-2 describe 缺省从严
r = call({"v":1,"id":"3","op":"describe","ids":["cli:mystery"]})
caps = r.get("result", {}).get("capabilities", [])
check("AC-2 effect defaults to delete",
      bool(caps) and caps[0]["effect"] == "delete" and caps[0]["effect_defaulted"] is True, r)

# AC-3 invoke 只裁决不执行
r = call({"v":1,"id":"4","op":"invoke","capability":"cli:rm","args":{"paths":["/etc/passwd"]}})
res = r.get("result", {})
check("AC-3 protected path denied", res.get("verdict") == "deny", r)
check("AC-3 no plan on deny", res.get("plan") is None, res)

# AC-4 confirm -> ticket -> commit
target = "/tmp/nlbroker-w2-out"
r = call({"v":1,"id":"5","op":"invoke","capability":"cli:mkdir","args":{"paths":[target]}})
res = r.get("result", {})
tok = res.get("ticket")
check("AC-4 confirm with ticket", res.get("verdict") == "confirm" and bool(tok), res)
check("AC-4 plan marked not executed", res.get("plan", {}).get("executed") is False, res)
check("AC-4 no side effect before commit", not os.path.isdir(target), target)

r = call({"v":1,"id":"6","op":"commit","ticket":tok,"capability":"cli:mkdir","args":{"paths":[target]}})
check("AC-4 commit succeeds", bool(r.get("ok")) and r["result"].get("status") == "ok", r)
check("AC-4 side effect after commit", os.path.isdir(target), target)

r = call({"v":1,"id":"7","op":"commit","ticket":tok,"capability":"cli:mkdir","args":{"paths":[target]}})
check("AC-4 replay rejected", (not r.get("ok")) and r.get("error") == "E_TICKET_MISMATCH", r)

r = call({"v":1,"id":"8","op":"invoke","capability":"cli:mkdir","args":{"paths":["/tmp/nlbroker-w2-b"]}})
tok2 = r.get("result", {}).get("ticket")
r = call({"v":1,"id":"9","op":"commit","ticket":tok2,"capability":"cli:mkdir",
          "args":{"paths":["/tmp/nlbroker-w2-CONTAMINATED"]}})
check("AC-4 tampered args rejected", (not r.get("ok")) and r.get("error") == "E_TICKET_MISMATCH", r)

# AC-9 审计
r = call({"v":1,"id":"10","op":"audit","since":0,"limit":50})
recs = r.get("result", {}).get("records", [])
check("AC-9 audit has records", len(recs) >= 4, len(recs))
need = ("audit_id", "verdict", "capability_id", "args_hash")
check("AC-9 audit fields present", bool(recs) and all(k in recs[0] for k in need), recs[:1])
check("AC-9 audit has no plaintext args", "/etc/passwd" not in json.dumps(recs), "plaintext leak")

sys.exit(1 if fails else 0)
PY
rc=$?
echo "---- verify_w2 rc=$rc"
exit $rc
