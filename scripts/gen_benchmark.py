#!/usr/bin/env python3
"""把中文指令模板展开成评测集（覆盖 12 个 intent），并与既有 33 条合并。"""
import json, os, sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SRC = os.path.join(ROOT, "models/benchmarks/nl-commands-zh.json")
DST = os.path.join(ROOT, "models/benchmarks/nl-commands-zh-v2.json")

# intent -> 句式（{x}, {y} 为槽位占位）
T = {
 "create_dir": ["创建一个叫 {x} 的文件夹","新建一个叫 {x} 的目录","建一个 {x} 文件夹",
                "帮我创建目录 {x}","在当前目录创建 {x} 文件夹","新建目录 {x}",
                "创建一个 {x} 目录","在 /tmp 下创建 {x} 目录","给我建个文件夹叫 {x}"],
 "create_file": ["创建一个叫 {x} 的文件","新建文件 {x}","建一个空文件 {x}",
                 "帮我创建一个 {x} 文件","在当前目录新建 {x} 文件","创建空文件 {x}",
                 "生成一个叫 {x} 的文件","touch {x}"],
 "list_dir": ["列出当前目录","查看当前目录内容","显示当前目录下的文件","列出 /home 下的内容",
              "看看这个目录有什么","显示目录内容","ls","列出所有文件","当前目录有哪些文件"],
 "view_file": ["查看 {x} 的内容","显示 {x} 文件内容","打开并查看 {x}","读取 {x}","cat {x}",
               "看一下 {x} 里写了什么","显示文件 {x} 的内容","打印 {x} 的内容"],
 "find_files": ["查找 {x} 文件","找到所有 {x}","搜索名为 {x} 的文件","在 /home 下找 {x}",
                "find {x}","找出所有的 {x} 文件","定位 {x} 文件","搜索文件 {x}"],
 "delete": ["删除 {x}","删掉 {x} 文件","把 {x} 删了","移除 {x}","清理 {x} 目录",
            "删除叫 {x} 的文件","删除 /tmp 下的 {x}"],
 "move": ["把 {x} 移动到 {y}","移动 {x} 到 {y}","将 {x} 挪到 {y}","把 {x} 重命名为 {y}",
          "mv {x} {y}","移动到 {y} 目录","把文件 {x} 移到 {y}"],
 "copy": ["复制 {x} 到 {y}","拷贝 {x} 到 {y}","把 {x} 复制一份","cp {x} {y}",
          "复制文件 {x} 到 {y} 目录","帮我复制 {x} 到 {y}"],
 "change_dir": ["进入 {x} 目录","切换到 {x}","到 {x} 目录下","cd {x}","进入 /home",
                "跳到 {x}","切到上级目录"],
 "show_process": ["查看进程","显示所有进程","当前有哪些进程","占用内存最多的进程","ps",
                  "查看系统负载","看看 CPU 占用","列出进程"],
 "search_content": ["在 {x} 里搜索 {y}","搜索包含 {y} 的文件","grep {y}","查找内容 {y}",
                    "在文件中查找 {y}","搜一下 {y} 出现在哪","查找包含 {y} 的行"],
 "run_cmd": ["运行 {x}","执行 {x} 命令","帮我跑一下 {x}","执行 make","运行脚本 {x}",
             "启动 {x} 程序","执行命令 {x}"],
}

CATEGORY = {
 "create_dir":"file_ops","create_file":"file_ops","list_dir":"file_ops","view_file":"file_ops",
 "find_files":"file_ops","delete":"file_ops","move":"file_ops","copy":"file_ops",
 "change_dir":"file_ops","show_process":"process","search_content":"search","run_cmd":"exec",
}
X = ["demo","build","logs","data","report.txt","notes.md","src","backup","tmpdir"]
Y = ["/tmp","/home/user","backup","archive"]

def main():
    old = json.load(open(SRC)) if os.path.exists(SRC) else []
    items = []
    for e in old:
        items.append({"input": e["input"], "intent": e["intent"]})
    n = 0
    for intent, pats in T.items():
        for i, p in enumerate(pats):
            txt = p.format(x=X[i % len(X)], y=Y[i % len(Y)])
            if any(it["input"] == txt for it in items):
                continue
            items.append({"input": txt, "intent": intent})
            n += 1
    out = []
    for i, it in enumerate(items, 1):
        out.append({"id": i, "input": it["input"], "intent": it["intent"],
                    "entities": {}, "difficulty": "easy" if i <= 60 else "medium",
                    "category": CATEGORY.get(it["intent"], "other")})
    json.dump(out, open(DST, "w"), ensure_ascii=False, indent=1)
    print("写入 %s：%d 条（新增 %d，覆盖 %d 个 intent）" % (DST, len(out), n, len(T)))

if __name__ == "__main__":
    main()
