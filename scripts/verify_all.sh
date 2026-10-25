#!/usr/bin/env bash
# T14 全量 AC 验证（AC-1 ~ AC-10）
# 用法：bash scripts/verify_all.sh
# 说明：AC-10 使用 Release 构建（Debug 带 ASan，RSS 不可比）
set -u
ROOT=$(cd "$(dirname "$0")/.." && pwd)
export NL_ROOT="$ROOT"
exec python3 - <<'PY'
import json, os, socket, subprocess, sys, tempfile, time, shutil, signal

ROOT = os.environ["NL_ROOT"]
BUILD = os.path.join(ROOT, "build")
REL = os.path.join(ROOT, "build-rel")
fails = []
def check(name, cond, detail=""):
    print(("PASS " if cond else "FAIL ") + name + ("" if cond else " :: " + str(detail)))
    if not cond: fails.append(name)

state = tempfile.mkdtemp(prefix="nlac-")
os.environ["XDG_STATE_HOME"] = state
http_root = os.path.join(state, "www"); os.makedirs(http_root, exist_ok=True)
with open(os.path.join(http_root, "health"), "w") as f: f.write("ac8-ok")

HTTP_PORT = 18111
reg = os.path.join(state, "reg.jsonl")
sock = os.path.join(state, "broker.sock")
caps = [
 {"id":"cli:echo","name":"echo","path":"/bin/echo","perm":"L1","effect":"read","reversible":True,"source":"scan","trusted":True,"namespace":"cli"},
 {"id":"cli:mkdir","name":"mkdir","path":"/bin/mkdir","perm":"L2","effect":"write","reversible":True,"source":"scan","trusted":True,"namespace":"cli"},
 {"id":"cli:rm","name":"rm","path":"/bin/rm","perm":"L3","effect":"delete","reversible":False,"source":"scan","trusted":True,"namespace":"cli"},
 {"id":"cli:mystery","name":"mystery","path":"/bin/true","perm":"L1","source":"scan","trusted":True,"namespace":"cli"},
 {"id":"http:GET|http://127.0.0.1:%d/health" % HTTP_PORT,"name":"health","namespace":"http","perm":"L1","effect":"read","reversible":True,"source":"manual","trusted":True},
 {"id":"http:GET|https://example.com/x","name":"https","namespace":"http","perm":"L1","effect":"read","reversible":True,"source":"manual","trusted":True},
 {"id":"dbus:org.freedesktop.DBus|ListNames","name":"ListNames","namespace":"dbus","perm":"L2","effect":"network","reversible":True,"source":"bus","trusted":True},
]
with open(reg, "w") as f:
    f.write(json.dumps({"format_version":2,"scanner":"t14"}) + "\n")
    for c in caps: f.write(json.dumps(c) + "\n")

httpd = subprocess.Popen([sys.executable, "-m", "http.server", str(HTTP_PORT), "--directory", http_root],
                         stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
fake = os.path.join(ROOT, "tests/fixtures/fake_mcp_server.py")
broker = subprocess.Popen([os.path.join(BUILD, "nlbroker"), "--socket", sock, "--registry", reg,
                           "--mcp-name", "fake", "--mcp-cmd", sys.executable, "--mcp-arg", fake],
                          stdout=subprocess.DEVNULL, stderr=subprocess.PIPE)
for _ in range(100):
    if os.path.exists(sock): break
    time.sleep(0.05)

def call(req):
    s = socket.socket(socket.AF_UNIX); s.connect(sock)
    s.sendall((json.dumps(req) + "\n").encode())
    buf = b""
    while b"\n" not in buf:
        c = s.recv(65536)
        if not c: break
        buf += c
    s.close(); return json.loads(buf.decode().strip())

try:
    # AC-1
    r = call({"v":1,"id":"1","op":"discover","limit":2})
    res = r.get("result", {})
    check("AC-1 discover limit", r.get("ok") and len(res.get("capabilities", [])) == 2, r)
    check("AC-1 cursor", res.get("cursor") is not None, res)
    r = call({"v":1,"id":"2","op":"discover","prefix":"http:"})
    check("AC-1 prefix filter", len(r.get("result", {}).get("capabilities", [])) == 2, r)

    # AC-2
    r = call({"v":1,"id":"3","op":"describe","ids":["cli:mystery"]})
    c = r.get("result", {}).get("capabilities", [])
    check("AC-2 effect defaults to delete", bool(c) and c[0]["effect"] == "delete" and c[0]["effect_defaulted"] is True, r)

    # AC-3
    r = call({"v":1,"id":"4","op":"invoke","capability":"cli:rm","args":{"paths":["/etc/passwd"]}})
    res = r.get("result", {})
    check("AC-3 deny protected path", res.get("verdict") == "deny", r)
    check("AC-3 no plan on deny", res.get("plan") is None, res)
    audit_mid = call({"v":1,"id":"4b","op":"audit","since":0,"limit":50}).get("result", {}).get("records", [])
    check("AC-3 invoke did not execute (no commit audit)",
          all(rec.get("verdict") != "commit" for rec in audit_mid), audit_mid)

    # AC-4
    target = os.path.join(state, "ac4-out")
    r = call({"v":1,"id":"5","op":"invoke","capability":"cli:mkdir","args":{"paths":[target]}})
    res = r.get("result", {}); tok = res.get("ticket")
    check("AC-4 confirm + ticket", res.get("verdict") == "confirm" and bool(tok), res)
    check("AC-4 plan not executed", res.get("plan", {}).get("executed") is False, res)
    check("AC-4 no side effect before commit", not os.path.isdir(target), target)
    r = call({"v":1,"id":"6","op":"commit","ticket":tok,"capability":"cli:mkdir","args":{"paths":[target]}})
    check("AC-4 commit ok", bool(r.get("ok")) and r["result"].get("status") == "ok", r)
    check("AC-4 side effect after commit", os.path.isdir(target), target)
    r = call({"v":1,"id":"7","op":"commit","ticket":tok,"capability":"cli:mkdir","args":{"paths":[target]}})
    check("AC-4 replay rejected", (not r.get("ok")) and r.get("error") == "E_TICKET_MISMATCH", r)
    r = call({"v":1,"id":"8","op":"invoke","capability":"cli:mkdir","args":{"paths":[target+"/b"]}})
    tok2 = r.get("result", {}).get("ticket")
    r = call({"v":1,"id":"9","op":"commit","ticket":tok2,"capability":"cli:mkdir","args":{"paths":[target+"/EVIL"]}})
    check("AC-4 tampered args rejected", (not r.get("ok")) and r.get("error") == "E_TICKET_MISMATCH", r)
    r = call({"v":1,"id":"9b","op":"commit","ticket":"deadbeef","capability":"cli:mkdir","args":{"paths":[target]}})
    check("AC-4 forged ticket rejected", (not r.get("ok")) and r.get("error") == "E_TICKET_INVALID", r)

    # AC-5 对抗：模型自报 effect=read
    r = call({"v":1,"id":"10","op":"invoke","capability":"cli:rm",
              "args":{"paths":["/etc/passwd"],"effect":"read"}})
    res = r.get("result", {})
    check("AC-5 fake effect still denied", res.get("verdict") == "deny", r)
    check("AC-5 fake effect flagged", res.get("untrusted_effect_attempt") is True, res)

    # AC-7 dbus（有总线才测）
    if os.path.exists("/run/dbus/system_bus_socket"):
        r = call({"v":1,"id":"11","op":"invoke",
                  "capability":"dbus:org.freedesktop.DBus|ListNames","args":{}})
        res = r.get("result", {})
        if r.get("ok"):
            r = call({"v":1,"id":"12","op":"commit","ticket":res.get("ticket"),
                      "capability":"dbus:org.freedesktop.DBus|ListNames","args":{}})
            check("AC-7 dbus call works", bool(r.get("ok")) and "org.freedesktop.DBus" in r["result"].get("output",""), r)
        else:
            check("AC-7 dbus call works", False, r)
    else:
        print("SKIP AC-7 (no system bus)")

    # AC-8 http
    r = call({"v":1,"id":"13","op":"invoke","capability":"http:GET|https://example.com/x","args":{}})
    check("AC-8 https unsupported", (not r.get("ok")) and r.get("error") == "E_SCHEME_UNSUPPORTED", r)
    check("AC-8 suggests cli:curl", "cli:curl" in json.dumps(r.get("result", {})), r)
    r = call({"v":1,"id":"14","op":"invoke",
              "capability":"http:GET|http://127.0.0.1:%d/health" % HTTP_PORT,"args":{}})
    res = r.get("result", {})
    check("AC-8 http invoke allowed", res.get("verdict") == "allow", r)
    r = call({"v":1,"id":"15","op":"commit","ticket":res.get("ticket"),
              "capability":"http:GET|http://127.0.0.1:%d/health" % HTTP_PORT,"args":{}})
    check("AC-8 http body", bool(r.get("ok")) and "ac8-ok" in r["result"].get("output",""), r)

    # AC-6 MCP 门面（独立进程）
    p = subprocess.run([os.path.join(BUILD, "nlbroker"), "--facade-mcp", "--registry", reg],
                       input="\n".join([
                           json.dumps({"jsonrpc":"2.0","id":1,"method":"tools/list"}),
                           json.dumps({"jsonrpc":"2.0","id":2,"method":"tools/call",
                                       "params":{"name":"nl_cli_rm","arguments":{"paths":["/tmp/x"]}}}),
                           json.dumps({"jsonrpc":"2.0","id":3,"method":"tools/call",
                                       "params":{"name":"nl_cli_echo","arguments":{"args":["facade-ok"]}}})]) + "\n",
                       capture_output=True, text=True, timeout=20)
    lines = [json.loads(l) for l in p.stdout.strip().split("\n") if l.strip()]
    listed = json.dumps(lines[0].get("result", {}))
    check("AC-6 facade hides L3+", "nl_cli_rm" not in listed, listed)
    check("AC-6 facade denies L3 call", "error" in lines[1], lines[1])
    check("AC-6 facade executes L1", "facade-ok" in json.dumps(lines[2]), lines[2])

    # AC-9 审计
    recs = call({"v":1,"id":"16","op":"audit","since":0,"limit":50}).get("result", {}).get("records", [])
    need = ("audit_id","verdict","capability_id","args_hash")
    check("AC-9 audit fields", bool(recs) and all(k in recs[0] for k in need), recs[:1])
    check("AC-9 no plaintext", "/etc/passwd" not in json.dumps(recs), "leak")

    # AC-10 资源（Release 构建 + 512MB 地址空间限制）
    subprocess.run(["cmake","-S",ROOT,"-B",REL,"-DCMAKE_BUILD_TYPE=Release","-DBUILD_TESTING=OFF"],
                   check=True, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    subprocess.run(["make","-C",REL,"-j4","nlbroker"], check=True,
                   stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    sock2 = os.path.join(state, "broker-rel.sock")
    mem = subprocess.Popen(["bash","-c",
        "ulimit -v 524288; exec %s --socket %s --registry %s" % (os.path.join(REL,"nlbroker"), sock2, reg)],
        stdout=subprocess.DEVNULL, stderr=subprocess.PIPE)
    hwm = None
    for _ in range(200):
        if os.path.exists(sock2): break
        if mem.poll() is not None: break
        time.sleep(0.02)
    try:
        with open("/proc/%d/status" % mem.pid) as f:
            for line in f:
                if line.startswith("VmHWM:"):
                    hwm = int(line.split()[1]) // 1024
        check("AC-10 VmHWM <= 80MB", hwm is not None and hwm <= 80, "%s MB" % hwm)
        check("AC-10 runs under 512MB limit", mem.poll() is None, "exited early")
    finally:
        mem.send_signal(signal.SIGTERM)
        try: mem.wait(timeout=5)
        except Exception: mem.kill()
finally:
    broker.send_signal(signal.SIGTERM)
    try: broker.wait(timeout=5)
    except Exception: broker.kill()
    httpd.terminate()
    shutil.rmtree(state, ignore_errors=True)

print("\n==== 结果：%d 项失败 ====" % len(fails))
for f in fails: print("FAILED:", f)
sys.exit(1 if fails else 0)
PY
