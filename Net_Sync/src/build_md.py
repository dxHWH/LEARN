"""把电子书章节转换成 Markdown（与电子书同源：src/chapters/*.html）
产物：../网络同步全景_面试指南.md（顶部「提问记录」+ 目录 + 全部章节）
规则：
  - 公式保留 LaTeX 源码（$...$ / $$...$$），Typora / VS Code 可直接渲染
  - deriv 推导 → aligned 公式块 + 逐步依据列表
  - 数据宏（bars / plot / gbars / dtab / lanes / win …）用 build.py 同一套数据与算法生成表格；[[路径]] 数字与电子书同源
  - flow 图：优先用 ASCII 字典里的字符图（按「图 x-y」查），否则按“分组框 → 方框 → 箭头”自动转成文字
"""
import html
import json
import pathlib
import re
from html.parser import HTMLParser

import build as B

ROOT = pathlib.Path(__file__).resolve().parent
OUT = ROOT.parent / "网络同步全景_面试指南.md"
BOOK = "《网络同步全景：从 HOK 帧同步到 Unreal 状态同步》"

QA = [
    {"time": "2026-10-13 15:06",
     "q": "阅读 D:\\works\\HOK_Trunk_New\\Docs\\HOK帧同步机制详解.md，结合帧同步、状态同步（特别是 UE）的完整知识脉络，适当发散，生成一版电子书",
     "a": "全书 15 章 + 4 附录：导读（三问、知识地图、事实等级）· Part I 原理（Ch1 两大流派与带宽实测 E1、Ch2 时间 / 网络 / 固定步长 / 对时 E2）· Part II 帧同步（Ch3 三条路线逐帧实测 E3、Ch4 确定性与定点数 D1–D7、Ch5 HOK 三层架构与逻辑表现分离、Ch6 下行冗余 / 900 窗口 / 补发 / Recover E5、Ch7 上行冗余与技能保序 E7、Ch8 网络质量 / hash 校验 / 录像 / CS 模式 E8、Ch9 回滚网络 E9）· Part III 状态同步（Ch10 插值 / 预测和解 / 延迟补偿 E10–E12、Ch11 UE 网络架构与复制管线 E13、Ch12 属性复制与 RPC E14、Ch13 CMC / GAS / Mover / 网络物理）· Part IV（Ch14 选型与案例、Ch15 面试追问 50 题）· 附录 A 协议参数 / B UE API / C 术语 / D 对照原文档勘误 7 条",
     "keys": ["*"]},
]

ASCII = {
"图 0-1": r"""
PART I 原理            PART II 帧同步（HOK 主线）           PART III 状态同步（UE 主线）       PART IV 选型与面试
Ch1 同步问题与两大流派 → Ch3 Lockstep 三条路线               Ch10 状态同步通论 ★               Ch14 选型、混合与案例
Ch2 时间与固定步长 ──┐   Ch4 确定性工程                        Ch11 UE 网络架构                  Ch15 面试追问 50 题
                     │   Ch5 HOK 架构                          Ch12 UE 属性复制与 RPC            附录 A–D
                     │   Ch6 HOK 下行                          Ch13 UE 移动与技能预测 ★ ──────→ Ch14
                     │   Ch7 HOK 上行
                     │   Ch8 质量 · 校验 · 录像
                     │   Ch9 回滚网络与混合 ★ ┄┄ 同一思想 ┄┄→ Ch10
                     └──────────────────────────────────────→ Ch10
★ 汇合点：帧同步的“回滚”= 状态同步的“预测 + 和解”（先按本地猜测推进，权威结果到达后回到分歧点重算）
""",
"图 3-2": r"""
             ① 严格 Lockstep                 ② 定频广播 + 不等人（HOK）★          ③ 回滚 Rollback
推进条件     收齐所有人的第 N 帧输入          收到服务器第 N 帧广播                 缺的输入先预测，立即推进；错了重算
一人卡顿     全员停（木桶效应）               只伤自己                              短：别人轻微跳变；长：仍全员等待
特点         实现最简单，画面永不回跳         操作延迟 = 自己到服务器的往返         延迟最低；需每帧快照 + 重算
代表         星际 · 魔兽 3 · 帝国时代         王者荣耀等现代 MOBA                   GGPO · 街霸 · 罪恶装备
""",
"图 5-1": r"""
┌ 表现层 · C# / Unity ───────────┐ ┌ 逻辑层 · C++ GameCore ─────────┐ ┌ Relay 服务器 ──────────────────┐
│ 输入采集（对象池命令）          │ │ 确定性战斗模拟 ★               │ │ 帧号分配 · 定频广播（15 帧/秒） │
│ 网络通信（Apollo、帧窗口）      │ │ 帧同步器（帧号、推进节奏）      │ │ 命令汇总与去重                  │
│ 表现播放（AGE 执行层）          │ │ 定点数 · 帧随机                 │ │ 历史帧缓存（补发 / Recover）    │
│ 资源与场景                      │ │ AGE 时间轴（决策）              │ │ 策略参数下发                    │
│ 不做：任何战斗判断              │ │ 不做：碰引擎 / 表现 / 收包      │ │ 不做：战斗逻辑（CC 模式）       │
└────────────────────────────────┘ └────────────────────────────────┘ └────────────────────────────────┘
""",
"图 6-1": r"""
发生丢帧（缺 n 帧）
 ├─ n ≤ k        → 冗余广播自动补齐（无需动作）
 ├─ k < n < 900  → 帧补发 CSID_REQUESTFRAPBOOTSINGLE(1036)
 └─ 超窗 / 断线  → Recover 追帧 CSID_RECOVERGAMEFRAP_REQ(1090)
三条路取回的帧都进入 900 帧接收窗口（缓存 · 排序 · 去重 · 定序）
""",
"图 14-1": r"""
                         权威：各客户端 / 无单点权威                  权威：服务器
网上传输入（状态由     │ 帧同步 Lockstep / 回滚 ★                   │ 服务器演算 + 上传输入
输入推出）             │ RTS · 格斗（GGPO）· HOK CC                 │ HOK CS · 守望先锋命令帧
                       │ + 服务器跑确定性逻辑做校验 / 托管          │ + 服务器确定性 → 可录制重放（LoL Chronobreak）
───────────────────────┼────────────────────────────────────────────┼──────────────────────────────────────────
网上传状态             │ 客户端权威 / 主机权威（作弊风险高）        │ 经典状态同步 ★：UE 默认 · Source · DOTA2 · Valorant
                       │ 适合：信任玩家、成本敏感、非竞技           │ + 局部预测回滚：CMC · GAS · 网络物理 · Rocket League
""",
}


def plain(s):
    s = re.sub(r"<(strong|b)>(.*?)</\1>", r"**\2**", s)
    s = re.sub(r"<code>(.*?)</code>", r"`\1`", s)
    s = re.sub(r'<span class="tag [a-z]+">(.*?)</span>', r"〔\1〕", s)
    return html.unescape(re.sub(r"<[^>]+>", "", s))


def md_bars(d):
    items = B.bar_items(d)
    unit = d.get("unit", "")
    nf = d.get("fmt", "g")
    rows = []
    for it in items:
        if str(it[0]).startswith("—"):
            rows.append(f"| **{it[0][1:]}** | |")
        else:
            rows.append(f"| {it[0]} | {it[3] if len(it) > 3 else (format(it[1], nf) + ' ' + unit).strip()} |")
    return (f"**{d.get('title', '')}**\n\n" if d.get("title") else "") + "| 项 | 值 |\n|---|---|\n" + "\n".join(rows)


def md_flow(d, cap):
    key = re.search(r"图 [A-Z]?\d+-\d+", cap)
    art = ASCII.get(key.group(0)) if key else None
    if art:
        return f"```text{art}```"
    B.flow_norm(d)
    boxes = d.get("boxes", [])
    name = lambda b: f"**{b['t']}**" + ("（" + "；".join(x for x in (b.get("s"), b.get("s2")) if x) + "）" if b.get("s") or b.get("s2") else "") + (" ★" if b.get("hl") else "")
    inside = lambda b, r: r["x"] <= b["x"] and b["x"] + b["w"] <= r["x"] + r["w"] + 1 and r["y"] <= b["y"] and b["y"] + b["h"] <= r["y"] + r["h"] + 1
    out, used = [], set()
    for r in d.get("rects", []):
        mem = [b for b in boxes if inside(b, r) and id(b) not in used]
        if not mem:
            continue
        out.append(f"- {r.get('t', '分组')}")
        for b in mem:
            used.add(id(b))
            out.append(f"  - {name(b)}")
    for b in boxes:
        if id(b) not in used:
            out.append(f"- {name(b)}")
    bid = B.flow_boxes(d)
    edges = [f"- {bid[e['from']]['t']} → {bid[e['to']]['t']}" + (f"（{e['label']}）" if e.get("label") else "")
             for e in d.get("arrows", []) if "from" in e and e["from"] != e["to"]]
    if edges:
        out += ["", "流向："] + edges
    texts = [t["t"] for t in d.get("texts", []) if len(t["t"]) > 6]
    if texts:
        out += [""] + [f"> {t}" for t in texts]
    return "\n".join(out)


def md_deriv(title, body):
    title, rows = B.deriv_parse(title, body)
    out = [f"**推导 · {plain(title)}**", ""]
    eqs, notes, n = [], [], 0

    def flush():
        if eqs:
            out.append("$$\n\\begin{aligned}\n" + " \\\\\n".join(eqs) + "\n\\end{aligned}\n$$")
            out.append("")
            out.extend(notes)
            out.append("")
            eqs.clear(); notes.clear()

    for r in rows:
        if r[0] == "c":
            flush()
            out.append(plain(r[1]))
            out.append("")
            continue
        n += 1
        eqs.append(f"{r[1]} &{r[2]} && \\text{{({n})}}")
        notes.append(f"- ({n}) {plain(r[3])}" + ("　**← 结论**" if r[0] == "k" else ""))
    flush()
    return "\n".join(out).rstrip()


class Node:
    def __init__(self, tag, attrs, parent=None):
        self.tag, self.attrs, self.parent, self.kids = tag, dict(attrs), parent, []

    def cls(self):
        return self.attrs.get("class", "").split()


VOID = {"br", "img", "input", "hr", "meta", "link"}


class P(HTMLParser):
    def __init__(self):
        super().__init__(convert_charrefs=True)
        self.root = Node("root", {})
        self.cur = self.root

    def handle_starttag(self, tag, attrs):
        n = Node(tag, attrs, self.cur)
        self.cur.kids.append(n)
        if tag not in VOID:
            self.cur = n

    def handle_endtag(self, tag):
        c = self.cur
        while c is not self.root and c.tag != tag:
            c = c.parent
        if c is not self.root:
            self.cur = c.parent

    def handle_data(self, data):
        self.cur.kids.append(data)


def inline(n, code=False):
    if isinstance(n, str):
        t = re.sub(r"\s+", " ", n)
        return t if code else t.replace("<", "&lt;").replace(">", "&gt;")
    t = "".join(inline(k, code or n.tag == "code") for k in n.kids)
    if n.tag in ("strong", "b"):
        return f"**{t.strip()}**" if t.strip() else ""
    if n.tag == "code":
        return f"`{t}`"
    if n.tag == "em":
        return f"*{t}*"
    if n.tag == "br":
        return "  \n"
    if n.tag == "span" and "tag" in n.cls():
        return f"〔{t.strip()}〕"
    return t


def find(n, pred):
    for k in n.kids:
        if isinstance(k, Node):
            if pred(k):
                return k
            r = find(k, pred)
            if r:
                return r
    return None


def walk(n):
    for k in n.kids:
        yield k
        if isinstance(k, Node):
            yield from walk(k)


def table(n):
    rows, span = [], {}
    for tr in [k for k in walk(n) if isinstance(k, Node) and k.tag == "tr"]:
        cells = [c for c in tr.kids if isinstance(c, Node) and c.tag in ("td", "th")]
        row, col, queue = [], 0, list(cells)
        while queue or col in span:
            if col in span:
                row.append(span[col][0])
                span[col][1] -= 1
                if span[col][1] == 0:
                    del span[col]
                col += 1
                continue
            c = queue.pop(0)
            txt = inline(c).strip().replace("|", "\\|")
            for _ in range(int(c.attrs.get("colspan", 1))):
                row.append(txt)
                if int(c.attrs.get("rowspan", 1)) > 1:
                    span[col] = [txt, int(c.attrs["rowspan"]) - 1]
                col += 1
        rows.append(row)
    if not rows:
        return ""
    w = max(len(r) for r in rows)
    rows = [r + [""] * (w - len(r)) for r in rows]
    return "\n".join(["| " + " | ".join(rows[0]) + " |", "|" + "---|" * w] + ["| " + " | ".join(r) + " |" for r in rows[1:]])


def lst(n, depth=0):
    out, i = [], 0
    for li in [k for k in n.kids if isinstance(k, Node) and k.tag == "li"]:
        i += 1
        txt = "".join(inline(k) for k in li.kids if not (isinstance(k, Node) and k.tag in ("ul", "ol"))).strip()
        out.append("  " * depth + (f"{i}. " if n.tag == "ol" else "- ") + txt)
        for sub in [k for k in li.kids if isinstance(k, Node) and k.tag in ("ul", "ol")]:
            out.append(lst(sub, depth + 1))
    return "\n".join(out)


def quote(s):
    return "\n".join("> " + l if l.strip() else ">" for l in s.strip().split("\n"))


def ch_no(no):
    m = re.match(r"CHAPTER\s+(\d+)", no)
    if m:
        return "导读" if int(m.group(1)) == 0 else f"第 {int(m.group(1))} 章"
    if no.startswith("APPENDIX"):
        return "附录 " + no.split("·")[-1].strip()
    return no


def block(n):
    if isinstance(n, str):
        s = n.strip()
        return (re.sub(r"\s+", " ", s) + "\n\n") if s else ""
    c, tag = n.cls(), n.tag
    if tag in ("h1", "h2", "h3", "h4"):
        return "#" * {"h1": 1, "h2": 2, "h3": 3, "h4": 4}[tag] + " " + inline(n).strip() + "\n\n"
    if tag == "p":
        return inline(n).strip() + "\n\n"
    if tag in ("ul", "ol"):
        return lst(n) + "\n\n"
    if tag == "table" or "tbl" in c:
        return table(n) + "\n\n"
    if "part-head" in c:
        return "---\n\n# " + inline(find(n, lambda k: k.tag == "h1")).strip() + "\n\n> " + inline(find(n, lambda k: k.tag == "p")).strip() + "\n\n"
    if "cover" in c:
        return ""
    if "ch-head" in c:
        no = ch_no(inline(find(n, lambda k: "no" in k.cls())).strip())
        h = inline(find(n, lambda k: k.tag == "h1")).strip()
        lead = find(n, lambda k: "lead" in k.cls())
        head = h if h.startswith(no) else f"{no} · {h}"
        return f"# {head}\n\n*{inline(lead).strip() if lead else ''}*\n\n"
    if "goals" in c:
        return "**本章目标**\n\n" + lst(find(n, lambda k: k.tag == "ul")) + "\n\n"
    if "co" in c or "ans30" in c:
        h = find(n, lambda k: "h" in k.cls() and k.tag == "span")
        title = inline(h).strip() if h else ""
        body = "".join(block(k) for k in n.kids if k is not h)
        return quote(f"**{title}**\n\n{body}") + "\n\n"
    if "recap" in c:
        t = find(n, lambda k: "t" in k.cls())
        return quote(f"**{inline(t).strip()}**\n\n" + "".join(block(k) for k in n.kids if k is not t)) + "\n\n"
    if tag == "details":
        s = find(n, lambda k: k.tag == "summary")
        body = "".join(inline(k) for k in n.kids if k is not s).strip()
        return f"**{inline(s).strip()}**\n\n> 答：{body}\n\n"
    if "cards" in c:
        out = []
        for card in [k for k in n.kids if isinstance(k, Node) and "card" in k.cls()]:
            t = find(card, lambda k: "t" in k.cls())
            p = find(card, lambda k: k.tag == "p")
            out.append(f"- **{inline(t).strip()}**：{inline(p).strip() if p else ''}")
        return "\n".join(out) + "\n\n"
    if "lab" in c:
        return "> 🧪 此处是交互实验（冗余计算器），仅电子书 `Net_Sync.html` 提供；公式见上方推导框。\n\n"
    return "".join(block(k) for k in n.kids)


PH, MT = [], []


def pre(t):
    def ph(s):
        PH.append(s)
        return f"\n§§{len(PH) - 1}§§\n"

    def code(m):
        lang, title, body = m.group(1), m.group(2).strip(), m.group(3).strip("\n")
        return ph(f"*{title}*\n\n```{ {'cpp': 'cpp', 'cs': 'csharp', 'py': 'python', 'bash': 'bash', 'ini': 'ini'}.get(lang, 'text') }\n{body}\n```")
    t = B.CODE_RE.sub(code, t)
    t = B.DERIV_RE.sub(lambda m: ph(md_deriv(m.group(1), m.group(2))), t)

    def mac(m):
        kind, d = m.group(1), json.loads(m.group(2))
        cap = plain(d.get("cap", ""))
        if kind == "bars":
            body = md_bars(d)
        elif kind == "flow":
            body = md_flow(d, cap)
        elif kind in B.BM.MD_MACROS:
            body = B.BM.MD_MACROS[kind](d)
        elif kind in B.NF.MD:
            body = B.NF.MD[kind](d)
        else:
            body = B.F.MD[kind](d)
        body = B.VAL_RE.sub(B.val_sub, body)
        return ph(f"{body}\n\n*{B.VAL_RE.sub(B.val_sub, cap.replace('**', ''))}*" if cap else body)
    t = B.MACRO_RE.sub(mac, t)
    t = B.VAL_RE.sub(B.val_sub, t)
    t = B.DISPLAY_RE.sub(lambda m: ph("$$\n" + m.group(1).strip() + "\n$$"), t)

    def im(m):
        MT.append("$" + m.group(1).strip() + "$")
        return f"⟦{len(MT) - 1}⟧"
    return B.INLINE_RE.sub(im, t)


def unmath(s):
    s = re.sub(r"[ \t]*§§(\d+)§§[ \t]*", lambda m: "\n\n" + PH[int(m.group(1))] + "\n\n", s)
    s = re.sub(r"⟦(\d+)⟧", lambda m: MT[int(m.group(1))], s).replace("\\$", "$")
    return B.VAL_RE.sub(B.val_sub, s)


HEADER = f"""# {BOOK}

> 配套电子书：`Net_Sync.html` / `Net_Sync.pdf`；实测程序：`demos/netsync_demo.cpp` → `figdata.json` / `run_output.txt`（E1 带宽、E2 固定步长插值、E3 三条路线 10 人断流、E5 下行冗余、E7 上行冗余、E8 ping 测量、E9 回滚与输入延迟、E10 快照插值、E11 预测与和解、E12 延迟补偿、E13 复制调度、E14 属性 vs RPC），`demos/determinism_demo.cpp` 用 `/fp:precise`、`/fp:fast /arch:AVX2`、`/Od /fp:strict` 三种选项编译 → `det_*.json` / `det_output.txt`（D1–D7 浮点求和、逐帧 hash、Q16.16 定点、1 ulp 蝴蝶效应、容器顺序、时间累加、定点精度）。
> 本文档与电子书由同一份源文件（`src/chapters/*.html`）生成：图中和正文中的数字全部由构建脚本从 demo 输出读取。事实来源标签：〔HOK 文档〕〔UE 源码 / 文档〕〔公开资料〕〔实测 Ex〕〔推断〕。

## 提问记录

| 时间 | 原始提问（摘要） | 回答对应章节 |
|---|---|---|
{{ROWS}}

## 目录

{{TOC}}

"""


def main():
    B.load_data()
    parts = []
    for f in sorted((ROOT / "chapters").glob("*.html")):
        p = P()
        p.feed(pre(f.read_text(encoding="utf-8")))
        parts.append(re.sub(r"\n{3,}", "\n\n", unmath(block(p.root))))
    body = "".join(parts)
    toc = []
    for m in re.finditer(r"^(#{1,2}) (.+)$", body, re.M):
        lvl, title = len(m.group(1)), m.group(2).strip()
        if lvl == 1 and ("·" in title[:8]):
            toc.append(f"- {title}")
        elif lvl == 1:
            toc.append(f"- **{title}**")
    rows = "\n".join(f"| {r['time']} | {r['q']} | {r['a']} |" for r in QA)
    md = HEADER.replace("{ROWS}", rows).replace("{TOC}", "\n".join(toc)) + body
    left = re.findall(r"<[a-zA-Z/][^>]*>", re.sub(r"`[^`\n]*`", "", re.sub(r"```[\s\S]*?```", "", md)))
    if left:
        print("[warn] 残留标签：", left[:8])
    if "[[" in md:
        print("[warn] 残留 [[ ]] 占位符：", re.findall(r"\[\[[^\]]*\]\]", md)[:5])
    OUT.write_text(md, encoding="utf-8")
    print(f"OK -> {OUT.name} ({OUT.stat().st_size / 1024:.1f} KB, lines={md.count(chr(10))}, math_inline={len(MT)}, code={md.count('```') // 2})")


if __name__ == "__main__":
    main()
