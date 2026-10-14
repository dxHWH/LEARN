"""把电子书章节转换成 Markdown（与电子书同源：src/chapters/*.html）
产物：../RPGDemo_源码复盘与面试指南.md（顶部「提问记录」+ 目录 + 全部章节）
规则：
  - 公式保留 LaTeX 源码（$...$ / $$...$$），Typora / VS Code 可直接渲染
  - deriv 推导 → aligned 公式块 + 逐步依据列表
  - 数据宏（bars / plot / gbars / dtab / tline …）用 build.py 同一套数据生成表格；[[路径]] 数字与电子书同源
  - flow 图：优先用 ASCII 字典里的字符图（按「图 x-y」查），否则按“分组框 → 方框 → 箭头”自动转成文字
"""
import html
import json
import pathlib
import re
from html.parser import HTMLParser

import build as B

ROOT = pathlib.Path(__file__).resolve().parent
OUT = ROOT.parent / "RPGDemo_源码复盘与面试指南.md"
BOOK = "《RPGDemo 源码复盘与游戏客户端面试》"

QA = [
    {"time": "2026-10-13 21:07",
     "q": "同理，阅读工程 https://github.com/dxHWH/RPGDemo ，帮我实现一本电子书，要结合游戏客户端面试追问的问题以及各种发散的场景题",
     "a": "全书 15 章 + 3 附录：Ch0 读前须知 / 项目一页纸 / 面试地图 · Part I（Ch1 全景：对象图、继承树、一帧调用链、依赖图、36 问题分布；Ch2 生命周期、单例写反、fake null、执行顺序、UnityEditor 引用致打包失败）· Part II（Ch3 玩家状态机 E1；Ch4 Animator 与动画事件；Ch5 时间与帧率 E9/E10；Ch6 战斗判定与弹反窗口 E2）· Part III（Ch7 属性伤害异常状态 E3/E4/E8；Ch8 技能系统 E11；Ch9 敌人 AI E7）· Part IV（Ch10 背包装备合成掉落 E5/E6；Ch11 UI 与表现；Ch12 性能工程与两周重构路线）· Part V（Ch13 12 条追问链；Ch14 30 道发散场景题；Ch15 60 题速问速答 + 速记卡）· 附录 A 问题总表 36 条 / B Play Mode 验证清单 / C 源码索引",
     "keys": ["*"]},
    {"time": "2026-10-14 10:46",
     "q": "工程代码只是 demo，补充方法论角度的分析：①平台跳跃状态判定（挂 Transform 检测是否合适、能否优化）②滑墙状态怎么设计 ③跳跃平台怎么搭建（瓦片系统原理与操作）④没有输入输出缓冲怎么优化手感 ⑤背包、装备乃至技能系统的设计思路与理由；发散讲解并防守追问，更新电子书",
     "a": "新增 Part VI 方法论篇：Ch17 状态判定（单射线边界 F1、六个坑、五种检测手段、感知层）· Ch18 滑墙与蹬墙跳（六条转移、墙覆盖检测、F5 单墙攀爬实验、参数清单）· Ch19 Tilemap（六部件原理、场景存储结构、十步流程、关卡度量 F4、复合碰撞）· Ch20 手感（延迟链、跳跃缓冲 F3、土狼时间 F2、跳跃曲线 F4、输出侧反馈）· Ch21 背包 / 装备 / 技能系统设计（四层 + 三通道、模型与决策、端到端协作、取舍）；第 3/5/8/10 章加方法论延伸指引，附录 B 新增 B13~B15",
     "keys": ["*"]},
    {"time": "2026-10-14 11:11",
     "q": "补充上下文：面试官看不到 DEMO，需要优化对 demo 的描述并对齐口径，理清这类功能更好的实现（讲方法不讲代码），把目前的问题当作易踩的坑，描述如何避免与正确实现",
     "a": "新增 Ch16 面试口径（三个风险、白板关卡草图与操作参数表、五句式“场景 → 做法 → 坑 → 正解 → 取舍”、术语对齐表、12 条“坑 → 正解”口径、方法论版 1 / 3 分钟介绍、关于 Demo 本身的追问防守）；Ch17~21 每章以“口径：30 秒怎么讲”开头、以“坑 → 正解”组织；第 0 章面试地图与实验清单同步更新",
     "keys": ["*"]},
    {"time": "2026-10-14 14:34",
     "q": "需要在电子书中区分当前做法和理想重构方案的做法，说明清楚当前状态和理想状态，让读者更清晰地认识；更新电子书",
     "a": "第 0 章新增 0.2「两条线：现状 vs 理想」（四种标记、理想格落地徽标、修正 / 重构 / 上线级三档、讲述时态，图 0-1）· 第 1~12、17~21 章 x.1 改为「30 秒面试版 · 现状 vs 理想」并各加一张对照表（橙列现状 / 绿列理想 + 落地徽标）· 全书代码块分〔当前源码 · 822270c〕/〔理想方案 · 骨架〕，图注分〔现状〕/〔理想方案〕/〔现状 vs 理想〕，正文用〔现状〕/〔理想〕标签替换“现象 / 正解” · 第 1 章新增图 1-6 理想架构 · 第 16 章 16.3 增三种时态表 · 实验清单标注每个实验对拍的是哪两条线 · 新增附录 D（成熟度阶梯图 D-1、全书对照表自动索引、何时不必上理想方案、迁移顺序）",
     "keys": ["*"]},
    {"time": "2026-10-14 15:23",
     "q": "还需要点出当前做法存在的问题，更新电子书",
     "a": "17 张「现状 vs 理想」对照表新增「存在的问题」列（107 行逐条写出后果 + 证据：附录 A 编号 / 实验编号 / 推断），按 P0~P3（缺陷，同附录 A）/ 隐患 / 局限 / ✓ 无问题 分级，表尾自动统计本章问题分级 · 全部 25 个〔当前源码〕代码块底部加「现状问题」条 · 26 张〔现状〕图注下方加问题行 · 第 0 章 0.2 增「问题分级」说明表 · 附录 D.2 索引增「现状问题分级」列与全书合计",
     "keys": ["*"]},
]

ASCII = {
"图 0-2": r"""
┌ 项目模块（第 1~12 章）──────────────────────────────────────────────────────────┐
│ 地基与手感：Entity/生命周期 → 玩家状态机 → Animator+动画事件 → 时间与帧率      │
│ 战斗与行为：战斗判定 · 属性与伤害 · 技能 · 敌人 AI                              │
│ 物品/UI/工程：背包装备合成 · 掉落 · UI 与表现 · 性能与工程                       │
└──────────────────────────────────────────────────────────────────────────────────┘
① 动画事件 ──决定──→ 战斗判定何时发生（第 6 章弹反窗口）
② 时间与帧率 ──决定──→ 状态机计时是否可靠（第 5 章冲刺距离）
                      ↓
★ 面试官最终考：时序是否正确 · 数据与行为是否解耦 · 性能是否可控 · 能否扩展到联网与大规模
""",
"图 6-3": r"""
攻击方 DamageInfo ──→ 检测（Hurtbox 层 · 去重）──→ ★ 受击方 Receive()（唯一裁决：无敌？弹反？闪避？格挡？）
                                                        │
                                                        ↓
                                                    HitResult（Hit / Evaded / Parried / Blocked / Immune）
                                      ┌─────────────────┼──────────────────┬───────────────────┐
                                      ↓                 ↓                  ↓                   ↓
                                 数值结算         反馈（按结果）     通知攻击方（被弹反→眩晕）   事件总线
""",
"图 12-2": r"""
前 5 天只修“错”：
  第 1 天  能打包、不崩（去掉 Editor 引用、分身注入属性、单例改正、.gitignore、判空）
  第 2~3 天 状态正确（Request + 守卫 + 每帧一次；死亡吸收态；墙滑出口；骷髅不自转移）
  第 4 天  数值正确（统一 Chance.Roll；冰冻判目标；死亡保护；合成扣料；等级曲线表）
  第 5 天  帧率无关（冲刺按固定步计数；周期计时累加；虚线用离散解）
后 5 天才改“结构”：
  第 6~7 天 输入层（InputFrame + 预输入缓冲）
  第 8 天  伤害管线（DamageInfo → Receive → HitResult；Hurtbox + 去重）
  第 9 天  修正器与 Buff（速度 / 颜色由效果推导；冻结取最晚截止）
  第 10 天 物品实例 + 对象池（Container 事务；剑 / 分身 / 掉落入池）
贯穿始终：每修一个问题写一个 EditMode 测试；每天跑一次真打包
""",
}


def plain(s):
    s = re.sub(r"<(strong|b)>(.*?)</\1>", r"**\2**", s)
    s = re.sub(r"<code>(.*?)</code>", r"`\1`", s)
    s = re.sub(r'<span class=["\']tag [a-z]+["\']>(.*?)</span>', r"〔\1〕", s)
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
    ltgt = lambda x: str(x).replace("<", "&lt;").replace(">", "&gt;")
    name = lambda b: f"**{ltgt(b['t'])}**" + ("（" + "；".join(ltgt(x) for x in (b.get("s"), b.get("s2")) if x) + "）" if b.get("s") or b.get("s2") else "") + (" ★" if b.get("hl") else "")
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
    edges = [f"- {ltgt(bid[e['from']]['t'])} → {ltgt(bid[e['to']]['t'])}" + (f"（{e['label']}）" if e.get("label") else "")
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
    if tag == "figure":                     # 手绘 SVG：按图注里的「图 x-y」查 ASCII 字典
        cap = find(n, lambda k: k.tag == "figcaption")
        ct = inline(cap).strip() if cap else ""
        key = re.search(r"图 [A-Z]?\d+-\d+", ct)
        art = ASCII.get(key.group(0)) if key else None
        if not art:
            print("[warn] 手绘图缺少 ASCII：", ct[:30])
            art = "\n（图见电子书）\n"
        return f"```text{art}```\n\n*{ct}*\n\n"
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
    if "pit" in c or "scene" in c:
        q = find(n, lambda k: "q" in k.cls())
        title = inline(q).strip() if q else ""
        body = "".join(block(k) for k in n.kids if k is not q)
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
        return "> 此处是交互实验，仅电子书 `RPG_Demo.html` 提供。\n\n"
    return "".join(block(k) for k in n.kids)


PH, MT = [], []


def pre(t):
    def ph(s):
        PH.append(s)
        return f"\n§§{len(PH) - 1}§§\n"

    def code(m):
        lang, title, body = m.group(1), m.group(2).strip(), m.group(3).strip("\n")
        title, _, prob = title.partition(" ‖ ")
        kind, title = B.code_kind(lang, title.strip())
        title = {"now": "〔当前源码〕", "ideal": "〔理想方案 · 骨架〕"}.get(kind, "") + title
        pb = f"\n\n> ⚠ **现状问题**　{B.VAL_RE.sub(B.val_sub, B.sev_md(prob, plain))}" if prob else ""
        return ph(f"*{title}*\n\n```{ {'cpp': 'cpp', 'cs': 'csharp', 'py': 'python', 'bash': 'bash', 'ini': 'ini'}.get(lang, 'text') }\n{body}\n```{pb}")
    t = B.CODE_RE.sub(code, t)
    t = B.DERIV_RE.sub(lambda m: ph(md_deriv(m.group(1), m.group(2))), t)

    def mac(m):
        kind, d = m.group(1), json.loads(m.group(2))
        cap = plain(d.get("cap", "")).replace("<", "&lt;").replace(">", "&gt;")
        if kind == "bars":
            body = md_bars(d)
        elif kind == "flow":
            body = md_flow(d, cap)
        elif kind in B.BM.MD_MACROS:
            body = B.BM.MD_MACROS[kind](d)
        elif kind == "tline":
            body = B.md_tline(d)
        elif kind == "cmp":
            body = B.md_cmp(d, plain)
            return ph(body)
        elif kind == "cmpsum":
            return ph(B.md_cmpsum())
        elif kind == "ladder":
            body = B.md_ladder(d)
        elif kind in B.PF.MD:
            body = B.PF.MD[kind](d)
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

> 配套电子书：`RPG_Demo.html` / `RPG_Demo.pdf`；源码快照：`repo/`（dxHWH/RPGDemo@822270c，Unity 2022.3.54f1c1，sparse checkout：Script / Animation / Prefabs / Scenes / Material / ProjectSettings）；资源解析脚本：`tools/scan_refs.py`（guid → 场景 / Prefab 引用、序列化字段）、`tools/scan_anim.py`（AnimatorController 结构与片段事件，输出 `tools/anim_dump.txt`）。
> 方法论篇实测：`demos/feel_verify.cpp` → `feeldata.json` / `feel_output.txt`（F1 检测几何、F2 土狼时间、F3 跳跃缓冲、F4 跳跃曲线、F5 单墙攀爬）；关卡数据：`tools/scan_tilemap.py` → `tools/tilemap_dump.json`。
> 实测程序：`demos/rpg_verify.cpp`（C# 逻辑逐行移植为 C++，MSVC /O2）→ `figdata.json` / `run_output.txt`：E1 玩家状态机逐帧回放、E2 弹反窗口、E3 暴击 / 闪避 / 冰冻破甲、E4 等级成长、E5 掉落、E6 合成、E7 骷髅 AI 自转移与脱战、E8 重叠冻结、E9 瞄准虚线 vs Box2D、E10 帧率依赖（冲刺 / 点燃 / 旋转剑）、E11 分身朝向。
> 本文档与电子书由同一份源文件（`src/chapters/*.html`）生成：图表与正文中的实测数字全部由构建脚本从 `figdata.json` 读取。事实标签：〔源码〕〔意图〕〔问题〕〔改进〕〔扩展〕〔实测 Ex〕〔模型〕〔推断〕。
> **全书区分两条线**：〔现状〕= Demo（822270c）里真实存在的做法与问题；〔理想〕= 重构 / 上线级方案（设计，除注明“模型验证”外均未实现）。代码块标题前缀〔当前源码〕/〔理想方案 · 骨架〕，图注前缀〔现状〕/〔理想方案〕/〔现状 vs 理想〕；每章 x.1 有一张「现状 → 问题 → 理想」对照表（问题按 P0~P3 / 隐患 / 局限 / 无问题分级），当前源码代码块与现状图下方都有「⚠ 现状问题」，附录 D 汇总。

## 提问记录

| 时间 | 原始提问（摘要） | 回答对应章节 |
|---|---|---|
{{ROWS}}

## 目录

{{TOC}}

"""


def main():
    B.load_data()
    B.collect_cmps()
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
    body = re.sub(r"^(\*?)(图 ([0-9A-Z]+-\d+))", lambda m: m.group(1) + (f"〔{B.FIGLABEL[B.FIGSTATE[m.group(3)]]}〕" if m.group(3) in B.FIGSTATE else "") + m.group(2), body, flags=re.M)
    def figprob(m):
        p = B.FIGPROB.get(m.group(2))
        return m.group(1) + (f"\n\n> ⚠ **现状问题**　{B.VAL_RE.sub(B.val_sub, B.sev_md(p, plain))}" if p else "")
    body = re.sub(r"^(\*?〔现状〕图 (\d+-\d+)[^\n]*)$", figprob, body, flags=re.M)
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
