"""第六部分（方法论篇）的几何插图宏 —— 电子书（SVG）与 md（表格 / 字符图）共用同一份 JSON 和同一套几何计算
  <!--probe {json} -->    斜坡上的地面检测：单射线 / 三射线 / 形状投射，胶囊按切点真实放置（数值来自 feeldata.json 的 F1）
  <!--wallcov {json} -->  墙检测射线覆盖：矮墙、墙中段、墙顶死区、双射线修正
  <!--seam {json} -->     逐格碰撞体的内部接缝 vs CompositeCollider2D 合并轮廓
  <!--tmap {json} -->     由 tools/tilemap_dump.json 渲染 SampleScene 的 Ground / BackGround 瓦片地图
"""
import html
import math

VALS = {}          # build.py 注入
WARN = []
COL = {"b": ("#EAF0FD", "#2F5BD3"), "o": ("#FFF4EC", "#C2410C"), "g": ("#EAF7EF", "#15803D"), "p": ("#F3EEFE", "#6D28D9"),
       "r": ("#FDEEEE", "#B91C1C"), "a": ("#FEF6E4", "#B45309"), "s": ("#F3F4F6", "#6B7280"), "t": ("#E6F6F5", "#0F766E")}
# 与 demos/feel_verify.cpp 相同的场景常量
R = 0.7703179 / 2
OFFY, HALFH = -0.29489255, 2.0689855 / 2
BOTTOM, TOP = OFFY - HALFH, OFFY + HALFH
GC_Y, GC_LEN, WC_Y, WC_X, WC_LEN = -1.221, 0.15, -0.006, 0.366, 0.25


def esc(s):
    return html.escape(str(s), quote=True)


def fig(svg, cap):
    return f"<figure>\n{svg}\n" + (f"<figcaption>{cap}</figcaption>" if cap else "") + "</figure>"


def capsule(cx, by, S, X, Y, stroke="#2F5BD3", fill="#EAF0FD", dash=False, op=1.0):
    """胶囊：底点 (cx, by)，半径 R，总高 2·HALFH；返回 SVG path"""
    x0, x1 = X(cx - R), X(cx + R)
    yb, yt = Y(by + R), Y(by + 2 * HALFH - R)
    rr = R * S
    d = f"M{x0:.1f},{yt:.1f} A{rr:.1f},{rr:.1f} 0 0 1 {x1:.1f},{yt:.1f} L{x1:.1f},{yb:.1f} A{rr:.1f},{rr:.1f} 0 0 1 {x0:.1f},{yb:.1f} Z"
    da = ' stroke-dasharray="5 4"' if dash else ""
    return f'<path d="{d}" fill="{fill}" fill-opacity="{op}" stroke="{stroke}" stroke-width="1.6"{da}/>'


def arrow(x1, y1, x2, y2, col, w=1.8):
    ux, uy = x2 - x1, y2 - y1
    L = math.hypot(ux, uy) or 1
    ux, uy = ux / L, uy / L
    px, py = -uy, ux
    s = 7
    tip = f'<path d="M{x2:.1f},{y2:.1f} L{x2 - ux * s + px * s * .45:.1f},{y2 - uy * s + py * s * .45:.1f} L{x2 - ux * s - px * s * .45:.1f},{y2 - uy * s - py * s * .45:.1f} z" fill="{col}"/>'
    return f'<line x1="{x1:.1f}" y1="{y1:.1f}" x2="{x2 - ux * 3:.1f}" y2="{y2 - uy * 3:.1f}" stroke="{col}" stroke-width="{w}"/>' + tip


# ============================================================ probe
def probe_eval(mode, deg, length=GC_LEN, d=0.3):
    t = math.radians(deg)
    by = R / math.cos(t) - R                 # 胶囊最低点（斜面过原点：y = x·tanθ）
    sy = by + (GC_Y - BOTTOM)                # 射线起点高度
    rays = []
    if mode in ("single", "long"):
        L = GC_LEN if mode == "single" else 0.3
        rays.append((0.0, sy, L, sy - L <= 0))
    elif mode == "tri":
        for x in (-d, 0.0, d):
            surf = x * math.tan(t)
            rays.append((x, sy, length, sy - length <= surf or sy <= surf))
    hit = any(r[3] for r in rays) if rays else True
    return by, rays, hit


def probe(dd):
    panels = dd["panels"]
    n = len(panels)
    W, PH = dd.get("w", 900), 330
    pw = W / n
    S = 104
    out = []
    for i, p in enumerate(panels):
        ox = i * pw
        t = math.radians(p["deg"])
        X = lambda x, ox=ox: ox + pw / 2 + x * S
        Y = lambda y: 250 - y * S
        by, rays, hit = probe_eval(p["mode"], p["deg"])
        # 地面（斜坡）
        xa, xb = -1.35, 1.35
        poly = f"{X(xa):.1f},{Y(xa * math.tan(t)):.1f} {X(xb):.1f},{Y(xb * math.tan(t)):.1f} {X(xb):.1f},{Y(-0.9):.1f} {X(xa):.1f},{Y(-0.9):.1f}"
        out.append(f'<clipPath id="pc{i}"><rect x="{ox + 4}" y="20" width="{pw - 8}" height="{PH - 30}"/></clipPath>')
        out.append(f'<g clip-path="url(#pc{i})"><polygon points="{poly}" fill="#E7E5E4" stroke="#78716C" stroke-width="1.5"/>')
        out.append(capsule(0, by, S, X, Y))
        if p["mode"] == "cast":
            out.append(capsule(0, by - 0.06, S, X, Y, stroke="#15803D", fill="none", dash=True))
            cxp, cyp = R * math.sin(t), R * math.sin(t) ** 2 / math.cos(t)
            nx, ny = -math.sin(t), math.cos(t)
            out.append(f'<circle cx="{X(cxp):.1f}" cy="{Y(cyp):.1f}" r="3.5" fill="#B91C1C"/>')
            out.append(arrow(X(cxp), Y(cyp), X(cxp + nx * 0.75), Y(cyp + ny * 0.75), "#B91C1C"))
            out.append(f'<text x="{X(cxp + nx * 0.75) + 6:.1f}" y="{Y(cyp + ny * 0.75) + 4:.1f}" class="xs b" style="fill:#B91C1C">法线与竖直夹角 {p["deg"]}°</text>')
        for (rx, ry, L, h) in rays:
            col = "#15803D" if h else "#B91C1C"
            out.append(f'<line x1="{X(rx):.1f}" y1="{Y(ry):.1f}" x2="{X(rx):.1f}" y2="{Y(ry - L):.1f}" stroke="{col}" stroke-width="2.6"/>')
            out.append(f'<circle cx="{X(rx):.1f}" cy="{Y(ry):.1f}" r="2.8" fill="{col}"/>')
        out.append("</g>")
        out.append(f'<text x="{ox + pw / 2:.1f}" y="16" text-anchor="middle" class="b sm">{esc(p["t"])}</text>')
        res = ("✔ " if hit else "✘ ") + p.get("r", "")
        out.append(f'<text x="{ox + pw / 2:.1f}" y="{PH + 8}" text-anchor="middle" class="sm b" style="fill:{"#15803D" if hit else "#B91C1C"}">{esc(res)}</text>')
        if p.get("s"):
            out.append(f'<text x="{ox + pw / 2:.1f}" y="{PH + 26}" text-anchor="middle" class="xs">{esc(p["s"])}</text>')
        if i:
            out.append(f'<line x1="{ox}" y1="24" x2="{ox}" y2="{PH + 20}" stroke="#E5E7EB"/>')
    svg = f'<svg viewBox="0 0 {W} {PH + 36}" xmlns="http://www.w3.org/2000/svg">' + "".join(out) + "</svg>"
    return fig(svg, dd.get("cap"))


def md_probe(dd):
    rows = ["| 方案 | 坡度 | 结果 | 说明 |", "|---|---|---|---|"]
    for p in dd["panels"]:
        _, _, hit = probe_eval(p["mode"], p["deg"])
        rows.append(f"| {p['t']} | {p['deg']}° | {'✔' if hit else '✘'} {p.get('r', '')} | {p.get('s', '')} |")
    return "\n".join(rows)


# ============================================================ wallcov
def wallcov(dd):
    panels = dd["panels"]
    n = len(panels)
    W = dd.get("w", 900)
    pw = W / n
    S = dd.get("s", 52)
    PH = dd.get("ph", 310)
    y0 = PH - 26
    out = []
    for i, p in enumerate(panels):
        ox = i * pw
        X = lambda x, ox=ox: ox + pw * 0.40 + x * S
        Y = lambda y: y0 - y * S
        floor = p.get("floor", 0.0)
        wb, wt = p["wall"]            # 墙面相对地面的下沿 / 上沿高度（米）
        by = p["by"]                  # 角色脚底高度
        wx = R + 0.043                # 墙面 x（贴墙）
        if p.get("floorOn", True):
            out.append(f'<rect x="{ox + 6}" y="{Y(floor):.1f}" width="{pw - 12}" height="{max(4, Y(floor - 0.3) - Y(floor)):.1f}" fill="#E7E5E4" stroke="#78716C"/>')
        out.append(f'<rect x="{X(wx):.1f}" y="{Y(wt):.1f}" width="{0.8 * S:.1f}" height="{Y(wb) - Y(wt):.1f}" fill="#E7E5E4" stroke="#78716C" stroke-width="1.5"/>')
        out.append(capsule(0, by, S, X, Y))
        rays = [("腰", by - BOTTOM + WC_Y)]
        if p.get("foot"):
            rays.append(("脚", by + 0.1))
        if p.get("head"):
            rays.append(("头", by + 2 * HALFH - 0.15))
        for nm, ry in rays:
            hit = wb < ry < wt
            col = "#15803D" if hit else "#B91C1C"
            out.append(f'<line x1="{X(WC_X):.1f}" y1="{Y(ry):.1f}" x2="{X(WC_X + WC_LEN):.1f}" y2="{Y(ry):.1f}" stroke="{col}" stroke-width="2.6"/>')
            out.append(f'<text x="{X(-R) - 4:.1f}" y="{Y(ry) + 4:.1f}" text-anchor="end" class="xs b" style="fill:{col}">{nm}{"✔" if hit else "✘"}</text>')
        out.append(f'<text x="{ox + pw / 2:.1f}" y="16" text-anchor="middle" class="b sm">{esc(p["t"])}</text>')
        out.append(f'<text x="{ox + pw / 2:.1f}" y="{PH + 2}" text-anchor="middle" class="xs">{esc(p.get("s", ""))}</text>')
        if i:
            out.append(f'<line x1="{ox}" y1="24" x2="{ox}" y2="{PH - 10}" stroke="#E5E7EB"/>')
    svg = f'<svg viewBox="0 0 {W} {PH + 10}" xmlns="http://www.w3.org/2000/svg">' + "".join(out) + "</svg>"
    return fig(svg, dd.get("cap"))


def md_wallcov(dd):
    rows = ["| 情形 | 墙面范围（相对地面） | 脚底高度 | 射线 | 说明 |", "|---|---|---|---|---|"]
    for p in dd["panels"]:
        by = p["by"]
        rs = [("腰", by - BOTTOM + WC_Y)] + ([("脚", by + 0.1)] if p.get("foot") else []) + ([("头", by + 2 * HALFH - 0.15)] if p.get("head") else [])
        txt = "；".join(f"{n} {y:.2f} m {'✔' if p['wall'][0] < y < p['wall'][1] else '✘'}" for n, y in rs)
        rows.append(f"| {p['t']} | {p['wall'][0]:g} ~ {p['wall'][1]:g} m | {by:g} m | {txt} | {p.get('s', '')} |")
    return "\n".join(rows)


# ============================================================ seam
def seam(dd):
    W, H = 900, 210
    S = 64
    out = []
    for side in (0, 1):
        ox = 30 + side * 450
        X = lambda x, ox=ox: ox + x * S
        Y = lambda y: 150 - y * S
        out.append(f'<text x="{ox + 2 * S:.1f}" y="18" text-anchor="middle" class="b sm">{"逐格 TilemapCollider2D：每格一个方框" if side == 0 else "CompositeCollider2D：合并成一个多边形"}</text>')
        for k in range(6):
            out.append(f'<rect x="{X(k * 1.0 - 0.6):.1f}" y="{Y(0):.1f}" width="{S:.1f}" height="{S:.1f}" fill="#E7E5E4" stroke="#D6D3D1"/>')
        if side == 0:
            for k in range(6):
                out.append(f'<rect x="{X(k - 0.6):.1f}" y="{Y(0):.1f}" width="{S:.1f}" height="{S:.1f}" fill="none" stroke="#78716C" stroke-width="1.4"/>')
            for k in range(1, 6):
                out.append(f'<line x1="{X(k - 0.6):.1f}" y1="{Y(0):.1f}" x2="{X(k - 0.6):.1f}" y2="{Y(-1):.1f}" stroke="#B91C1C" stroke-width="2.4"/>')
            out.append(f'<text x="{X(2.4):.1f}" y="{Y(-1) + 18:.1f}" text-anchor="middle" class="xs b" style="fill:#B91C1C">红线 = 内部接缝（共 5 条竖边，每条都是一个可能“绊脚”的角）</text>')
        else:
            out.append(f'<rect x="{X(-0.6):.1f}" y="{Y(0):.1f}" width="{6 * S:.1f}" height="{S:.1f}" fill="none" stroke="#15803D" stroke-width="2.2"/>')
            out.append(f'<text x="{X(2.4):.1f}" y="{Y(-1) + 18:.1f}" text-anchor="middle" class="xs b" style="fill:#15803D">只剩外轮廓：滑过表面时没有内部角可以“卡住”</text>')
        # 角色（简化成方框底）在第 2~3 格之间滑动
        bx = 1.75
        out.append(f'<rect x="{X(bx):.1f}" y="{Y(1.15):.1f}" width="{0.7 * S:.1f}" height="{1.15 * S:.1f}" rx="10" fill="#EAF0FD" stroke="#2F5BD3" stroke-width="1.6"/>')
        out.append(arrow(X(bx + 0.8), Y(0.55), X(bx + 1.6), Y(0.55), "#2F5BD3"))
        if side == 0:
            out.append(f'<circle cx="{X(2.4):.1f}" cy="{Y(0):.1f}" r="6" fill="none" stroke="#B91C1C" stroke-width="2"/>')
    svg = f'<svg viewBox="0 0 {W} {H}" xmlns="http://www.w3.org/2000/svg">' + "".join(out) + "</svg>"
    return fig(svg, dd.get("cap"))


def md_seam(dd):
    return ("```text\n逐格碰撞体：   ┌─┬─┬─┬─┬─┬─┐   ← 每个 ┬ 下面都有一条内部竖边（接缝），快速滑过时可能被“绊一下”\n"
            "               └─┴─┴─┴─┴─┴─┘\n合并成多边形： ┌───────────┐   ← 只有外轮廓，没有内部角\n               └───────────┘\n```")


# ============================================================ tmap
def tm_data():
    return VALS["tm"]


def tmap(dd):
    T = tm_data()
    x0, x1, y0, y1 = dd.get("x", [-24, 42])[0], dd.get("x", [-24, 42])[1], 0, dd.get("ymax", 15)
    W = dd.get("w", 900)
    cs = (W - 40) / (x1 - x0)
    H = (y1 - y0) * cs + 70
    X = lambda x: 20 + (x - x0) * cs
    Y = lambda y: 30 + (y1 - y) * cs
    out = [f'<rect x="{X(x0):.1f}" y="{Y(y1):.1f}" width="{(x1 - x0) * cs:.1f}" height="{(y1 - y0) * cs:.1f}" fill="#FAFAF9" stroke="#E5E7EB"/>']
    for gx in range(x0, x1 + 1):
        if gx % 5 == 0:
            out.append(f'<line x1="{X(gx):.1f}" y1="{Y(y1):.1f}" x2="{X(gx):.1f}" y2="{Y(y0):.1f}" stroke="#EEF2F6"/>')
            out.append(f'<text x="{X(gx):.1f}" y="{Y(y0) + 13:.1f}" text-anchor="middle" class="xs mono">{gx}</text>')
    for gy in range(y0, y1 + 1, 2):
        out.append(f'<text x="{X(x0) - 4:.1f}" y="{Y(gy) + 4:.1f}" text-anchor="end" class="xs mono">{gy}</text>')
    palette = ["#A8A29E", "#78716C", "#D6D3D1", "#57534E", "#C2410C", "#B45309"]
    bg = T["tilemaps"].get("BackGround", {})
    for x, y, k in bg.get("tiles", []):
        out.append(f'<rect x="{X(x):.1f}" y="{Y(y + 1):.1f}" width="{cs:.1f}" height="{cs:.1f}" fill="#DBEAFE" stroke="#93C5FD" stroke-width="0.6"/>')
    g = T["tilemaps"]["Ground"]
    for x, y, k in g["tiles"]:
        out.append(f'<rect x="{X(x):.1f}" y="{Y(y + 1):.1f}" width="{cs:.1f}" height="{cs:.1f}" fill="{palette[k % len(palette)]}" fill-opacity="0.55" stroke="#FFFFFF" stroke-width="0.6"/>')
    for path in g["composite"]["paths"]:
        pts = " ".join(f"{X(px):.1f},{Y(py):.1f}" for px, py in path)
        out.append(f'<polygon points="{pts}" fill="none" stroke="#B91C1C" stroke-width="2"/>')
    for name, (mx, my) in T["markers"].items():
        lab = "玩家出生点" if name == "player" else "骷髅"
        col = "#2F5BD3" if name == "player" else "#C2410C"
        b = my + BOTTOM
        out.append(f'<rect x="{X(mx - R):.1f}" y="{Y(my + TOP):.1f}" width="{2 * R * cs:.1f}" height="{2 * HALFH * cs:.1f}" rx="{R * cs:.1f}" fill="{col}" fill-opacity="0.25" stroke="{col}"/>')
        out.append(f'<text x="{X(mx):.1f}" y="{Y(my + TOP) - 4:.1f}" text-anchor="middle" class="xs b" style="fill:{col}">{lab}</text>')
    if dd.get("apexFrom"):                       # 跳跃顶点参考线：floorY + F4 实测顶点高度
        ax0, ax1, fy = dd["apexFrom"]
        ay = fy + VALS["F4"]["apex"]
        out.append(f'<line x1="{X(ax0):.1f}" y1="{Y(ay):.1f}" x2="{X(ax1):.1f}" y2="{Y(ay):.1f}" stroke="#15803D" stroke-dasharray="5 4" stroke-width="1.4"/>')
        out.append(f'<text x="{X(ax0) + 4:.1f}" y="{Y(ay) - 4:.1f}" class="xs b" style="fill:#15803D">原地跳跃时脚底能到的最高处（地面 +{VALS["F4"]["apex"]:.2f} m）</text>')
    for nt in dd.get("notes", []):
        if nt.get("line"):
            (ax, ay), (bx, by) = nt["line"]
            out.append(f'<line x1="{X(ax):.1f}" y1="{Y(ay):.1f}" x2="{X(bx):.1f}" y2="{Y(by):.1f}" stroke="{COL[nt.get("c", "s")][1]}" stroke-dasharray="4 3" stroke-width="1.4"/>')
        if nt.get("t"):
            out.append(f'<text x="{X(nt["x"]):.1f}" y="{Y(nt["y"]):.1f}" text-anchor="{nt.get("a", "middle")}" class="xs b" style="fill:{COL[nt.get("c", "s")][1]}">{esc(nt["t"])}</text>')
    ly = H - 14
    out.append(f'<rect x="20" y="{ly - 9}" width="12" height="12" fill="#A8A29E" fill-opacity="0.55"/><text x="38" y="{ly + 1}" class="xs">Ground 瓦片（颜色 = 瓦片资源）</text>')
    out.append(f'<rect x="230" y="{ly - 9}" width="12" height="12" fill="#DBEAFE" stroke="#93C5FD"/><text x="248" y="{ly + 1}" class="xs">BackGround 瓦片（无碰撞）</text>')
    out.append(f'<line x1="420" y1="{ly - 3}" x2="446" y2="{ly - 3}" stroke="#B91C1C" stroke-width="2"/><text x="452" y="{ly + 1}" class="xs">CompositeCollider2D 合并后的多边形</text>')
    svg = f'<svg viewBox="0 0 {W} {H:.0f}" xmlns="http://www.w3.org/2000/svg">' + "".join(out) + "</svg>"
    return fig(svg, dd.get("cap"))


def md_tmap(dd):
    T = tm_data()
    g = {(x, y) for x, y, _ in T["tilemaps"]["Ground"]["tiles"]}
    bgs = {(x, y) for x, y, _ in T["tilemaps"].get("BackGround", {}).get("tiles", [])}
    x0, x1 = dd.get("x", [-24, 42])
    mk = {}
    for name, (mx, my) in T["markers"].items():
        mk[(math.floor(mx), math.floor(my + BOTTOM + 0.5))] = "P" if name == "player" else "S"
    lines = []
    for y in range(dd.get("ymax", 15) - 1, 0, -1):
        row = "".join(mk.get((x, y), "#" if (x, y) in g else ("·" if (x, y) in bgs else " ")) for x in range(x0, x1))
        lines.append(f"{y:>3} |{row}|")
    lines.append("     " + "".join(str(abs(x) % 10) if x % 5 == 0 else " " for x in range(x0, x1)) + f"   (x 从 {x0} 到 {x1 - 1})")
    return "```text\n" + "\n".join(lines) + "\n图例：# = Ground 瓦片（有碰撞）  · = BackGround 瓦片  P = 玩家出生点  S = 骷髅\n```"


MACROS = {"probe": probe, "wallcov": wallcov, "seam": seam, "tmap": tmap}
MD = {"probe": md_probe, "wallcov": md_wallcov, "seam": md_seam, "tmap": md_tmap}
