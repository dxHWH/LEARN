"""拼装《排序算法：从原理到工程》电子书：src/chapters/*.html -> ../Sorting_Algorithms.html

章节里可用的“宏”（构建时展开，保证 SVG 合法且风格统一）：
  <!--code:cpp:标题-->  原始代码（无需转义） <!--/code-->
  <!--trace {json} -->      数组状态逐行演示图（排序过程 trace）
  <!--bars {json} -->       横向柱状图（实测数据）
  <!--lines {json} -->      折线图（复杂度增长曲线）
所有宏都会被包进 <figure>，json 里的 "cap" 作为图注。
"""
import html
import json
import math
import pathlib
import re
import sys
import xml.etree.ElementTree as ET

ROOT = pathlib.Path(__file__).resolve().parent
OUT = ROOT.parent / "Sorting_Algorithms.html"

CODE_RE = re.compile(r"<!--code:(\w+):(.*?)-->\n?(.*?)\n?<!--/code-->", re.S)
MACRO_RE = re.compile(r"<!--(trace|bars|lines)\s+(\{.*?\})\s*-->", re.S)

COL = {
    "b": ("#EAF0FD", "#2F5BD3"), "o": ("#FFF4EC", "#C2410C"), "g": ("#EAF7EF", "#15803D"),
    "p": ("#F3EEFE", "#6D28D9"), "r": ("#FDEEEE", "#B91C1C"), "a": ("#FEF6E4", "#B45309"),
    "s": ("#F3F4F6", "#6B7280"), "t": ("#E6F6F5", "#0F766E"),
}
# trace 单元格状态：字符 -> (填充, 描边, 图例名)
ST = {
    ".": ("#FFFFFF", "#CBD5E1", None),
    "c": ("#FEF3C7", "#B45309", "正在比较"),
    "x": ("#FDE2E2", "#B91C1C", "交换 / 移动"),
    "w": ("#FFEDD5", "#C2410C", "写入"),
    "d": ("#DCFCE7", "#15803D", "已就位"),
    "v": ("#EDE9FE", "#6D28D9", "基准 pivot"),
    "k": ("#DBEAFE", "#2F5BD3", "当前元素 / 键"),
    "e": ("#CCFBF1", "#0F766E", "等于 pivot"),
    "m": ("#F3F4F6", "#D1D5DB", "不在当前范围"),
}


def esc(s):
    return html.escape(str(s), quote=True)


def fig(svg, cap):
    c = f"<figcaption>{cap}</figcaption>" if cap else ""
    return f"<figure>\n{svg}\n{c}</figure>"


def trace(d):
    rows = d["rows"]
    W = d.get("w", 860)
    n = max(len(r["a"]) for r in rows)
    labW = d.get("labw", 96)
    hasNote = any(r.get("note") for r in rows)
    noteW = d.get("notew", 290 if hasNote else 0)
    cw = min(46, (W - labW - noteW - 10) / n)
    ch = 32
    y = 8
    out, used = [], set()
    names = d.get("names", {})
    for r in rows:
        a = r["a"]
        s = r.get("s", "")
        p = r.get("p", {})
        g = r.get("g", [])
        if p:
            y += 16
        top = y
        if r.get("lab"):
            out.append(f'<text x="{labW - 10}" y="{top + ch / 2 + 5}" text-anchor="end" class="sm b">{esc(r["lab"])}</text>')
        for i, v in enumerate(a):
            st = s[i] if i < len(s) else "."
            fill, stroke, name = ST[st]
            if name:
                used.add(st)
            x = labW + i * cw
            out.append(f'<rect x="{x + 1:.1f}" y="{top}" width="{cw - 2:.1f}" height="{ch}" rx="5" fill="{fill}" stroke="{stroke}" stroke-width="{1.6 if st != "." else 1}"/>')
            tc = "#9CA3AF" if st == "m" else "#111827"
            if v != "":
                out.append(f'<text x="{x + cw / 2:.1f}" y="{top + ch / 2 + 5}" text-anchor="middle" class="mono b" style="fill:{tc};font-size:{13 if cw >= 34 else 11}px">{esc(v)}</text>')
        for lab, idx in p.items():
            x = labW + idx * cw + cw / 2
            out.append(f'<text x="{x:.1f}" y="{top - 4}" text-anchor="middle" class="xs b" style="fill:#C2410C">{esc(lab)}↓</text>')
        for sp in r.get("sep", []):
            x = labW + sp * cw
            out.append(f'<line x1="{x:.1f}" y1="{top - 3}" x2="{x:.1f}" y2="{top + ch + 3}" stroke="#111827" stroke-width="2.2"/>')
        y = top + ch
        if r.get("ix"):
            for i in range(len(a)):
                out.append(f'<text x="{labW + i * cw + cw / 2:.1f}" y="{y + 12}" text-anchor="middle" class="xs mono">{i}</text>')
            y += 14
        if g:
            y += 4
            for lo, hi, lab, *col in g:
                c = COL[col[0] if col else "b"][1]
                x1, x2 = labW + lo * cw + 3, labW + (hi + 1) * cw - 3
                out.append(f'<path d="M{x1:.1f},{y} v5 H{x2:.1f} v-5" fill="none" stroke="{c}" stroke-width="1.4"/>')
                out.append(f'<text x="{(x1 + x2) / 2:.1f}" y="{y + 18}" text-anchor="middle" class="xs" style="fill:{c}">{esc(lab)}</text>')
            y += 20
        if r.get("note"):
            nx = labW + n * cw + 14
            out.append(f'<text x="{nx:.1f}" y="{top + ch / 2 + 5}" class="sm">{esc(r["note"])}</text>')
        y += 10
    if d.get("legend", True) and used:
        x = labW
        y += 6
        for st in ".cxwdvkem":
            if st in used:
                fill, stroke, name = ST[st]
                name = names.get(st, name)
                out.append(f'<rect x="{x}" y="{y}" width="14" height="14" rx="3" fill="{fill}" stroke="{stroke}" stroke-width="1.4"/>')
                out.append(f'<text x="{x + 20}" y="{y + 12}" class="xs">{esc(name)}</text>')
                x += 34 + 13 * len(name)
        y += 22
    svg = f'<svg viewBox="0 0 {W} {y:.0f}" xmlns="http://www.w3.org/2000/svg">' + "".join(out) + "</svg>"
    return fig(svg, d.get("cap"))


def bars(d):
    items = d["items"]
    W = d.get("w", 860)
    labW = d.get("labw", 170)
    bh, gap = 22, 8
    vmax = d.get("max") or max(it[1] for it in items)
    room = W - labW - 120
    unit = d.get("unit", "")
    y = 10
    out = []
    if d.get("title"):
        out.append(f'<text x="{labW}" y="{y + 10}" class="b">{esc(d["title"])}</text>')
        y += 26
    for it in items:
        lab, v = it[0], it[1]
        c = COL[it[2] if len(it) > 2 else "b"]
        w = max(2, room * v / vmax)
        out.append(f'<text x="{labW - 10}" y="{y + 16}" text-anchor="end" class="sm">{esc(lab)}</text>')
        out.append(f'<rect x="{labW}" y="{y}" width="{w:.1f}" height="{bh}" rx="4" fill="{c[0]}" stroke="{c[1]}" stroke-width="1.4"/>')
        txt = it[3] if len(it) > 3 else f"{v:g} {unit}"
        out.append(f'<text x="{labW + w + 8:.1f}" y="{y + 16}" class="sm mono b" style="fill:{c[1]}">{esc(txt)}</text>')
        y += bh + gap
    y += 4
    svg = f'<svg viewBox="0 0 {W} {y}" xmlns="http://www.w3.org/2000/svg">' + "".join(out) + "</svg>"
    return fig(svg, d.get("cap"))


def lines(d):
    W, H = d.get("w", 860), d.get("h", 340)
    L, R, T, B = 70, 150, 20, 46
    x0, x1 = d["x"]
    xs = [x0 + (x1 - x0) * i / 120 for i in range(121)]
    ser = []
    for lab, expr, col in d["series"]:
        ys = [eval(expr, {"math": math, "log2": math.log2, "n": max(x, 1e-9)}) for x in xs]
        ser.append((lab, ys, col))
    ymax = d.get("ymax") or max(max(s[1]) for s in ser)
    px = lambda x: L + (W - L - R) * (x - x0) / (x1 - x0)
    py = lambda v: T + (H - T - B) * (1 - min(v, ymax) / ymax)
    out = [f'<line x1="{L}" y1="{H - B}" x2="{W - R}" y2="{H - B}" stroke="#9CA3AF"/>',
           f'<line x1="{L}" y1="{T}" x2="{L}" y2="{H - B}" stroke="#9CA3AF"/>']
    for k in range(5):
        v = ymax * k / 4
        out.append(f'<line x1="{L}" y1="{py(v):.1f}" x2="{W - R}" y2="{py(v):.1f}" stroke="#EEF0F3"/>')
        out.append(f'<text x="{L - 8}" y="{py(v) + 4:.1f}" text-anchor="end" class="xs">{v:g}</text>')
    for k in range(6):
        x = x0 + (x1 - x0) * k / 5
        out.append(f'<text x="{px(x):.1f}" y="{H - B + 18}" text-anchor="middle" class="xs">{x:g}</text>')
    out.append(f'<text x="{(L + W - R) / 2}" y="{H - 8}" text-anchor="middle" class="sm">{esc(d.get("xlab", "n"))}</text>')
    out.append(f'<text x="14" y="{T + 4}" class="sm">{esc(d.get("ylab", ""))}</text>')
    for lab, ys, col in ser:
        c = COL[col][1]
        pts = " ".join(f"{px(x):.1f},{py(v):.1f}" for x, v in zip(xs, ys) if v <= ymax * 1.0001)
        out.append(f'<polyline points="{pts}" fill="none" stroke="{c}" stroke-width="2.4"/>')
        # 标签放在曲线末端（被截断则放在截断处）
        last = [(x, v) for x, v in zip(xs, ys) if v <= ymax * 1.0001][-1]
        out.append(f'<text x="{px(last[0]) + 8:.1f}" y="{py(last[1]) + 4:.1f}" class="sm b" style="fill:{c}">{esc(lab)}</text>')
    svg = f'<svg viewBox="0 0 {W} {H}" xmlns="http://www.w3.org/2000/svg">' + "".join(out) + "</svg>"
    return fig(svg, d.get("cap"))


def macro(m: re.Match) -> str:
    kind, body = m.group(1), m.group(2)
    try:
        d = json.loads(body)
    except json.JSONDecodeError as e:
        raise SystemExit(f"[{kind}] JSON 错误: {e}\n{body[:200]}")
    return {"trace": trace, "bars": bars, "lines": lines}[kind](d)


def code_block(m: re.Match) -> str:
    lang, title, body = m.group(1), m.group(2).strip(), m.group(3)
    body = body.strip("\n")
    return (
        '<div class="code"><div class="bar"><span>%s</span><button>复制</button></div>'
        '<pre data-lang="%s">%s</pre></div>' % (html.escape(title or lang), lang, html.escape(body))
    )


def main() -> int:
    shell = (ROOT / "shell.html").read_text(encoding="utf-8")
    parts = []
    for f in sorted((ROOT / "chapters").glob("*.html")):
        t = f.read_text(encoding="utf-8")
        t = CODE_RE.sub(code_block, t)
        t = MACRO_RE.sub(macro, t)
        parts.append(t)
    content = "\n".join(parts)

    bad = 0
    for i, svg in enumerate(re.findall(r"<svg[\s\S]*?</svg>", content)):
        try:
            ET.fromstring(svg.replace("&nbsp;", " "))
        except ET.ParseError as e:
            bad += 1
            print(f"[SVG#{i}] 解析失败: {e}\n{svg[:160]}...", file=sys.stderr)
    ids = re.findall(r'id="([^"]+)"', content)
    dup = {x for x in ids if ids.count(x) > 1}
    if dup:
        print("重复 id:", dup, file=sys.stderr)
    left = re.findall(r"<!--(trace|bars|lines|code)", content)
    if left:
        print("未展开的宏:", left, file=sys.stderr)
    scripts = (ROOT / "labs.js").read_text(encoding="utf-8") if (ROOT / "labs.js").exists() else ""
    OUT.write_text(shell.replace("{{CONTENT}}", content).replace("{{SCRIPTS}}", scripts), encoding="utf-8")
    print(f"OK -> {OUT}  ({OUT.stat().st_size / 1024:.1f} KB, figure={content.count('<figure>')}, "
          f"svg={len(re.findall('<svg', content))}, bad_svg={bad}, dup_id={len(dup)}, lab={content.count('class=\"lab')})")
    return 1 if bad or left else 0


if __name__ == "__main__":
    sys.exit(main())
