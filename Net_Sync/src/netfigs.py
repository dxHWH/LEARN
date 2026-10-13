"""本书专用图宏（build.py 展开为 SVG，build_md.py 转成文本 / 表格）
  lanes  三条路线的逐帧时间线：每个客户端一条泳道，竖线 = 执行了一帧，红块 = 卡顿，橙三角 = 回滚（数字 = 回滚深度）
         数据来自 demos/figdata.json 的 E3.lanes（C++ 实测）
  win    HOK 接收窗口逐步演示：构建时用 Python 真实执行“入窗 → 连续出窗 → 找洞 → 区间合并请求补发 → 超窗走 Recover”
"""
import html

import bpmacros as BM

VALS = {}          # 由 build.py 注入
WARN = []
COL = {
    "b": ("#EAF0FD", "#2F5BD3"), "o": ("#FFF4EC", "#C2410C"), "g": ("#EAF7EF", "#15803D"),
    "p": ("#F3EEFE", "#6D28D9"), "r": ("#FDEEEE", "#B91C1C"), "a": ("#FEF6E4", "#B45309"),
    "s": ("#F3F4F6", "#6B7280"), "t": ("#E6F6F5", "#0F766E"), "k": ("#E5E7EB", "#374151"),
}


def esc(s):
    return html.escape(str(s), quote=True)


def fig(svg, cap):
    c = f"<figcaption>{cap}</figcaption>" if cap else ""
    return f"<figure>\n{svg}\n{c}</figure>"


def get(path):
    v = VALS
    for p in path.split("."):
        v = v[int(p)] if isinstance(v, list) else v[p]
    return v


# ============================================================ lanes
def lanes(d):
    src = get(d["src"])
    a, b = d["a"], d["b"]
    W, LW, R = 860, 178, 14
    X = lambda t: LW + (W - LW - R) * (t - a) / (b - a)
    LH, GH = 26, 30
    o0, o1 = get(d["o"]) if isinstance(d.get("o"), str) else d.get("o", [None, None])
    out = []
    y = 26
    rows_y = []
    for key, rname in d["routes"]:
        out.append(f'<text x="8" y="{y + 14}" class="b" style="font-size:12.5px;fill:#1E3A8A">{esc(rname)}</text>')
        y += GH - 8
        for who, wname in d["who"]:
            rows_y.append((y, key, who, wname))
            y += LH
        y += 10
    H = y + 30
    if o0 is not None:
        out.insert(0, f'<rect x="{X(o0):.1f}" y="20" width="{X(o1) - X(o0):.1f}" height="{H - 50}" fill="#FEF6E4"/>')
        out.append(f'<text x="{(X(o0) + X(o1)) / 2:.1f}" y="16" text-anchor="middle" class="xs b" style="fill:#B45309">{esc(d.get("olab", "断流"))}</text>')
    t = a - a % 500 + (500 if a % 500 else 0)
    while t <= b:
        out.insert(0, f'<line x1="{X(t):.1f}" y1="20" x2="{X(t):.1f}" y2="{H - 30}" stroke="#EEF0F3"/>')
        out.append(f'<text x="{X(t):.1f}" y="{H - 16}" text-anchor="middle" class="xs mono">{t / 1000:g} s</text>')
        t += 500
    for (yy, key, who, wname) in rows_y:
        L = src[key][who]
        mid = yy + LH / 2
        out.append(f'<text x="{LW - 8}" y="{mid + 4}" text-anchor="end" class="sm">{esc(wname)}</text>')
        out.append(f'<line x1="{LW}" y1="{mid}" x2="{W - R}" y2="{mid}" stroke="#CBD5E1"/>')
        for f0, f1 in L["fz"]:
            out.append(f'<rect x="{X(f0):.1f}" y="{yy + 3}" width="{max(1.5, X(f1) - X(f0)):.1f}" height="{LH - 6}" rx="3" fill="#FDEEEE" stroke="#B91C1C" stroke-width="1"/>')
        for te in L["ex"]:
            out.append(f'<line x1="{X(te):.1f}" y1="{mid - 6}" x2="{X(te):.1f}" y2="{mid + 6}" stroke="#15803D" stroke-width="1.1"/>')
        lastlab = -1e9
        for tr, dep in L["rb"]:
            x = X(tr)
            out.append(f'<path d="M{x - 3:.1f},{yy + 1} L{x + 3:.1f},{yy + 1} L{x:.1f},{yy + 5.5} z" fill="#C2410C" fill-opacity="{0.45 if dep < 3 else 1}"/>')
            if dep >= d.get("rblab", 5) and x - lastlab > 26:
                out.append(f'<text x="{x + 4:.1f}" y="{yy + 8}" class="mono b" style="font-size:9px;fill:#C2410C">{dep}</text>')
                lastlab = x
    lx = LW
    ly = H - 2
    leg = [("tick", "执行了一帧逻辑"), ("fz", "卡顿（帧间隔 > 2 帧）"), ("rb", "回滚（浅色 = 重算 1~2 帧；数字 = 重算 ≥ 5 帧）")]
    for k, nm in leg:
        if k == "tick":
            out.append(f'<line x1="{lx + 5}" y1="{ly - 10}" x2="{lx + 5}" y2="{ly}" stroke="#15803D" stroke-width="1.4"/>')
        elif k == "fz":
            out.append(f'<rect x="{lx}" y="{ly - 10}" width="14" height="10" rx="2" fill="#FDEEEE" stroke="#B91C1C"/>')
        else:
            out.append(f'<path d="M{lx},{ly - 9} L{lx + 10},{ly - 9} L{lx + 5},{ly - 2} z" fill="#C2410C"/>')
        out.append(f'<text x="{lx + 18}" y="{ly}" class="xs">{esc(nm)}</text>')
        lx += 40 + 11 * len(nm)
    svg = f'<svg viewBox="0 0 {W} {H + 4:.0f}" xmlns="http://www.w3.org/2000/svg">' + "".join(out) + "</svg>"
    return fig(svg, d.get("cap"))


def md_lanes(d):
    src = get(d["src"])
    rows = ["| 路线 | 客户端 | 窗口内执行帧数 | 卡顿区间（s） | 回滚次数（最深） |", "|---|---|---|---|---|"]
    for key, rname in d["routes"]:
        for who, wname in d["who"]:
            L = src[key][who]
            fz = "、".join(f"{x / 1000:.2f}–{y / 1000:.2f}" for x, y in L["fz"]) or "无"
            rb = f"{len(L['rb'])}（{max([r[1] for r in L['rb']], default=0)}）" if L["rb"] else "0"
            rows.append(f"| {rname} | {wname} | {len(L['ex'])} | {fz} | {rb} |")
    return "\n".join(rows)


# ============================================================ win：HOK 接收窗口（构建时真实执行）
def win_run(d):
    L = d.get("L", 16)          # 演示用窗口长度（真实为 900）
    beg = d["base"]
    have = set()
    maxf = beg - 1
    consumed_upto = beg - 1
    steps = []
    nseg = d.get("seg", 3)
    for st in d["steps"]:
        acts = []
        recv = list(st.get("recv", []))
        if "pkt" in st:
            k = st.get("k", 3)
            recv = list(range(st["pkt"], st["pkt"] - k, -1))      # 冗余帧倒序：新 → 旧
        stop_at = None
        if recv:
            took = []
            for f in recv:
                if f < beg or f in have:
                    if st.get("trunc", True) and "pkt" in st and f < beg:
                        stop_at = f
                        break
                    acts.append(f"{f} 已处理过 → 丢弃")
                    continue
                if f >= beg + L:
                    acts.append(f"{f} 超出窗口上界 {beg + L - 1} → 进入 Recover 追帧")
                    continue
                have.add(f)
                took.append(f)
                maxf = max(maxf, f)
            if took:
                acts.insert(0, "入窗 " + "、".join(map(str, took)))
            if stop_at is not None:
                acts.append(f"遇到已处理的 {stop_at} → 停止解包（倒序截断）")
            run = []
            while beg in have:
                have.discard(beg)
                run.append(beg)
                consumed_upto = beg
                beg += 1
            if run:
                acts.append(f"连续出窗 {run[0]}" + (f"–{run[-1]}" if len(run) > 1 else "") + " → 交付逻辑层")
            elif recv and maxf > beg:
                acts.append(f"期待帧 {beg} 未到 → 后面的帧只能等")
        if st.get("tick"):
            if maxf > beg or st.get("timeout"):
                hi = min(maxf + 1, beg + L)
                segs, cur = [], None
                for f in range(beg, hi):
                    if f not in have:
                        if cur and cur[1] == f - 1:
                            cur[1] = f
                        else:
                            cur = [f, f]
                            segs.append(cur)
                    else:
                        cur = None
                if not segs:
                    segs = [[beg, -1]]                     # 没有已知空洞但超时：从期待帧开始“之后能给的都给我”
                if len(segs) > nseg - 1:                   # 区间数有上限，最后一个位置留给开放区间
                    capped = segs[:nseg - 1] + [[segs[nseg - 1][0], -1]]
                else:
                    capped = segs
                txt = "；".join(f"[{a}, {b}]" if b != -1 else f"[{a}, -1（之后能给的都给我）]" for a, b in capped)
                acts.append(("超时 90 ms 未推进 → " if st.get("timeout") else "发现空洞 → ") + "请求补发 1036：" + txt)
            else:
                acts.append("无空洞，什么也不做")
        steps.append(dict(lab=st.get("lab", ""), beg=beg, maxf=maxf, have=set(have), done=consumed_upto, acts=acts))
    return steps


def win(d):
    steps = win_run(d)
    L = d.get("L", 16)
    view0 = d.get("view", d["base"])
    nview = d.get("nview", L + 2)
    W = 860
    LW, CW = 30, 21
    gx = LW
    AX = gx + nview * CW + 14
    AW = W - AX - 6
    CIRC = "①②③④⑤⑥⑦⑧⑨⑩⑪⑫"
    out = []
    y = 30
    for k in range(nview):
        out.append(f'<text x="{gx + k * CW + CW / 2:.1f}" y="18" text-anchor="middle" class="mono" style="font-size:9px;fill:#6B7280">{(view0 + k) % 1000:03d}</text>')
    out.append(f'<text x="{AX}" y="18" class="xs b">这一步发生了什么（构建时真实执行）</text>')
    RH = 46
    for si, st in enumerate(steps):
        out.append(f'<text x="{LW - 6}" y="{y + 20}" text-anchor="end" class="b" style="font-size:13px;fill:#1E3A8A">{CIRC[si]}</text>')
        for k in range(nview):
            f = view0 + k
            x = gx + k * CW
            if f <= st["done"]:
                fill, stroke, dash = "#E5E7EB", "#9CA3AF", ""
            elif f in st["have"]:
                fill, stroke, dash = "#BBF7D0", "#15803D", ""
            elif f < st["maxf"]:
                fill, stroke, dash = "#FFFFFF", "#B91C1C", ' stroke-dasharray="3 2"'
            elif f >= st["beg"] + L:
                fill, stroke, dash = "#F8FAFC", "#E2E8F0", ""
            else:
                fill, stroke, dash = "#FFFFFF", "#CBD5E1", ""
            out.append(f'<rect x="{x + 1}" y="{y + 6}" width="{CW - 2}" height="18" rx="3" fill="{fill}" stroke="{stroke}" stroke-width="1.2"{dash}/>')
        bx = gx + (st["beg"] - view0) * CW + CW / 2
        if 0 <= st["beg"] - view0 < nview:
            out.append(f'<path d="M{bx - 4:.1f},{y + 33} L{bx + 4:.1f},{y + 33} L{bx:.1f},{y + 26} z" fill="#2F5BD3"/>')
            out.append(f'<text x="{bx:.1f}" y="{y + 43}" text-anchor="middle" class="mono" style="font-size:8.5px;fill:#2F5BD3">beg</text>')
        if st["maxf"] >= st["beg"] and 0 <= st["maxf"] - view0 < nview:
            mx = gx + (st["maxf"] - view0) * CW + CW / 2
            out.append(f'<path d="M{mx - 4:.1f},{y + 1} L{mx + 4:.1f},{y + 1} L{mx:.1f},{y + 6} z" fill="#C2410C"/>')
        we = st["beg"] + L - view0
        if 0 < we <= nview:
            xx = gx + we * CW
            out.append(f'<line x1="{xx:.1f}" y1="{y + 3}" x2="{xx:.1f}" y2="{y + 27}" stroke="#6D28D9" stroke-width="1.6" stroke-dasharray="3 2"/>')
        ty = y + 12
        for ln in BM.wrap(st["lab"], 10.8, AW):
            out.append(f'<text x="{AX}" y="{ty}" class="b" style="font-size:10.8px;fill:#1E3A8A">{esc(ln)}</text>')
            ty += 13.5
        for a in st["acts"]:
            for k, ln in enumerate(BM.wrap(a, 10.2, AW - 10)):
                out.append(f'<text x="{AX + (10 if k else 0)}" y="{ty}" style="font-size:10.2px;fill:#1F2937">{"· " if not k else ""}{esc(ln)}</text>')
                ty += 13
        y += max(RH, ty - y + 6)
    ly = y + 8
    lx = 12
    for fill, stroke, dash, nm in [("#E5E7EB", "#9CA3AF", "", "已消费"), ("#BBF7D0", "#15803D", "", "已到达、等待出窗"),
                                    ("#FFFFFF", "#B91C1C", ' stroke-dasharray="3 2"', "空洞（后面的帧已到）"), ("#FFFFFF", "#CBD5E1", "", "未到达")]:
        out.append(f'<rect x="{lx}" y="{ly - 10}" width="14" height="12" rx="2" fill="{fill}" stroke="{stroke}"{dash}/>')
        out.append(f'<text x="{lx + 18}" y="{ly}" class="xs">{nm}</text>')
        lx += 36 + 11 * len(nm)
    out.append(f'<path d="M{lx + 2},{ly} L{lx + 10},{ly} L{lx + 6},{ly - 7} z" fill="#2F5BD3"/><text x="{lx + 14}" y="{ly}" class="xs">期待帧 _begFrqNo</text>')
    lx += 120
    out.append(f'<path d="M{lx + 2},{ly - 9} L{lx + 10},{ly - 9} L{lx + 6},{ly - 3} z" fill="#C2410C"/><text x="{lx + 14}" y="{ly}" class="xs">已收最大帧</text>')
    lx += 86
    out.append(f'<line x1="{lx + 6}" y1="{ly - 11}" x2="{lx + 6}" y2="{ly + 1}" stroke="#6D28D9" stroke-width="1.6" stroke-dasharray="3 2"/><text x="{lx + 12}" y="{ly}" class="xs">窗口上界</text>')
    H = ly + 10
    svg = f'<svg viewBox="0 0 {W} {H}" xmlns="http://www.w3.org/2000/svg">' + "".join(out) + "</svg>"
    return fig(svg, d.get("cap"))


def md_win(d):
    steps = win_run(d)
    rows = ["| 步骤 | 期待帧 beg | 已收最大帧 | 窗口内已到未消费 | 发生了什么 |", "|---|---|---|---|---|"]
    for st in steps:
        have = "、".join(map(str, sorted(st["have"]))) or "—"
        rows.append(f"| {st['lab']} | {st['beg']} | {st['maxf']} | {have} | {'；'.join(st['acts'])} |")
    return "\n".join(rows)


MACROS = {"lanes": lanes, "win": win}
MD = {"lanes": md_lanes, "win": md_win}
