"""拼装《游戏 AI 系统工程师面试：强化学习与机器学习理论基础》电子书：src/chapters/*.html -> ../RL_GameAI.html

章节里可用的写法（构建时展开）：
  $...$ / $$...$$            LaTeX 公式（构建时用 KaTeX 渲染成 HTML，字体内嵌，离线 / 打印 PDF 都能看）；字面量美元写 \\$
  <!--code:cpp|py|text|bash:标题-->  原始代码（无需转义） <!--/code-->
  <!--deriv 标题                逐行推导（等号对齐 + 每步依据）
  LHS & = RHS || 依据（可含 $行内公式$）
  & = RHS2 || 依据
  # 解说行（整行文字）
  * & = 结论 || 依据         （* 开头 = 结论行高亮）
  -->
  <!--bars {json} -->            横向柱状图
  <!--conv {json} -->            构建时真实求解 MRP：V=(I-γP)^-1 R 与迭代策略评估误差，对数坐标画 γ-压缩收敛
  <!--flow {json} -->            方框 + 箭头结构图：boxes(id/x/y/w/h 或 cr=[列,行] + cols/colw/rows/bh, t/s/s2, c 色键, hl 红框★)、
                                 chains 依次连线、arrows(from/to 自动选边 或 p 折线, dash, label)、rects 背景框、texts；文字超框会告警
  [[id.key]]                     引用 conv 宏算出的数（如 [[mrp.e10]]），正文与 md 数字同源
"""
import base64
import html
import json
import math
import os
import pathlib
import re
import subprocess
import sys
import tempfile
import xml.etree.ElementTree as ET

import figs as F
import rlcore as RC

ROOT = pathlib.Path(__file__).resolve().parent
OUT = ROOT.parent / "RL_GameAI.html"
NODE = r"C:\Users\haydenyhu\.workbuddy\binaries\node\versions\22.22.2-2\node.exe"
NODE_MODULES = pathlib.Path(r"C:\Users\haydenyhu\.workbuddy\binaries\node\workspace\node_modules")
KATEX_DIR = NODE_MODULES / "katex" / "dist"

# 全书统一的 LaTeX 宏（md 输出时也按这张表展开，保证 Typora / VSCode 能直接渲染）
MACROS = {
    "\\E": "\\mathbb{E}",
    "\\R": "\\mathbb{R}",
    "\\Var": "\\operatorname{Var}",
    "\\KL": "D_{\\mathrm{KL}}",
    "\\argmax": "\\operatorname*{arg\\,max}",
    "\\argmin": "\\operatorname*{arg\\,min}",
    "\\clip": "\\operatorname{clip}",
    "\\sgn": "\\operatorname{sgn}",
}

CODE_RE = re.compile(r"<!--code:(\w+):(.*?)-->\n?(.*?)\n?<!--/code-->", re.S)
MACRO_RE = re.compile(r"<!--(bars|conv|flow|plot|gbars|graph|grid|dtab|gpath)\s+(\{.*?\})\s*-->", re.S)
DERIV_RE = re.compile(r"<!--deriv ([^\n]*)\n(.*?)-->", re.S)
DISPLAY_RE = re.compile(r"\$\$([\s\S]+?)\$\$")
INLINE_RE = re.compile(r"(?<![\\$])\$(?!\$)([^$\n]+?)(?<!\\)\$(?!\$)")
VAL_RE = re.compile(r"\[\[([\w.]+?)(?:\|([^\]]+))?\]\]")
VALS = F.VALS
DEMOS = ROOT.parent / "demos"


INC_RE = re.compile(r"\{\{demo:([\w.]+)(?:#([^}]+))?\}\}")


def include(t):
    """{{demo:文件}} 插入 demos/ 下的整个文件；{{demo:文件#标记}} 只插入“标记（开始）”与“标记（结束）”两行之间的内容。
    书中的手撕代码与实测输出因此和 demo 文件逐字一致。"""
    def rep(m):
        txt = (DEMOS / m.group(1)).read_text(encoding="utf-8")
        if m.group(2):
            lines = txt.split("\n")
            a = next(i for i, l in enumerate(lines) if m.group(2) + "（开始）" in l)
            b = next(i for i, l in enumerate(lines) if m.group(2) + "（结束）" in l)
            txt = "\n".join(lines[a + 1:b])
        return txt.strip("\n")
    return INC_RE.sub(rep, t)


def val_sub(m):
    v = F.get(m.group(1))
    return F.fmtv(v, m.group(2))


def load_data():
    """rlcore 的确定性计算 + demos/*_output.json 的 C++ 实测，统一放进 VALS，并对拍。"""
    VALS["lane"] = RC.lane_all()
    VALS["dng"] = RC.dungeon_all()
    for f in sorted(DEMOS.glob("*_output.json")):
        VALS[f.stem.replace("_output", "")] = json.loads(f.read_text(encoding="utf-8"))
    msgs = []

    def close(a, b, tol, what):
        if isinstance(a, list):
            assert len(a) == len(b), what
            for x, y in zip(a, b):
                close(x, y, tol, what)
            return
        if abs(a - b) > tol:
            raise SystemExit(f"[对拍失败] {what}: Python={a} C++={b}")
    if "ch02" in VALS:
        e5 = VALS["ch02"]["e5"]
        fmt = lambda v: "(" + ", ".join(f"{x:.3f}" for x in v) + ")"
        e5["rows"] = [{"name": "BN，batch A（另外 7 个样本 ~ N(0, 1)）", "out": fmt(e5["bn_a"])},
                      {"name": "BN，batch B（另外 7 个样本 ~ N(5, 3²)）", "out": fmt(e5["bn_b"])},
                      {"name": "BN，batch 大小为 1", "out": fmt(e5["one"])},
                      {"name": "LN（任何 batch）", "out": fmt(e5["ln"])}]
    if "ch21" in VALS:
        e3 = VALS["ch21"]["e3"]
        e3["sprt_rows"] = [dict(v, truth=f"{float(k):.0%}") for k, v in e3["sprt"].items()]
        e1 = VALS["ch21"]["e1"]                      # 键里有小数点，路径引用会被拆开：起别名
        e1["p05"], e1["p09"] = e1["0.5"], e1["0.9"]
    if "ch07" in VALS:
        # 累积遗憾在对数横轴上的斜率：ΔR / Δln T（T 从约 2000 到 20000），与 Lai-Robbins 常数 Σ Δ/KL 比较
        e3 = VALS["ch07"]["e3"]
        def near(pts, x):
            return min(pts, key=lambda p: abs(p[0] - x))
        sl = []
        for r in e3["runs"]:
            z_, a_, b_ = near(r["pts"], 200), near(r["pts"], 2000), near(r["pts"], e3["T"])
            sl.append({"name": r["name"], "slope0": (a_[1] - z_[1]) / math.log(a_[0] / z_[0]), "slope": (b_[1] - a_[1]) / math.log(b_[0] / a_[0]),
                       "end": r["end"], "r1000": r["r1000"], "pulls": r["pulls"]})
        VALS["ch07d"] = {"slopes": sl}
    if "ch03" in VALS:
        L, Cp = VALS["lane"], VALS["ch03"]
        close(L["Vu"], Cp["Vu"], 1e-9, "均匀策略 V_π")
        close(L["Vstar"], Cp["Vstar"], 1e-8, "V*")
        close(L["k_eval"], Cp["k_eval"], 0, "迭代评估次数")
        close(L["k_vi"], Cp["k_vi"], 0, "价值迭代次数")
        close([L["brute"]["V0"], L["brute"]["V1"], L["brute"]["V2"]], Cp["brute"], 1e-9, "8 个确定性策略的价值")
        msgs.append("ch03 ✓")
    if "ch04" in VALS:
        D, Cp = VALS["dng"], VALS["ch04"]["dungeon"]
        for tag in ("s0", "s2"):
            for k in ("sweeps", "sweeps_inplace", "pi_rounds"):
                close(D[tag][k], Cp[tag][k], 0, f"地牢 {tag}.{k}")
            close(D[tag]["Vstart"], Cp[tag]["Vstart"], 1e-7, f"地牢 {tag}.Vstart")
        close(D["sutton_V"], VALS["ch04"]["sutton_V"], 1e-7, "4×4 网格均匀策略价值")
        msgs.append("ch04 ✓")
        G = VALS["ch04"]["gamma"]
        VALS["ch04d"] = {k: [[1 / (1 - r["g"]), r[k]] for r in G] for k in ("vi", "vi_in", "theory", "pi")}
    if "ch05" in VALS:
        S, Cp = RC.rw_seeds(), VALS["ch05"]
        close(S["seed_td"], Cp["rw5"]["seed_td"], 1e-12, "随机游走 run0 TD(0) α=0.1 学习曲线")
        close(S["seed_mc"], Cp["rw5"]["seed_mc"], 1e-12, "随机游走 run0 MC α=0.02 学习曲线")
        close(S["batch_td"], Cp["batch"]["seed_td"][:30], 1e-9, "run0 批量 TD（确定性等价）")
        close(S["batch_mc"], Cp["batch"]["seed_mc"][:30], 1e-12, "run0 批量 MC")
        close(S["seed_n4"], Cp["rw19"]["seed_n4"], 1e-12, "19 状态 run0 4 步 TD")
        close(S["seed_lam08"], Cp["rw19"]["seed_lam08"], 1e-12, "19 状态 run0 离线 TD(0.8)")
        msgs.append("ch05 ✓")
        VALS["tr"] = RC.trace_demo()
        rw = Cp["rw19"]
        # 图用的点列：n 步 / λ 的最小误差 vs 参数
        VALS["ch05d"] = {
            "n_best": [[r["n"], r["best"]] for r in rw["nstep"]],
            "off_best": [[r["lam"], r["best"]] for r in rw["lam_off"]],
            "on_best": [[r["lam"], r["best"]] for r in rw["lam_on"]],
            "n_argmin": min(rw["nstep"], key=lambda r: r["best"]),
            "off_argmin": min(rw["lam_off"], key=lambda r: r["best"]),
            "on_argmin": min(rw["lam_on"], key=lambda r: r["best"]),
            "snap_names": ["初值（全 0.5）", "1 个回合后", "10 个回合后", "100 个回合后", "真值"],
        }
        VALS["ch05d"]["var"] = RC.rw_var()
        VALS["ab"] = RC.ab_example()
        VALS["ch05d"]["lam_tab"] = [{"lam": a["lam"], "off": a["best"], "off_a": a["best_a"], "on": b["best"], "on_a": b["best_a"]}
                                    for a, b in zip(rw["lam_off"], rw["lam_on"])]
        VALS["ch05d"]["n_tab"] = rw["nstep"]
        sn = Cp["rw5"]["snap"] + [Cp["rw5"]["true"]]
        VALS["ch05d"]["snap"] = [[[i + 1, v[i]] for i in range(5)] for v in sn]
    return msgs

COL = {
    "b": ("#EAF0FD", "#2F5BD3"), "o": ("#FFF4EC", "#C2410C"), "g": ("#EAF7EF", "#15803D"),
    "p": ("#F3EEFE", "#6D28D9"), "r": ("#FDEEEE", "#B91C1C"), "a": ("#FEF6E4", "#B45309"),
    "s": ("#F3F4F6", "#6B7280"), "t": ("#E6F6F5", "#0F766E"), "k": ("#E5E7EB", "#374151"),
}
CIRC = "①②③④⑤⑥⑦⑧⑨⑩⑪⑫⑬⑭⑮⑯⑰⑱⑲⑳"


def esc(s):
    return html.escape(str(s), quote=True)


def fig(svg, cap):
    c = f"<figcaption>{cap}</figcaption>" if cap else ""
    return f"<figure>\n{svg}\n{c}</figure>"


def expand_macros(tex):
    """md 端用：把自定义宏展开成标准 LaTeX。"""
    for k in sorted(MACROS, key=len, reverse=True):
        tex = re.sub(re.escape(k) + r"(?![A-Za-z])", lambda m, v=MACROS[k]: v, tex)
    return tex


# ============================================================ bars
def bars(d):
    items = d["items"]
    W = d.get("w", 860)
    labW = d.get("labw", 170)
    bh, gap = 22, 8
    vmax = d.get("max") or max(it[1] for it in items)
    room = W - labW - 150
    unit = d.get("unit", "")
    y = 10
    out = []
    if d.get("title"):
        out.append(f'<text x="{labW}" y="{y + 10}" class="b">{esc(d["title"])}</text>')
        y += 26
    for it in items:
        lab, v = it[0], it[1]
        c = COL[it[2] if len(it) > 2 else "b"]
        w = max(2, min(room, room * v / vmax))
        out.append(f'<text x="{labW - 10}" y="{y + 16}" text-anchor="end" class="sm">{esc(lab)}</text>')
        out.append(f'<rect x="{labW}" y="{y}" width="{w:.1f}" height="{bh}" rx="4" fill="{c[0]}" stroke="{c[1]}" stroke-width="1.4"/>')
        txt = it[3] if len(it) > 3 else f"{v:g} {unit}"
        out.append(f'<text x="{labW + w + 8:.1f}" y="{y + 16}" class="sm mono b" style="fill:{c[1]}">{esc(txt)}</text>')
        y += bh + gap
    y += 4
    svg = f'<svg viewBox="0 0 {W} {y}" xmlns="http://www.w3.org/2000/svg">' + "".join(out) + "</svg>"
    return fig(svg, d.get("cap"))


# ============================================================ conv：MRP 解析解 + 迭代策略评估（γ-压缩）
def solve(A, b):
    """高斯消元（列主元），纯 Python，避免依赖 numpy。"""
    n = len(A)
    M = [row[:] + [b[i]] for i, row in enumerate(A)]
    for c in range(n):
        p = max(range(c, n), key=lambda r: abs(M[r][c]))
        M[c], M[p] = M[p], M[c]
        for r in range(n):
            if r != c and M[r][c] != 0:
                f = M[r][c] / M[c][c]
                M[r] = [x - f * y for x, y in zip(M[r], M[c])]
    return [M[i][n] / M[i][i] for i in range(n)]


def conv_run(d):
    P, R, g, K = d["P"], d["R"], d["gamma"], d.get("K", 40)
    n = len(R)
    for row in P:
        assert abs(sum(row) - 1) < 1e-9, "P 每行和必须为 1"
    A = [[(1.0 if i == j else 0.0) - g * P[i][j] for j in range(n)] for i in range(n)]
    Vs = solve(A, R)
    V = [0.0] * n
    errs, Vk = [], [V[:]]
    for k in range(K + 1):
        errs.append(max(abs(V[i] - Vs[i]) for i in range(n)))
        V = [R[i] + g * sum(P[i][j] * V[j] for j in range(n)) for i in range(n)]
        Vk.append(V[:])
    e0 = errs[0]
    bound = [e0 * g ** k for k in range(K + 1)]
    ratio = [errs[k + 1] / errs[k] for k in range(K) if errs[k] > 0]
    vals = {"V": "(" + ", ".join(f"{v:.3f}" for v in Vs) + ")", "e0": f"{e0:.3f}", "gamma": f"{g:g}",
            "maxratio": f"{max(ratio):.3f}", "K": str(K)}
    for i, v in enumerate(Vs):
        vals[f"V{i}"] = f"{v:.3f}"
    for k in range(K + 1):
        vals[f"e{k}"] = f"{errs[k]:.4g}"
        vals[f"b{k}"] = f"{bound[k]:.4g}"
    for i in range(n):
        vals[f"V1_{i}"] = f"{Vk[1][i]:.3f}"
        vals[f"V2_{i}"] = f"{Vk[2][i]:.3f}"
    vals["tight"] = f"{errs[K] / bound[K]:.3f}"
    return dict(Vs=Vs, errs=errs, bound=bound, vals=vals, K=K, g=g, Vk=Vk[:K + 1])


SUP = str.maketrans("-0123456789", "⁻⁰¹²³⁴⁵⁶⁷⁸⁹")


def conv(d):
    r = conv_run(d)
    if d.get("id"):
        VALS[d["id"]] = r["vals"]
    W, H = 860, 310
    T, B_ = 30, 46
    K, n = r["K"], len(r["Vs"])
    names = d.get("names", [f"s{i}" for i in range(n)])
    cols = ["#2F5BD3", "#C2410C", "#15803D", "#6D28D9"]
    out = []
    # ---- 左：每个状态的 V_k(s) 逼近 V_π(s)
    L1, R1 = 52, 392
    vmax = max(max(r["Vs"]), max(max(v) for v in r["Vk"])) * 1.08
    vmin = min(0.0, min(min(v) for v in r["Vk"]))
    X1 = lambda k: L1 + (R1 - L1) * k / K
    Y1 = lambda v: T + (H - T - B_) * (vmax - v) / (vmax - vmin)
    out.append(f'<text x="{L1}" y="16" class="b sm">左：各状态价值 V_k(s) 逐步逼近 V_π(s)</text>')
    out.append(f'<rect x="{L1}" y="{T}" width="{R1 - L1}" height="{H - T - B_}" fill="#FFFFFF" stroke="#CBD5E1"/>')
    step_v = 2 if vmax <= 16 else 5
    v = 0
    while v <= vmax:
        y = Y1(v)
        out.append(f'<line x1="{L1}" y1="{y:.1f}" x2="{R1}" y2="{y:.1f}" stroke="#F1F5F9"/>')
        out.append(f'<text x="{L1 - 6}" y="{y + 4:.1f}" text-anchor="end" class="xs mono">{v:g}</text>')
        v += step_v
    for i in range(n):
        c = cols[i % len(cols)]
        y = Y1(r["Vs"][i])
        out.append(f'<line x1="{L1}" y1="{y:.1f}" x2="{R1}" y2="{y:.1f}" stroke="{c}" stroke-width="1.2" stroke-dasharray="5 4" opacity=".7"/>')
        pts = " ".join(f"{X1(k):.1f},{Y1(vk[i]):.1f}" for k, vk in enumerate(r["Vk"]))
        out.append(f'<polyline points="{pts}" fill="none" stroke="{c}" stroke-width="2"/>')
    # 标签放在左侧、各自虚线的正上方（左侧 k 小时曲线还在低处，不会压到曲线）；按值排序避免互相重叠
    order = sorted(range(n), key=lambda i: -r["Vs"][i])
    last = -1e9
    for i in order:
        y = max(Y1(r["Vs"][i]) - 3, last + 11.5)
        last = y
        out.append(f'<text x="{L1 + 8}" y="{y:.1f}" class="xs b" style="fill:{cols[i % len(cols)]}">{esc(names[i])}：Vπ = {r["Vs"][i]:.2f}</text>')
    # ---- 右：对数坐标误差 vs γ^k 上界
    L2, R2 = 472, 846
    lo = math.floor(math.log10(max(min(x for x in r["errs"] if x > 0), 1e-12)))
    top = math.log10(max(r["bound"])) + 0.2
    X2 = lambda k: L2 + (R2 - L2) * k / K
    Y2 = lambda v: T + (H - T - B_) * (top - math.log10(max(v, 10 ** lo))) / (top - lo)
    out.append(f'<text x="{L2}" y="16" class="b sm">右：误差 ‖V_k − V_π‖∞（对数坐标）</text>')
    out.append(f'<rect x="{L2}" y="{T}" width="{R2 - L2}" height="{H - T - B_}" fill="#FFFFFF" stroke="#CBD5E1"/>')
    for e in range(lo, math.floor(top) + 1):
        y = Y2(10 ** e)
        out.append(f'<line x1="{L2}" y1="{y:.1f}" x2="{R2}" y2="{y:.1f}" stroke="#E5E7EB" stroke-dasharray="3 3"/>')
        out.append(f'<text x="{L2 - 6}" y="{y + 4:.1f}" text-anchor="end" class="xs mono">10{str(e).translate(SUP)}</text>')
    pts = " ".join(f"{X2(k):.1f},{Y2(v):.1f}" for k, v in enumerate(r["bound"]))
    out.append(f'<polyline points="{pts}" fill="none" stroke="#C2410C" stroke-width="2" stroke-dasharray="7 5"/>')
    pts = " ".join(f"{X2(k):.1f},{Y2(v):.1f}" for k, v in enumerate(r["errs"]) if v > 0)
    out.append(f'<polyline points="{pts}" fill="none" stroke="#2F5BD3" stroke-width="2.2"/>')
    for k, v in enumerate(r["errs"]):
        if v > 0 and k % 4 == 0:
            out.append(f'<circle cx="{X2(k):.1f}" cy="{Y2(v):.1f}" r="2.6" fill="#2F5BD3"/>')
    lx, ly = R2 - 236, T + 16
    out.append(f'<rect x="{lx - 8}" y="{ly - 12}" width="240" height="46" rx="6" fill="#FFFFFF" stroke="#E5E7EB"/>')
    out.append(f'<line x1="{lx}" y1="{ly}" x2="{lx + 26}" y2="{ly}" stroke="#2F5BD3" stroke-width="2.2"/>')
    out.append(f'<text x="{lx + 32}" y="{ly + 4}" class="xs">实际误差（构建时迭代算出）</text>')
    out.append(f'<line x1="{lx}" y1="{ly + 20}" x2="{lx + 26}" y2="{ly + 20}" stroke="#C2410C" stroke-width="2" stroke-dasharray="7 5"/>')
    out.append(f'<text x="{lx + 32}" y="{ly + 24}" class="xs">上界 γᵏ·‖V_0 − V_π‖∞（γ = {r["g"]:g}）</text>')
    step = 5 if K <= 40 else 10
    for X, (l, rr) in ((X1, (L1, R1)), (X2, (L2, R2))):
        for k in range(0, K + 1, step):
            out.append(f'<text x="{X(k):.1f}" y="{H - B_ + 16}" text-anchor="middle" class="xs mono">{k}</text>')
        out.append(f'<text x="{(l + rr) / 2}" y="{H - 8}" text-anchor="middle" class="sm">迭代次数 k</text>')
    svg = f'<svg viewBox="0 0 {W} {H}" xmlns="http://www.w3.org/2000/svg">' + "".join(out) + "</svg>"
    return fig(svg, d.get("cap"))


# ============================================================ flow：方框 + 箭头的结构图（架构 / 流程 / 族谱 / 路线图）
MARK = {"b": "ab", "o": "ao", "g": "ag", "r": "ar", "p": "ap", "t": "at", "a": "aa"}
WARN = []


def text_w(s, size):
    w = 0.0
    for ch in str(s):
        w += size if ord(ch) > 0x2E80 else (size * 0.3 if ch == " " else size * 0.58)
    return w


def flow_norm(d):
    """布局简写：box 的 "cr":[列,行] → 用 cols/colw/rows/bh 换算坐标；"chains" → 依次连箭头。"""
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


def flow_edge_pts(a, bx):
    """自动选择出入边：水平分离走左右边中点，否则走上下边中点。"""
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
    for e in d.get("arrows", []):
        c = COL[e.get("c", "s")][1] if e.get("c", "s") != "s" else "#4B5563"
        pts = e["p"] if "p" in e else flow_edge_pts(boxes[e["from"]], boxes[e["to"]])
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
        raise SystemExit(f"[{kind}] JSON 错误: {e}\n{body[:200]}")
    fn = {"bars": bars, "conv": conv, "flow": flow}.get(kind) or F.MACROS[kind]
    return fn(d)


# ============================================================ deriv：逐行推导
def split_amp(line):
    """在花括号深度 0、且不在 \\begin..\\end 内的第一个 & 处切开。"""
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
    """texts: [(tex, display)] -> [html]，一次 node 调用批量渲染。"""
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
        t = include(f.read_text(encoding="utf-8"))
        t = CODE_RE.sub(lambda m: keep(code_block(m)), t)
        t = DERIV_RE.sub(deriv, t)
        t = MACRO_RE.sub(macro, t)
        parts.append(t)
    content = "\n".join(parts)
    content = VAL_RE.sub(val_sub, content)
    content = re.sub(r"<svg[\s\S]*?</svg>", lambda m: keep(m.group(0)), content)

    # ---- 公式：先收集，一次渲染
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
    left = re.findall(r"<!--(bars|conv|flow|plot|gbars|graph|grid|dtab|code|deriv)", content) + VAL_RE.findall(content)
    if left:
        print("未展开的宏:", left, file=sys.stderr)
    for w in WARN + F.WARN:
        print(w, file=sys.stderr)
    if checks:
        print("[对拍] Python ⇄ C++：" + "，".join(checks))
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
