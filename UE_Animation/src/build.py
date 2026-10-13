"""拼装《UE 动画系统：从骨骼到动画蓝图》电子书：src/chapters/*.html -> ../UE_Animation.html

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
OUT = ROOT.parent / "UE_Animation.html"

CODE_RE = re.compile(r"<!--code:(\w+):(.*?)-->\n?(.*?)\n?<!--/code-->", re.S)
MACRO_RE = re.compile(r"<!--(trace|bars|lines|grid|bspace|sync|curves|dials)\s+(\{.*?\})\s*-->", re.S)

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



# ============================================================ grid 宏：构建时真跑一遍搜索，渲染第 k 步的状态
#  <!--grid {"map":["S..#..","...#.G"], "diag":true, "algo":"astar|dijkstra|greedy|bfs|wastar|jps", "h":"octile",
#            "w":2, "panels":[{"steps":3,"title":"第 3 步"},{"steps":-1,"title":"完成：展开 {exp}，代价 {cost}"}],
#            "nums":"fgh|g|", "arrows":true, "cell":48, "per":2, "cap":"..."} -->
#  代价：直走 10、斜走 14；平局按 (f, h, 入队顺序) 决胜，结果确定
import heapq

DIRS8 = [(1, 0), (-1, 0), (0, 1), (0, -1), (1, 1), (1, -1), (-1, 1), (-1, -1)]


def _hfun(name, x, y, gx, gy):
    dx, dy = abs(x - gx), abs(y - gy)
    mx, mn = max(dx, dy), min(dx, dy)
    return {"zero": 0, "manhattan": 10 * (dx + dy), "octile": 10 * mx + 4 * mn,
            "euclid": round(10 * math.hypot(dx, dy)), "chebyshev": 10 * mx}[name]


def grid_search(rows, algo="astar", h="octile", w=1.0, diag=True, steps=-1, tie="small_h"):
    H, W = len(rows), len(rows[0])
    walk = lambda x, y: 0 <= x < W and 0 <= y < H and rows[y][x] != "#"
    S = G = None
    for y, r in enumerate(rows):
        for x, ch in enumerate(r):
            if ch == "S": S = (x, y)
            if ch == "G": G = (x, y)
    def can(x, y, dx, dy):
        if not walk(x + dx, y + dy): return False
        if dx and dy: return diag and walk(x + dx, y) and walk(x, y + dy)
        return True
    hv = lambda c: 0 if algo in ("dijkstra", "bfs") else _hfun(h, c[0], c[1], G[0], G[1])
    def fv(g, hh):
        if algo == "greedy": return hh
        if algo == "wastar": return g + round(w * hh)
        return g + hh
    g = {S: 0}; par = {S: None}; closed = []; cset = set(); seq = 0
    tk = (lambda hh: hh) if tie == "small_h" else (lambda hh: -hh)
    heap = [(fv(0, hv(S)), tk(hv(S)), 0, S)] if algo != "bfs" else None
    fifo = [S] if algo == "bfs" else None; fi = 0
    cur = None; found = False; exp = 0

    def jump(x, y, dx, dy):
        while True:
            if not walk(x, y): return None
            if (x, y) == G: return (x, y)
            if dx and dy:
                if jump(x + dx, y, dx, 0) or jump(x, y + dy, 0, dy): return (x, y)
                if not (walk(x + dx, y) and walk(x, y + dy)): return None
            elif dx:
                if (walk(x, y - 1) and not walk(x - dx, y - 1)) or (walk(x, y + 1) and not walk(x - dx, y + 1)): return (x, y)
            else:
                if (walk(x - 1, y) and not walk(x - 1, y - dy)) or (walk(x + 1, y) and not walk(x + 1, y - dy)): return (x, y)
            x += dx; y += dy

    while steps < 0 or exp < steps:
        if algo == "bfs":
            if fi >= len(fifo): break
            c = fifo[fi]; fi += 1
        else:
            if not heap: break
            _, _, _, c = heapq.heappop(heap)
            if c in cset: continue
        cset.add(c); closed.append(c); cur = c; exp += 1
        if c == G: found = True; break
        x, y = c
        if algo == "jps":
            succ = []
            pp = par[c]
            if pp is None:
                succ = [(dx, dy) for dx, dy in DIRS8 if can(x, y, dx, dy)]
            else:
                dx = (x > pp[0]) - (x < pp[0]); dy = (y > pp[1]) - (y < pp[1])
                if dx and dy:
                    v, hz = walk(x, y + dy), walk(x + dx, y)
                    if v: succ.append((0, dy))
                    if hz: succ.append((dx, 0))
                    if v and hz: succ.append((dx, dy))
                elif dx:
                    nx, up, dn = walk(x + dx, y), walk(x, y + 1), walk(x, y - 1)
                    if nx:
                        succ.append((dx, 0))
                        if up: succ.append((dx, 1))
                        if dn: succ.append((dx, -1))
                    if up: succ.append((0, 1))
                    if dn: succ.append((0, -1))
                else:
                    nx, rt, lf = walk(x, y + dy), walk(x + 1, y), walk(x - 1, y)
                    if nx:
                        succ.append((0, dy))
                        if rt: succ.append((1, dy))
                        if lf: succ.append((-1, dy))
                    if rt: succ.append((1, 0))
                    if lf: succ.append((-1, 0))
            for dx, dy in succ:
                j = jump(x + dx, y + dy, dx, dy)
                if j is None or j in cset: continue
                ddx, ddy = abs(j[0] - x), abs(j[1] - y)
                ng = g[c] + 10 * max(ddx, ddy) + 4 * min(ddx, ddy)
                if j in g and ng >= g[j]: continue
                g[j] = ng; par[j] = c; seq += 1
                heapq.heappush(heap, (ng + hv(j), tk(hv(j)), seq, j))
            continue
        for dx, dy in DIRS8:
            if not can(x, y, dx, dy): continue
            j = (x + dx, y + dy)
            if algo == "bfs":
                if j in g: continue
                g[j] = g[c] + (14 if dx and dy else 10); par[j] = c; fifo.append(j); continue
            if j in cset: continue
            ng = g[c] + (14 if dx and dy else 10)
            if j in g and ng >= g[j]: continue
            g[j] = ng; par[j] = c; seq += 1
            heapq.heappush(heap, (fv(ng, hv(j)), tk(hv(j)), seq, j))
    openset = [c for c in g if c not in cset]
    path = []
    if found:
        c = G
        while c is not None: path.append(c); c = par[c]
        path.reverse()
    return dict(W=W, H=H, S=S, G=G, g=g, par=par, closed=cset, open=openset, cur=cur, found=found, exp=exp,
                path=path, hv=hv, fv=fv, cost=g.get(G) if found else None)


def grid_smooth(rows, path):
    """拉绳：从当前锚点出发，贪心找最远的视线可达路点（与 demo 的 los 一致：经过格角时两侧都要可走）"""
    H, W = len(rows), len(rows[0])
    walk = lambda x, y: 0 <= x < W and 0 <= y < H and rows[y][x] != "#"
    def los(x0, y0, x1, y1):
        dx, dy = abs(x1 - x0), abs(y1 - y0)
        sx, sy = (1 if x1 > x0 else -1), (1 if y1 > y0 else -1)
        x, y, n, err = x0, y0, 1 + dx + dy, dx - dy
        dx *= 2; dy *= 2
        while n > 0:
            if not walk(x, y): return False
            if err > 0: x += sx; err -= dy; n -= 1
            elif err < 0: y += sy; err += dx; n -= 1
            elif n >= 3:
                if not walk(x + sx, y) or not walk(x, y + sy): return False
                x += sx; y += sy; err += dx - dy; n -= 2
            else: y += sy; err += dx; n -= 1
        return True
    out = [path[0]]; a = 0
    while a + 1 < len(path):
        b = a + 1
        while b + 1 < len(path) and los(*path[a], *path[b + 1]): b += 1
        out.append(path[b]); a = b
    return out


def grid(d):
    rows = d["map"]
    panels = d.get("panels", [{"steps": -1}])
    per = d.get("per", min(2, len(panels)))
    Wt = d.get("w_px", 860)
    st0 = grid_search(rows, d.get("algo", "astar"), d.get("h", "octile"), d.get("w", 1.0), d.get("diag", True), -1)
    Wc, Hc = st0["W"], st0["H"]
    gap = 26
    C = d.get("cell", 46)
    C = min(C, (Wt - (per - 1) * gap) / (per * Wc))
    pw = C * Wc
    totalw = per * pw + (per - 1) * gap
    ox0 = (Wt - totalw) / 2
    nums = d.get("nums", "fgh" if C >= 44 else "")
    arrows = d.get("arrows", C >= 30)
    out, used = [], set()
    rowsN = (len(panels) + per - 1) // per
    titleH = 26
    ph = titleH + C * Hc + 18
    for k, pn in enumerate(panels):
        algo = pn.get("algo", d.get("algo", "astar"))
        st = grid_search(pn.get("map", rows), algo, pn.get("h", d.get("h", "octile")), pn.get("w", d.get("w", 1.0)),
                         pn.get("diag", d.get("diag", True)), pn.get("steps", -1), pn.get("tie", d.get("tie", "small_h")))
        px = ox0 + (k % per) * (pw + gap)
        py = 6 + (k // per) * ph
        title = pn.get("title", "")
        title = (title.replace("{exp}", str(st["exp"])).replace("{cost}", str(st["cost"]) if st["cost"] is not None else "—")
                 .replace("{open}", str(len(st["open"]))))
        if st["found"]:
            pth = st["path"]
            turns = sum(1 for k in range(2, len(pth)) if (pth[k][0] - pth[k - 1][0], pth[k][1] - pth[k - 1][1]) != (pth[k - 1][0] - pth[k - 2][0], pth[k - 1][1] - pth[k - 2][1]))
            title = title.replace("{turns}", str(turns + 2)).replace("{len}", f"{st['cost'] / 10:.1f}")
        title_idx = len(out)
        out.append(f'<text x="{px + pw / 2:.1f}" y="{py + 17}" text-anchor="middle" class="b sm">{esc(title)}</text>')
        gy0 = py + titleH
        pathset = set(st["path"]) if pn.get("showpath", True) else set()
        grows = pn.get("map", rows)
        for y in range(Hc):
            for x in range(Wc):
                c = (x, y); ch = grows[y][x]
                X, Y = px + x * C, gy0 + y * C
                if ch == "#": fill, stroke, key = "#4B5563", "#374151", "wall"
                elif c in pathset and algo != "jps": fill, stroke, key = "#FFEDD5", "#C2410C", "path"
                elif c == st["cur"] and not st["found"]: fill, stroke, key = "#FEF3C7", "#B45309", "cur"
                elif c in st["closed"]: fill, stroke, key = "#DBEAFE", "#93C5FD", "closed"
                elif c in st["g"]: fill, stroke, key = "#DCFCE7", "#4ADE80", "open"
                else: fill, stroke, key = "#FFFFFF", "#E5E7EB", None
                if key: used.add(key)
                sw = 2 if key in ("cur", "path") else 1
                out.append(f'<rect x="{X + .5:.1f}" y="{Y + .5:.1f}" width="{C - 1:.1f}" height="{C - 1:.1f}" fill="{fill}" stroke="{stroke}" stroke-width="{sw}"/>')
                if c == st["S"] or c == st["G"]:
                    col = "#2F5BD3" if c == st["S"] else "#B91C1C"
                    r = C * 0.28 if nums else C * 0.36
                    cy = Y + (C * 0.36 if nums == "fgh" and c in st["g"] and c != st["S"] else C / 2)
                    out.append(f'<circle cx="{X + C / 2:.1f}" cy="{cy:.1f}" r="{r:.1f}" fill="{col}"/>')
                    if C >= 18:
                        out.append(f'<text x="{X + C / 2:.1f}" y="{cy + 4:.1f}" text-anchor="middle" class="b wt" style="font-size:{max(9, min(13, C * .3)):.0f}px">{"S" if c == st["S"] else "G"}</text>')
                    if c == st["S"] or not (nums == "fgh" and c in st["g"]):
                        continue
                if nums and c in st["g"] and ch != "#":
                    gg = st["g"][c]; hh = st["hv"](c); ff = st["fv"](gg, hh)
                    if nums == "fgh":
                        if c != st["G"]:
                            out.append(f'<text x="{X + C / 2:.1f}" y="{Y + C * .47:.1f}" text-anchor="middle" class="mono b" style="font-size:{C * .27:.0f}px">{ff}</text>')
                        out.append(f'<text x="{X + 3:.1f}" y="{Y + C - 4:.1f}" class="mono" style="font-size:{C * .2:.0f}px;fill:#2F5BD3">{gg}</text>')
                        out.append(f'<text x="{X + C - 3:.1f}" y="{Y + C - 4:.1f}" text-anchor="end" class="mono" style="font-size:{C * .2:.0f}px;fill:#C2410C">{hh}</text>')
                    elif nums == "g":
                        out.append(f'<text x="{X + C / 2:.1f}" y="{Y + C / 2 + 4:.1f}" text-anchor="middle" class="mono" style="font-size:{C * .3:.0f}px">{gg}</text>')
        if arrows:
            for c, p in st["par"].items():
                if p is None or c == st["S"]: continue
                if algo == "jps": continue
                cx, cy = px + c[0] * C + C / 2, gy0 + c[1] * C + C / 2
                vx, vy = p[0] - c[0], p[1] - c[1]
                ln = math.hypot(vx, vy)
                L = C * (0.42 if nums else 0.36)
                x2, y2 = cx + vx / ln * L, cy + vy / ln * L
                x1, y1 = cx + vx / ln * L * 0.35, cy + vy / ln * L * 0.35
                if nums == "fgh":
                    x1, y1 = cx + vx / ln * L * 0.62, cy + vy / ln * L * 0.62
                out.append(f'<line x1="{x1:.1f}" y1="{y1:.1f}" x2="{x2:.1f}" y2="{y2:.1f}" stroke="#6B7280" stroke-width="1.2" marker-end="url(#a)"/>')
        if st["found"] and pn.get("showpath", True) and (algo == "jps" or pn.get("line")):
            pts = " ".join(f"{px + x * C + C / 2:.1f},{gy0 + y * C + C / 2:.1f}" for x, y in st["path"])
            out.append(f'<polyline points="{pts}" fill="none" stroke="#C2410C" stroke-width="2.6" stroke-linejoin="round"/>')
            for x, y in st["path"]:
                out.append(f'<circle cx="{px + x * C + C / 2:.1f}" cy="{gy0 + y * C + C / 2:.1f}" r="{max(2.5, C * .14):.1f}" fill="#C2410C"/>')
            used.add("jp" if algo == "jps" else "pline")
        if pn.get("smooth") and st["found"]:
            sm = grid_smooth(grows, st["path"])
            pts = " ".join(f"{px + x * C + C / 2:.1f},{gy0 + y * C + C / 2:.1f}" for x, y in sm)
            out.append(f'<polyline points="{pts}" fill="none" stroke="#15803D" stroke-width="3" stroke-linejoin="round"/>')
            for x, y in sm:
                out.append(f'<circle cx="{px + x * C + C / 2:.1f}" cy="{gy0 + y * C + C / 2:.1f}" r="{max(3, C * .18):.1f}" fill="#15803D"/>')
            used.add("smooth")
            ln = sum(math.hypot(sm[k][0] - sm[k - 1][0], sm[k][1] - sm[k - 1][1]) for k in range(1, len(sm)))
            out[title_idx] = out[title_idx].replace("{sm}", str(len(sm))).replace("{smlen}", f"{ln:.1f}")
        if pn.get("note"):
            out.append(f'<text x="{px + pw / 2:.1f}" y="{gy0 + C * Hc + 15}" text-anchor="middle" class="xs">{esc(pn["note"])}</text>')
    y = 6 + rowsN * ph + 4
    names = {"wall": ("#4B5563", "#374151", "障碍"), "open": ("#DCFCE7", "#4ADE80", "open（待展开）"),
             "closed": ("#DBEAFE", "#93C5FD", "closed（已展开）"), "cur": ("#FEF3C7", "#B45309", "刚展开的节点"),
             "path": ("#FFEDD5", "#C2410C", "最终路径"), "jp": ("#C2410C", "#C2410C", "跳点路径"),
             "smooth": ("#15803D", "#15803D", "拉绳后的路径"), "pline": ("#C2410C", "#C2410C", "原始路径（折线）")}
    x = ox0
    for k in ["wall", "open", "closed", "cur", "path", "jp", "pline", "smooth"]:
        if k in used:
            f_, s_, nm = names[k]
            out.append(f'<rect x="{x:.1f}" y="{y}" width="14" height="14" rx="3" fill="{f_}" stroke="{s_}"/>')
            out.append(f'<text x="{x + 20:.1f}" y="{y + 12}" class="xs">{esc(nm)}</text>')
            x += 34 + 12.5 * len(nm)
    if nums == "fgh":
        out.append(f'<text x="{x + 6:.1f}" y="{y + 12}" class="xs">格内：中间 f，<tspan style="fill:#2F5BD3">左下 g</tspan>，<tspan style="fill:#C2410C">右下 h</tspan>（直走 10、斜走 14）</text>')
    y += 24
    svg = f'<svg viewBox="0 0 {Wt} {y:.0f}" xmlns="http://www.w3.org/2000/svg">' + "".join(out) + "</svg>"
    return fig(svg, d.get("cap"))


# ============================================================ UE 动画专用宏（与 demos/anim_demo.cpp 同一套算法，构建时计算）
def delaunay(S):
    T = []; n = len(S)
    for i in range(n):
        for j in range(i + 1, n):
            for k in range(j + 1, n):
                (ax, ay), (bx, by), (cx, cy) = S[i][1:3], S[j][1:3], S[k][1:3]
                d = 2 * (ax * (by - cy) + bx * (cy - ay) + cx * (ay - by))
                if abs(d) < 1e-9: continue
                ux = ((ax*ax+ay*ay)*(by-cy)+(bx*bx+by*by)*(cy-ay)+(cx*cx+cy*cy)*(ay-by))/d
                uy = ((ax*ax+ay*ay)*(cx-bx)+(bx*bx+by*by)*(ax-cx)+(cx*cx+cy*cy)*(bx-ax))/d
                r2 = (ax-ux)**2 + (ay-uy)**2
                if all((S[m][1]-ux)**2 + (S[m][2]-uy)**2 >= r2*(1-1e-9) for m in range(n) if m not in (i, j, k)):
                    T.append((i, j, k))
    return T


def bs_weights(S, T, px, py):
    for t in T:
        (ax, ay), (bx, by), (cx, cy) = [S[i][1:3] for i in t]
        det = (by-cy)*(ax-cx) + (cx-bx)*(ay-cy)
        w0 = ((by-cy)*(px-cx) + (cx-bx)*(py-cy)) / det
        w1 = ((cy-ay)*(px-cx) + (ax-cx)*(py-cy)) / det
        w2 = 1 - w0 - w1
        if min(w0, w1, w2) >= -1e-9:
            return {t[0]: w0, t[1]: w1, t[2]: w2}, t, (px, py)
    best = None
    for t in T:
        for a, b in ((t[0], t[1]), (t[1], t[2]), (t[2], t[0])):
            if sum(1 for u in T if a in u and b in u) != 1: continue
            ax, ay = S[a][1:3]; bx, by = S[b][1:3]
            tt = max(0, min(1, ((px-ax)*(bx-ax) + (py-ay)*(by-ay)) / ((bx-ax)**2 + (by-ay)**2)))
            qx, qy = ax + (bx-ax)*tt, ay + (by-ay)*tt
            d = (qx-px)**2 + (qy-py)**2
            if best is None or d < best[0]: best = (d, a, b, tt, qx, qy)
    _, a, b, tt, qx, qy = best
    return {a: 1-tt, b: tt}, None, (qx, qy)


def bspace(d):
    S = d["samples"]; T = delaunay(S)
    W = 860; PW, PH = 500, 440; L, TOP = 50, 20
    (x0, x1), (y0, y1) = d["xr"], d["yr"]
    X = lambda x: L + PW * (x - x0) / (x1 - x0)
    Y = lambda y: TOP + PH * (1 - (y - y0) / (y1 - y0))
    o = [f'<rect x="{L}" y="{TOP}" width="{PW}" height="{PH}" fill="#FBFBFD" stroke="#D1D5DB"/>']
    o.append(f'<line x1="{X(0):.1f}" y1="{TOP}" x2="{X(0):.1f}" y2="{TOP+PH}" stroke="#E5E7EB"/><line x1="{L}" y1="{Y(0):.1f}" x2="{L+PW}" y2="{Y(0):.1f}" stroke="#E5E7EB"/>')
    o.append(f'<text x="{L+PW/2}" y="{TOP+PH+30}" text-anchor="middle" class="sm">{esc(d.get("xlab","Vx（右为正）"))}</text>')
    o.append(f'<text x="{L-8}" y="{TOP+12}" text-anchor="end" class="xs">{y1:g}</text><text x="{L-8}" y="{TOP+PH}" text-anchor="end" class="xs">{y0:g}</text>')
    o.append(f'<text x="{L}" y="{TOP+PH+14}" class="xs">{x0:g}</text><text x="{L+PW}" y="{TOP+PH+14}" text-anchor="end" class="xs">{x1:g}</text>')
    o.append(f'<text x="{L-8}" y="{Y(0)+4:.1f}" text-anchor="end" class="xs">0</text>')
    for t in T:
        pts = " ".join(f"{X(S[i][1]):.1f},{Y(S[i][2]):.1f}" for i in t)
        o.append(f'<polygon points="{pts}" fill="none" stroke="#93C5FD" stroke-width="1.2"/>')
    cols = ["#C2410C", "#15803D", "#6D28D9", "#B91C1C"]
    panel = []
    for qi, q in enumerate(d.get("query", [])):
        c = cols[qi % len(cols)]
        w, tri, (cx, cy) = bs_weights(S, T, q[0], q[1])
        if tri:
            pts = " ".join(f"{X(S[i][1]):.1f},{Y(S[i][2]):.1f}" for i in tri)
            o.append(f'<polygon points="{pts}" fill="{c}" fill-opacity="0.10" stroke="{c}" stroke-width="1.6"/>')
        for i, wi in w.items():
            if wi > 1e-6:
                o.append(f'<line x1="{X(cx):.1f}" y1="{Y(cy):.1f}" x2="{X(S[i][1]):.1f}" y2="{Y(S[i][2]):.1f}" stroke="{c}" stroke-dasharray="4 3" stroke-width="1.2"/>')
        if (cx, cy) != (q[0], q[1]):
            o.append(f'<line x1="{X(q[0]):.1f}" y1="{Y(q[1]):.1f}" x2="{X(cx):.1f}" y2="{Y(cy):.1f}" stroke="{c}" stroke-width="1.4" marker-end="url(#ao)"/>')
            o.append(f'<rect x="{X(q[0])-5:.1f}" y="{Y(q[1])-5:.1f}" width="10" height="10" fill="#fff" stroke="{c}" stroke-width="2"/>')
        o.append(f'<circle cx="{X(cx):.1f}" cy="{Y(cy):.1f}" r="6" fill="{c}"/>')
        dx, dy = (q[3], q[4]) if len(q) > 4 else (9, -8)
        o.append(f'<text x="{X(q[0])+dx:.1f}" y="{Y(q[1])+dy:.1f}" class="b sm" style="fill:{c}">{esc(q[2])}</text>')
        panel.append((c, q, w, tri is None))
    for i, (nm, x, y) in enumerate(S):
        o.append(f'<circle cx="{X(x):.1f}" cy="{Y(y):.1f}" r="5.5" fill="#fff" stroke="#2F5BD3" stroke-width="2"/>')
        o.append(f'<text x="{X(x):.1f}" y="{Y(y)+18:.1f}" text-anchor="middle" class="xs b" style="fill:#1E3A8A">{esc(nm)}</text>')
    px, py = L + PW + 30, TOP + 6
    o.append(f'<text x="{px}" y="{py+10}" class="b">查询点 → 采样权重（Σ=1）</text>'); py += 30
    for c, q, w, outside in panel:
        o.append(f'<text x="{px}" y="{py+10}" class="b sm" style="fill:{c}">{esc(q[2])}：({q[0]:g}, {q[1]:g}){"  → 投影到边界" if outside else ""}</text>'); py += 20
        for i, wi in sorted(w.items(), key=lambda kv: -kv[1]):
            if wi <= 1e-6: continue
            o.append(f'<text x="{px+8}" y="{py+11}" class="sm">{esc(S[i][0])}</text>')
            o.append(f'<rect x="{px+80}" y="{py+1}" width="{170*wi:.1f}" height="12" rx="3" fill="{c}" fill-opacity="0.75"/>')
            o.append(f'<text x="{px+256}" y="{py+11}" text-anchor="end" class="sm mono">{wi:.3f}</text>'); py += 17
        py += 10
    H = max(TOP + PH + 40, py + 10)
    svg = f'<svg viewBox="0 0 {W} {H:.0f}" xmlns="http://www.w3.org/2000/svg">' + "".join(o) + "</svg>"
    return fig(svg, d.get("cap"))


def sync(d):
    """同步组时间线：每种模式两条动画的支撑脚（L 蓝 / R 橙），以及两者不一致的时刻（红）"""
    (wl, wL), (rl, rL) = d["walk"], d["run"]; T = d.get("T", 3.0); W = 860; L = 150; PW = W - L - 20
    gl = 0.5 * wl + 0.5 * rl
    foot = lambda ph, pl: 0 if ((ph - pl) % 1.0) < 0.5 else 1
    o = []; y = 8; N = 600
    names = {"none": "不同步", "norm": "同步组：归一化时间", "marker": "同步组：同步标记"}
    for mode in d["modes"]:
        o.append(f'<text x="8" y="{y+14}" class="b sm">{esc(names[mode])}</text>'); y += 22
        rows = {"Walk": [], "Run": []}; bad = 0
        for k in range(N):
            t = T * k / N
            if mode == "none": pw, pr = (t / wl) % 1, (t / rl) % 1
            else:
                pw = (t / gl) % 1
                pr = pw if mode == "norm" else (rL + (pw - wL) % 1) % 1
            fw, fr = foot(pw, wL), foot(pr, rL)
            rows["Walk"].append(fw); rows["Run"].append(fr); bad += fw != fr
        for nm in ("Walk", "Run"):
            o.append(f'<text x="{L-8}" y="{y+12}" text-anchor="end" class="sm">{nm}</text>')
            seq = rows[nm]; k0 = 0
            for k in range(1, N + 1):
                if k == N or seq[k] != seq[k0]:
                    x = L + PW * k0 / N; w = PW * (k - k0) / N
                    col = ("#DBEAFE", "#2F5BD3") if seq[k0] == 0 else ("#FFEDD5", "#C2410C")
                    o.append(f'<rect x="{x:.1f}" y="{y}" width="{w:.1f}" height="16" fill="{col[0]}" stroke="{col[1]}" stroke-width="0.8"/>')
                    if w > 26: o.append(f'<text x="{x+w/2:.1f}" y="{y+12}" text-anchor="middle" class="xs b" style="fill:{col[1]}">{"L" if seq[k0]==0 else "R"}</text>')
                    k0 = k
            y += 20
        o.append(f'<text x="{L-8}" y="{y+10}" text-anchor="end" class="xs">不一致</text>')
        for k in range(N):
            if rows["Walk"][k] != rows["Run"][k]:
                o.append(f'<rect x="{L+PW*k/N:.1f}" y="{y+2}" width="{PW/N+0.3:.2f}" height="9" fill="#EF4444"/>')
        o.append(f'<text x="{L+PW}" y="{y+24}" text-anchor="end" class="xs">不一致 {100*bad/N:.1f}%</text>')
        y += 34
    for k in range(int(T) + 1):
        x = L + PW * k / T
        o.append(f'<text x="{x:.1f}" y="{y+4}" text-anchor="middle" class="xs">{k} s</text>')
    y += 22
    o.append(f'<rect x="{L}" y="{y-10}" width="12" height="12" fill="#DBEAFE" stroke="#2F5BD3"/><text x="{L+18}" y="{y}" class="xs">左脚支撑</text>')
    o.append(f'<rect x="{L+90}" y="{y-10}" width="12" height="12" fill="#FFEDD5" stroke="#C2410C"/><text x="{L+108}" y="{y}" class="xs">右脚支撑</text>')
    o.append(f'<rect x="{L+180}" y="{y-10}" width="12" height="12" fill="#EF4444"/><text x="{L+198}" y="{y}" class="xs">两条动画的支撑脚不同 → 混合后两只脚都悬空或交叉</text>')
    y += 10
    svg = f'<svg viewBox="0 0 {W} {y:.0f}" xmlns="http://www.w3.org/2000/svg">' + "".join(o) + "</svg>"
    return fig(svg, d.get("cap"))


def inertial_offset(x0, v0, t1, t):
    sgn = -1 if x0 < 0 else 1; x0 *= sgn; v0 *= sgn
    if x0 < 1e-9: return 0
    v0 = min(v0, 0.0)
    if v0 < 0: t1 = min(t1, -5 * x0 / v0)
    if t >= t1: return 0
    a0 = max(0.0, (-8 * v0 * t1 - 20 * x0) / (t1 * t1))
    A = -(a0*t1*t1 + 6*v0*t1 + 12*x0) / (2*t1**5)
    B = (3*a0*t1*t1 + 16*v0*t1 + 30*x0) / (2*t1**4)
    C = -(3*a0*t1*t1 + 12*v0*t1 + 20*x0) / (2*t1**3)
    return sgn * (A*t**5 + B*t**4 + C*t**3 + a0/2*t*t + v0*t + x0)


def curves(d):
    a = lambda t: 50 * math.sin(2 * math.pi * t / 1.0)
    b = lambda t: 10 + 20 * math.sin(2 * math.pi * t / 0.8 + 1)
    t0, T = d.get("t0", 0.4), d.get("T", 0.2); ta, tb = d.get("range", [0.2, 0.9])
    dt = 1 / 120; x0 = a(t0) - b(t0); v0 = ((a(t0) - a(t0 - dt)) - (b(t0) - b(t0 - dt))) / dt
    def res(m, t):
        if t < t0: return a(t)
        al = max(0, min(1, (t - t0) / T))
        if m == "cut": return b(t)
        if m == "linear": return (1 - al) * a(t) + al * b(t)
        if m == "smooth": s_ = al * al * (3 - 2 * al); return (1 - s_) * a(t) + s_ * b(t)
        return b(t) + inertial_offset(x0, v0, T, t - t0)
    names = {"cut": ("硬切", "#B91C1C"), "linear": ("线性交叉淡化", "#B45309"), "smooth": ("SmoothStep 淡化", "#6D28D9"), "inertial": ("惯性化（五次多项式）", "#15803D")}
    W = 860; per = 2; gap = 24; pw = (W - 20 - gap) / per; ph = 170; o = []
    for k, m in enumerate(d["methods"]):
        ox = 10 + (k % per) * (pw + gap); oy = 8 + (k // per) * (ph + 40)
        X = lambda t: ox + 34 + (pw - 44) * (t - ta) / (tb - ta)
        Y = lambda v: oy + 24 + (ph - 40) * (1 - (v + 55) / 110)
        nm, c = names[m]
        o.append(f'<text x="{ox + pw/2:.1f}" y="{oy+14}" text-anchor="middle" class="b sm" style="fill:{c}">{nm}</text>')
        o.append(f'<rect x="{X(t0):.1f}" y="{oy+24}" width="{X(t0+T)-X(t0):.1f}" height="{ph-40}" fill="#F3F4F6"/>')
        o.append(f'<line x1="{X(ta):.1f}" y1="{Y(0):.1f}" x2="{X(tb):.1f}" y2="{Y(0):.1f}" stroke="#E5E7EB"/>')
        o.append(f'<line x1="{X(t0):.1f}" y1="{oy+24}" x2="{X(t0):.1f}" y2="{oy+ph-16}" stroke="#9CA3AF" stroke-dasharray="3 3"/>')
        n = 240
        for f, col, dash in ((a, "#9CA3AF", "5 4"), (b, "#2F5BD3", "2 3")):
            pts = " ".join(f"{X(ta+(tb-ta)*i/n):.1f},{Y(f(ta+(tb-ta)*i/n)):.1f}" for i in range(n + 1))
            o.append(f'<polyline points="{pts}" fill="none" stroke="{col}" stroke-width="1.2" stroke-dasharray="{dash}"/>')
        pts = []
        for i in range(n + 1):
            t = ta + (tb - ta) * i / n
            if m == "cut" and pts and t >= t0 and (t - (tb - ta) / n) < t0:
                o.append(f'<polyline points="{" ".join(pts)}" fill="none" stroke="{c}" stroke-width="2.4"/>'); pts = []
            pts.append(f"{X(t):.1f},{Y(res(m, t)):.1f}")
        o.append(f'<polyline points="{" ".join(pts)}" fill="none" stroke="{c}" stroke-width="2.4"/>')
        o.append(f'<text x="{X(t0)+3:.1f}" y="{oy+ph-4}" class="xs">切换 t0  ·  过渡区 {T:g} s</text>')
    y = 8 + ((len(d["methods"]) + per - 1) // per) * (ph + 40) - 14
    o.append(f'<line x1="20" y1="{y}" x2="44" y2="{y}" stroke="#9CA3AF" stroke-dasharray="5 4"/><text x="50" y="{y+4}" class="xs">源动画 A（继续播放时的走向）</text>')
    o.append(f'<line x1="250" y1="{y}" x2="274" y2="{y}" stroke="#2F5BD3" stroke-dasharray="2 3"/><text x="280" y="{y+4}" class="xs">目标动画 B</text>')
    o.append(f'<line x1="380" y1="{y}" x2="404" y2="{y}" stroke="#111827" stroke-width="2.4"/><text x="410" y="{y+4}" class="xs">最终输出（灰底 = 过渡区）</text>')
    svg = f'<svg viewBox="0 0 {W} {y+12:.0f}" xmlns="http://www.w3.org/2000/svg">' + "".join(o) + "</svg>"
    return fig(svg, d.get("cap"))


def dials(d):
    """分层混合结果：每根骨骼一个“朝向表盘”（俯视，0° = 正前方，顺时针为正）"""
    par = d["par"]; names = d["names"]; show = d["show"]
    def comp(loc):
        c = []
        for i, p in enumerate(par): c.append(loc[i] + (c[p] if p >= 0 else 0))
        return c
    def weights(fb, depth):
        w = []
        for i in range(len(par)):
            k, p = 0, i
            while p >= 0 and p != fb: p = par[p]; k += 1
            if p != fb: w.append(0.0)
            elif depth < 0: w.append(0.0)
            elif depth == 0: w.append(1.0)
            else: w.append(min(1.0, (k + 1) / depth))
        return w
    base, aim = d["base"], d["aim"]
    rows = []; mode_of = {r[0]: r[1] for r in d["rows"]}
    for lab, mode, depth in d["rows"]:
        if mode == "base": out = comp(base)
        elif mode == "aim": out = comp(aim)
        else:
            w = weights(d["filter"], depth)
            if mode == "local": out = comp([base[i] + (aim[i] - base[i]) * w[i] for i in range(len(par))])
            else:
                cb, ca = comp(base), comp(aim); out = [cb[i] + (ca[i] - cb[i]) * w[i] for i in range(len(par))]
        rows.append((lab, out))
    W = 860; L = 190; cw = (W - L - 10) / len(show); R = 21; o = []; y = 12
    for j, bi in enumerate(show):
        o.append(f'<text x="{L + cw*j + cw/2:.1f}" y="{y+8}" text-anchor="middle" class="sm b">{esc(names[bi])}</text>')
    y += 22
    for lab, out in rows:
        o.append(f'<text x="{L-12}" y="{y+R+5}" text-anchor="end" class="sm b">{esc(lab)}</text>')
        for j, bi in enumerate(show):
            cx, cy = L + cw*j + cw/2, y + R
            ang = out[bi]; tg = d["target"][j]
            if tg is None or mode_of[lab] in ("base", "aim"):
                o.append(f'<circle cx="{cx:.1f}" cy="{cy:.1f}" r="{R}" fill="#F9FAFB" stroke="#9CA3AF" stroke-width="1.1"/>')
            else:
                good = abs(ang - tg) < 0.5
                o.append(f'<circle cx="{cx:.1f}" cy="{cy:.1f}" r="{R}" fill="{"#EAF7EF" if good else "#FFF4EC"}" stroke="{"#15803D" if good else "#C2410C"}" stroke-width="1.3"/>')
                ta = math.radians(tg)
                o.append(f'<line x1="{cx:.1f}" y1="{cy:.1f}" x2="{cx+R*math.sin(ta):.1f}" y2="{cy-R*math.cos(ta):.1f}" stroke="#9CA3AF" stroke-dasharray="2 2"/>')
            r = math.radians(ang); tx, ty = cx + (R-3)*math.sin(r), cy - (R-3)*math.cos(r)
            bx, by = cx + (R-11)*math.sin(r), cy - (R-11)*math.cos(r); nx, ny = math.cos(r) * 4.5, math.sin(r) * 4.5
            o.append(f'<line x1="{cx:.1f}" y1="{cy:.1f}" x2="{bx:.1f}" y2="{by:.1f}" stroke="#111827" stroke-width="2.2"/>')
            o.append(f'<polygon points="{tx:.1f},{ty:.1f} {bx+nx:.1f},{by+ny:.1f} {bx-nx:.1f},{by-ny:.1f}" fill="#111827"/>')
            o.append(f'<circle cx="{cx:.1f}" cy="{cy:.1f}" r="2.5" fill="#111827"/>')
            o.append(f'<text x="{cx+R+4:.1f}" y="{cy+4:.1f}" class="xs mono">{ang:+.0f}°</text>')
        y += 2 * R + 14
    o.append(f'<text x="20" y="{y+6}" class="xs">俯视表盘：↑ = 角色正前方，顺时针为正；虚线 = 期望朝向（瞄准姿势在组件空间的朝向）；绿 = 一致，橙 = 偏了，灰 = 输入姿势 / 不考察</text>')
    svg = f'<svg viewBox="0 0 {W} {y+16:.0f}" xmlns="http://www.w3.org/2000/svg">' + "".join(o) + "</svg>"
    return fig(svg, d.get("cap"))

def macro(m: re.Match) -> str:
    kind, body = m.group(1), m.group(2)
    try:
        d = json.loads(body)
    except json.JSONDecodeError as e:
        raise SystemExit(f"[{kind}] JSON 错误: {e}\n{body[:200]}")
    return {"trace": trace, "bars": bars, "lines": lines, "grid": grid, "bspace": bspace, "sync": sync, "curves": curves, "dials": dials}[kind](d)


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
    left = re.findall(r"<!--(trace|bars|lines|grid|bspace|sync|curves|dials|code)", content)
    if left:
        print("未展开的宏:", left, file=sys.stderr)
    scripts = (ROOT / "labs.js").read_text(encoding="utf-8") if (ROOT / "labs.js").exists() else ""
    OUT.write_text(shell.replace("{{CONTENT}}", content).replace("{{SCRIPTS}}", scripts), encoding="utf-8")
    print(f"OK -> {OUT}  ({OUT.stat().st_size / 1024:.1f} KB, figure={content.count('<figure>')}, "
          f"svg={len(re.findall('<svg', content))}, bad_svg={bad}, dup_id={len(dup)}, lab={content.count('class=\"lab')})")
    return 1 if bad or left else 0


if __name__ == "__main__":
    sys.exit(main())
