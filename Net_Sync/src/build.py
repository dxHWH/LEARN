"""拼装《网络同步全景：从 HOK 帧同步到 Unreal 状态同步》电子书：src/chapters/*.html -> ../Net_Sync.html

章节里可用的写法（构建时展开）：
  $...$ / $$...$$                     LaTeX 公式（KaTeX 构建时渲染，字体内嵌）；字面量美元写 \\$
  <!--code:cpp|cs|py|text|bash|ini:标题-->  原始代码（无需转义） <!--/code-->
  <!--deriv 标题 ... -->              逐行推导（等号对齐 + 每步依据）
  <!--bars {json} -->                 横向柱状图；items 的值可以写数据路径字符串（如 "e3.relay.gfz"）
  <!--flow {json} -->                 方框 + 箭头结构图（架构 / 流程 / 族谱 / 决策树）
  <!--plot|gbars|graph|dtab {json} --> figs.py：折线 / 分组柱 / 状态图 / 数据表
  <!--seq|tree|spans {json} -->       bpmacros.py：时序图 / 树 + 说明列 / 生命周期条
  <!--lanes|win {json} -->            netfigs.py：三路线逐帧时间线 / HOK 接收窗口逐步演示（构建时真实执行）
  [[路径|格式]]                       正文内联数字，例如 [[e3.relay.gfz|.0f]]；全部来自 demos/*.json
"""
import base64
import html
import json
import os
import pathlib
import re
import subprocess
import sys
import tempfile
import xml.etree.ElementTree as ET

import bpmacros as BM
import figs as F
import netfigs as NF

ROOT = pathlib.Path(__file__).resolve().parent
OUT = ROOT.parent / "Net_Sync.html"
NODE = r"C:\Users\haydenyhu\.workbuddy\binaries\node\versions\22.22.2-2\node.exe"
NODE_MODULES = pathlib.Path(r"C:\Users\haydenyhu\.workbuddy\binaries\node\workspace\node_modules")
KATEX_DIR = NODE_MODULES / "katex" / "dist"
MACROS = {}

CODE_RE = re.compile(r"<!--code:(\w+):(.*?)-->\n?(.*?)\n?<!--/code-->", re.S)
MACRO_RE = re.compile(r"<!--(bars|flow|plot|gbars|graph|dtab|seq|tree|spans|lanes|win)\s+(\{.*?\})\s*-->", re.S)
DERIV_RE = re.compile(r"<!--deriv ([^\n]*)\n(.*?)-->", re.S)
DISPLAY_RE = re.compile(r"\$\$([\s\S]+?)\$\$")
INLINE_RE = re.compile(r"(?<![\\$])\$(?!\$)([^$\n]+?)(?<!\\)\$(?!\$)")
VAL_RE = re.compile(r"\[\[([\w.\-]+?)(?:\|([^\]]+))?\]\]")
VALS = F.VALS
NF.VALS = VALS
DEMOS = ROOT.parent / "demos"


def val_sub(m):
    return F.fmtv(F.get(m.group(1)), m.group(2))


OPTS = {"precise": "/O2 /fp:precise", "fast": "/O2 /fp:fast /arch:AVX2", "strict": "/Od /fp:strict"}


def load_data():
    """demos/figdata.json（netsync_demo.cpp）+ demos/det_*.json（determinism_demo.cpp 三种编译选项）→ VALS，并派生出图表用的视图。"""
    D = json.loads((DEMOS / "figdata.json").read_text(encoding="utf-8"))
    VALS.update(D)
    det = {t: json.loads((DEMOS / f"det_{t}.json").read_text(encoding="utf-8")) for t in ("precise", "fast", "strict")}
    VALS["det"] = det
    msgs = []
    fp, ff, fs = det["precise"]["D2"]["frames"], det["fast"]["D2"]["frames"], det["strict"]["D2"]["frames"]
    first = next((i + 1 for i, (a, b) in enumerate(zip(fp, ff)) if a != b), -1)
    assert fp == fs, "precise 与 strict 的浮点逐帧 hash 应完全一致"
    assert det["precise"]["D3"]["final"] == det["fast"]["D3"]["final"] == det["strict"]["D3"]["final"], "定点数 hash 应三份一致"
    VALS["detc"] = {"firstdiff": first, "fixed": det["precise"]["D3"]["final"],
                    "fp": det["precise"]["D2"]["final"], "ff": det["fast"]["D2"]["final"],
                    "d4": [[r[0], r[1]] for r in det["precise"]["D4"]],
                    "d1": [{"tag": t, "opt": OPTS[t], **det[t]["D1"]} for t in ("precise", "fast", "strict")],
                    "d6": [{"tag": t, "opt": OPTS[t], **det[t]["D6"]} for t in ("precise", "fast", "strict")]}
    msgs.append(f"确定性：precise≡strict，fast 自第 {first} 帧起不同，定点三份一致 ✓")
    VALS["e3"] = {r["key"]: r for r in D["E3"]["routes"]}
    e5 = {}
    for r in D["E5"]["rows"]:
        e5.setdefault(r["m"], {"name": r["name"], "sim": [], "ana": [], "rows": []})
        g = e5[r["m"]]
        if r["hole"] > 0:
            g["sim"].append([r["k"], r["hole"]])
        g["ana"].append([r["k"], r["analytic"]])
        g["rows"].append(r)
    p, Lb = 0.05, 4.0
    e5["g5"]["closed"] = [[k, p * (1 - 1 / Lb) ** (k - 1)] for k in range(1, 9)]
    for k in (3, 5, 8):
        sim = next(r["hole"] for r in e5["g5"]["rows"] if r["k"] == k)
        cf = p * (1 - 1 / Lb) ** (k - 1)
        assert abs(sim - cf) / cf < 0.08, f"突发丢包闭式解与仿真不符 k={k}: {sim} vs {cf}"
    e5["tab"] = [{"k": k, **{m: next(r["hole"] for r in e5[m]["rows"] if r["k"] == k) for m in ("b1", "b5", "b10", "g5")},
                  "cf": p * (1 - 1 / Lb) ** (k - 1), "bytes": 36 + 24 * k} for k in range(1, 9)]
    VALS["e5"] = e5
    msgs.append("E5 突发丢包闭式解 p(1-1/L)^(k-1) 与仿真相符 ✓")
    VALS["e7"] = {m: {str(r["s"]): r for r in D["E7"]["rows"] if r["m"] == m} for m in ("b5", "g10")}
    VALS["e8"] = {f"{r['fps']}_{r['spike']}": r for r in D["E8"]["rows"]}
    VALS["e9"] = {f"{r['rtt']}_{r['d']}": r for r in D["E9"]["rows"]}
    rtts = [40, 80, 120, 160, 200]
    VALS["e9g"] = {"groups": [f"RTT {x} ms" for x in rtts],
                   **{f"depth{d}": [VALS["e9"][f"{x}_{d}"]["depth"] for x in rtts] for d in range(5)},
                   **{f"rb{d}": [VALS["e9"][f"{x}_{d}"]["rbps"] for x in rtts] for d in range(5)},
                   **{f"fzd{d}": [VALS["e9"][f"{x}_{d}"]["fzDelay"] / 1000 for x in rtts] for d in range(5)}}
    VALS["e9sel"] = [dict(r, est=max(0.0, (r["rtt"] / 2 + 1 + r["rtt"] * 0.04) / (1000 / 60) - r["d"] + 1)) for r in D["E9"]["rows"] if r["rtt"] in (80, 160)]
    VALS["e10"] = {f"{int(r['J'])}_{int(r['D'])}": r for r in D["E10"]["rows"]}
    VALS["e12"] = {str(r["ping"]): r for r in D["E12"]["rows"]}
    VALS["e12p"] = {k: [[r["ping"], r[k]] for r in D["E12"]["rows"]] for k in ("none", "rewind", "cap")}
    VALS["e13"] = {f"{r['s']}_{r['c']}": r for r in D["E13"]["rows"]}
    VALS["e13g"] = {"groups": ["Pawn 英雄\n(期望 33 ms)", "投射物\n(期望 33 ms)", "拾取物 / 机关\n(期望 100 ms)", "PlayerState\n(期望 1000 ms)"],
                    **{f"max{s}": [min(VALS["e13"][f"{s}_{c}"]["max"], 2000) for c in range(4)] for s in range(3)},
                    **{f"avg{s}": [VALS["e13"][f"{s}_{c}"]["avg"] for c in range(4)] for s in range(3)}}
    VALS["e1p"] = {k: [[r["n"], r[k]] for r in D["E1"]["rows"]] for k in ("fs", "full", "delta", "aoi")}
    VALS["e11"] = {str(r["s"]): r for r in D["E11"]["rows"]}
    return msgs


COL = F.COL
CIRC = "①②③④⑤⑥⑦⑧⑨⑩⑪⑫⑬⑭⑮⑯⑰⑱⑲⑳"


def esc(s):
    return html.escape(str(s), quote=True)


def fig(svg, cap):
    c = f"<figcaption>{cap}</figcaption>" if cap else ""
    return f"<figure>\n{svg}\n{c}</figure>"


def expand_macros(tex):
    for k in sorted(MACROS, key=len, reverse=True):
        tex = re.sub(re.escape(k) + r"(?![A-Za-z])", lambda m, v=MACROS[k]: v, tex)
    return tex


def bar_items(d):
    return [[it[0], F.get(it[1]) if isinstance(it[1], str) else it[1], *it[2:]] for it in d["items"]]


# ============================================================ bars
def bars(d):
    items = bar_items(d)
    W = d.get("w", 860)
    labW = d.get("labw", 170)
    bh, gap = 22, 8
    vmax = d.get("max") or max(it[1] for it in items if not str(it[0]).startswith("—"))
    room = W - labW - 150
    unit = d.get("unit", "")
    nf = d.get("fmt", "g")
    y = 10
    out = []
    if d.get("title"):
        out.append(f'<text x="{labW}" y="{y + 10}" class="b">{esc(d["title"])}</text>')
        y += 26
    for it in items:
        lab, v = it[0], it[1]
        if str(lab).startswith("—"):
            out.append(f'<text x="12" y="{y + 14}" class="sm b">{esc(lab[1:])}</text>')
            y += 22
            continue
        c = COL[it[2] if len(it) > 2 else "b"]
        w = max(2, min(room, room * v / vmax))
        out.append(f'<text x="{labW - 10}" y="{y + 16}" text-anchor="end" class="sm">{esc(lab)}</text>')
        out.append(f'<rect x="{labW}" y="{y}" width="{w:.1f}" height="{bh}" rx="4" fill="{c[0]}" stroke="{c[1]}" stroke-width="1.4"/>')
        txt = it[3] if len(it) > 3 else f"{format(v, nf)} {unit}".strip()
        out.append(f'<text x="{labW + w + 8:.1f}" y="{y + 16}" class="sm mono b" style="fill:{c[1]}">{esc(txt)}</text>')
        y += bh + gap
    y += 4
    svg = f'<svg viewBox="0 0 {W} {y}" xmlns="http://www.w3.org/2000/svg">' + "".join(out) + "</svg>"
    return fig(svg, d.get("cap"))


# ============================================================ flow：方框 + 箭头
MARK = {"b": "ab", "o": "ao", "g": "ag", "r": "ar", "p": "ap", "t": "at", "a": "aa"}
WARN = []


def text_w(s, size):
    w = 0.0
    for ch in str(s):
        w += size if ord(ch) > 0x2E80 else (size * 0.3 if ch == " " else size * 0.58)
    return w


def flow_norm(d):
    if d.get("_norm"):
        return d
    cols, rows = d.get("cols", []), d.get("rows", [])
    colw = d.get("colw", [])
    for b in d.get("boxes", []):
        if "cr" in b:
            c, r = b["cr"]
            b.setdefault("x", cols[c])
            b.setdefault("y", rows[r])
            b.setdefault("w", colw[c] if c < len(colw) else d.get("bw", 150))
        b.setdefault("w", d.get("bw", 150))
        b.setdefault("h", d.get("bh", 48))
    arrows = d.setdefault("arrows", [])
    for ch in d.get("chains", []):
        ids, kw = ch[0], (ch[1] if len(ch) > 1 else {})
        for a, b in zip(ids, ids[1:]):
            arrows.append(dict(kw, **{"from": a, "to": b}))
    d["_norm"] = True
    return d


def flow_boxes(d):
    return {b["id"]: b for b in d.get("boxes", []) if "id" in b}


def _anchor(b, side):
    x, y, w, h = b["x"], b["y"], b["w"], b["h"]
    return {"r": (x + w, y + h / 2), "l": (x, y + h / 2), "t": (x + w / 2, y), "b": (x + w / 2, y + h)}[side]


def flow_edge_pts(a, bx, e=None):
    if e and e.get("s"):
        return [_anchor(a, e["s"][0]), _anchor(bx, e["s"][1])]
    ax0, ax1, bx0, bx1 = a["x"], a["x"] + a["w"], bx["x"], bx["x"] + bx["w"]
    if ax1 <= bx0:
        return [_anchor(a, "r"), _anchor(bx, "l")]
    if bx1 <= ax0:
        return [_anchor(a, "l"), _anchor(bx, "r")]
    if a["y"] + a["h"] <= bx["y"]:
        return [_anchor(a, "b"), _anchor(bx, "t")]
    return [_anchor(a, "t"), _anchor(bx, "b")]


def flow(d):
    flow_norm(d)
    W, H = d.get("w", 900), d["h"]
    out = []
    boxes = flow_boxes(d)
    for r in d.get("rects", []):
        c = COL[r.get("c", "s")]
        dash = ' stroke-dasharray="5 4"' if r.get("dash") else ""
        out.append(f'<rect x="{r["x"]}" y="{r["y"]}" width="{r["w"]}" height="{r["h"]}" rx="{r.get("rx", 12)}" '
                   f'fill="{r.get("fill", c[0])}" stroke="{c[1]}" stroke-width="1.2"{dash}/>')
        if r.get("t"):
            out.append(f'<text x="{r["x"] + 10}" y="{r["y"] + 17}" class="xs b" style="fill:{c[1]}">{esc(r["t"])}</text>')
    for e in d.get("arrows", []):
        c = COL[e.get("c", "s")][1] if e.get("c", "s") != "s" else "#4B5563"
        pts = e["p"] if "p" in e else flow_edge_pts(boxes[e["from"]], boxes[e["to"]], e)
        dash = ' stroke-dasharray="6 4"' if e.get("dash") else ""
        mk = "" if e.get("head") is False else f' marker-end="url(#{MARK.get(e.get("c", "s"), "a")})"'
        ps = " ".join(f"{x:.1f},{y:.1f}" for x, y in pts)
        out.append(f'<polyline points="{ps}" fill="none" stroke="{c}" stroke-width="{e.get("sw", 1.6)}"{dash}{mk}/>')
        if e.get("label"):
            lx = e.get("lx", (pts[0][0] + pts[-1][0]) / 2 + 6)
            ly = e.get("ly", (pts[0][1] + pts[-1][1]) / 2 - 4)
            out.append(f'<text x="{lx}" y="{ly}" class="xs" style="fill:{c}">{esc(e["label"])}</text>')
    for b in d.get("boxes", []):
        c = COL[b.get("c", "b")]
        sw = 2.4 if b.get("hl") else 1.4
        st = "#B91C1C" if b.get("hl") else c[1]
        out.append(f'<rect x="{b["x"]}" y="{b["y"]}" width="{b["w"]}" height="{b["h"]}" rx="{b.get("rx", 9)}" '
                   f'fill="{b.get("fill", c[0])}" stroke="{st}" stroke-width="{sw}"/>')
        cx = b["x"] + b["w"] / 2
        lines = [(b["t"], "b", b.get("ts", 13.5), "#111827")]
        if b.get("s"):
            lines.append((b["s"], "sm", b.get("ss", 11.2), c[1] if b.get("sc") else "#4B5563"))
        if b.get("s2"):
            lines.append((b["s2"], "xs", 10.5, c[1] if b.get("sc2", True) else "#6B7280"))
        gap = b.get("lh", 17)
        y0 = b["y"] + b["h"] / 2 - (len(lines) - 1) * gap / 2 + 4.5
        for i, (txt, cls, size, col) in enumerate(lines):
            tw = text_w(txt, size)
            if tw > b["w"] - 6:
                WARN.append(f"[flow] 文字可能超出方框（{tw:.0f}>{b['w'] - 6}）：{txt}")
            out.append(f'<text x="{cx:.1f}" y="{y0 + i * gap:.1f}" text-anchor="middle" class="{cls}" '
                       f'style="font-size:{size}px;fill:{col}">{esc(txt)}</text>')
        if b.get("hl"):
            out.append(f'<text x="{b["x"] + b["w"] - 9}" y="{b["y"] + 13}" text-anchor="middle" class="xs b" style="fill:#B91C1C">★</text>')
    for t in d.get("texts", []):
        style = f' style="fill:{t["fill"]}"' if t.get("fill") else ""
        out.append(f'<text x="{t["x"]}" y="{t["y"]}" text-anchor="{t.get("a", "start")}" class="{t.get("cls", "sm")}"{style}>{esc(t["t"])}</text>')
    svg = f'<svg viewBox="0 0 {W} {H}" xmlns="http://www.w3.org/2000/svg">' + "".join(out) + "</svg>"
    return fig(svg, d.get("cap"))


def macro(m: re.Match) -> str:
    kind, body = m.group(1), m.group(2)
    try:
        d = json.loads(body)
    except json.JSONDecodeError as e:
        raise SystemExit(f"[{kind}] JSON 错误: {e}\n{body[:300]}")
    if kind in BM.MACROS:
        return BM.MACROS[kind](d)
    if kind in NF.MACROS:
        return NF.MACROS[kind](d)
    fn = {"bars": bars, "flow": flow}.get(kind) or F.MACROS[kind]
    return fn(d)


# ============================================================ deriv
def split_amp(line):
    depth = env = 0
    i = 0
    while i < len(line):
        c = line[i]
        if line.startswith("\\begin{", i):
            env += 1
        elif line.startswith("\\end{", i):
            env -= 1
        if c == "\\":
            i += 2
            continue
        if c == "{":
            depth += 1
        elif c == "}":
            depth -= 1
        elif c == "&" and depth == 0 and env == 0:
            return line[:i].strip(), line[i + 1:].strip()
        i += 1
    return None, line.strip()


def deriv_parse(title, body):
    rows = []
    for raw in body.strip("\n").split("\n"):
        line = raw.strip()
        if not line:
            continue
        if line.startswith("#"):
            rows.append(("c", line[1:].strip()))
            continue
        key = line.startswith("*")
        if key:
            line = line[1:].strip()
        eq, _, why = line.partition(" || ")
        lhs, rhs = split_amp(eq)
        rows.append(("k" if key else "e", lhs or "", rhs, why.strip()))
    return title.strip(), rows


def deriv(m: re.Match) -> str:
    title, rows = deriv_parse(m.group(1), m.group(2))
    out = [f'<div class="deriv"><div class="dh">推导 · {title}</div><div class="dt"><table>']
    n = 0
    for r in rows:
        if r[0] == "c":
            out.append(f'<tr class="dc"><td></td><td colspan="3">{r[1]}</td></tr>')
            continue
        n += 1
        no = CIRC[n - 1] if n <= len(CIRC) else f"({n})"
        lhs = f"$\\displaystyle {r[1]}$" if r[1] else ""
        rhs = f"$\\displaystyle {{}}{r[2]}$"
        cls = ' class="dk"' if r[0] == "k" else ""
        out.append(f'<tr{cls}><td class="dn">{no}</td><td class="dl">{lhs}</td><td class="dr">{rhs}</td><td class="dw">{r[3]}</td></tr>')
    out.append("</table></div></div>")
    return "".join(out)


# ============================================================ KaTeX
def katex_css():
    css = (KATEX_DIR / "katex.min.css").read_text(encoding="utf-8")

    def font(m):
        name = m.group(1)
        data = base64.b64encode((KATEX_DIR / "fonts" / f"{name}.woff2").read_bytes()).decode()
        return f'src:url(data:font/woff2;base64,{data}) format("woff2")'

    css = re.sub(r'src:url\(fonts/([\w-]+)\.woff2\) format\("woff2"\)(?:,url\([^)]*\) format\("[^"]*"\))*', font, css)
    assert "url(fonts/" not in css, "KaTeX 字体内嵌失败"
    return css


def render_math(texts):
    if not texts:
        return []
    with tempfile.TemporaryDirectory() as td:
        a, b = os.path.join(td, "in.json"), os.path.join(td, "out.json")
        pathlib.Path(a).write_text(json.dumps({"macros": MACROS, "items": texts}, ensure_ascii=False), encoding="utf-8")
        env = dict(os.environ, NODE_PATH=str(NODE_MODULES))
        subprocess.run([NODE, str(ROOT / "katex_render.js"), a, b], check=True, env=env)
        res = json.loads(pathlib.Path(b).read_text(encoding="utf-8"))
    errs = [(t, r["err"]) for t, r in zip(texts, res) if not r["ok"]]
    if errs:
        for t, e in errs:
            print(f"[KaTeX] {e}\n    {t[0][:120]}", file=sys.stderr)
        raise SystemExit(f"KaTeX 渲染失败 {len(errs)} 处")
    return [r["html"] for r in res]


def code_block(m: re.Match) -> str:
    lang, title, body = m.group(1), m.group(2).strip(), m.group(3)
    body = body.strip("\n")
    return (
        '<div class="code"><div class="bar"><span>%s</span><button>复制</button></div>'
        '<pre data-lang="%s">%s</pre></div>' % (html.escape(title or lang), lang, html.escape(body))
    )


def main() -> int:
    shell = (ROOT / "shell.html").read_text(encoding="utf-8")
    checks = load_data()
    stash = []

    def keep(s):
        stash.append(s)
        return f"\x00{len(stash) - 1}\x00"

    parts = []
    for f in sorted((ROOT / "chapters").glob("*.html")):
        t = f.read_text(encoding="utf-8")
        t = CODE_RE.sub(lambda m: keep(code_block(m)), t)
        t = DERIV_RE.sub(deriv, t)
        t = MACRO_RE.sub(macro, t)
        parts.append(t)
    content = "\n".join(parts)
    content = VAL_RE.sub(val_sub, content)
    content = re.sub(r"<svg[\s\S]*?</svg>", lambda m: keep(m.group(0)), content)

    found = []

    def collect_d(m):
        found.append((m.group(1).strip(), True)); return f"\x01{len(found) - 1}\x01"

    def collect_i(m):
        found.append((m.group(1).strip(), False)); return f"\x01{len(found) - 1}\x01"
    content = DISPLAY_RE.sub(collect_d, content)
    content = INLINE_RE.sub(collect_i, content)
    uniq = list(dict.fromkeys(found))
    rendered = dict(zip(uniq, render_math(uniq)))
    content = re.sub(r"\x01(\d+)\x01", lambda m: rendered[found[int(m.group(1))]], content)
    stray = re.findall(r"(?<!\\)\$", re.sub(r"\x00\d+\x00", "", content))
    content = content.replace("\\$", "$")
    while "\x00" in content:
        content = re.sub(r"\x00(\d+)\x00", lambda m: stash[int(m.group(1))], content)

    bad = 0
    for i, svg in enumerate(re.findall(r'<svg viewBox[\s\S]*?</svg>', content)):
        try:
            ET.fromstring(svg.replace("&nbsp;", " "))
        except ET.ParseError as e:
            bad += 1
            print(f"[SVG#{i}] 解析失败: {e}\n{svg[:160]}...", file=sys.stderr)
    ids = re.findall(r' id="([^"]+)"', content)
    dup = {x for x in ids if ids.count(x) > 1}
    if dup:
        print("重复 id:", dup, file=sys.stderr)
    left = re.findall(r"<!--(bars|flow|plot|gbars|graph|dtab|seq|tree|spans|lanes|win|code|deriv)\b", content) + VAL_RE.findall(content)
    if left:
        print("未展开的宏:", left, file=sys.stderr)
    for w in WARN + F.WARN + NF.WARN:
        print(w, file=sys.stderr)
    for c in checks:
        print("[校验]", c)
    if stray:
        print(f"[warn] 有 {len(stray)} 个未配对的 $（检查公式定界符）", file=sys.stderr)
    scripts = (ROOT / "labs.js").read_text(encoding="utf-8") if (ROOT / "labs.js").exists() else ""
    page = shell.replace("{{KATEX_CSS}}", katex_css()).replace("{{CONTENT}}", content).replace("{{SCRIPTS}}", scripts)
    OUT.write_text(page, encoding="utf-8")
    print(f"OK -> {OUT}  ({OUT.stat().st_size / 1024:.1f} KB, figure={content.count('<figure>')}, "
          f"svg={len(re.findall('<svg viewBox', content))}, bad_svg={bad}, dup_id={len(dup)}, "
          f"math={len(found)} (uniq {len(uniq)}), deriv={content.count('class=\"deriv\"')}, lab={content.count('class=\"lab')})")
    return 1 if bad or left else 0


if __name__ == "__main__":
    sys.exit(main())
