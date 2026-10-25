#!/usr/bin/env python3
"""
verify_paraphrase.py — 语义簇覆盖测试（"是不是只有我挑的那几句能用"）

为什么要这个东西：
    之前的验证是「我想得到几种说法 → 试几句」，那叫抽样，不叫覆盖。
    抽样永远会漏掉真实用户第一句话就说的那个说法（例如「当前路径」）。

这里的判据是**改写不变性**：同一个意思的一批说法，必须落到同一个命令。
一个簇里有一条过不了，就是词典漏了这个说法 —— 不解释成"用户说法奇怪"。

用法:
    python3 scripts/verify_paraphrase.py [nlsh 路径] [--no-model] [-v]
    默认 nlsh 路径: build/nlsh（未安装时用源码构建产物）

退出码: 0 全过 / 1 有失败
"""

import json
import os
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
DATASET = os.path.join(ROOT, "tests", "paraphrase", "nl-paraphrase-zh.json")


def resolve_nlsh(argv):
    for a in argv:
        if not a.startswith("-"):
            return a
    for cand in (os.path.join(ROOT, "build", "nlsh"),
                 os.path.expanduser("~/.local/bin/nlsh")):
        if os.path.exists(cand):
            return cand
    sys.exit("找不到 nlsh 可执行文件")


def run_one(nlsh, utterance, no_model=True, timeout=25):
    """返回 (是否解析出命令, 命令首词basename, 原始输出)"""
    cmd = [nlsh]
    if no_model:
        cmd.append("--no-model")
    cmd += ["--dry-run", "-c", utterance]
    try:
        p = subprocess.run(cmd, capture_output=True, text=True, timeout=timeout)
    except subprocess.TimeoutExpired:
        return False, None, "<超时>"

    out = (p.stdout or "") + (p.stderr or "")

    # 期望形态: "[dry-run] /usr/bin/pwd" 或 "[dry-run] ls -la"
    for line in out.splitlines():
        line = line.strip()
        if line.startswith("[dry-run]"):
            rest = line[len("[dry-run]"):].strip()
            if not rest:
                continue
            return True, os.path.basename(rest.split()[0]), rest
    return False, None, out.strip().splitlines()[0] if out.strip() else "<空输出>"


def main():
    argv = sys.argv[1:]
    nlsh = resolve_nlsh([a for a in argv if not (a.startswith("--dataset"))])
    no_model = "--no-model" in argv or True   # 覆盖测试一律不加载模型：测的是词典/规则
    verbose = "-v" in argv

    dataset = DATASET
    for i, a in enumerate(argv):
        if a == "--dataset" and i + 1 < len(argv):
            dataset = argv[i + 1]

    data = json.load(open(dataset, encoding="utf-8"))
    clusters = data["clusters"]

    total = passed = 0
    failures = []
    print(f"nlsh: {nlsh}")
    print(f"数据集: {dataset}")
    print()
    print(f"{'能力':10} {'说法数':>6} {'通过':>6} {'通过率':>8}")
    print("-" * 36)

    for c in clusters:
        cap, expect, utts = c["cap"], c["expect"], c["utterances"]
        ok = 0
        for u in utts:
            total += 1
            parsed, got, raw = run_one(nlsh, u, no_model)
            if parsed and got == expect:
                ok += 1
                passed += 1
            else:
                failures.append((cap, expect, u, raw if parsed else "无法理解"))

        rate = ok / len(utts) * 100 if utts else 0
        flag = "  " if ok == len(utts) else "!!"
        print(f"{cap:10} {len(utts):>6} {ok:>6} {rate:>7.0f}% {flag}")

    print("-" * 36)
    print(f"{'合计':10} {total:>6} {passed:>6} {passed/total*100:>7.0f}%")
    print()

    if failures:
        print("失败的说法（每条都是词典/规则的真实缺口）:")
        cur = None
        for cap, expect, u, raw in failures:
            if cap != cur:
                print(f"\n  ▍ {cap}（期望 {expect}）")
                cur = cap
            print(f"    ✗ {u:<28} → {raw}")
        print()
        print(f"{len(failures)} 条失败 / {total} 条")
        return 1

    print("全部语义簇通过")
    return 0


if __name__ == "__main__":
    sys.exit(main())
