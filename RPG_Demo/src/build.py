"""拼装《RPGDemo 源码复盘与游戏客户端面试》电子书：src/chapters/*.html -> ../RPG_Demo.html

章节里可用的写法（构建时展开）：
  $...$ / $$...$$                     LaTeX 公式（KaTeX 构建时渲染，字体内嵌）；字面量美元写 \\$
  <!--code:cpp|cs|py|text|bash|ini:标题-->  原始代码（无需转义） <!--/code-->
  <!--deriv 标题 ... -->              逐行推导（等号对齐 + 每步依据）
  <!--bars {json} -->                 横向柱状图；items 的值可以写数据路径字符串（如 "e3.relay.gfz"）
  <!--flow {json} -->                 方框 + 箭头结构图（架构 / 流程 / 族谱 / 决策树）
  <!--plot|gbars|graph|dtab {json} --> figs.py：折线 / 分组柱 / 状态图 / 数据表
  <!--seq|tree|spans {json} -->       bpmacros.py：时序图 / 树 + 说明列 / 生命周期条
  [[路径|格式]]                       正文内联数字，例如 [[E5.rows.0.final|.1f]]；全部来自 demos/figdata.json
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
import platfig as PF

ROOT = pathlib.Path(__file__).resolve().parent
OUT = ROOT.parent / "RPG_Demo.html"
NODE = r"C:\Users\haydenyhu\.workbuddy\binaries\node\versions\22.22.2-2\node.exe"
NODE_MODULES = pathlib.Path(r"C:\Users\haydenyhu\.workbuddy\binaries\node\workspace\node_modules")
KATEX_DIR = NODE_MODULES / "katex" / "dist"
MACROS = {}

CODE_RE = re.compile(r"<!--code:(\w+):(.*?)-->\n?(.*?)\n?<!--/code-->", re.S)
MACRO_RE = re.compile(r"<!--(bars|flow|plot|gbars|graph|dtab|seq|tree|spans|tline|probe|wallcov|seam|tmap|cmp|ladder|cmpsum)\s+(\{.*?\})\s*-->", re.S)
DERIV_RE = re.compile(r"<!--deriv ([^\n]*)\n(.*?)-->", re.S)
DISPLAY_RE = re.compile(r"\$\$([\s\S]+?)\$\$")
INLINE_RE = re.compile(r"(?<![\\$])\$(?!\$)([^$\n]+?)(?<!\\)\$(?!\$)")
VAL_RE = re.compile(r"\[\[([\w.\-]+?)(?:\|([^\]]+))?\]\]")
VALS = F.VALS
PF.VALS = VALS
DEMOS = ROOT.parent / "demos"


def val_sub(m):
    return F.fmtv(F.get(m.group(1)), m.group(2))


def load_data():
    """demos/figdata.json（rpg_verify.cpp）→ VALS；派生图表视图并断言关键结论（与正文描述一致）。"""
    D = json.loads((DEMOS / "figdata.json").read_text(encoding="utf-8"))
    VALS.update(D)
    msgs = []
    e1 = D["E1"]
    assert "Fall→Idle，Idle→WallSlide" in [r["trans"] for r in e1["S1"]["rows"]], "S1 应出现一帧两次转移"
    assert e1["S2"]["wsFrames"] >= 60 and e1["S3"]["rows"][3]["trans"] == "Death→Dash", "S2/S3 结论"
    assert e1["S5"]["combo"] == [0, 1], "S5 连招计数"
    msgs.append(f"E1 S1 一帧两次转移 / S2 无墙滑行 {e1['S2']['wsFrames'] - 10} 帧 / S3 Death→Dash ✓")
    e2 = {str(r["fps"]): r for r in D["E2"]["rows"]}
    assert abs(e2["60"]["cw"] - 1.0) < 0.03 and abs(e2["60"]["ow"] - 1.333) < 0.01 and e2["60"]["chi"] < 0.4167 + 1 / 60
    VALS["e2"] = e2
    e3 = D["E3"]
    assert all(abs(r["mc"] - r["exact"]) < 0.2 for r in e3["crit"]), "暴击蒙特卡洛与 (c+1)/100 一致"
    VALS["e3"] = {"crit": {str(r["c"]): r for r in e3["crit"]}, "chill": e3["chill"], "cp": e3["cp"]}
    e4 = {str(r["lv"]): r for r in D["E4"]["rows"]}
    VALS["e4"] = e4
    VALS["e4p"] = {"comp": [[r["lv"], r["mul"]] for r in D["E4"]["rows"]], "lin": [[r["lv"], r["lmul"]] for r in D["E4"]["rows"]]}
    e5 = {r["name"]: r for r in D["E5"]["rows"]}
    assert all(abs(r["mc"] - r["final"]) < 0.3 for r in D["E5"]["rows"]), "掉落蒙特卡洛与精确解一致"
    VALS["e5"] = e5
    msgs.append("E3 暴击 / E5 掉落：蒙特卡洛与解析解一致 ✓")
    VALS["e6"] = {f"{r['s0']}_{r['i0']}": r for r in D["E6"]["rows"]}
    e7 = D["E7"]
    assert e7["C"]["giveUp"] < 0 and e7["Cf"]["giveUp"] > 0 and e7["Af"]["self"] == 0
    NOW, IDEAL = "现状（源码）", "理想（修正版）"
    names = {"A": ("A 玩家站在前方 2.5 m", NOW), "Af": ("A 玩家站在前方 2.5 m", IDEAL), "B": ("B 玩家站在身后 1.5 m", NOW),
             "C": ("C 3 s 后玩家跳上 4 m 高平台", NOW), "Cf": ("C 3 s 后玩家跳上 4 m 高平台", IDEAL)}
    VALS["e7t"] = [dict(e7[k], scen=names[k][0], ver=names[k][1]) for k in ("A", "Af", "B", "C", "Cf")]
    VALS["e7c"] = [dict(e7[k], ver=names[k][1], give=("从不放弃" if e7[k]["giveUp"] < 0 else f"第 {e7[k]['giveUp']:.1f} s 放弃")) for k in ("C", "Cf")]
    msgs.append(f"E7 源码自转移 {e7['A']['self']} 次 / 修正 0；平台场景源码从不放弃 ✓")
    e8 = {f"{r['fps']}_{round(r['cd'] * 100)}": r for r in D["E8"]["rows"]}
    assert all(r["fix"] > 99.9 for r in D["E8"]["rows"]) and all(r["pct"] < 90 for r in D["E8"]["rows"])
    VALS["e8"] = e8
    VALS["e8r"] = {"min": min(r["pct"] for r in D["E8"]["rows"]), "max": max(r["pct"] for r in D["E8"]["rows"])}
    VALS["e8g"] = {"groups": [f"hitCoolDown={c:g}" for c in (0, 0.2, 0.35, 0.5, 0.7)],
                   "f60": [e8[f"60_{c}"]["pct"] for c in (0, 20, 35, 50, 70)],
                   "f144": [e8[f"144_{c}"]["pct"] for c in (0, 20, 35, 50, 70)],
                   "fix": [100.0] * 5}
    e9 = D["E9"]
    assert all(abs(r["dy"] - r["f"]) < 1e-3 for r in e9["rows"]), "E9 偏差 = ½·g·s·Δt·t"
    for r in e9["rows"]:
        r["ady"], r["af"] = abs(r["dy"]), abs(r["f"])
    VALS["e9"] = {f"{r['ang']}_{round(r['t'] * 100)}": r for r in e9["rows"]}
    VALS["e9p"] = {"dot": [[p[0], p[1]] for p in e9["curve45"]], "sim": [[p[0], p[2]] for p in e9["curve45"]]}
    msgs.append("E9 虚线与物理轨迹偏差 = ½·g·s·Δt·t ✓")
    e10 = D["E10"]
    VALS["e10"] = {"dash": {str(r["fps"]): r for r in e10["dash"]}, "hitch": {str(int(r["ms"])): r for r in e10["hitch"]},
                   "ign": {str(r["fps"]): r for r in e10["ign"]}, "spin": {f"{round(r['cd'] * 100)}_{r['fps']}": r for r in e10["spin"]}}
    VALS["e10g"] = {"groups": ["30 fps", "60 fps", "144 fps", "240 fps"],
                    **{f"s{c}": [VALS["e10"]["spin"][f"{c}_{f}"]["hits"] for f in (30, 60, 144, 240)] for c in (0, 10, 35)}}
    VALS["e11"] = {str(r["n"]): r for r in D["E11"]["rows"]}
    VALS["e11p"] = {"one": [[r["n"], r["one"]] for r in D["E11"]["rows"]], "two": [[r["n"], r["two"]] for r in D["E11"]["rows"] if r["n"] >= 2],
                    "theo": [[r["n"], r["theo"]] for r in D["E11"]["rows"]]}
    # ---- 第六部分：demos/feel_verify.cpp → feeldata.json；tools/scan_tilemap.py → tilemap_dump.json
    FD = json.loads((DEMOS / "feeldata.json").read_text(encoding="utf-8"))
    VALS.update(FD)
    assert 25 < FD["F1"]["a1"] < 26 and FD["F1"]["tri"] > 60, "F1 单射线坡度上限约 25.5°"
    VALS["f2p"] = {f"c{c}": [[r[0], r[i + 1]] for r in FD["F2"]["curve"]] for i, c in enumerate((0, 50, 100, 150))}
    VALS["f3p"] = {f"b{c}": [[r[0], r[i + 1]] for r in FD["F3"]["curve"]] for i, c in enumerate((0, 80, 120, 200))}
    VALS["f2"] = {"p0": FD["F2"]["pop"][0], "p1": FD["F2"]["pop"][1]}
    VALS["f3"] = {"p0": FD["F3"]["pop"][0], "p1": FD["F3"]["pop"][1]}
    assert VALS["f2"]["p0"]["c100"] > 90 > VALS["f2"]["p0"]["c0"], "coyote 100 ms 应把准时型成功率拉到 90% 以上"
    f5 = FD["F5"]["rows"]
    for r in f5:
        r["res"] = f"✔ {r['t']:.2f} s" if r["top"] else "✘ 20 s 内爬不上"
        r["kind"] = "现状" if r["n"].startswith("源码") else ("理想（检测）" if "脚部" in r["n"] else "参数调整")
        r["n"] = r["n"].replace("源码：", "")
    VALS["f5"] = {str(i): r for i, r in enumerate(f5)}
    assert f5[0]["top"] and not f5[4]["top"] and f5[9]["top"] and f5[10]["top"], "F5：反应 0 帧可翻上、100 ms 卡住、加脚部射线后可翻上"
    for r in FD["F4"]["var"]:
        r["n"] = r["n"].replace("源码：", "现状 · ")
    VALS["f4v"] = {str(i): r for i, r in enumerate(FD["F4"]["var"])}
    msgs.append(f"F1 单射线坡度上限 {FD['F1']['a1']:.1f}° / F5 源码反应 100 ms 卡在墙顶下、加脚部射线可翻上 ✓")
    TM = json.loads((ROOT.parent / "tools" / "tilemap_dump.json").read_text(encoding="utf-8"))
    VALS["tm"] = TM
    g = TM["tilemaps"]["Ground"]
    occ = {(x, y) for x, y, _ in g["tiles"]}
    shared = sum(((x + 1, y) in occ) + ((x, y + 1) in occ) for x, y in occ)
    VALS["tms"] = {"n": len(occ), "edges": 4 * len(occ), "shared": shared, "inner": 2 * shared, "outer": 4 * len(occ) - 2 * shared,
                   "polys": len(g["composite"]["paths"]), "verts": sum(len(p) for p in g["composite"]["paths"]),
                   "assets": len(g["assets"]), "sprites": g["sprites"], "bg": len(TM["tilemaps"]["BackGround"]["tiles"]),
                   "png": TM["tileAssets"]["pngCount"], "tileAssets": TM["tileAssets"]["count"]}
    assert VALS["tms"]["n"] == 79 and VALS["tms"]["verts"] == 16
    msgs.append(f"Tilemap：{len(occ)} 格 → 逐格 {4 * len(occ)} 条边（{2 * shared} 条在内部），合并后 {VALS['tms']['verts']} 个顶点 ✓")
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


# ============================================================ tline：时间轴上的区间（数值可写数据路径）
def tv(v):
    return F.get(v) if isinstance(v, str) else v


def tline(d):
    W, LW = d.get("w", 860), d.get("labw", 170)
    x0, x1 = d["x"]
    rows, RH = d["rows"], d.get("rh", 30)
    X = lambda t: LW + (W - LW - 18) * (tv(t) - x0) / (x1 - x0)
    top = 30 if d.get("marks") else 10
    H = top + len(rows) * RH + 34
    out = []
    for t in d.get("ticks") or F._ticks(x0, x1, 10):
        out.append(f'<line x1="{X(t):.1f}" y1="{top - 4}" x2="{X(t):.1f}" y2="{H - 30}" stroke="#EEF2F6"/>')
        out.append(f'<text x="{X(t):.1f}" y="{H - 16}" text-anchor="middle" class="xs mono">{t:g}</text>')
    if d.get("xlabel"):
        out.append(f'<text x="{W - 18}" y="{H - 2}" text-anchor="end" class="xs">{esc(d["xlabel"])}</text>')
    for k, r in enumerate(rows):
        y = top + k * RH
        if k % 2:
            out.append(f'<rect x="6" y="{y}" width="{W - 12}" height="{RH}" fill="#F9FAFB"/>')
        out.append(f'<text x="{LW - 10}" y="{y + RH / 2 + 4}" text-anchor="end" class="sm b">{esc(r["n"])}</text>')
        for a, bb, c, lab in r.get("segs", []):
            xa, xb = X(a), X(bb)
            cc = COL[c]
            out.append(f'<rect x="{xa:.1f}" y="{y + 6}" width="{max(2, xb - xa):.1f}" height="{RH - 12}" rx="5" fill="{cc[0]}" stroke="{cc[1]}" stroke-width="1.3"/>')
            if lab:
                lab = VAL_RE.sub(val_sub, lab)
                tw_ = text_w(lab, 10.5)
                if tw_ < xb - xa - 8:
                    out.append(f'<text x="{(xa + xb) / 2:.1f}" y="{y + RH / 2 + 4}" text-anchor="middle" class="xs b" style="fill:{cc[1]}">{esc(lab)}</text>')
                else:
                    out.append(f'<text x="{xb + 6:.1f}" y="{y + RH / 2 + 4}" class="xs b" style="fill:{cc[1]}">{esc(lab)}</text>')
        for t, c, lab in r.get("pts", []):
            xp = X(t); cc = COL[c][1]
            out.append(f'<path d="M{xp:.1f},{y + 7} L{xp + 5:.1f},{y + RH / 2} L{xp:.1f},{y + RH - 7} L{xp - 5:.1f},{y + RH / 2} z" fill="{cc}"/>')
            if lab:
                out.append(f'<text x="{xp + 8:.1f}" y="{y + RH / 2 + 4}" class="xs" style="fill:{cc}">{esc(VAL_RE.sub(val_sub, lab))}</text>')
    for t, lab, c in d.get("marks", []):
        xm = X(t); cc = COL[c][1]
        out.append(f'<line x1="{xm:.1f}" y1="{top - 6}" x2="{xm:.1f}" y2="{H - 30}" stroke="{cc}" stroke-dasharray="4 3" stroke-width="1.3"/>')
        out.append(f'<text x="{xm:.1f}" y="{top - 10}" text-anchor="middle" class="xs b" style="fill:{cc}">{esc(VAL_RE.sub(val_sub, lab))}</text>')
    svg = f'<svg viewBox="0 0 {W} {H}" xmlns="http://www.w3.org/2000/svg">' + "".join(out) + "</svg>"
    return fig(svg, d.get("cap"))


def md_tline(d):
    f = d.get("fmt", ".3f")
    out = ["| 行 | 区间 / 时刻 | 说明 |", "|---|---|---|"]
    for r in d["rows"]:
        for a, bb, c, lab in r.get("segs", []):
            out.append(f"| {r['n']} | [{format(tv(a), f)}, {format(tv(bb), f)}] | {VAL_RE.sub(val_sub, lab)} |")
        for t, c, lab in r.get("pts", []):
            out.append(f"| {r['n']} | {format(tv(t), f)} | {VAL_RE.sub(val_sub, lab)} |")
    for t, lab, c in d.get("marks", []):
        out.append(f"| （竖线） | {format(tv(t), f)} | {VAL_RE.sub(val_sub, lab)} |")
    return "\n".join(out)


def macro(m: re.Match) -> str:
    kind, body = m.group(1), m.group(2)
    try:
        d = json.loads(body)
    except json.JSONDecodeError as e:
        raise SystemExit(f"[{kind}] JSON 错误: {e}\n{body[:300]}")
    if kind in BM.MACROS:
        return BM.MACROS[kind](d)
    if kind in PF.MACROS:
        return PF.MACROS[kind](d)
    fn = {"bars": bars, "flow": flow, "tline": tline, "cmp": cmp, "ladder": ladder, "cmpsum": cmpsum}.get(kind) or F.MACROS[kind]
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


# ============================================================ 问题分级（对照表“存在的问题”列、代码块问题条、现状图注）
SEV = {"P0": ("p0", "P0"), "P1": ("p1", "P1"), "P2": ("p2", "P2"), "P3": ("p3", "P3"),
       "隐患": ("hz", "隐患"), "局限": ("lim", "局限"), "无": ("ok", "✓ 无问题")}
SEV_KEYS = ["p0", "p1", "p2", "p3", "hz", "lim", "ok"]
SEV_RE = re.compile(r"^(P[0-3]|隐患|局限|无)\s+([\s\S]*)$")


def sev_split(s):
    m = SEV_RE.match(s.strip())
    assert m, f"问题描述缺少分级前缀（P0~P3 / 隐患 / 局限 / 无）：{s[:60]}"
    return SEV[m.group(1)], m.group(2)


def sev_html(s):
    (c, lab), txt = sev_split(s)
    return f'<span class="sev {c}">{lab}</span> {txt}'


def sev_md(s, plain=lambda x: x):
    (c, lab), txt = sev_split(s)
    return f"**{lab}** {plain(txt)}"


def code_kind(lang, title):
    """代码块分类：【改进】= 理想方案骨架；cs 且标题带 .cs 文件名 = 当前源码"""
    if title.startswith("【改进】"):
        return "ideal", title[len("【改进】"):].strip()
    if lang == "cs" and ".cs" in title:
        return "now", title
    return "", title


CODE_BADGE = {"now": ("now", "当前源码 · 822270c"), "ideal": ("ideal", "理想方案 · 骨架")}


def code_block(m: re.Match) -> str:
    lang, title, body = m.group(1), m.group(2).strip(), m.group(3)
    body = body.strip("\n")
    title, _, prob = title.partition(" ‖ ")
    kind, title = code_kind(lang, title.strip())
    badge = f'<span class="cb {CODE_BADGE[kind][0]}">{CODE_BADGE[kind][1]}</span>' if kind else ""
    pb = f'<div class="cprob"><span class="cpl">现状问题</span>{sev_html(VAL_RE.sub(val_sub, prob))}</div>' if prob else ""   # 代码块先被 stash，需在此展开数值
    return (
        '<div class="code%s"><div class="bar"><span>%s%s</span><button>复制</button></div>'
        '<pre data-lang="%s">%s</pre>%s</div>' % (" " + kind if kind else "", badge, html.escape(title or lang), lang, html.escape(body), pb)
    )


# ============================================================ 图的“现状 / 理想”标记（图号 → 类别）
FIGSTATE = {}
for _k, _v in (("now", "1-1 1-2 1-3 1-4 1-5 2-1 2-2 3-1 4-1 4-2 5-2 5-3 5-4 6-1 7-1 7-2 7-3 8-1 8-2 9-1 9-2 10-1 10-2 11-2 16-1 19-3"),
               ("ideal", "1-6 3-2 6-3 8-3 9-3 10-3 12-1 12-2 17-2 18-1 20-5 21-1 21-2 21-3 21-4"),
               ("cmp", "0-1 6-2 7-4 11-1 17-1 18-2 19-2 20-1 20-2 20-3 20-4 D-1")):
    for _n in _v.split():
        FIGSTATE[_n] = _k
FIGLABEL = {"now": "现状", "ideal": "理想方案", "cmp": "现状 vs 理想"}
FIGPROB = {
 "1-1": "局限 数值散在场景与 SO 里：读代码看不到“算什么”，策划改数要开场景",
 "1-2": "P2 血条上的 Trigger 碰撞体会混进玩家所有 OverlapCircleAll 的结果（Queries Hit Triggers 开启，#27）",
 "1-3": "局限 Entity 基类集中了检测、翻转、击退、减速：非共有能力也被所有角色继承",
 "1-4": "局限 命中在动画更新阶段结算：时机受播放速度 / 冻结影响，同帧互砍的先后取决于 Animator 更新顺序",
 "1-5": "P0 三个单例写反了两个（#4）；所有依赖指向唯一的 Player，无法单测、无法换玩家",
 "2-1": "P2 组件在 Start 才取；执行顺序配在父类上，对子类无效（#31）",
 "2-2": "P0 单例写反：多放一个管理器就销毁旧实例，新的又不登记（推断，#4）",
 "3-1": "P0 两个全局入口不检查当前状态：死亡后冲刺 / 接剑即“复活”（E1-S3/S4，#3）；WallSlide 缺“墙没了”出口（#14）",
 "4-1": "P2 AimSword 变假就自动掷剑：被打断也会掷出（推断，#25）",
 "4-2": "P2 玩家选择了冲刺，却掷出一把方向陈旧的剑（推断，#25）",
 "5-2": "P2 冲刺距离随帧率与卡顿变化（E10，#20）",
 "5-3": "P2 hitCoolDown = 0 时命中数 = 帧数：高帧率玩家伤害成倍增加（E10，#21）",
 "5-4": "P2 瞄准虚线与实际轨迹不一致，玩家照着虚线瞄会打低（E9，#22）",
 "6-1": "P1 闪避也闪白击退；弹反成功仍结算伤害（#5）",
 "7-1": "P1 冰冻破甲判错对象（#10）；TakeDamage 无死亡保护，尸体可重复掉落（#7）；暴击 &lt;= 多 1%（#16）",
 "7-2": "P2 隐式复利，与命名“每级 +25%”的意图不符（E4，#18）",
 "7-3": "P1 多个写者赋绝对值：重叠冻结只冻住部分时间，一个效果的恢复解除另一个（E8，#11）",
 "8-1": "P2 朝向判断写在循环里，正确率随碰撞体数量跳动（E11，#23）",
 "8-2": "P2 7 个 bool 标志交叉判断，加一种剑要改多处；子类 Update 不调 base，冷却永不减（#24）",
 "9-1": "P1 Battle 每帧自转移（E7，#8）；战斗计时每帧重置、永不脱战（#9）",
 "9-2": "P1 每帧一次无意义的 Exit / Enter + 全场景 Find（E7，#8 #34）",
 "10-1": "局限 以配置作键、没有物品实例；三套容器代码复制",
 "10-2": "P1 点击空装备格空引用（#13）；一次操作全量刷新 3 次",
 "11-2": "P2 异常结束后颜色永久残留（推断，#29）",
 "16-1": "P2 关卡度量先画后定：3 m 沟太宽松，7 m 立柱依赖不稳定的墙交互（F4 / F5）",
 "19-3": "P2 立柱是唯一必须借助墙交互的设计点，而墙交互成败取决于手速（F5）",
}
FIGPROB_RE = re.compile(r'((?:<figcaption>|<p class="tcap">)<span class="fs now">现状</span>图 (\d+-\d+)[\s\S]*?)(</figcaption>|</p>)')


def fig_prob(m):
    p = FIGPROB.get(m.group(2))
    return m.group(1) + (f'<span class="fprob"><span class="cpl">现状问题</span>{sev_html(p)}</span>' if p else "") + m.group(3)
FIG_RE = re.compile(r'(<figcaption>|<p class="tcap">)(图 ([0-9A-Z]+-\d+))')


def fig_badge(m):
    k = FIGSTATE.get(m.group(3))
    cls = {"cmp": "vs"}.get(k, k)   # 不能用 cmp：会命中对照表容器的 .cmp 样式
    return m.group(1) + (f'<span class="fs {cls}">{FIGLABEL[k]}</span>' if k else "") + m.group(2)


# ============================================================ cmp：现状 vs 理想 对照表
VERB = [("模型", "g"), ("保持", "b"), ("公开", "p")]


def cmp_ver(v):
    if not v:
        return "s", "设计方案 · 未实现"
    for pre, c in VERB:
        if v.startswith(pre):
            return c, v
    return "s", v


CMP_HEAD = ["维度", "当前做法（Demo 现状）", "存在的问题", "理想重构方案", "收益 / 代价"]


def cmp_sev_line(d):
    cnt = {k: 0 for k in SEV_KEYS}
    for r in d["rows"]:
        cnt[sev_split(r[2])[0][0]] += 1
    lab = {c: l for c, l in SEV.values()}
    return cnt, [(k, lab[k], cnt[k]) for k in SEV_KEYS if cnt[k]]


def cmp(d):
    assert d.get("v") == 2, f"cmp 需为 v2 行格式：{d['t']}"
    head = d.get("cols", CMP_HEAD)
    h = (f'<colgroup><col class="c-dim"><col class="c-now"><col class="c-prob"><col class="c-ideal"><col class="c-why"></colgroup>'
         f'<tr><th>{head[0]}</th><th class="now">{head[1]}</th><th class="prob">{head[2]}</th><th class="ideal">{head[3]}</th><th>{head[4]}</th></tr>')
    rows = []
    for r in d["rows"]:
        dim, now, prob, ideal, why, ver = r
        c, vt = cmp_ver(ver)
        rows.append(f'<tr><td class="dim">{dim}</td><td class="now">{now}</td><td class="prob">{sev_html(prob)}</td>'
                    f'<td class="ideal">{ideal}<br><span class="vb {c}">{vt}</span></td><td>{why}</td></tr>')
    _, parts = cmp_sev_line(d)
    sl = "现状问题：" + "　".join(f'<span class="nw"><span class="sev {k}">{l}</span>×{n}</span>' for k, l, n in parts) + "（P0~P3 编号见附录 A，分级说明见 0.2）"
    foot = f'<div class="cmp-f">{sl}' + (f'<br>{d["foot"]}' if d.get("foot") else "") + "</div>"
    return (f'<div class="cmp"><div class="cmp-h"><span class="tag now">现状</span> → <span class="sev p1">问题</span> → <span class="tag ideal">理想</span>　{d["t"]}</div>'
            f'<div class="tbl"><table>{h}{"".join(rows)}</table></div>{foot}</div>')


def md_cmp(d, plain):
    head = d.get("cols", CMP_HEAD)
    head = [head[0], "〔现状〕" + head[1], "⚠ " + head[2], "〔理想〕" + head[3], head[4]]
    out = [f"**〔现状〕→ 问题 →〔理想〕　{plain(d['t'])}**", "", "| " + " | ".join(head) + " |", "|" + "---|" * len(head)]
    cell = lambda x: VAL_RE.sub(val_sub, plain(x)).replace("|", "\\|").replace("\n", " ")
    for r in d["rows"]:
        dim, now, prob, ideal, why, ver = r
        c, vt = cmp_ver(ver)
        vt = VAL_RE.sub(val_sub, vt)
        out.append(f"| **{cell(dim)}** | {cell(now)} | {cell(sev_md(prob))} | {cell(ideal)}（{vt}） | {cell(why)} |")
    _, parts = cmp_sev_line(d)
    out += ["", "> 现状问题：" + "　".join(f"{l} ×{n}" for k, l, n in parts) + "（P0~P3 编号见附录 A）"]
    if d.get("foot"):
        out += ["> " + plain(d["foot"])]
    return "\n".join(out)


# ============================================================ ladder：成熟度阶梯（模块 × 档位）
def ladder(d):
    cols, rows = d["cols"], d["rows"]
    W, LW, G = d.get("w", 900), d.get("labw", 108), 8
    cw = (W - LW - 10 - G * (len(cols) - 1)) / len(cols)
    X = lambda i: LW + i * (cw + G)
    RH, RG = d.get("rh", 30), 6
    out = []
    for a, b, c, t in d.get("groups", []):
        x0, x1 = X(a) + 2, X(b) + cw - 2
        cc = COL[c][1]
        out.append(f'<path d="M{x0:.1f},30 L{x0:.1f},23 L{x1:.1f},23 L{x1:.1f},30" fill="none" stroke="{cc}" stroke-width="1.4"/>')
        out.append(f'<text x="{(x0 + x1) / 2:.1f}" y="16" text-anchor="middle" class="xs b" style="fill:{cc}">{esc(t)}</text>')
    top = 36
    for i, (t, c) in enumerate(cols):
        out.append(f'<rect x="{X(i):.1f}" y="{top}" width="{cw:.1f}" height="26" rx="13" fill="{COL[c][1]}"/>')
        out.append(f'<text x="{X(i) + cw / 2:.1f}" y="{top + 17}" text-anchor="middle" class="sm b" style="fill:#fff;stroke:none">{esc(t)}</text>')
    y0 = top + 26 + 10
    for k, r in enumerate(rows):
        y = y0 + k * (RH + RG)
        if k % 2 == 0:
            out.append(f'<rect x="4" y="{y - 3}" width="{W - 8}" height="{RH + 6}" rx="6" fill="#F8FAFC"/>')
        out.append(f'<text x="{LW - 10}" y="{y + RH / 2 + 4.5}" text-anchor="end" class="sm b">{esc(r[0])}</text>')
        for i, cell in enumerate(r[1:]):
            cc = COL[cols[i][1]]
            gray = cell.startswith("（")
            fill, stroke, tc = ("#F3F4F6", "#D1D5DB", "#6B7280") if gray else (cc[0], cc[1], "#111827")
            out.append(f'<rect x="{X(i):.1f}" y="{y}" width="{cw:.1f}" height="{RH}" rx="7" fill="{fill}" stroke="{stroke}" stroke-width="1.1"/>')
            tw = text_w(cell, 11)
            if tw > cw - 8:
                WARN.append(f"[ladder] 文字可能超出（{tw:.0f}>{cw - 8:.0f}）：{cell}")
            out.append(f'<text x="{X(i) + cw / 2:.1f}" y="{y + RH / 2 + 4}" text-anchor="middle" class="xs" style="font-size:11px;fill:{tc}">{esc(cell)}</text>')
    H = y0 + len(rows) * (RH + RG) + 2
    svg = f'<svg viewBox="0 0 {W} {H}" xmlns="http://www.w3.org/2000/svg">' + "".join(out) + "</svg>"
    return fig(svg, d.get("cap"))


def md_ladder(d):
    cols = d["cols"]
    out = []
    if d.get("groups"):
        out += ["分档：" + "；".join("、".join(cols[i][0] for i in range(a, b + 1)) + f" = {t}" for a, b, c, t in d["groups"]), ""]
    head = ["模块"] + [c[0] for c in cols]
    out += ["| " + " | ".join(head) + " |", "|" + "---|" * len(head)]
    for r in d["rows"]:
        out.append(f"| **{r[0]}** | " + " | ".join(x.replace("|", "\\|") for x in r[1:]) + " |")
    return "\n".join(out)


# ============================================================ cmpsum：全书对照表自动索引（构建时扫描所有 cmp 宏）
CMPS = []
CMP_SCAN = re.compile(r"<!--cmp\s+(\{.*?\})\s*-->", re.S)
CMP_KIND = [("s", "设计方案 · 未实现"), ("g", "模型验证"), ("b", "保持现状"), ("p", "公开参考")]


def collect_cmps():
    CMPS.clear()
    for f in sorted((ROOT / "chapters").glob("*.html")):
        m = re.search(r"_ch(\d+)_", f.name)
        if m:
            for mm in CMP_SCAN.finditer(f.read_text(encoding="utf-8")):
                CMPS.append((int(m.group(1)), json.loads(mm.group(1))))
    rows, tot = cmpsum_data()
    sev = {k: 0 for k in SEV_KEYS}
    for _, d in CMPS:
        for k, v in cmp_sev_line(d)[0].items():
            sev[k] += v
    VALS["cmpsum"] = dict(tot, tables=len(rows), total=sum(r[2] for r in rows), sev=sev,
                          defect=sev["p0"] + sev["p1"] + sev["p2"] + sev["p3"])
    return CMPS


def cmpsum_data():
    rows, tot = [], {k: 0 for k, _ in CMP_KIND}
    for n, d in CMPS:
        cnt, ver = {k: 0 for k, _ in CMP_KIND}, []
        for r in d["rows"]:
            c, _ = cmp_ver(r[5])
            cnt[c] += 1
            if c == "g":
                ver.append(r[0])
        for k in cnt:
            tot[k] += cnt[k]
        rows.append((n, d["t"], len(d["rows"]), cnt, ver, cmp_sev_line(d)[1]))
    return rows, tot


def cmpsum(d):
    rows, tot = cmpsum_data()
    sevc = lambda ps: " ".join(f'<span class="nw"><span class="sev {k}">{l}</span>×{n}</span>' for k, l, n in ps)
    h = ("<tr><th>节</th><th>对照表</th><th>维度</th><th>现状问题分级</th>" + "".join(f'<th><span class="vb {k}">{t}</span></th>' for k, t in CMP_KIND)
         + "<th>有 C++ 模型支撑的理想做法</th></tr>")
    body = "".join(f"<tr><td>{n}.1</td><td>{t}</td><td>{m}</td><td>{sevc(ps)}</td>" + "".join(f"<td>{c[k] or '—'}</td>" for k, _ in CMP_KIND)
                   + f"<td>{'、'.join(v) or '—'}</td></tr>" for n, t, m, c, v, ps in rows)
    S = VALS["cmpsum"]["sev"]
    lab = {c: l for c, l in SEV.values()}
    body += (f"<tr><td colspan=2><b>合计（{len(rows)} 张表）</b></td><td><b>{sum(r[2] for r in rows)}</b></td>"
             f"<td>{sevc([(k, lab[k], S[k]) for k in SEV_KEYS if S[k]])}</td>"
             + "".join(f"<td><b>{tot[k]}</b></td>" for k, _ in CMP_KIND) + "<td></td></tr>")
    return f'<div class="tbl"><table>{h}{body}</table></div>'


def md_cmpsum():
    rows, tot = cmpsum_data()
    head = ["节", "对照表", "维度", "现状问题分级"] + [t for _, t in CMP_KIND] + ["有 C++ 模型支撑的理想做法"]
    out = ["| " + " | ".join(head) + " |", "|" + "---|" * len(head)]
    sevc = lambda ps: " ".join(f"{l}×{n}" for k, l, n in ps)
    for n, t, m, c, v, ps in rows:
        out.append(f"| {n}.1 | {t} | {m} | {sevc(ps)} | " + " | ".join(str(c[k] or "—") for k, _ in CMP_KIND) + f" | {'、'.join(v) or '—'} |")
    S = VALS["cmpsum"]["sev"]
    lab = {c: l for c, l in SEV.values()}
    out.append(f"| **合计** | {len(rows)} 张表 | **{sum(r[2] for r in rows)}** | {sevc([(k, lab[k], S[k]) for k in SEV_KEYS if S[k]])} | "
               + " | ".join(f"**{tot[k]}**" for k, _ in CMP_KIND) + " |  |")
    return "\n".join(out)


def main() -> int:
    shell = (ROOT / "shell.html").read_text(encoding="utf-8")
    checks = load_data()
    collect_cmps()
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
    content = FIG_RE.sub(fig_badge, content)
    content = FIGPROB_RE.sub(fig_prob, content)
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
    left = re.findall(r"<!--(bars|flow|plot|gbars|graph|dtab|seq|tree|spans|tline|probe|wallcov|seam|tmap|cmp|code|deriv)\b", content) + VAL_RE.findall(content)
    if left:
        print("未展开的宏:", left, file=sys.stderr)
    for w in WARN + F.WARN + PF.WARN:
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
