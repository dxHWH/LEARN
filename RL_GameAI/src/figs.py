"""第 3 章起新增的图宏（build.py 展开为 SVG，build_md.py 转成表格 / 字符图）。
  plot   折线图：series 可以是表达式 f(x)（构建时求值）或数据路径 path（demos/*.json、rlcore 结果）
  gbars  分组柱状图
  graph  状态图 / backup 树：圆形状态、实心动作点、带概率标签的弯曲箭头、自环
  grid   网格世界：构建时真实运行 策略评估 / 价值迭代 / 策略迭代，画每一步的价值热力图 + 贪心箭头
数字一律来自计算，正文用 [[路径|格式]] 引用。
"""
import html
import math

import rlcore as C

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
            out.append(f"**{s.get('n', '')}**：" + "，".join(f"({x:g}, {y:.4g})" for x, y in pts))
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


# ============================================================ grid
def grid_panels(d):
    """返回 [(标题, Grid, V(list|None), 每格动作集合, 附加信息)]，并把统计量写入 VALS[id]。"""
    base = {k: d[k] for k in ("map", "gamma", "slip", "step", "goal", "trap", "mode", "tol", "pi0") if k in d}
    stats = {}
    panels = []
    cache = {}
    for pd in d["panels"]:
        cfg = dict(base, **pd)
        key = (cfg.get("mode"), cfg.get("slip", 0), cfg.get("gamma", 0.9))
        if key not in cache:
            gw = C.Grid(cfg["map"] if isinstance(cfg["map"], list) else getattr(C, cfg["map"]), gamma=cfg.get("gamma", 0.9),
                        slip=cfg.get("slip", 0.0), step=cfg.get("step", -1.0), goal=cfg.get("goal", 10.0), trap=cfg.get("trap", -10.0))
            m = gw.mdp
            tol = cfg.get("tol", 1e-10)
            mode = cfg["mode"]
            if mode == "eval":
                Vinf = C.eval_exact(m, C.uniform(m))
                _, k, hist = C.eval_iter(m, C.uniform(m), tol, keep=True)
                cache[key] = (gw, hist, Vinf, k, None)
            elif mode == "vi":
                Vinf, k, hist = C.value_iteration(m, tol, keep=True)
                cache[key] = (gw, hist, Vinf, k, None)
            elif mode == "pi":
                pi0 = cfg.get("pi0", 1)
                rounds = C.policy_iteration(m, [pi0] * m.n)
                cache[key] = (gw, None, rounds[-1][1], len(rounds), rounds)
        gw, hist, Vinf, k, rounds = cache[key]
        m = gw.mdp
        st = stats.setdefault(f"{cfg.get('mode')}_{str(cfg.get('slip', 0)).replace('.', '')}", {})
        st["n"] = k
        st["Vstart"] = Vinf[gw.start()]
        sh = pd.get("show", "inf")
        if cfg["mode"] == "pi":
            idx = len(rounds) - 1 if sh == "inf" else sh
            acts, V = rounds[idx]
            sets = [[acts[s]] if m.A[s] else [] for s in range(m.n)]
            title = pd.get("title", "第 {i} 轮：π{i}").format(i=idx, n=len(rounds), v=V[gw.start()])
        else:
            if sh == "inf":
                V = Vinf
                kk = "∞"
            else:
                V = hist[min(sh, len(hist) - 1)]
                kk = str(sh)
            sets = C.greedy_sets(m, V) if pd.get("arrows", d.get("arrows", True)) else [[] for _ in range(m.n)]
            title = pd.get("title", "k = {k}").format(k=kk, n=k, v=V[gw.start()])
        info = {}
        if pd.get("path"):
            path = [gw.start()]
            cur = path[0]
            for _ in range(200):
                if not m.A[cur]:
                    break
                r_, c_ = gw.cells[cur]
                cur = gw.move(r_, c_, sets[cur][0])
                if cur in path:
                    break
                path.append(cur)
            info["path"] = path
            st["pathlen"] = len(path) - 1
        panels.append((title, gw, V, sets, info))
    if d.get("id"):
        VALS[d["id"]] = stats
    return panels


def _vcolor(v, vmax):
    if vmax <= 0:
        return "#FFFFFF"
    t = max(-1.0, min(1.0, v / vmax))
    if t >= 0:   # 白 → 绿
        r, g, b = 255 - int(t * (255 - 134)), 255 - int(t * (255 - 213)), 255 - int(t * (255 - 160))
    else:        # 白 → 红
        t = -t
        r, g, b = 255 - int(t * (255 - 244)), 255 - int(t * (255 - 165)), 255 - int(t * (255 - 165))
    return f"#{r:02X}{g:02X}{b:02X}"


def grid(d):
    panels = grid_panels(d)
    cs = d.get("cell", 40)
    cols = d.get("cols", 2)
    gw0 = panels[0][1]
    pw, ph = gw0.W * cs, gw0.H * cs
    gapx, gapy, top = d.get("gapx", 40), 34, 22
    W = d.get("w", 860)
    left = (W - (cols * pw + (cols - 1) * gapx)) / 2
    rows = math.ceil(len(panels) / cols)
    H = rows * (ph + top + gapy) + 4
    vmax = max(abs(v) for _, g, V, _, _ in panels for s, v in enumerate(V) if g.mdp.A[s]) or 1
    vmax = d.get("vmax", vmax)
    fmt = d.get("fmt", ".1f")
    showv = d.get("values", True)
    out = []
    for i, (title, gw, V, sets, info) in enumerate(panels):
        ox = left + (i % cols) * (pw + gapx)
        oy = (i // cols) * (ph + top + gapy) + top
        out.append(f'<text x="{ox}" y="{oy - 7}" class="b sm">{esc(title)}</text>')
        for r in range(gw.H):
            for c in range(gw.W):
                x, y = ox + c * cs, oy + r * cs
                ch = gw.rows[r][c]
                if ch == "#":
                    out.append(f'<rect x="{x}" y="{y}" width="{cs}" height="{cs}" fill="#4B5563" stroke="#374151"/>')
                    continue
                s = gw.idx[(r, c)]
                if ch in "GXT":
                    fill, stroke, lab = {"G": ("#BBF7D0", "#15803D", "G"),
                                         "X": ("#FECACA", "#B91C1C", "陷阱"), "T": ("#E5E7EB", "#6B7280", "终")}[ch]
                    out.append(f'<rect x="{x}" y="{y}" width="{cs}" height="{cs}" fill="{fill}" stroke="{stroke}" stroke-width="1"/>')
                    if ch == "G":
                        out.append(f'<text x="{x + cs / 2}" y="{y + cs / 2 - 1}" text-anchor="middle" class="b" style="font-size:13px;fill:{stroke}">G</text>')
                        out.append(f'<text x="{x + cs / 2}" y="{y + cs / 2 + 11}" text-anchor="middle" class="mono" style="font-size:9.5px;fill:{stroke}">{d.get("goal", 10):+g}</text>')
                    else:
                        out.append(f'<text x="{x + cs / 2}" y="{y + cs / 2 + 4}" text-anchor="middle" class="b" style="font-size:{10 if cs < 38 else 11}px;fill:{stroke}">{esc(lab)}</text>')
                    continue
                out.append(f'<rect x="{x}" y="{y}" width="{cs}" height="{cs}" fill="{_vcolor(V[s], vmax)}" stroke="#CBD5E1" stroke-width="0.8"/>')
                if showv:
                    out.append(f'<text x="{x + cs / 2}" y="{y + 12}" text-anchor="middle" class="mono" style="font-size:{9.5 if cs < 44 else 10.5}px;fill:#111827">{format(V[s], fmt)}</text>')
                cx, cy = x + cs / 2, y + (cs * 0.62 if showv else cs / 2)
                L_ = cs * (0.24 if showv else 0.32)
                for a in ([] if d.get("hide_ties") and len(sets[s]) == 4 else sets[s]):
                    dr, dc, _ = C.DIRS[a]
                    ex, ey = cx + dc * L_, cy + dr * L_
                    out.append(f'<line x1="{cx:.1f}" y1="{cy:.1f}" x2="{ex - dc * 4:.1f}" y2="{ey - dr * 4:.1f}" stroke="#1E3A8A" stroke-width="1.6"/>')
                    out.append(arrow_head(ex, ey, dc, dr, 6, "#1E3A8A"))
                if ch == "S":
                    out.append(f'<text x="{x + 3}" y="{y + cs - 3}" class="b" style="font-size:9px;fill:#C2410C">S</text>')
        if info.get("path"):
            pts = [(ox + gw.cells[s][1] * cs + cs / 2, oy + gw.cells[s][0] * cs + cs / 2) for s in info["path"]]
            last = info["path"][-1]
            r_, c_ = gw.cells[last]
            if gw.mdp.A[last]:
                pass
            ps = " ".join(f"{x:.1f},{y:.1f}" for x, y in pts)
            out.append(f'<polyline points="{ps}" fill="none" stroke="#F97316" stroke-width="3" stroke-opacity=".55" stroke-linejoin="round"/>')
        out.append(f'<rect x="{ox}" y="{oy}" width="{pw}" height="{ph}" fill="none" stroke="#94A3B8" stroke-width="1.2"/>')
    return fig(svg_wrap(W, H, out), d.get("cap"))


def md_grid(d):
    panels = grid_panels(d)
    fmt = d.get("fmt", ".1f")
    out = ["```text"]
    for title, gw, V, sets, info in panels:
        out.append(title)
        onpath = set(info.get("path", []))
        for r in range(gw.H):
            cells = []
            for c in range(gw.W):
                ch = gw.rows[r][c]
                if ch == "#":
                    cells.append("   ████    ")
                    continue
                s = gw.idx[(r, c)]
                if ch in "GXT":
                    cells.append({"G": "    G      ", "X": "   陷阱    ", "T": "    终     "}[ch])
                    continue
                arr = "" if d.get("hide_ties") and len(sets[s]) == 4 else "".join(C.DIRS[a][2] for a in sets[s])
                mark = "*" if s in onpath else " "
                cells.append(f"{format(V[s], fmt):>6}{mark}{arr:<4}" if d.get("values", True) else f" {mark}{arr:<4} ")
            out.append(" ".join(cells))
        out.append("")
    out.append("```")
    return "\n".join(out)


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



# ============================================================ gpath：网格地图 + 多条（demo 实测的）路径
def _gp_rows(d):
    rows = res(d["rows"]) if isinstance(d["rows"], dict) else d["rows"]
    return [str(r) for r in rows]


def gpath(d):
    """rows：地图字符串（S 起点 / G 终点 / C 悬崖 / # 墙 / . 空地），可写数据路径 {"path": ...}；
    paths：[{"path": 数据路径（格子下标 r*W+c 的列表）, "n": 图例, "c": 颜色, "off": 像素偏移}]。"""
    rows = _gp_rows(d)
    H, Wc = len(rows), len(rows[0])
    cs = d.get("cell", 44)
    W = d.get("w", 860)
    leg_h = 26
    ox, oy = (W - Wc * cs) / 2, leg_h + 8
    out = []
    lab = {"S": ("#DBEAFE", "#1D4ED8", "S"), "G": ("#BBF7D0", "#15803D", "G"), "C": ("#FECACA", "#B91C1C", "悬崖"), "#": ("#4B5563", "#374151", "")}
    for r in range(H):
        for c in range(Wc):
            ch = rows[r][c]
            x, y = ox + c * cs, oy + r * cs
            fill, stroke, txt = lab.get(ch, ("#FFFFFF", "#CBD5E1", ""))
            out.append(f'<rect x="{x}" y="{y}" width="{cs}" height="{cs}" fill="{fill}" stroke="{stroke}" stroke-width="0.8"/>')
            if txt:
                out.append(f'<text x="{x + cs / 2}" y="{y + cs / 2 + 4}" text-anchor="middle" class="b" style="font-size:{11 if len(txt) > 1 else 13}px;fill:{stroke}">{esc(txt)}</text>')
    lx = ox
    for p_ in d.get("paths", []):
        col = COL[p_.get("c", "b")][1]
        pts = res({"path": p_["path"]}) if isinstance(p_["path"], str) else p_["path"]
        off = p_.get("off", 0)
        xy = [(ox + (s % Wc) * cs + cs / 2 + off, oy + (s // Wc) * cs + cs / 2 + off) for s in pts]
        dash = ' stroke-dasharray="7 4"' if p_.get("dash") else ""
        out.append(f'<polyline points="{" ".join(f"{x:.1f},{y:.1f}" for x, y in xy)}" fill="none" stroke="{col}" stroke-width="{p_.get("sw", 3)}" stroke-linejoin="round"{dash} opacity="0.9"/>')
        if len(xy) >= 2:
            (x0, y0), (x1, y1) = xy[-2], xy[-1]
            L = math.hypot(x1 - x0, y1 - y0) or 1
            out.append(arrow_head(x1, y1, (x1 - x0) / L, (y1 - y0) / L, 10, col))
        name = p_.get("n", "")
        out.append(f'<line x1="{lx}" y1="{leg_h / 2 + 4}" x2="{lx + 26}" y2="{leg_h / 2 + 4}" stroke="{col}" stroke-width="3"{dash}/>')
        out.append(f'<text x="{lx + 32}" y="{leg_h / 2 + 8}" class="sm" style="fill:{col}">{esc(name)}</text>')
        lx += 46 + len(name) * 12.5
    H_ = oy + H * cs + 8
    return fig(svg_wrap(W, H_, out), d.get("cap"))


def md_gpath(d):
    rows = _gp_rows(d)
    H, Wc = len(rows), len(rows[0])
    grid_ = [[{"S": "S", "G": "G", "C": "▒", "#": "█"}.get(ch, "·") for ch in r] for r in rows]
    legend = []
    for k, p_ in enumerate(d.get("paths", [])):
        mark = "①②③④⑤⑥"[k]
        pts = res({"path": p_["path"]}) if isinstance(p_["path"], str) else p_["path"]
        for s in pts:
            r, c = divmod(s, Wc)
            if grid_[r][c] == "·":
                grid_[r][c] = mark
        legend.append(f"{mark} {p_.get('n', '')}（{len(pts) - 1} 步）")
    body = "\n".join(" ".join(r) for r in grid_)
    return "```text\n" + body + "\n" + "；".join(legend) + "\n（S 起点 · G 终点 · ▒ 悬崖；多条路径重合的格子只标先出现的那条）\n```"


MACROS = {"plot": plot, "gbars": gbars, "graph": graph, "grid": grid, "dtab": dtab, "gpath": gpath}
MD = {"plot": md_plot, "gbars": md_gbars, "graph": md_graph, "grid": md_grid, "dtab": md_dtab, "gpath": md_gpath}
