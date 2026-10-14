"""蓝图风格事件图表 / 时序图 / 树 / 生命周期条 —— 电子书（SVG）与 md（文本）共用同一份 JSON

  <!--bpgraph {json} -->  蓝图风格节点图：节点按网格(x 列, y 行)自动排版，执行线自动走线
  <!--seq {json} -->      时序图：参与者 + 消息（call / net / rep / 自注释）
  <!--tree {json} -->     树 + 多列说明（继承树、组件树、行为树）
  <!--spans {json} -->    生命周期条：对象 × 阶段
  <!--bpnet {json} -->    类的网络规格面板（Class Defaults 复制设置 + 变量复制 + 事件 Replicates / 执行端）
  <!--repmx {json} -->    复制接收矩阵：每个复制变量在 Listen Server 双人局里发给谁、OnRep 在哪执行

bpgraph 节点可选字段：
  net  执行端标签（见 NETB）        rep  复制标注："RN:条件 → 去向" / "R:…" / "L:…"（不复制）
  var  摘要里显示的变量名（默认从 "Set X" 推出）
图下方会自动生成“网络摘要”：本图各事件的执行端、写入的复制变量 / 本地变量、跨端调用、组播。
"""
import html
import sys

COL = {
    "b": ("#EAF0FD", "#2F5BD3"), "o": ("#FFF4EC", "#C2410C"), "g": ("#EAF7EF", "#15803D"),
    "p": ("#F3EEFE", "#6D28D9"), "r": ("#FDEEEE", "#B91C1C"), "a": ("#FEF6E4", "#B45309"),
    "s": ("#F3F4F6", "#6B7280"), "t": ("#E6F6F5", "#0F766E"),
}


def esc(s):
    return html.escape(str(s), quote=True)


def fig(svg, cap):
    c = f"<figcaption>{cap}</figcaption>" if cap else ""
    return f"<figure>\n{svg}\n{c}</figure>"


def tw(s, size=11.0, bold=False):
    """估算文字宽度（px）：中日韩字符 ≈ 1 em，ASCII ≈ 0.55~0.62 em"""
    w = 0.0
    for ch in str(s):
        o = ord(ch)
        if o > 0x2E80:
            w += size
        elif ch.isupper() or ch.isdigit():
            w += size * 0.63
        elif ch in " .,:;|'!il()[]":
            w += size * 0.33
        else:
            w += size * 0.55
    return w * (1.07 if bold else 1.0)


def warn(msg):
    print("[layout] " + msg, file=sys.stderr)


def fit(text, base, avail, bold=False, floor=8.6, what=""):
    """字号自适应：放不下就按比例缩小（不低于 floor），仍放不下才告警"""
    avail *= 0.93                      # 估算偏乐观，留 7% 余量
    w = tw(text, base, bold)
    if w <= avail:
        return base
    size = base * avail / w
    if size < floor:
        warn(f"{what}过长（{w:.0f}>{avail:.0f}）：{text}")
        return floor
    return round(size, 2)


def wrap(text, size, width):
    """按估算宽度把一行文字折成多行（中文逐字、英文按词）"""
    out, cur = [], ""
    for tok in __import__("re").findall(r"[\x00-\x7f]+?(?=[^\x00-\x7f]|\s|$)|\s|[^\x00-\x7f]", text):
        if tw(cur + tok, size) > width and cur.strip():
            out.append(cur.rstrip()); cur = tok.lstrip()
        else:
            cur += tok
    if cur.strip():
        out.append(cur.rstrip())
    return out


REPK = {"RN": ("⇄ RepNotify", "p"), "R": ("⇄ Replicated", "b"), "L": ("○ 不复制", "s"), "RD": ("⇠ 读复制值", "t")}


def rep_info(rep):
    """"RN:Owner Only → 拥有者 OnRep_Ammo" -> ("⇄ RepNotify · Owner Only → 拥有者 OnRep_Ammo", 色, 种类)"""
    kind, _, rest = rep.partition(":")
    lab, c = REPK[kind]
    if rest:
        lab += (" " if rest.startswith("→") else " · ") + rest
    return lab, c, kind


def rep_lines(rep, width):
    """复制标注放不下时在“→”处断成两行（第二行写去向），再不行才按宽度折"""
    full, _, kind = rep_info(rep)
    if tw(full, 9.6, True) <= width * 0.93:
        return [full]
    if kind == "RD":  # 读复制值：首行写类别，后面按宽度折行列出读的变量
        return [REPK["RD"][0] + "："] + wrap(rep.partition(":")[2], 10.2, width * 0.9)
    if "→" not in full:
        return [full]
    head, _, tail = full.partition("→")
    return [head.strip(), "→ " + tail.strip()]


def var_name(n):
    if n.get("var"):
        return n["var"]
    t = n["t"]
    if t.startswith("Set "):
        t = t[4:]
    return t.split("（")[0].split("(")[0].strip()


def net_summary(nodes):
    """一张事件图表的网络摘要：返回 [(标签, 内容)]"""
    ex = {}
    for n in nodes:
        if n["k"] in ("event", "cevent") and n.get("net"):
            lab = bp_badge(n)[0]
            ex.setdefault(lab, []).append(n["t"])
    reps = [f"{var_name(n)}（{rep_info(n['rep'])[0].replace('⇄ ', '').split(' →')[0]}）" for n in nodes if n.get("rep") and rep_info(n["rep"])[2] not in ("L", "RD")]
    reads = [n["rep"].partition(":")[2].split("→")[0].strip() for n in nodes if n.get("rep") and rep_info(n["rep"])[2] == "RD"]
    locs = [var_name(n) for n in nodes if n.get("rep") and rep_info(n["rep"])[2] == "L"]
    rpcs = [f"{n['t']}（{bp_badge(n)[0]}）" for n in nodes if n["k"] not in ("event", "cevent") and n.get("net") in ("SR", "CR", "CU", "MU", "M")]
    mcs = [f"{n['t']}（{'定义' if n['k'] in ('event', 'cevent') else '调用'}）" for n in nodes if n.get("net") in ("M", "MU")]
    rows = [("▶ 执行端", "；".join(f"{k}：{'、'.join(dict.fromkeys(v))}" for k, v in ex.items()) or "—"),
            ("⇄ 写入的复制变量", "、".join(dict.fromkeys(reps)) or "无"),
            ("○ 只在本端的变量", "、".join(dict.fromkeys(locs)) or "无"),
            *([("⇠ 读取的复制数据", "、".join(dict.fromkeys(reads)))] if reads else []),
            ("↗ 跨端调用（RPC）", "、".join(dict.fromkeys(rpcs)) or "无"),
            ("◎ 组播 NetMulticast", "、".join(dict.fromkeys(mcs)) or "无（持续状态用 RepNotify，一次性表现才用多播）")]
    return rows


# ================================================================== bpgraph
BPK = {  # kind -> (表头底色, 主色)
    "event": ("#FDE2E2", "#B91C1C"), "cevent": ("#FDE2E2", "#B91C1C"),
    "func": ("#DBEAFE", "#1D4ED8"), "pure": ("#DCFCE7", "#15803D"),
    "flow": ("#E5E7EB", "#374151"), "set": ("#EDE9FE", "#6D28D9"),
    "call": ("#FFEDD5", "#C2410C"), "timer": ("#CCFBF1", "#0F766E"), "ret": ("#E5E7EB", "#374151"),
}
NETB = {
    "S": ("仅服务器", "b"), "C": ("仅拥有者客户端", "a"), "M": ("多播 · 各端", "o"),
    "L": ("本地控制端", "g"), "A": ("所有端", "s"), "R": ("RepNotify · 各端", "p"),
    "SR": ("Run on Server · Reliable", "b"), "CR": ("Run on owning Client · Reliable", "a"),
    "MU": ("Multicast · Unreliable", "o"), "U": ("仅本机 UI", "t"), "P": ("本机（各进程各自）", "t"),
    "CU": ("Run on owning Client · Unreliable", "a"), "SO": ("服务器 + 拥有者客户端", "a"),
}
HR = 16


def bp_norm(nd):
    k = nd.get("k", "func")
    ex = nd.get("ex")
    if ex is None:
        ex = [] if k in ("pure", "ret") else ["then"]
    nin = nd.get("nin", k not in ("event", "cevent", "pure"))
    return dict(nd, k=k, ex=ex, nin=nin, i=nd.get("i", []), o=nd.get("o", []))


def bp_badge(n):
    if not n.get("net"):
        return None
    lab, c = NETB.get(n["net"], (n["net"], "s"))
    return n.get("netl", lab), c


def bp_layout(d):
    W = d.get("w", 860)
    PADX, GAP = 14, 20
    RG = d.get("rg", 52)
    TOP = (46 if d.get("title") else 30) + (22 if d.get("g") else 0)
    nodes = [bp_norm(n) for n in d["nodes"]]
    by = {n["id"]: n for n in nodes}
    cols = d.get("cols") or max(n["x"] + n.get("span", 1) for n in nodes)
    colW = (W - 2 * PADX) / cols
    for n in nodes:
        n["w"] = colW * n.get("span", 1) - GAP
        n["hh"] = 22 + (13 if n.get("s") else 0)
        n["right"] = (n["ex"] if len(n["ex"]) > 1 else []) + n["o"]
        body = len(n["i"]) + len(n["right"])
        n["replines"] = rep_lines(n["rep"], n["w"] - 18) if n.get("rep") else []
        n["h"] = n["hh"] + (body * HR + 8 if body else 4) + ((7 + 12 * len(n["replines"])) if n.get("rep") else 0)
        n["px"] = PADX + n["x"] * colW
    nrow = max(n["y"] for n in nodes) + 1
    rowH = [max([n["h"] + n.get("dy", 0) for n in nodes if n["y"] == r] or [0]) for r in range(nrow)]
    rowTop = [TOP]
    for r in range(nrow - 1):
        rowTop.append(rowTop[-1] + rowH[r] + RG)
    for n in nodes:
        n["py"] = rowTop[n["y"]] + n.get("dy", 0)
    return nodes, by, rowTop, rowH, W, PADX


def bp_pin_out(n, pin):
    if len(n["ex"]) == 1 and pin == n["ex"][0]:
        return n["px"] + n["w"], n["py"] + 11
    j = n["right"].index(pin)
    return n["px"] + n["w"], n["py"] + n["hh"] + (len(n["i"]) + j) * HR + 12


def bpgraph(d):
    nodes, by, rowTop, rowH, W, PADX = bp_layout(d)
    o_bg, o_ln, o_nd = [], [], []
    title = d.get("title", "")
    if title:
        o_bg.append(f'<text x="{PADX}" y="20" class="b" style="font-size:13px">{esc(title)}</text>')
    used_net = []
    used_rep = set()
    # ---- 注释框
    for g in d.get("g", []):
        gs = [by[i] for i in g["ids"]]
        x0 = min(n["px"] for n in gs) - 8; y0 = min(n["py"] for n in gs) - 32
        x1 = max(n["px"] + n["w"] for n in gs) + 8; y1 = max(n["py"] + n["h"] for n in gs) + 8
        c = COL[g.get("c", "s")]
        o_bg.append(f'<rect x="{x0:.1f}" y="{y0:.1f}" width="{x1 - x0:.1f}" height="{y1 - y0:.1f}" rx="10" fill="{c[0]}" fill-opacity="0.55" stroke="{c[1]}" stroke-dasharray="5 4"/>')
        o_bg.append(f'<text x="{x0 + 10:.1f}" y="{y0 + 13:.1f}" class="b" style="font-size:11px;fill:{c[1]}">{esc(g["t"])}</text>')
        if tw(g["t"], 11, True) + 20 > x1 - x0:
            warn(f"注释框标题过长：{g['t']}")
    # ---- 执行线
    for l in d.get("l", []):
        src, dst = l[0], l[1]
        sid, _, pin = src.partition(":")
        sn, tn = by[sid], by[dst]
        pin = pin or sn["ex"][0]
        if pin not in sn["ex"]:
            warn(f"{sid} 没有执行引脚 {pin}")
            continue
        sx, sy = bp_pin_out(sn, pin)
        tx, ty = tn["px"], tn["py"] + 11
        if not tn["nin"]:
            warn(f"{dst} 没有执行输入")
        skip = tn["y"] == sn["y"] and tn["x"] > sn["x"] + sn.get("span", 1) and any(
            m["y"] == sn["y"] and sn["x"] < m["x"] < tn["x"] for m in by.values())
        if skip:   # 同一行跨过中间节点：从下方绕行，避免穿过节点
            sr = sn["y"]; my = rowTop[sr] + rowH[sr] + 14
            p = f"M{sx:.1f},{sy:.1f} H{sx + 10:.1f} V{my:.1f} H{tx - 12:.1f} V{ty:.1f} H{tx:.1f}"
        elif tx > sx + 16:
            c = max(26, (tx - sx) * 0.45)
            p = f"M{sx:.1f},{sy:.1f} C{sx + c:.1f},{sy:.1f} {tx - c:.1f},{ty:.1f} {tx:.1f},{ty:.1f}"
        else:
            sr, tr = sn["y"], tn["y"]
            if tr > sr:
                my = rowTop[sr] + rowH[sr] + 14
            elif tr < sr:
                my = rowTop[sr] - 40
            else:
                my = rowTop[sr] + rowH[sr] + 14
            p = f"M{sx:.1f},{sy:.1f} H{sx + 10:.1f} V{my:.1f} H{tx - 12:.1f} V{ty:.1f} H{tx:.1f}"
        o_ln.append(f'<path d="{p}" fill="none" stroke="#4B5563" stroke-width="1.7"/>')
        if len(l) > 2:
            o_ln.append(f'<text x="{(sx + tx) / 2:.1f}" y="{min(sy, ty) - 4:.1f}" text-anchor="middle" class="xs">{esc(l[2])}</text>')
    # ---- 数据线
    for a, b in d.get("d", []):
        an, ap = a.split("."); bn, bp_ = b.split(".")
        A, B = by[an], by[bn]
        ja = int(ap[1:]); jb = int(bp_[1:])
        ax = A["px"] + A["w"] - 8; ay = A["py"] + A["hh"] + (len(A["i"]) + len(A["right"]) - len(A["o"]) + ja) * HR + 12
        bx = B["px"] + 8; by_ = B["py"] + B["hh"] + jb * HR + 12
        c = max(30, abs(bx - ax) * 0.4)
        o_ln.append(f'<path d="M{ax:.1f},{ay:.1f} C{ax + c:.1f},{ay:.1f} {bx - c:.1f},{by_:.1f} {bx:.1f},{by_:.1f}" fill="none" stroke="#2F5BD3" stroke-width="1.2" stroke-opacity="0.8"/>')
    # ---- 节点
    for n in nodes:
        hf, st = BPK.get(n["k"], BPK["func"])
        px, py, w, h, hh = n["px"], n["py"], n["w"], n["h"], n["hh"]
        o_nd.append(f'<rect x="{px:.1f}" y="{py:.1f}" width="{w:.1f}" height="{h:.1f}" rx="6" fill="#FFFFFF" stroke="{st}" stroke-width="1.3"/>')
        o_nd.append(f'<path d="M{px:.1f},{py + hh:.1f} V{py + 6:.1f} Q{px:.1f},{py:.1f} {px + 6:.1f},{py:.1f} H{px + w - 6:.1f} Q{px + w:.1f},{py:.1f} {px + w:.1f},{py + 6:.1f} V{py + hh:.1f} Z" fill="{hf}" stroke="{st}" stroke-width="1.3"/>')
        tx0 = px + (16 if n["nin"] else 9)
        room = w - (tx0 - px) - (16 if len(n["ex"]) == 1 else 8)
        fs = fit(n["t"], 11.5, room, True, what="标题")
        o_nd.append(f'<text x="{tx0:.1f}" y="{py + 15:.1f}" class="b" style="font-size:{fs}px;fill:{st}">{esc(n["t"])}</text>')
        if n.get("s"):
            fs = fit(n["s"], 10, room, what="副标题")
            o_nd.append(f'<text x="{tx0:.1f}" y="{py + 29:.1f}" style="font-size:{fs}px;fill:#4B5563">{esc(n["s"])}</text>')
        if n["nin"]:
            o_nd.append(f'<polygon points="{px + 4:.1f},{py + 6:.1f} {px + 11:.1f},{py + 11:.1f} {px + 4:.1f},{py + 16:.1f}" fill="#FFFFFF" stroke="#374151" stroke-width="1.2"/>')
        if len(n["ex"]) == 1:
            o_nd.append(f'<polygon points="{px + w - 12:.1f},{py + 6:.1f} {px + w - 5:.1f},{py + 11:.1f} {px + w - 12:.1f},{py + 16:.1f}" fill="#FFFFFF" stroke="#374151" stroke-width="1.2"/>')
        for j, s in enumerate(n["i"]):
            yy = py + hh + j * HR + 12
            o_nd.append(f'<circle cx="{px + 8:.1f}" cy="{yy - 3.5:.1f}" r="3" fill="#2F5BD3"/>')
            fs = fit(s, 10.5, w - 20, what="输入引脚")
            o_nd.append(f'<text x="{px + 15:.1f}" y="{yy:.1f}" style="font-size:{fs}px;fill:#374151">{esc(s)}</text>')
        for j, s in enumerate(n["right"]):
            yy = py + hh + (len(n["i"]) + j) * HR + 12
            is_exec = j < len(n["right"]) - len(n["o"])
            if is_exec:
                o_nd.append(f'<polygon points="{px + w - 12:.1f},{yy - 8.5:.1f} {px + w - 5:.1f},{yy - 3.5:.1f} {px + w - 12:.1f},{yy + 1.5:.1f}" fill="#FFFFFF" stroke="#374151" stroke-width="1.2"/>')
                fs = fit(s, 10.5, w - 22, True, what="输出引脚")
                o_nd.append(f'<text x="{px + w - 16:.1f}" y="{yy:.1f}" text-anchor="end" class="b" style="font-size:{fs}px;fill:#374151">{esc(s)}</text>')
            else:
                o_nd.append(f'<circle cx="{px + w - 8:.1f}" cy="{yy - 3.5:.1f}" r="3" fill="#2F5BD3"/>')
                fs = fit(s, 10.5, w - 22, what="输出引脚")
                o_nd.append(f'<text x="{px + w - 15:.1f}" y="{yy:.1f}" text-anchor="end" style="font-size:{fs}px;fill:#374151">{esc(s)}</text>')
        if n.get("rep"):
            rl, rc, rk = rep_info(n["rep"])
            c = COL[rc]
            nl = len(n["replines"]); ch = 2 + 12 * nl
            o_nd.append(f'<rect x="{px + 4:.1f}" y="{py + h - ch - 4:.1f}" width="{w - 8:.1f}" height="{ch}" rx="4" fill="{c[0]}" stroke="{c[1]}" stroke-width="0.9"{' stroke-dasharray="3 2"' if rk == "L" else ""}/>')
            for k, t in enumerate(n["replines"]):
                fs = fit(t, 9.6, w - 16, True, what="复制标注")
                o_nd.append(f'<text x="{px + 9:.1f}" y="{py + h - ch + 6.4 + 12 * k:.1f}" class="b" style="font-size:{fs}px;fill:{c[1]}">{esc(t)}</text>')
            used_rep.add(rk)
        bd = bp_badge(n)
        if bd:
            lab, ck = bd
            if (lab, ck) not in used_net:
                used_net.append((lab, ck))
            bw = tw(lab, 9.5, True) + 12
            c = COL[ck]
            o_nd.append(f'<rect x="{px + w - bw:.1f}" y="{py - 15:.1f}" width="{bw:.1f}" height="14" rx="7" fill="{c[0]}" stroke="{c[1]}"/>')
            o_nd.append(f'<text x="{px + w - bw / 2:.1f}" y="{py - 5:.1f}" text-anchor="middle" class="b" style="font-size:9.5px;fill:{c[1]}">{esc(lab)}</text>')
    bottom = max(n["py"] + n["h"] for n in nodes) + (14 if d.get("g") else 6)
    # ---- 图例
    y = bottom + 18
    lg = []
    x = PADX
    lg.append(f'<polygon points="{x},{y - 9} {x + 7},{y - 4} {x},{y + 1}" fill="#FFFFFF" stroke="#374151" stroke-width="1.2"/>')
    lg.append(f'<text x="{x + 12}" y="{y}" class="xs">执行引脚 / 执行线</text>')
    x += 124
    lg.append(f'<circle cx="{x + 3}" cy="{y - 4}" r="3" fill="#2F5BD3"/><text x="{x + 11}" y="{y}" class="xs">数据引脚（← 表示连入的值）</text>')
    x += 182
    for lab, ck in used_net:
        c = COL[ck]; bw = tw(lab, 9.5, True) + 12
        if x + bw > W - 10:
            x = PADX; y += 20
        lg.append(f'<rect x="{x}" y="{y - 11}" width="{bw:.1f}" height="14" rx="7" fill="{c[0]}" stroke="{c[1]}"/>')
        lg.append(f'<text x="{x + bw / 2:.1f}" y="{y - 1}" text-anchor="middle" class="b" style="font-size:9.5px;fill:{c[1]}">{esc(lab)}</text>')
        x += bw + 10
    for rk in ("RN", "R", "L", "RD"):
        if rk in used_rep:
            lab = {"RN": "⇄ RepNotify：复制 + 回调", "R": "⇄ Replicated：只复制", "L": "○ 不复制", "RD": "⇠ 只读：复制下来的值"}[rk]
            c = COL[REPK[rk][1]]; bw = tw(lab, 9.5, True) + 14
            if x + bw > W - 10:
                x = PADX; y += 20
            lg.append(f'<rect x="{x}" y="{y - 11}" width="{bw:.1f}" height="14" rx="4" fill="{c[0]}" stroke="{c[1]}"{' stroke-dasharray="3 2"' if rk == "L" else ""}/>')
            lg.append(f'<text x="{x + 7}" y="{y - 1}" class="b" style="font-size:9.5px;fill:{c[1]}">{esc(lab)}</text>')
            x += bw + 10
    # ---- 网络摘要
    if d.get("sum", True):
        y += 14
        rows = net_summary(nodes)
        lines = []
        for k, v in rows:
            parts = wrap(v, 10.5, W - 2 * PADX - 150)
            for j, ptxt in enumerate(parts):
                lines.append((k if j == 0 else "", ptxt))
        bh = len(lines) * 16 + 30
        lg.append(f'<rect x="{PADX}" y="{y}" width="{W - 2 * PADX}" height="{bh}" rx="8" fill="#F8FAFC" stroke="#CBD5E1"/>')
        lg.append(f'<text x="{PADX + 12}" y="{y + 17}" class="b" style="font-size:11px;fill:#1F2937">本图网络摘要</text>')
        yy = y + 34
        for k, v in lines:
            if k:
                lg.append(f'<text x="{PADX + 12}" y="{yy}" class="b" style="font-size:10.5px;fill:#374151">{esc(k)}</text>')
            lg.append(f'<text x="{PADX + 150}" y="{yy}" style="font-size:10.5px;fill:#1F2937">{esc(v)}</text>')
            yy += 16
        y += bh
    H = y + 12
    svg = f'<svg viewBox="0 0 {W} {H:.0f}" xmlns="http://www.w3.org/2000/svg">' + "".join(o_bg + o_ln + o_nd + lg) + "</svg>"
    return fig(svg, d.get("cap"))


def md_bpgraph(d):
    nodes = [bp_norm(n) for n in d["nodes"]]
    by = {n["id"]: n for n in nodes}
    links, incoming = {}, set()
    for l in d.get("l", []):
        sid, _, pin = l[0].partition(":")
        pin = pin or by[sid]["ex"][0]
        links.setdefault((sid, pin), []).append(l[1]); incoming.add(l[1])

    def desc(n):
        s = n["t"]
        if n.get("s"): s += f"（{n['s']}）"
        bd = bp_badge(n)
        if bd: s += f"〔{bd[0]}〕"
        if n.get("rep"): s += f"〔{rep_info(n['rep'])[0]}〕"
        if n["i"]: s += "　入：" + "；".join(n["i"])
        if n["o"]: s += "　出：" + "、".join(n["o"])
        return s

    out, seen = [], set()

    def walk(nid, ind):
        n = by[nid]
        for pin in n["ex"]:
            ts = links.get((nid, pin), [])
            if not ts:
                continue
            sub = ind
            if len(n["ex"]) > 1:
                out.append("  " * ind + f"[{pin}]"); sub = ind + 1
            for t in ts:
                if t in seen:
                    out.append("  " * sub + f"→ （回到 {by[t]['t']}）"); continue
                seen.add(t); out.append("  " * sub + "→ " + desc(by[t])); walk(t, sub)

    for n in sorted(nodes, key=lambda n: (n["y"], n["x"])):
        if n["id"] in incoming or n["k"] == "pure" or n["id"] in seen:
            continue
        seen.add(n["id"]); out.append("■ " + desc(n)); walk(n["id"], 1); out.append("")
    pures = [n for n in nodes if n["id"] not in seen]
    if pures:
        out.append("数据 / 独立节点：" + "；".join(desc(n) for n in pures))
    for g in d.get("g", []):
        out.append(f"注释框「{g['t']}」：" + "、".join(by[i]["t"] for i in g["ids"]))
    head = f"**{d['title']}**\n\n" if d.get("title") else ""
    tail = ""
    if d.get("sum", True):
        tail = "\n\n" + "\n".join(f"- **{k}**：{v}" for k, v in net_summary(nodes))
    return head + "```text\n" + "\n".join(out).rstrip() + "\n```" + tail


# ================================================================== seq
SEQK = {"call": ("#374151", "", "a", "调用"), "net": ("#2F5BD3", "6 3", "ab", "RPC / 网络消息"),
        "rep": ("#6D28D9", "2 3", "ap", "属性复制"), "local": ("#15803D", "", "ag", "本地事件")}


def seq(d):
    P = d["p"]; n = len(P); W = d.get("w", 860); L, R = 34, 10
    colW = (W - L - R) / n
    cx = [L + colW * (k + 0.5) for k in range(n)]
    pc = d.get("pc", ["s"] * n)
    o_top, o_life, o = [], [], []
    grp = d.get("grp", [])
    y = 8 + (26 if grp else 0)
    for a_, b_, lab_, c_ in grp:
        c = COL[c_]
        gx0 = cx[a_] - colW / 2 + 2; gx1 = cx[b_] + colW / 2 - 2
        o_top.append(f'<rect x="{gx0:.1f}" y="6" width="{gx1 - gx0:.1f}" height="20" rx="5" fill="{c[1]}"/>')
        o_top.append(f'<text x="{(gx0 + gx1) / 2:.1f}" y="20" text-anchor="middle" class="b wt" style="font-size:11px">{esc(lab_)}</text>')
    bh = 38
    for k, name in enumerate(P):
        c = COL[pc[k]]
        o_top.append(f'<rect x="{cx[k] - colW / 2 + 5:.1f}" y="{y}" width="{colW - 10:.1f}" height="{bh}" rx="8" fill="{c[0]}" stroke="{c[1]}" stroke-width="1.4"/>')
        lines = name.split("\n")
        for j, ln in enumerate(lines):
            yy = y + bh / 2 + 4 + (j - (len(lines) - 1) / 2) * 14
            o_top.append(f'<text x="{cx[k]:.1f}" y="{yy:.1f}" text-anchor="middle" class="b" style="font-size:11.5px;fill:{c[1]}">{esc(ln)}</text>')
            if tw(ln, 11.5, True) > colW - 16:
                warn(f"参与者名过长：{ln}")
    y0 = y + bh
    y = y0 + 16
    num = 0
    used = set()
    for st in d["s"]:
        if st[0] == "—":
            o.append(f'<rect x="{L - 26}" y="{y - 2}" width="{W - L + 22}" height="20" rx="4" fill="#FEF6E4"/>')
            o.append(f'<text x="{L - 18}" y="{y + 12}" class="b" style="font-size:11px;fill:#B45309">{esc(st[1])}</text>')
            y += 28
            continue
        a, b, lab = st[0], st[1], st[2]
        kind = st[3] if len(st) > 3 else "call"
        num += 1
        o.append(f'<circle cx="{L - 18}" cy="{y + 9}" r="8" fill="#F3F4F6" stroke="#9CA3AF"/>')
        o.append(f'<text x="{L - 18}" y="{y + 12.5}" text-anchor="middle" style="font-size:9.5px;fill:#374151">{num}</text>')
        if a == b:
            wbox = tw(lab, 10.5) * 1.1 + 16
            x0 = cx[a] + 8
            if x0 + wbox > W - 4:
                x0 = cx[a] - 8 - wbox
            if x0 < L - 6:
                warn(f"自注释过长：{lab}")
            c = COL[st[3]] if len(st) > 3 and st[3] in COL else COL["s"]
            o.append(f'<rect x="{x0:.1f}" y="{y}" width="{wbox:.1f}" height="19" rx="4" fill="{c[0]}" stroke="{c[1]}"/>')
            o.append(f'<text x="{x0 + 7:.1f}" y="{y + 13.5:.1f}" style="font-size:10.5px;fill:#1F2937">{esc(lab)}</text>')
            o.append(f'<rect x="{cx[a] - 4:.1f}" y="{y}" width="8" height="19" fill="{c[1]}" fill-opacity="0.35"/>')
            y += 27
        else:
            col, dash, mk, _ = SEQK[kind]
            used.add(kind)
            x1, x2 = cx[a], cx[b]
            dx = 6 if x2 > x1 else -6
            fs = fit(lab, 10.5, abs(x2 - x1) + colW * 0.9, what="消息文字")
            o.append(f'<text x="{(x1 + x2) / 2:.1f}" y="{y + 9:.1f}" text-anchor="middle" style="font-size:{fs}px;fill:#1F2937">{esc(lab)}</text>')
            da = f' stroke-dasharray="{dash}"' if dash else ""
            o.append(f'<line x1="{x1 + dx:.1f}" y1="{y + 16}" x2="{x2 - dx:.1f}" y2="{y + 16}" stroke="{col}" stroke-width="1.5"{da} marker-end="url(#{mk})"/>')
            y += 30
    for a_, b_, lab_, c_ in grp:
        c = COL[c_]
        gx0 = cx[a_] - colW / 2 + 2; gx1 = cx[b_] + colW / 2 - 2
        o_life.append(f'<rect x="{gx0:.1f}" y="26" width="{gx1 - gx0:.1f}" height="{y - 26:.0f}" fill="{c[0]}" fill-opacity="0.45"/>')
    for k in range(n):
        o_life.append(f'<line x1="{cx[k]:.1f}" y1="{y0}" x2="{cx[k]:.1f}" y2="{y}" stroke="#CBD5E1" stroke-width="1.4" stroke-dasharray="4 3"/>')
    y += 8
    x = L
    for kind in ("call", "net", "rep", "local"):
        if kind in used:
            col, dash, mk, nm = SEQK[kind]
            da = f' stroke-dasharray="{dash}"' if dash else ""
            o.append(f'<line x1="{x}" y1="{y}" x2="{x + 26}" y2="{y}" stroke="{col}" stroke-width="1.5"{da} marker-end="url(#{mk})"/>')
            o.append(f'<text x="{x + 32}" y="{y + 4}" class="xs">{nm}</text>')
            x += 50 + tw(nm, 10.5)
    o.append(f'<rect x="{x}" y="{y - 7}" width="18" height="12" rx="3" fill="#F3F4F6" stroke="#6B7280"/><text x="{x + 24}" y="{y + 4}" class="xs">该参与者内部发生的事</text>')
    H = y + 14
    svg = f'<svg viewBox="0 0 {W} {H:.0f}" xmlns="http://www.w3.org/2000/svg">' + "".join(o_life + o_top + o) + "</svg>"
    return fig(svg, d.get("cap"))


def md_seq(d):
    P = [p.replace("\n", " ") for p in d["p"]]
    out = ["参与者：" + " ｜ ".join(P), ""]
    for a_, b_, lab_, c_ in d.get("grp", []):
        out.insert(1, f"（{lab_}：" + "、".join(P[a_:b_ + 1]) + "）")
    num = 0
    tag = {"call": "", "net": "（RPC / 网络）", "rep": "（复制）", "local": "（本地）"}
    for st in d["s"]:
        if st[0] == "—":
            out += ["", f"**{st[1]}**", ""]; continue
        num += 1
        a, b, lab = st[0], st[1], st[2]
        kind = st[3] if len(st) > 3 else "call"
        if a == b:
            out.append(f"{num}. 〔{P[a]}〕{lab}")
        else:
            out.append(f"{num}. {P[a]} → {P[b]}：{lab}{tag.get(kind, '')}")
    return "\n".join(out)


# ================================================================== tree
def tree_rows(d):
    rows = []

    def rec(nd, depth, lasts):
        rows.append((depth, nd, lasts))
        kids = nd.get("c", [])
        for i, k in enumerate(kids):
            rec(k, depth + 1, lasts + [i == len(kids) - 1])
    roots = d["root"] if isinstance(d["root"], list) else [d["root"]]
    for r in roots:
        rec(r, 0, [])
    return rows


def tree(d):
    W = d.get("w", 860)
    cols = d.get("cols", ["名称", "说明"])
    xs = d.get("x", [12, 330, 640][:len(cols)])
    keys = d.get("keys", ["d", "w", "e"])
    rows = tree_rows(d)
    RH, IND = 25, 20
    o = []
    y = 8
    for j, c in enumerate(cols):
        o.append(f'<text x="{xs[j] + (4 if j else 0)}" y="{y + 13}" class="b" style="font-size:11.5px">{esc(c)}</text>')
    o.append(f'<line x1="8" y1="{y + 21}" x2="{W - 8}" y2="{y + 21}" stroke="#D1D5DB"/>')
    y += 28
    for idx, (depth, nd, lasts) in enumerate(rows):
        if idx % 2:
            o.append(f'<rect x="6" y="{y}" width="{W - 12}" height="{RH}" fill="#F9FAFB"/>')
        mid = y + RH / 2
        for k in range(1, depth):
            if not lasts[k - 1]:
                xk = 12 + (k - 1) * IND + 8
                o.append(f'<line x1="{xk}" y1="{y}" x2="{xk}" y2="{y + RH}" stroke="#9CA3AF"/>')
        if depth:
            xd = 12 + (depth - 1) * IND + 8
            o.append(f'<line x1="{xd}" y1="{y}" x2="{xd}" y2="{mid if lasts[-1] else y + RH}" stroke="#9CA3AF"/>')
            o.append(f'<line x1="{xd}" y1="{mid}" x2="{12 + depth * IND}" y2="{mid}" stroke="#9CA3AF"/>')
        bx = 12 + depth * IND
        c = COL[nd.get("col", "s")]
        bw = tw(nd["n"], 11, True) + 14
        o.append(f'<rect x="{bx}" y="{y + 3}" width="{bw:.1f}" height="{RH - 6}" rx="5" fill="{c[0]}" stroke="{c[1]}" stroke-width="1.2"/>')
        o.append(f'<text x="{bx + 7}" y="{mid + 4}" class="b" style="font-size:11px;fill:{c[1]}">{esc(nd["n"])}</text>')
        if len(xs) > 1 and bx + bw > xs[1] - 4:
            warn(f"树节点过宽：{nd['n']}")
        for j in range(1, len(cols)):
            v = nd.get(keys[j - 1], "")
            lim = (xs[j + 1] if j + 1 < len(xs) else W) - xs[j] - 8
            fs = fit(v, 10.5, lim, what="树说明")
            o.append(f'<text x="{xs[j] + 4}" y="{mid + 4}" style="font-size:{fs}px;fill:#374151">{esc(v)}</text>')
        y += RH
    H = y + 8
    svg = f'<svg viewBox="0 0 {W} {H:.0f}" xmlns="http://www.w3.org/2000/svg">' + "".join(o) + "</svg>"
    return fig(svg, d.get("cap"))


def md_tree(d):
    cols = d.get("cols", ["名称", "说明"]); keys = d.get("keys", ["d", "w", "e"])
    out = ["| " + " | ".join(cols) + " |", "|" + "---|" * len(cols)]
    for depth, nd, lasts in tree_rows(d):
        name = "　" * max(0, depth - 1) + ("└ " if depth else "") + nd["n"]
        out.append("| " + " | ".join([name] + [str(nd.get(keys[j], "")).replace("|", "\\|") for j in range(len(cols) - 1)]) + " |")
    return "\n".join(out)


# ================================================================== spans
def spans(d):
    W = d.get("w", 860); ph = d["ph"]; n = len(ph)
    LW = d.get("labw", 150)
    cw = (W - LW - 14) / n
    o = []
    y = 8
    for k, p in enumerate(ph):
        x = LW + k * cw
        o.append(f'<rect x="{x + 1:.1f}" y="{y}" width="{cw - 2:.1f}" height="34" rx="5" fill="#F3F4F6"/>')
        lines = p.split("\n")
        for j, ln in enumerate(lines):
            o.append(f'<text x="{x + cw / 2:.1f}" y="{y + 15 + j * 13 - (len(lines) - 1) * 5:.1f}" text-anchor="middle" class="b" style="font-size:10.5px;fill:#374151">{esc(ln)}</text>')
            if tw(ln, 10.5, True) > cw - 6:
                warn(f"阶段名过长：{ln}")
    y += 42
    for idx, (lab, segs) in enumerate(d["rows"]):
        if idx % 2:
            o.append(f'<rect x="6" y="{y}" width="{W - 12}" height="26" fill="#F9FAFB"/>')
        o.append(f'<text x="{LW - 10}" y="{y + 17}" text-anchor="end" class="b" style="font-size:11px">{esc(lab)}</text>')
        for a, b, c, note in segs:
            x0 = LW + a * cw + 3; x1 = LW + (b + 1) * cw - 3
            cc = COL[c]
            o.append(f'<rect x="{x0:.1f}" y="{y + 4}" width="{x1 - x0:.1f}" height="18" rx="9" fill="{cc[0]}" stroke="{cc[1]}" stroke-width="1.3"/>')
            o.append(f'<text x="{(x0 + x1) / 2:.1f}" y="{y + 17}" text-anchor="middle" style="font-size:10px;fill:{cc[1]}">{esc(note)}</text>')
            if tw(note, 10) > x1 - x0 - 6:
                warn(f"生命周期条文字过长：{note}")
        y += 26
    for k in range(1, n):
        x = LW + k * cw
        o.insert(0, f'<line x1="{x:.1f}" y1="50" x2="{x:.1f}" y2="{y}" stroke="#E5E7EB" stroke-dasharray="3 3"/>')
    H = y + 8
    svg = f'<svg viewBox="0 0 {W} {H:.0f}" xmlns="http://www.w3.org/2000/svg">' + "".join(o) + "</svg>"
    return fig(svg, d.get("cap"))


def md_spans(d):
    ph = [p.replace("\n", "") for p in d["ph"]]
    out = ["| 对象 | " + " | ".join(ph) + " | 说明 |", "|---|" + "---|" * (len(ph) + 1)]
    for lab, segs in d["rows"]:
        cells = [""] * len(ph)
        for a, b, c, note in segs:
            for k in range(a, b + 1):
                cells[k] = "■"
        out.append(f"| {lab} | " + " | ".join(cells) + " | " + "；".join(s[3] for s in segs) + " |")
    return "\n".join(out)


# ================================================================== bpnet：类的网络规格面板
EXECL = {"S": ("仅服务器", "b"), "C": ("仅拥有者客户端", "a"), "M": ("服务器 + 所有客户端", "o"),
         "L": ("本地控制端", "g"), "A": ("所有端（每个副本）", "s"), "R": ("服务器(Set 时) + 收到的客户端", "p"),
         "P": ("本机（不联网）", "t"), "U": ("本机 UI", "t"), "SO": ("服务器 + 拥有者客户端", "a"),
         "X": ("调用方所在端", "s")}
REPC = {"RepNotify": "p", "Replicated": "b", "None": "s"}
REPL = {"Run on Server": "b", "Run on owning Client": "a", "Multicast": "o", "Not Replicated": "s",
        "RepNotify 函数": "p", "引擎事件": "s", "函数": "s", "事件分发器": "t"}


def chip(o, x, y, text, ck, size=9.6, maxw=None, dash=False):
    c = COL[ck]
    fs = fit(text, size, maxw - 12, True, what="标签") if maxw else size
    w = tw(text, fs, True) + 12
    da = ' stroke-dasharray="3 2"' if dash else ""
    o.append(f'<rect x="{x:.1f}" y="{y - 11:.1f}" width="{w:.1f}" height="15" rx="7" fill="{c[0]}" stroke="{c[1]}"{da}/>')
    o.append(f'<text x="{x + 6:.1f}" y="{y:.1f}" class="b" style="font-size:{fs}px;fill:{c[1]}">{esc(text)}</text>')
    return w


def bpnet(d):
    W = 860; o = []; y = 6
    o.append(f'<rect x="6" y="{y}" width="{W - 12}" height="34" rx="8" fill="#1F2937"/>')
    o.append(f'<text x="18" y="{y + 22}" class="b wt" style="font-size:14px">{esc(d["cls"])}</text>')
    o.append(f'<text x="{34 + tw(d["cls"], 14, True)}" y="{y + 22}" class="wt" style="font-size:11px;fill:#D1D5DB">父类 {esc(d.get("parent", ""))}　·　存在于：{esc(d.get("where", ""))}</text>')
    y += 52
    if d.get("defaults"):
        o.append(f'<text x="14" y="{y}" class="b" style="font-size:11px;fill:#374151">Class Defaults → Replication</text>')
        x = 196
        for k, v in d["defaults"]:
            t = f"{k}：{v}"
            ck = "g" if v.startswith("✓") else ("r" if v.startswith("✗") else "s")
            wv = tw(t, 9.6, True) + 12
            if x + wv > W - 10:
                x = 196; y += 20
            chip(o, x, y, t, ck)
            x += wv + 8
        y += 26
    if d.get("vars"):
        o.append(f'<rect x="6" y="{y - 4}" width="{W - 12}" height="22" rx="4" fill="#EEF2FF"/>')
        o.append(f'<text x="14" y="{y + 11}" class="b" style="font-size:11.5px;fill:#1E3A8A">变量（My Blueprint → Variables；Details → Replication / Replication Condition）</text>')
        y += 30
        X = [30, 196, 288, 380, 466, 640]
        for j, hd in enumerate(["名称", "类型", "Replication", "Condition", "谁写", "收到后 / 说明"]):
            o.append(f'<text x="{X[j]}" y="{y}" class="b" style="font-size:10.5px;fill:#6B7280">{hd}</text>')
        y += 8
        for idx, (nm, ty, rp, cond, wr, note) in enumerate(d["vars"]):
            wl = wrap(wr, 10.5, X[5] - X[4] - 8); nl = wrap(note, 10.5, W - X[5] - 14)
            ml = wrap(nm, 11.6, X[1] - X[0] - 6)
            rh = 22 + 13 * (max(len(wl), len(nl), len(ml)) - 1)
            if idx % 2 == 0:
                o.append(f'<rect x="8" y="{y}" width="{W - 16}" height="{rh}" fill="#F9FAFB"/>')
            yy = y + 15
            ck = REPC.get(rp, "s"); c = COL[ck]
            if rp == "None":
                o.append(f'<circle cx="17" cy="{yy - 4}" r="3.5" fill="#fff" stroke="#9CA3AF" stroke-width="1.3"/>')
            else:
                o.append(f'<circle cx="14" cy="{yy - 4}" r="3.5" fill="{c[1]}"/><circle cx="21" cy="{yy - 4}" r="3.5" fill="{c[1]}" fill-opacity="0.55"/>')
            for k, t in enumerate(ml):
                o.append(f'<text x="{X[0]}" y="{yy + 13 * k}" class="b" style="font-size:{fit(t, 11, X[1] - X[0] - 6, True)}px">{esc(t)}</text>')
            o.append(f'<text x="{X[1]}" y="{yy}" class="mono" style="font-size:{fit(ty, 10, X[2] - X[1] - 6)}px;fill:#4B5563">{esc(ty)}</text>')
            chip(o, X[2], yy, rp if rp != "None" else "None 不复制", ck, maxw=X[3] - X[2] - 4, dash=rp == "None")
            if cond and cond not in ("—", "None"):
                chip(o, X[3], yy, cond, "a", maxw=X[4] - X[3] - 4)
            else:
                o.append(f'<text x="{X[3]}" y="{yy}" style="font-size:10.5px;fill:#9CA3AF">{"None（全部）" if cond == "None" else "—"}</text>')
            for k, t in enumerate(wl):
                o.append(f'<text x="{X[4]}" y="{yy + 13 * k}" style="font-size:10.5px;fill:#374151">{esc(t)}</text>')
            for k, t in enumerate(nl):
                o.append(f'<text x="{X[5]}" y="{yy + 13 * k}" style="font-size:10.5px;fill:#374151">{esc(t)}</text>')
            y += rh
        y += 16
    if d.get("events"):
        o.append(f'<rect x="6" y="{y - 4}" width="{W - 12}" height="22" rx="4" fill="#FEF2F2"/>')
        o.append(f'<text x="14" y="{y + 11}" class="b" style="font-size:11.5px;fill:#991B1B">事件 / 函数（自定义事件 Details → Graph → Replicates · Reliable）</text>')
        y += 30
        X = [30, 214, 300, 446, 504, 674]
        for j, hd in enumerate(["名称", "类型", "Replicates", "Reliable", "在哪执行", "谁触发"]):
            o.append(f'<text x="{X[j]}" y="{y}" class="b" style="font-size:10.5px;fill:#6B7280">{hd}</text>')
        y += 8
        for idx, (nm, kind, rp, rel, ex, who) in enumerate(d["events"]):
            whl = wrap(who, 10.5, W - X[5] - 14)
            ml = wrap(nm, 11.6, X[1] - X[0] - 6)
            rh = 22 + 13 * (max(len(whl), len(ml)) - 1)
            if idx % 2 == 0:
                o.append(f'<rect x="8" y="{y}" width="{W - 16}" height="{rh}" fill="#F9FAFB"/>')
            yy = y + 15
            lab, ck = EXECL.get(ex, (ex, "s"))
            o.append(f'<rect x="12" y="{yy - 10}" width="10" height="10" rx="2" fill="{COL[ck][1]}"/>')
            for k, t in enumerate(ml):
                o.append(f'<text x="{X[0]}" y="{yy + 13 * k}" class="b" style="font-size:{fit(t, 11, X[1] - X[0] - 6, True)}px">{esc(t)}</text>')
            o.append(f'<text x="{X[1]}" y="{yy}" style="font-size:{fit(kind, 10.5, X[2] - X[1] - 6)}px;fill:#4B5563">{esc(kind)}</text>')
            chip(o, X[2], yy, rp, REPL.get(rp, "s"), maxw=X[3] - X[2] - 4)
            o.append(f'<text x="{X[3] + 8}" y="{yy}" class="b" style="font-size:11px;fill:{"#15803D" if rel == "✓" else "#9CA3AF"}">{esc(rel)}</text>')
            chip(o, X[4], yy, lab, ck, maxw=X[5] - X[4] - 4)
            for k, t in enumerate(whl):
                o.append(f'<text x="{X[5]}" y="{yy + 13 * k}" style="font-size:10.5px;fill:#374151">{esc(t)}</text>')
            y += rh
        y += 8
    H = y + 6
    svg = f'<svg viewBox="0 0 {W} {H:.0f}" xmlns="http://www.w3.org/2000/svg">' + "".join(o) + "</svg>"
    return fig(svg, d.get("cap"))


def md_bpnet(d):
    out = [f"**{d['cls']}**（父类 {d.get('parent', '')}；存在于：{d.get('where', '')}）", ""]
    if d.get("defaults"):
        out += ["Class Defaults → Replication：" + "；".join(f"{k} {v}" for k, v in d["defaults"]), ""]
    if d.get("vars"):
        out += ["| 变量 | 类型 | Replication | Condition | 谁写 | 收到后 / 说明 |", "|---|---|---|---|---|---|"]
        out += [f"| {a} | {b} | {c if c != 'None' else 'None（不复制）'} | {e} | {f} | {g} |" for a, b, c, e, f, g in d["vars"]]
        out.append("")
    if d.get("events"):
        out += ["| 事件 / 函数 | 类型 | Replicates | Reliable | 在哪执行 | 谁触发 |", "|---|---|---|---|---|---|"]
        out += [f"| {a} | {b} | {c} | {e} | {EXECL.get(f, (f,))[0]} | {g} |" for a, b, c, e, f, g in d["events"]]
    return "\n".join(out)


# ================================================================== repmx：复制接收矩阵
def repmx_rows(d):
    rows = []
    for var, rp, cond, owned, use in d["rows"]:
        owners = ["主机玩家的", "客户端玩家的"] if owned else ["—"]
        for ow in owners:
            onrep = rp == "RepNotify"
            srv = "权威值（直接读写）" + ("；蓝图 Set 时 OnRep 在这里执行" if onrep else "")
            cli_owner = ow == "客户端玩家的"
            if cond in ("—", "None"):
                cli, cls_ = "收到" + (" → OnRep" if onrep else ""), "yes"
            elif cond == "Owner Only":
                cli, cls_ = (("收到" + (" → OnRep" if onrep else "")), "yes") if cli_owner else ("不发送（这份不归它）", "no")
            elif cond == "Skip Owner":
                cli, cls_ = ("不发送（它是拥有者，已本地预测）", "no") if cli_owner else (("收到" + (" → OnRep" if onrep else "")), "yes")
            elif cond == "Initial Only":
                cli, cls_ = "只在首次复制时收到一次" + (" → OnRep" if onrep else ""), "mid"
            else:
                cli, cls_ = cond, "mid"
            if cond == "Skip Owner" and ow == "主机玩家的":
                srv += "；主机就是拥有者"
            rows.append((var, f"{rp} · {cond}", ow, srv, cli, cls_, use))
    return rows


def repmx(d):
    out = ['<div class="tbl"><table>', "<tr><th>变量</th><th>复制设置</th><th>这一份属于</th><th>主机进程（= 服务器）</th><th>客户端进程</th><th>用途</th></tr>"]
    last = None
    for var, setting, ow, srv, cli, cls_, use in repmx_rows(d):
        first = var != last
        out.append("<tr>" + (f"<td><b>{esc(var)}</b></td><td>{esc(setting)}</td>" if first else "<td></td><td></td>")
                   + f"<td>{esc(ow)}</td><td>{esc(srv)}</td><td class=\"{cls_}\">{esc(cli)}</td><td>{esc(use) if first else ''}</td></tr>")
        last = var
    out.append("</table></div>")
    if d.get("cap"):
        out.append(f'<p class="tcap">{d["cap"]}</p>')
    return "\n".join(out)


def md_repmx(d):
    out = ["| 变量 | 复制设置 | 这一份属于 | 主机进程（= 服务器） | 客户端进程 | 用途 |", "|---|---|---|---|---|---|"]
    last = None
    for var, setting, ow, srv, cli, cls_, use in repmx_rows(d):
        first = var != last
        mk = {"yes": "✓ ", "no": "✗ ", "mid": "◐ "}.get(cls_, "")
        out.append(f"| {var if first else ''} | {setting if first else ''} | {ow} | {srv} | {mk}{cli} | {use if first else ''} |")
        last = var
    return "\n".join(out)


MACROS = {"bpgraph": bpgraph, "seq": seq, "tree": tree, "spans": spans, "bpnet": bpnet, "repmx": repmx}
MD_MACROS = {"bpgraph": md_bpgraph, "seq": md_seq, "tree": md_tree, "spans": md_spans, "bpnet": md_bpnet, "repmx": md_repmx}
