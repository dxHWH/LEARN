"""拼装《UE 双人联机射击 Demo：面试复盘与正确实现》电子书：src/chapters/*.html -> ../UE_Coop_Shooter.html

章节里可用的“宏”（构建时展开）：
  <!--code:cpp:标题-->  原始代码（无需转义） <!--/code-->
  <!--bars {json} -->    横向柱状图；items 的值可以写数据路径字符串，例如 "E2[rtt=200,loss=0.1].p99"
  <!--lanes {json} -->   开火时序泳道图（数据来自 demos/figdata.json 的 E1_lanes）
  <!--plot {json} -->    折线 / 阶梯图（series 是数据路径，指向 [[x,y],...]）
  <!--dtab {json} -->    数据表（src 指向 figdata 里的数组，where 过滤，cols 选列）
  {{v:路径:格式}}         正文里的内联数字，例如 {{v:E3[v=480,rtt=200].none:.1f}}
所有数字都来自 demos/coop_net_demo.cpp 的输出（figdata.json），书、md、demo 三者一致。
"""
import html
import json
import pathlib
import re
import sys
import xml.etree.ElementTree as ET

import bpmacros as BM

ROOT = pathlib.Path(__file__).resolve().parent
OUT = ROOT.parent / "UE_Coop_Shooter.html"
DATA = json.loads((ROOT.parent / "demos" / "figdata.json").read_text(encoding="utf-8"))

CODE_RE = re.compile(r"<!--code:(\w+):(.*?)-->\n?(.*?)\n?<!--/code-->", re.S)
MACRO_RE = re.compile(r"<!--(bars|lanes|plot|dtab|bpgraph|seq|tree|spans|bpnet|repmx)\s+(\{.*?\})\s*-->", re.S)
VAL_RE = re.compile(r"\{\{v:([^:}]+)(?::([^}]*))?\}\}")

COL = {
    "b": ("#EAF0FD", "#2F5BD3"), "o": ("#FFF4EC", "#C2410C"), "g": ("#EAF7EF", "#15803D"),
    "p": ("#F3EEFE", "#6D28D9"), "r": ("#FDEEEE", "#B91C1C"), "a": ("#FEF6E4", "#B45309"),
    "s": ("#F3F4F6", "#6B7280"), "t": ("#E6F6F5", "#0F766E"),
}
SCHEME = ["原蓝图方案", "预测 + 直接覆盖", "预测 + 序号对账"]


def esc(s):
    return html.escape(str(s), quote=True)


def fig(svg, cap):
    c = f"<figcaption>{cap}</figcaption>" if cap else ""
    return f"<figure>\n{svg}\n{c}</figure>"


# ------------------------------------------------------------------ 数据路径
def _num(s):
    try:
        return float(s)
    except ValueError:
        return s


def resolve(path):
    """'E2[rtt=200,loss=0.1].p99' / 'E5.2.pts' / 'E4.cases.2.4' -> 值"""
    cur = DATA
    for tok in re.findall(r"[^.\[\]]+(?:\[[^\]]*\])?", path):
        m = re.fullmatch(r"([^\[]+)(?:\[([^\]]*)\])?", tok)
        key, flt = m.group(1), m.group(2)
        if isinstance(cur, list):
            cur = cur[int(key)]
        else:
            cur = cur[key]
        if flt:
            conds = [c.split("=") for c in flt.split(",")]
            hits = [it for it in cur if all(abs(float(it[k]) - float(v)) < 1e-6 if isinstance(_num(v), float) else it[k] == v for k, v in conds)]
            if len(hits) != 1:
                raise SystemExit(f"[data] 路径 {path} 过滤得到 {len(hits)} 条")
            cur = hits[0]
    return cur


def fmt(v, f):
    if not f:
        return f"{v:g}" if isinstance(v, (int, float)) else str(v)
    return format(v, f)


def val_sub(m):
    return fmt(resolve(m.group(1)), m.group(2))


# ------------------------------------------------------------------ bars
def bars(d):
    items = [[it[0], resolve(it[1]) if isinstance(it[1], str) else it[1], *it[2:]] for it in d["items"]]
    W = d.get("w", 860)
    labW = d.get("labw", 200)
    bh, gap = 22, 8
    vmax = d.get("max") or max(it[1] for it in items)
    room = W - labW - 130
    unit = d.get("unit", "")
    nf = d.get("fmt", "g")
    y = 10
    out = []
    if d.get("title"):
        out.append(f'<text x="{labW}" y="{y + 10}" class="b">{esc(d["title"])}</text>')
        y += 26
    for it in items:
        lab, v = it[0], it[1]
        if lab.startswith("—"):
            out.append(f'<text x="12" y="{y + 14}" class="sm b">{esc(lab[1:])}</text>')
            y += 22
            continue
        c = COL[it[2] if len(it) > 2 else "b"]
        w = max(2, room * min(v, vmax) / vmax)
        out.append(f'<text x="{labW - 10}" y="{y + 16}" text-anchor="end" class="sm">{esc(lab)}</text>')
        out.append(f'<rect x="{labW}" y="{y}" width="{w:.1f}" height="{bh}" rx="4" fill="{c[0]}" stroke="{c[1]}" stroke-width="1.4"/>')
        txt = it[3] if len(it) > 3 else f"{format(v, nf)} {unit}".strip()
        out.append(f'<text x="{labW + w + 8:.1f}" y="{y + 16}" class="sm mono b" style="fill:{c[1]}">{esc(txt)}</text>')
        y += bh + gap
    y += 4
    svg = f'<svg viewBox="0 0 {W} {y}" xmlns="http://www.w3.org/2000/svg">' + "".join(out) + "</svg>"
    return fig(svg, d.get("cap"))


# ------------------------------------------------------------------ lanes：开火时序泳道
LANE_NAMES = ["本机（开火者）", "服务器", "另一端客户端"]


def lane_events(scheme):
    return [dict(t=e[0], lane=e[1], kind=e[2], v=e[3]) for e in DATA["E1_lanes"][str(scheme)]]


def lanes(d):
    sc = d["scheme"]
    evs = lane_events(sc)
    t1 = d.get("t1", 1.5)
    W, L, R = 860, 128, 16
    X = lambda t: L + (W - L - R) * t / t1
    LY = [74, 154, 234]
    o = []
    if d.get("title"):
        o.append(f'<text x="{L}" y="18" class="b">{esc(d["title"])}</text>')
    for i, nm in enumerate(LANE_NAMES):
        o.append(f'<line x1="{L}" y1="{LY[i]}" x2="{W - R}" y2="{LY[i]}" stroke="#CBD5E1" stroke-width="1.4"/>')
        o.append(f'<text x="{L - 10}" y="{LY[i] + 4}" text-anchor="end" class="sm b">{esc(nm)}</text>')
    k = 0.0
    while k <= t1 + 1e-9:
        x = X(k)
        o.append(f'<line x1="{x:.1f}" y1="{LY[0] - 22}" x2="{x:.1f}" y2="{LY[2] + 16}" stroke="#F1F2F5"/>')
        o.append(f'<text x="{x:.1f}" y="{LY[2] + 32}" text-anchor="middle" class="xs">{k:.1f} s</text>')
        k += 0.1
    # 连线：按下 -> 服务器 -> 两端的表现
    inp = {e["v"]: e for e in evs if e["kind"] == "input"}
    srv = {e["v"]: e for e in evs if e["lane"] == 1}
    for s, e in inp.items():
        if s in srv and srv[s]["t"] <= t1:
            col = "#B91C1C" if srv[s]["kind"] == "reject" else "#94A3B8"
            o.append(f'<line x1="{X(e["t"]):.1f}" y1="{LY[0] + 6}" x2="{X(srv[s]["t"]):.1f}" y2="{LY[1] - 6}" stroke="{col}" stroke-width="1"/>')
            for f in evs:
                if f["lane"] in (0, 2) and f["kind"] in ("fx", "ghost") and f["v"] == s and f["t"] > srv[s]["t"] and f["t"] <= t1 and not (sc and f["lane"] == 0):
                    if sc and f["lane"] == 2:
                        continue
                    y2 = LY[f["lane"]] + (6 if f["lane"] == 2 else -6)
                    o.append(f'<line x1="{X(srv[s]["t"]):.1f}" y1="{LY[1] + (6 if f["lane"] == 2 else -6)}" x2="{X(f["t"]):.1f}" y2="{y2}" stroke="{col}" stroke-width="1" stroke-dasharray="3 2"/>')
    if sc:  # 预测方案：远端按 BurstCounter 播放，用第 k 发有效射击连线
        oks = sorted([e for e in evs if e["lane"] == 1 and e["kind"] == "ok"], key=lambda e: e["t"])
        for f in evs:
            if f["lane"] == 2 and f["kind"] == "fx" and f["v"] <= len(oks) and f["t"] <= t1:
                s_ = oks[f["v"] - 1]
                o.append(f'<line x1="{X(s_["t"]):.1f}" y1="{LY[1] + 6}" x2="{X(f["t"]):.1f}" y2="{LY[2] - 6}" stroke="#94A3B8" stroke-width="1" stroke-dasharray="3 2"/>')
    used = set()
    for e in evs:
        if e["t"] > t1:
            continue
        x, y = X(e["t"]), LY[e["lane"]]
        kd = e["kind"]
        used.add((e["lane"], kd))
        if kd == "input":
            o.append(f'<polygon points="{x:.1f},{y + 3} {x - 5:.1f},{y + 12} {x + 5:.1f},{y + 12}" fill="#2F5BD3"/>')
        elif kd in ("fx", "ok"):
            c = "#15803D" if kd == "ok" else "#C2410C"
            o.append(f'<circle cx="{x:.1f}" cy="{y - (0 if kd == "ok" else 0)}" r="5" fill="{c}"/>')
        elif kd in ("ghost", "reject"):
            o.append(f'<g stroke="#B91C1C" stroke-width="2.4"><line x1="{x - 5:.1f}" y1="{y - 5}" x2="{x + 5:.1f}" y2="{y + 5}"/><line x1="{x - 5:.1f}" y1="{y + 5}" x2="{x + 5:.1f}" y2="{y - 5}"/></g>')
        elif kd == "rep":
            o.append(f'<rect x="{x - 4:.1f}" y="{y - 22}" width="8" height="8" transform="rotate(45 {x:.1f} {y - 18})" fill="#fff" stroke="#6B7280"/>')
            o.append(f'<text x="{x:.1f}" y="{y - 27}" text-anchor="middle" class="xs mono">{e["v"]}</text>')
        elif kd == "dry":
            o.append(f'<rect x="{x - 6:.1f}" y="{y - 6}" width="12" height="12" fill="#111827"/>')
            o.append(f'<text x="{x + 9:.1f}" y="{y + 20}" class="xs b">空仓</text>')
        elif kd == "bullet":
            o.append(f'<rect x="{x - 2:.1f}" y="{y + 14}" width="4" height="10" rx="1" fill="#0F766E"/>')
    # 图例
    y = LY[2] + 50
    leg = [("input", "按下（本机）", "#2F5BD3"), ("fx", "看到开火表现", "#C2410C"), ("ok", "服务器接受（扣弹）", "#15803D"),
           ("ghost", "空放：有表现、没子弹", "#B91C1C"), ("rep", "弹药同步到达（数字=弹数）", "#6B7280"), ("bullet", "子弹 Actor 出现在本机", "#0F766E"), ("dry", "空仓停火", "#111827")]
    kinds = {k for _, k in used}
    if "reject" in kinds:
        kinds.add("ghost")
    x = L
    for k, nm, c in leg:
        if k not in kinds:
            continue
        if k == "input":
            o.append(f'<polygon points="{x + 6},{y - 9} {x + 1},{y} {x + 11},{y}" fill="{c}"/>')
        elif k in ("fx", "ok"):
            o.append(f'<circle cx="{x + 6}" cy="{y - 4}" r="5" fill="{c}"/>')
        elif k == "ghost":
            o.append(f'<g stroke="{c}" stroke-width="2.2"><line x1="{x + 1}" y1="{y - 9}" x2="{x + 11}" y2="{y + 1}"/><line x1="{x + 1}" y1="{y + 1}" x2="{x + 11}" y2="{y - 9}"/></g>')
        elif k == "rep":
            o.append(f'<rect x="{x + 2}" y="{y - 8}" width="8" height="8" transform="rotate(45 {x + 6} {y - 4})" fill="#fff" stroke="{c}"/>')
        elif k == "bullet":
            o.append(f'<rect x="{x + 4}" y="{y - 10}" width="4" height="10" rx="1" fill="{c}"/>')
        elif k == "dry":
            o.append(f'<rect x="{x}" y="{y - 10}" width="11" height="11" fill="{c}"/>')
        o.append(f'<text x="{x + 16}" y="{y}" class="xs">{esc(nm)}</text>')
        x += 30 + 11.2 * len(nm)
        if x > W - 160:
            x = L; y += 18
    y += 12
    svg = f'<svg viewBox="0 0 {W} {y:.0f}" xmlns="http://www.w3.org/2000/svg">' + "".join(o) + "</svg>"
    return fig(svg, d.get("cap"))


# ------------------------------------------------------------------ plot：折线 / 阶梯
def plot(d):
    W, H = d.get("w", 860), d.get("h", 300)
    L, R, T, B = 64, 190, 32, 44
    (x0, x1), (y0, y1) = d["x"], d["y"]
    px = lambda x: L + (W - L - R) * (x - x0) / (x1 - x0)
    py = lambda v: T + (H - T - B) * (1 - (min(max(v, y0), y1) - y0) / (y1 - y0))
    o = [f'<line x1="{L}" y1="{H - B}" x2="{W - R}" y2="{H - B}" stroke="#9CA3AF"/>',
         f'<line x1="{L}" y1="{T}" x2="{L}" y2="{H - B}" stroke="#9CA3AF"/>']
    ny = d.get("yticks", 5)
    for k in range(ny + 1):
        v = y0 + (y1 - y0) * k / ny
        o.append(f'<line x1="{L}" y1="{py(v):.1f}" x2="{W - R}" y2="{py(v):.1f}" stroke="#EEF0F3"/>')
        o.append(f'<text x="{L - 8}" y="{py(v) + 4:.1f}" text-anchor="end" class="xs">{v:g}</text>')
    nx = d.get("xticks", 8)
    for k in range(nx + 1):
        x = x0 + (x1 - x0) * k / nx
        o.append(f'<text x="{px(x):.1f}" y="{H - B + 16}" text-anchor="middle" class="xs">{x:g}</text>')
    o.append(f'<text x="{(L + W - R) / 2}" y="{H - 8}" text-anchor="middle" class="sm">{esc(d.get("xlab", ""))}</text>')
    o.append(f'<text x="{L - 40}" y="{T - 16}" class="xs">{esc(d.get("ylab", ""))}</text>')
    for band in d.get("bands", []):  # [x0,x1,"文字"]
        o.append(f'<rect x="{px(band[0]):.1f}" y="{T}" width="{px(band[1]) - px(band[0]):.1f}" height="{H - T - B}" fill="#FEF6E4"/>')
        o.append(f'<text x="{(px(band[0]) + px(band[1])) / 2:.1f}" y="{T + 14}" text-anchor="middle" class="xs" style="fill:#B45309">{esc(band[2])}</text>')
    if d.get("marks"):
        for t in resolve(d["marks"]):
            if x0 <= t <= x1:
                o.append(f'<line x1="{px(t):.1f}" y1="{H - B}" x2="{px(t):.1f}" y2="{H - B - 8}" stroke="#2F5BD3" stroke-width="2"/>')
    ly = T + 10
    for lab, path, col, *rest in d["series"]:
        pts = resolve(path)
        c = COL[col][1]
        step = d.get("step", False)
        seq = []
        for i, (x, v) in enumerate(pts):
            if step and i:
                seq.append(f"{px(x):.1f},{py(pts[i - 1][1]):.1f}")
            seq.append(f"{px(x):.1f},{py(v):.1f}")
        if step and pts:
            seq.append(f"{px(x1):.1f},{py(pts[-1][1]):.1f}")
        dash = ' stroke-dasharray="5 3"' if rest and rest[0] == "dash" else ""
        o.append(f'<polyline points="{" ".join(seq)}" fill="none" stroke="{c}" stroke-width="2.2"{dash}/>')
        o.append(f'<line x1="{W - R + 12}" y1="{ly}" x2="{W - R + 34}" y2="{ly}" stroke="{c}" stroke-width="2.6"{dash}/>')
        o.append(f'<text x="{W - R + 40}" y="{ly + 4}" class="xs b" style="fill:{c}">{esc(lab)}</text>')
        ly += 20
    svg = f'<svg viewBox="0 0 {W} {H}" xmlns="http://www.w3.org/2000/svg">' + "".join(o) + "</svg>"
    return fig(svg, d.get("cap"))


# ------------------------------------------------------------------ dtab：数据表
def _get(it, k):
    return it[int(k)] if isinstance(it, list) else it[k]


def dtab_rows(d):
    src = resolve(d["src"])
    rows = []
    for it in src:
        if any(abs(float(_get(it, k)) - float(v)) > 1e-6 for k, v in d.get("where", {}).items()):
            continue
        row = []
        for col in d["cols"]:
            name, key, f = col[0], col[1], col[2] if len(col) > 2 else ""
            if key == "scheme":
                row.append(SCHEME[int(it["s"])])
            elif key.startswith("="):
                row.append(key[1:].format(*it) if isinstance(it, list) else key[1:].format(**it))
            elif key.startswith("map:"):
                _, k, *opts = key.split(":")
                row.append(opts[int(_get(it, k))])
            else:
                row.append(fmt(_get(it, key), f))
        rows.append(row)
    return [c[0] for c in d["cols"]], rows


def dtab(d):
    head, rows = dtab_rows(d)
    hl = d.get("hl", {})  # 列名 -> 阈值着色 {"col":["<",50]}
    out = ['<div class="tbl"><table>', "<tr>" + "".join(f"<th>{esc(h)}</th>" for h in head) + "</tr>"]
    for r in rows:
        tds = []
        for h, c in zip(head, r):
            cls = ""
            if h in hl:
                op, th = hl[h]
                try:
                    v = float(re.sub(r"[^\d.\-]", "", c))
                    bad = v < th if op == "<" else v > th
                    cls = ' class="no"' if bad else ' class="yes"'
                except ValueError:
                    pass
            tds.append(f"<td{cls}>{esc(c)}</td>")
        out.append("<tr>" + "".join(tds) + "</tr>")
    out.append("</table></div>")
    if d.get("cap"):
        out.append(f'<p class="tcap">{d["cap"]}</p>')
    return "\n".join(out)


def macro(m: re.Match) -> str:
    kind, body = m.group(1), m.group(2)
    try:
        d = json.loads(body)
    except json.JSONDecodeError as e:
        raise SystemExit(f"[{kind}] JSON 错误: {e}\n{body[:200]}")
    if kind in BM.MACROS:
        return BM.MACROS[kind](d)
    return {"bars": bars, "lanes": lanes, "plot": plot, "dtab": dtab}[kind](d)


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
        t = VAL_RE.sub(val_sub, t)
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
    left = re.findall(r"<!--(bars|lanes|plot|dtab|bpgraph|seq|tree|spans|bpnet|repmx|code)|\{\{v:", content)
    if left:
        print("未展开的宏:", left, file=sys.stderr)
    scripts = (ROOT / "labs.js").read_text(encoding="utf-8") if (ROOT / "labs.js").exists() else ""
    OUT.write_text(shell.replace("{{CONTENT}}", content).replace("{{SCRIPTS}}", scripts), encoding="utf-8")
    print(f"OK -> {OUT}  ({OUT.stat().st_size / 1024:.1f} KB, figure={content.count('<figure>')}, "
          f"svg={len(re.findall('<svg', content))}, bad_svg={bad}, dup_id={len(dup)}, lab={content.count('class=\"lab')})")
    return 1 if bad or left else 0


if __name__ == "__main__":
    sys.exit(main())
