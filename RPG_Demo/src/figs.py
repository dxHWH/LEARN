"""第 3 章起新增的图宏（build.py 展开为 SVG，build_md.py 转成表格 / 字符图）。
  plot   折线图：series 可以是表达式 f(x)（构建时求值）或数据路径 path（demos/*.json、rlcore 结果）
  gbars  分组柱状图
  graph  状态图 / backup 树：圆形状态、实心动作点、带概率标签的弯曲箭头、自环
数字一律来自计算，正文用 [[路径|格式]] 引用。
"""
import html
import math


VALS = {}      # 由 build.py 注入（同一个 dict）
WARN = []
_CLIP = [0]
COL = {
    "b": ("#EAF0FD", "#2F5BD3"), "o": ("#FFF4EC", "#C2410C"), "g": ("#EAF7EF", "#15803D"),
    "p": ("#F3EEFE", "#6D28D9"), "r": ("#FDEEEE", "#B91C1C"), "a": ("#FEF6E4", "#B45309"),
    "s": ("#F3F4F6", "#6B7280"), "t": ("#E6F6F5", "#0F766E"), "k": ("#E5E7EB", "#374151"),
}
SUP = str.maketrans("-0123456789", "⁻⁰¹²³⁴⁵⁶⁷⁸⁹")


def esc(s):
    return html.escape(str(s), quote=True)


def fig(svg, cap):
    c = f"<figcaption>{cap}</figcaption>" if cap else ""
    return f"<figure>\n{svg}\n{c}</figure>"


def svg_wrap(W, H, out):
    return f'<svg viewBox="0 0 {W} {H:.0f}" xmlns="http://www.w3.org/2000/svg">' + "".join(out) + "</svg>"


def get(path):
    """'ch03.mc.rms' / 'lane.Vu.0' → 值。"""
    parts = path.split(".")
    v = VALS[parts[0]]
    for p in parts[1:]:
        v = v[int(p)] if isinstance(v, list) else v[p]
    return v


def res(x):
    return get(x["path"]) if isinstance(x, dict) and "path" in x else x


def fmtv(v, f=None):
    if f:
        return format(v, f)
    if isinstance(v, float):
        return f"{v:.4g}"
    return str(v)


def arrow_head(x, y, ux, uy, size, color):
    """在 (x,y) 画朝向 (ux,uy) 的实心三角（不用 marker，避免随线宽缩放）。"""
    px, py = -uy, ux
    a = (x, y)
    b = (x - ux * size + px * size * 0.5, y - uy * size + py * size * 0.5)
    c = (x - ux * size - px * size * 0.5, y - uy * size - py * size * 0.5)
    return f'<path d="M{a[0]:.1f},{a[1]:.1f} L{b[0]:.1f},{b[1]:.1f} L{c[0]:.1f},{c[1]:.1f} z" fill="{color}"/>'


# ============================================================ plot
def _ticks(lo, hi, n=6):
    span = hi - lo
    raw = span / n
    mag = 10 ** math.floor(math.log10(raw))
    step = min((m * mag for m in (1, 2, 2.5, 5, 10) if m * mag >= raw), default=mag * 10)
    t = math.ceil(lo / step - 1e-9) * step
    out = []
    while t <= hi + 1e-9:
        out.append(round(t, 10))
        t += step
    return out


def plot_series(d):
    env = {"math": math, "V": get, **{k: getattr(math, k) for k in dir(math) if not k.startswith("_")}}
    x0, x1 = d["x"]
    out = []
    for s in d["series"]:
        if "f" in s:
            if s.get("int"):
                xs = list(range(int(math.ceil(x0)), int(x1) + 1, s.get("step", 1)))
            elif d.get("logx"):
                xs = [10 ** (math.log10(x0) + (math.log10(x1) - math.log10(x0)) * i / 160) for i in range(161)]
            else:
                xs = [x0 + (x1 - x0) * i / 200 for i in range(201)]
            pts = [(x, eval(s["f"], env, {"x": x})) for x in xs]
        else:
            pts = [tuple(p) for p in res(s["path"] if "path" in s else s["pts"])] if "pts" in s else [tuple(p) for p in get(s["path"])]
        out.append((s, pts))
    return out


def plot(d):
    W, H = d.get("w", 860), d.get("h", 300)
    L, R, T, B = d.get("ml", 64), 24, 26, 46
    ser = plot_series(d)
    x0, x1 = d["x"]
    ally = [y for _, pts in ser for _, y in pts if y is not None and math.isfinite(y)]
    y0, y1 = d.get("y", [min(ally), max(ally)])
    lx, ly = d.get("logx"), d.get("logy")
    tx = (lambda v: math.log10(v)) if lx else (lambda v: v)
    ty = (lambda v: math.log10(max(v, 1e-300))) if ly else (lambda v: v)
    X = lambda v: L + (W - L - R) * (tx(v) - tx(x0)) / (tx(x1) - tx(x0))
    Y = lambda v: T + (H - T - B) * (ty(y1) - ty(v)) / (ty(y1) - ty(y0))
    out = [f'<rect x="{L}" y="{T}" width="{W - L - R}" height="{H - T - B}" fill="#FFFFFF" stroke="#CBD5E1"/>']
    if d.get("title"):
        out.append(f'<text x="{L}" y="16" class="b sm">{esc(d["title"])}</text>')
    # 刻度
    if ly:
        yt = [10 ** e for e in range(math.ceil(math.log10(y0) - 1e-9), math.floor(math.log10(y1) + 1e-9) + 1)]
        ylab = lambda v: "10" + str(round(math.log10(v))).translate(SUP)
    else:
        yt = d.get("yt") or _ticks(y0, y1)
        ylab = lambda v: f"{v:g}"
    for v in yt:
        out.append(f'<line x1="{L}" y1="{Y(v):.1f}" x2="{W - R}" y2="{Y(v):.1f}" stroke="#EEF2F6"/>')
        out.append(f'<text x="{L - 7}" y="{Y(v) + 4:.1f}" text-anchor="end" class="xs mono">{ylab(v)}</text>')
    if lx:
        xt = [10 ** e for e in range(math.ceil(math.log10(x0) - 1e-9), math.floor(math.log10(x1) + 1e-9) + 1)]
        xlab = lambda v: "10" + str(round(math.log10(v))).translate(SUP)
    else:
        xt = d.get("xt") or _ticks(x0, x1, 8)
        xlab = lambda v: f"{v:g}"
    for v in xt:
        out.append(f'<text x="{X(v):.1f}" y="{H - B + 16}" text-anchor="middle" class="xs mono">{xlab(v)}</text>')
    if d.get("xlabel"):
        out.append(f'<text x="{(L + W - R) / 2}" y="{H - 8}" text-anchor="middle" class="sm">{esc(d["xlabel"])}</text>')
    if d.get("ylabel"):
        out.append(f'<text x="14" y="{(T + H - B) / 2}" text-anchor="middle" class="sm" transform="rotate(-90 14 {(T + H - B) / 2})">{esc(d["ylabel"])}</text>')
    for v, lab, c, *lyv in d.get("vlines", []):   # 可选第 4 项：标签放在数据坐标 y 处（避免与图例 / 相邻标签重叠）
        col = COL[c][1]
        out.append(f'<line x1="{X(v):.1f}" y1="{T}" x2="{X(v):.1f}" y2="{H - B}" stroke="{col}" stroke-dasharray="4 4"/>')
        if lab:
            out.append(f'<text x="{X(v) + 4:.1f}" y="{Y(lyv[0]) if lyv else T + 14:.1f}" class="xs b" style="fill:{col}">{esc(lab)}</text>')
    for v, lab, c in d.get("hlines", []):
        col = COL[c][1]
        out.append(f'<line x1="{L}" y1="{Y(v):.1f}" x2="{W - R}" y2="{Y(v):.1f}" stroke="{col}" stroke-dasharray="4 4"/>')
        out.append(f'<text x="{W - R - 4}" y="{Y(v) - 4:.1f}" text-anchor="end" class="xs b" style="fill:{col}">{esc(lab)}</text>')
    # 曲线（裁剪到绘图区）
    _CLIP[0] += 1
    cid = f"plotclip{_CLIP[0]}"
    out.append(f'<clipPath id="{cid}"><rect x="{L}" y="{T}" width="{W - L - R}" height="{H - T - B}"/></clipPath>')
    for s, pts in ser:
        col = COL[s.get("c", "b")][1]
        good = [(x, y) for x, y in pts if y is not None and math.isfinite(y) and (not ly or y > 0)]
        dash = ' stroke-dasharray="7 5"' if s.get("dash") else ""
        if not s.get("only_dots"):
            ps = " ".join(f"{X(x):.1f},{Y(y):.1f}" for x, y in good)
            out.append(f'<polyline clip-path="url(#{cid})" points="{ps}" fill="none" stroke="{col}" stroke-width="{s.get("sw", 2)}"{dash}/>')
        if s.get("dots") or s.get("only_dots"):
            for x, y in good:
                out.append(f'<circle cx="{X(x):.1f}" cy="{Y(y):.1f}" r="3" fill="{col}"/>')
        if s.get("end"):   # 曲线末端标签
            x, y = good[-1]
            out.append(f'<text x="{X(x) - 4:.1f}" y="{Y(y) - 6:.1f}" text-anchor="end" class="xs b" style="fill:{col}">{esc(s["n"])}</text>')
    # 图例
    leg = [s for s, _ in ser if s.get("n") and not s.get("end") and not s.get("noleg")]
    if leg:
        lw = max(len(s["n"]) for s in leg) * 11 + 56
        pos = d.get("legend", "tr")
        lx_ = W - R - lw - 8 if "r" in pos else L + 10
        ly_ = T + 10 if "t" in pos else H - B - 10 - 18 * len(leg)
        out.append(f'<rect x="{lx_}" y="{ly_}" width="{lw}" height="{18 * len(leg) + 8}" rx="6" fill="#FFFFFF" stroke="#E5E7EB"/>')
        for i, s in enumerate(leg):
            col = COL[s.get("c", "b")][1]
            yy = ly_ + 16 + 18 * i
            dash = ' stroke-dasharray="6 4"' if s.get("dash") else ""
            if s.get("only_dots"):
                out.append(f'<circle cx="{lx_ + 22}" cy="{yy - 4}" r="3.2" fill="{col}"/>')
            else:
                out.append(f'<line x1="{lx_ + 10}" y1="{yy - 4}" x2="{lx_ + 36}" y2="{yy - 4}" stroke="{col}" stroke-width="2.2"{dash}/>')
            out.append(f'<text x="{lx_ + 44}" y="{yy}" class="xs">{esc(s["n"])}</text>')
    return fig(svg_wrap(W, H, out), d.get("cap"))


def md_plot(d):
    ser = plot_series(d)
    if d.get("mdx"):
        xs = d["mdx"]
        env = {"math": math, "V": get, **{k: getattr(math, k) for k in dir(math) if not k.startswith("_")}}
        head = "| x | " + " | ".join(s["n"] for s, _ in ser) + " |"
        rows = []
        for x in xs:
            cells = []
            for s, pts in ser:
                if "f" in s:
                    cells.append(f"{eval(s['f'], env, {'x': x}):.4g}")
                else:
                    m = [y for xx, y in pts if abs(xx - x) < 1e-9]
                    cells.append(("发散" if m[0] >= 99 else f"{m[0]:.4g}") if m else "")
            rows.append(f"| {x:g} | " + " | ".join(cells) + " |")
        return "\n".join([head, "|" + "---|" * (len(ser) + 1)] + rows)
    out = []
    for s, pts in ser:
        if "f" not in s:
            note = ""
            if len(pts) > 12:   # 长序列等间隔抽 10 个点，md 只给趋势，完整曲线见电子书
                idx = sorted({round(i * (len(pts) - 1) / 9) for i in range(10)})
                note = f"（共 {len(pts)} 点，等间隔抽 10 个）"
                pts = [pts[i] for i in idx]
            out.append(f"**{s.get('n', '')}**{note}：" + "，".join(f"({x:g}, {y:.4g})" for x, y in pts))
    return "\n\n".join(out)


# ============================================================ gbars
def gbars(d):
    groups = res(d["groups"])
    series = [(s["n"], res(s["v"]), s.get("c", "b")) for s in d["series"]]
    hl = res(d.get("hl", -1))
    W, H = d.get("w", 860), d.get("h", 300)
    L, R, T, B = 52, 16, 30, 56
    allv = [v for _, vs, _ in series for v in vs]
    vmin, vmax = min(0.0, min(allv)), d.get("ymax", max(allv) * 1.12)
    Y = lambda v: T + (H - T - B) * (vmax - v) / (vmax - vmin)
    out = [f'<rect x="{L}" y="{T}" width="{W - L - R}" height="{H - T - B}" fill="#FFFFFF" stroke="#CBD5E1"/>']
    for v in _ticks(vmin, vmax, 5):
        out.append(f'<line x1="{L}" y1="{Y(v):.1f}" x2="{W - R}" y2="{Y(v):.1f}" stroke="#EEF2F6"/>')
        out.append(f'<text x="{L - 6}" y="{Y(v) + 4:.1f}" text-anchor="end" class="xs mono">{v:g}</text>')
    gw = (W - L - R) / len(groups)
    bw = gw * 0.78 / len(series)
    fmt = d.get("fmt", ".1f")
    for g, name in enumerate(groups):
        gx = L + g * gw
        if g == hl:
            out.append(f'<rect x="{gx + 2:.1f}" y="{T + 2}" width="{gw - 4:.1f}" height="{H - T - B - 4}" rx="6" fill="#FEF3C7" stroke="#F59E0B" stroke-dasharray="4 3"/>')
        for k, (sn, vs, c) in enumerate(series):
            v = vs[g]
            x = gx + gw * 0.11 + k * bw
            y0, y1 = sorted((Y(0), Y(v)))
            out.append(f'<rect x="{x:.1f}" y="{y0:.1f}" width="{bw - 2:.1f}" height="{max(1, y1 - y0):.1f}" rx="2" fill="{COL[c][0]}" stroke="{COL[c][1]}" stroke-width="1.2"/>')
            out.append(f'<text x="{x + bw / 2 - 1:.1f}" y="{(Y(v) - 4) if v >= 0 else (Y(v) + 11):.1f}" text-anchor="middle" class="mono" style="font-size:9px;fill:{COL[c][1]}">{format(v, fmt)}</text>')
        lines = str(name).split("\n")
        for i, ln in enumerate(lines):
            out.append(f'<text x="{gx + gw / 2:.1f}" y="{H - B + 16 + i * 13}" text-anchor="middle" class="xs{" b" if g == hl else ""}">{esc(ln)}</text>')
    lx = L + 10
    for k, (sn, _, c) in enumerate(series):
        out.append(f'<rect x="{lx}" y="{T + 8}" width="12" height="12" rx="2" fill="{COL[c][0]}" stroke="{COL[c][1]}"/>')
        out.append(f'<text x="{lx + 17}" y="{T + 18}" class="xs">{esc(sn)}</text>')
        lx += len(sn) * 11 + 40
    if d.get("title"):
        out.append(f'<text x="{L}" y="18" class="b sm">{esc(d["title"])}</text>')
    return fig(svg_wrap(W, H, out), d.get("cap"))


def md_gbars(d):
    groups = res(d["groups"])
    series = [(s["n"], res(s["v"])) for s in d["series"]]
    hl = res(d.get("hl", -1))
    fmt = d.get("fmt", ".1f")
    head = "| " + d.get("gname", "组") + " | " + " | ".join(n for n, _ in series) + " |"
    rows = [f"| {'**' + str(g).replace(chr(10), ' ') + '** ★' if i == hl else str(g).replace(chr(10), ' ')} | " + " | ".join(format(vs[i], fmt) for _, vs in series) + " |"
            for i, g in enumerate(groups)]
    return "\n".join([head, "|" + "---|" * (len(series) + 1)] + rows)


# ============================================================ graph
def graph(d):
    W, H = d.get("w", 860), d["h"]
    nodes = {n["id"]: n for n in d["nodes"]}
    out = []
    for r in d.get("rects", []):
        c = COL[r.get("c", "s")]
        out.append(f'<rect x="{r["x"]}" y="{r["y"]}" width="{r["w"]}" height="{r["h"]}" rx="{r.get("rx", 10)}" fill="{r.get("fill", c[0])}" stroke="{c[1]}" stroke-dasharray="{r.get("dash", "0")}"/>')

    def rad(n):
        k = n.get("k", "state")
        return n.get("r", 6 if k == "act" else 26) if k != "box" else max(n.get("w", 90), n.get("h", 40)) / 2

    def boundary(n, ux, uy):
        if n.get("k") == "box":
            hw, hh = n.get("w", 90) / 2, n.get("h", 40) / 2
            t = min(hw / abs(ux) if ux else 1e9, hh / abs(uy) if uy else 1e9)
            return n["x"] + ux * t, n["y"] + uy * t
        r = rad(n)
        return n["x"] + ux * r, n["y"] + uy * r

    labels = []
    for e in d.get("edges", []):
        a, b = nodes[e["f"]], nodes[e["t"]]
        c = COL[e.get("c", "k")][1]
        sw = e.get("sw", 1.5)
        dash = ' stroke-dasharray="5 4"' if e.get("dash") else ""
        if e["f"] == e["t"]:
            th = {"top": -90, "bottom": 90, "left": 180, "right": 0}[e.get("loop", "top")]
            r = rad(a)
            P = lambda ang, rr: (a["x"] + rr * math.cos(math.radians(ang)), a["y"] + rr * math.sin(math.radians(ang)))
            s0, s1 = P(th - 28, r), P(th + 28, r + 2)
            c1, c2 = P(th - 42, r + 44), P(th + 42, r + 44)
            out.append(f'<path d="M{s0[0]:.1f},{s0[1]:.1f} C{c1[0]:.1f},{c1[1]:.1f} {c2[0]:.1f},{c2[1]:.1f} {s1[0]:.1f},{s1[1]:.1f}" fill="none" stroke="{c}" stroke-width="{sw}"{dash}/>')
            ux, uy = s1[0] - c2[0], s1[1] - c2[1]
            ln = math.hypot(ux, uy)
            out.append(arrow_head(s1[0], s1[1], ux / ln, uy / ln, 8, c))
            lp = P(th, r + 40)
            if e.get("l"):
                labels.append((lp[0] + e.get("dx", 0), lp[1] + 4 + e.get("dy", 0), e["l"], c, e.get("lcls", "xs b")))
            continue
        mx, my = (a["x"] + b["x"]) / 2, (a["y"] + b["y"]) / 2
        dx, dy = b["x"] - a["x"], b["y"] - a["y"]
        ln = math.hypot(dx, dy)
        nx, ny = -dy / ln, dx / ln
        bend = e.get("bend", 0)
        cx, cy = mx + nx * bend, my + ny * bend
        u = (cx - a["x"], cy - a["y"]); lu = math.hypot(*u)
        p0 = boundary(a, u[0] / lu, u[1] / lu)
        v = (cx - b["x"], cy - b["y"]); lv = math.hypot(*v)
        gap = 2 if e.get("arrow", True) else 0
        p2 = boundary(b, v[0] / lv, v[1] / lv)
        p2 = (p2[0] + v[0] / lv * gap, p2[1] + v[1] / lv * gap)
        out.append(f'<path d="M{p0[0]:.1f},{p0[1]:.1f} Q{cx:.1f},{cy:.1f} {p2[0]:.1f},{p2[1]:.1f}" fill="none" stroke="{c}" stroke-width="{sw}"{dash}/>')
        if e.get("arrow", True):
            tx, ty = p2[0] - cx, p2[1] - cy
            lt = math.hypot(tx, ty)
            out.append(arrow_head(p2[0], p2[1], tx / lt, ty / lt, 8, c))
        if e.get("l"):
            t = e.get("lt", 0.5)
            bx = (1 - t) ** 2 * p0[0] + 2 * (1 - t) * t * cx + t * t * p2[0]
            by = (1 - t) ** 2 * p0[1] + 2 * (1 - t) * t * cy + t * t * p2[1]
            labels.append((bx + e.get("dx", 0), by + 4 + e.get("dy", 0), e["l"], c, e.get("lcls", "xs b")))
    for n in d["nodes"]:
        k = n.get("k", "state")
        c = COL[n.get("c", "b")]
        if k == "act":
            out.append(f'<circle cx="{n["x"]}" cy="{n["y"]}" r="{rad(n)}" fill="{c[1]}"/>')
        elif k == "box":
            w, h = n.get("w", 90), n.get("h", 40)
            out.append(f'<rect x="{n["x"] - w / 2}" y="{n["y"] - h / 2}" width="{w}" height="{h}" rx="8" fill="{c[0]}" stroke="{c[1]}" stroke-width="1.5"/>')
        else:
            dbl = n.get("term")
            out.append(f'<circle cx="{n["x"]}" cy="{n["y"]}" r="{rad(n)}" fill="{n.get("fill", c[0])}" stroke="{c[1]}" stroke-width="{2 if n.get("hl") else 1.5}"/>')
            if dbl:
                out.append(f'<circle cx="{n["x"]}" cy="{n["y"]}" r="{rad(n) - 4}" fill="none" stroke="{c[1]}" stroke-width="1"/>')
        if n.get("t") and k != "act":
            dy = -3 if n.get("s") else 4.5
            out.append(f'<text x="{n["x"]}" y="{n["y"] + dy}" text-anchor="middle" class="b" style="font-size:{n.get("ts", 13)}px">{esc(n["t"])}</text>')
            if n.get("s"):
                out.append(f'<text x="{n["x"]}" y="{n["y"] + 12}" text-anchor="middle" class="xs">{esc(n["s"])}</text>')
        if n.get("lab"):
            pos = n.get("lp", "r")
            off = rad(n) + 6
            lx, ly, an = {"r": (n["x"] + off, n["y"] + 4, "start"), "l": (n["x"] - off, n["y"] + 4, "end"),
                          "t": (n["x"], n["y"] - off - 2, "middle"), "b": (n["x"], n["y"] + off + 11, "middle")}[pos]
            out.append(f'<text x="{lx}" y="{ly}" text-anchor="{an}" class="{n.get("lcls", "xs")}" style="fill:{n.get("lfill", "#374151")}">{esc(n["lab"])}</text>')
    for x, y, t, c, cls in labels:
        out.append(f'<text x="{x:.1f}" y="{y:.1f}" text-anchor="middle" class="{cls}" style="fill:{c}">{esc(t)}</text>')
    for t in d.get("texts", []):
        style = f' style="fill:{t["fill"]}"' if t.get("fill") else ""
        out.append(f'<text x="{t["x"]}" y="{t["y"]}" text-anchor="{t.get("a", "start")}" class="{t.get("cls", "sm")}"{style}>{esc(t["t"])}</text>')
    return fig(svg_wrap(W, H, out), d.get("cap"))


def md_graph(d):
    nodes = {n["id"]: n for n in d["nodes"]}
    name = lambda i: nodes[i].get("t") or nodes[i].get("lab") or i
    return "\n".join(f"- {name(e['f'])} → {name(e['t'])}" + (f"：{e['l']}" if e.get("l") else "") for e in d.get("edges", []))


# ============================================================ dtab：数据驱动表格（行来自 VALS 里的 list[dict]）
def _cell(r, k, f):
    if k.startswith("="):
        v = eval(k[1:], {"math": math}, dict(r))
    else:
        v = r
        for p in k.split("."):
            v = v[int(p)] if isinstance(v, list) else v[p]
    return fmtv(v, f) if not isinstance(v, str) else v


def dtab_rows(d):
    rows = get(d["rows"])
    hl = [i for i, r in enumerate(rows) if d.get("hlif") and eval(d["hlif"], {"math": math}, dict(r, i=i))]
    out = [[_cell(r, c[1], c[2] if len(c) > 2 else None) for c in d["cols"]] for r in rows]
    return [c[0] for c in d["cols"]], out, hl


def dtab(d):
    head, rows, hl = dtab_rows(d)
    h = "<tr>" + "".join(f"<th>{x}</th>" for x in head) + "</tr>"
    b = "".join(f'<tr{" class=\"hl\"" if i in hl else ""}>' + "".join(f"<td>{esc(x)}</td>" for x in r) + "</tr>" for i, r in enumerate(rows))
    cap = f'<p class="tcap">{d["cap"]}</p>' if d.get("cap") else ""
    return f'<div class="tbl"><table>{h}{b}</table></div>{cap}'


def md_dtab(d):
    head, rows, hl = dtab_rows(d)
    lines = ["| " + " | ".join(head) + " |", "|" + "---|" * len(head)]
    for i, r in enumerate(rows):
        lines.append("| " + " | ".join((f"**{x}**" if i in hl else x).replace("|", "\\|") for x in r) + " |")
    return "\n".join(lines)


MACROS = {"plot": plot, "gbars": gbars, "graph": graph, "dtab": dtab}
MD = {"plot": md_plot, "gbars": md_gbars, "graph": md_graph, "dtab": md_dtab}
