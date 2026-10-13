"""把电子书章节转换成 Markdown：../UE动画系统_面试指南.md
- 正文、表格、代码、提示框、自测题与电子书一致（同一份 chapters/*.html）
- 数据图（bars / bspace / sync / curves / dials）用 build.py 里的同一套算法生成表格或字符图，数字一致
- 手绘 SVG 图用下面 ASCII 字典里的字符图替代
"""
import html
import json
import math
import pathlib
import re
from html.parser import HTMLParser

import build as B

ROOT = pathlib.Path(__file__).resolve().parent
OUT = ROOT.parent / "UE动画系统_面试指南.md"

ASCII = {
"图 0-1": r"""
游戏线程  [角色/移动 Tick] → [NativeUpdateAnimation / EventGraph：读状态、写变量]          [Complete/Finalize：派发 Notify]
                                         │                                                   ▲            │
工作线程                                  └→ [Update（Proxy）：推进时间、算权重] → [Evaluate：采样→混合→组件空间→IK] ┘            │
渲染线程                                                                                          [上传骨骼矩阵 → GPU 蒙皮] ◄┘
""",
"图 1-1": r"""
 SkeletalMeshComponent ──AnimClass──► AnimBlueprint ──Target Skeleton──┐
        │                                                               ▼
        └──► USkeletalMesh ──引用──────────────────────────────►  USkeleton  ◄── AnimSequence / BlendSpace / AimOffset
                   │                                     （骨骼层级·插槽·曲线名·同步标记名）   ◄── AnimMontage / PoseAsset / Composite
                   └──► UPhysicsAsset（碰撞体 · 约束）
""",
"图 1-2": r"""
骨骼层级（节选）                       实际存储：i  Name         Parent
root                                         0  root          -1
└ pelvis                                     1  pelvis         0
  ├ spine_01                                 2  spine_01       1
  │ └ spine_02                               3  spine_02       2
  │   └ spine_03                             4  spine_03       3
  │     ├ neck_01 └ head                     5  neck_01        4
  │     └ clavicle_l                         6  head           5
  │       └ upperarm_l                       7  clavicle_l     4   ← 父下标 < 子下标
  │         └ lowerarm_l └ hand_l            8  upperarm_l     7
  └ thigh_l └ calf_l └ foot_l                …
从前往后扫一遍，处理到 i 时父骨骼一定已经算完
""",
"图 2-1": r"""
LBS：前臂扭转 180°                       扭转骨分摊：每段只扭 60°
 ┌────────╲      ╱────────┐               ┌────────┬────────┬────────┐
 │         ╲    ╱         │               │   •    │   •    │   •    │
 │          ╲  ╱          │               │lowerarm│twist_01│  hand  │
 │          ╱  ╲          │               │        │        │        │
 └────────╱      ╲────────┘               └────────┴────────┴────────┘
  关节处半径 ×cos(θ/2)，180° 塌成一条线        截面保持（Mannequin 的 twist 骨就是干这个的）
""",
"图 3-1": r"""
              0°（正前）
           ↑ 欧拉插值 → 0°（沿 340° 的远路）
      ╭───────────╮
     │      │      │     Yaw 170°  → q = ( cos85°, 0, 0,  sin85° )
     │      │      │     Yaw −170° → q = ( cos85°, 0, 0, −sin85° )
      ╲    ╱ ╲    ╱      dot = cos170° ≈ −0.985 < 0
     B=−170° ↓ A=170°    不取反直接 lerp：结果 0°（错）
      四元数最短路径 → 180°   dot<0 时把 B 取反再 lerp：结果 180°（对）
""",
"图 3-2": r"""
  基础：跑步      +   加性：受击后仰 ΔR=20°（腿部 Δ=0）   × α   =   结果：边跑边后仰
     O                     O                                         O
     |                      \                                         \
     |                       \                                         \
    / \                   （无腿部变化）                                / \
""",
"图 B1-1": r"""
 ① 绑定时（参考姿势）                         ② 播放动画：肘部弯曲 60°
   上臂 1.0   上臂 0.5/前臂 0.5   前臂 1.0                    ●腕
 (=========●=============●=============)                     /
  肩      上臂骨     肘      前臂骨    腕          ●肩=======●肘   ← 前臂转了 60°，
 每个顶点：受哪几根骨头影响 + 各占多少（和为 1）                    肘部 0.5/0.5 的顶点各跟一半
""",
"图 B1-3": r"""
 ①问状态 → ②选动画 → ③取姿势 → ④混合 → ⑤修正 → ⑥算矩阵 → ⑦显卡蒙皮
 EventGraph  状态机/    序列播放器   混合空间/   IK/Control  FK→蒙皮矩阵  GPU Skinning
             蒙太奇     （解压+插值） 分层混合    Rig
""",
"图 B2-1": r"""
 问“右手在哪”：
   局部空间（相对父骨骼）：在前臂末端，沿前臂方向 28 cm
   组件空间（相对角色根）：在角色前方 40、右侧 25、离地 120 cm
   世界空间（相对地图原点）：在地图坐标 (12040, −335, 220)
 角色在世界里移动时，只有世界空间的数在变
""",
"图 B2-2": r"""
 y                      ● 手 (33.23, 42.05)
 │                     /  前臂 28（肘再转 +45°，朝向 75°）
 │                    /
 │        肘 (25.98, 15.00) ●
 │              ／ 上臂 30（肩转 30°）
 ●──────────────────────── x
 肩 (0,0)
 肘 = (30·cos30°, 30·sin30°)；手 = 肘 + (28·cos75°, 28·sin75°)
""",
"图 B2-3": r"""
 ① 绑定时：手骨在 x=50，顶点在 x=55（组件空间绝对坐标）
 ② 乘参考姿势的逆：顶点相对手骨 = 55 − 50 = (+5, 0)
 ③ 乘当前骨头变换：手骨在 (70, 20) 且转了 90° → (70, 20) + (0, +5) = (70, 25)
""",
"图 B3-1": r"""
 pitch = 0°：yaw 环（水平）、pitch 环、roll 环各管一个方向
 pitch = 90°：机头朝上，roll 环被转到水平面，与 yaw 环共面 → 转 yaw 和转 roll 效果一样
""",
"图 B3-2": r"""
 虚部 i
   │     ● 结果 cos75° + i·sin75°
   │   ／   ● cos30° + i·sin30°
   │ ／ ／
   ●───────● 1 ── 实部
 (cos30°+i·sin30°)(cos45°+i·sin45°) = 0.2588 + 0.9659i = 转 75°
""",
"图 B3-3": r"""
 θ（绕 Z 轴）:   0°    60°     180°    360°    420°    540°    720°
 w = cos(θ/2):  1    0.866     0      −1     −0.866    0       1
 z = sin(θ/2):  0    0.5       1       0     −0.5     −1       0
 60° 与 420° 朝向相同，四元数互为相反数；转满 720° 四元数才回到原值
""",
"图 B4-1": r"""
 0 ──────●──────●─────────────────────────●──── 60
        A=10   结果=20                    B=50
        └ 走了全程的 25% ┘     也可读作：0.75×10 + 0.25×50 = 20
""",
"图 B4-2": r"""
              Jog_F (0,420)
               ●
               │＼
               │  ＼       输入点把三角形切成三块：
               │    ＼     对着 Walk_F 的一块（P-Jog_F-Jog_R）占 15.8% → Walk_F 0.158
 Walk_F(0,160) ●──●P ＼    对着 Jog_F  的一块（P-Walk_F-Jog_R）占 36.8% → Jog_F 0.368
                ＼ (180,180)＼  对着 Jog_R  的一块（P-Walk_F-Jog_F）占 47.4% → Jog_R 0.474
                  ＼         ＼
                    ＼─────────● Jog_R (380,0)
 加权：0.158·(0,160) + 0.368·(0,420) + 0.474·(380,0) = (180, 180)
""",
"图 5-1": r"""
Branch Filter = spine_01      depth=0   depth=2   depth=3   depth=4   +clavicle_r/-1
pelvis                          0         0         0         0         0
spine_01  (k=0)                 1         0.5       0.333     0.25      1
spine_02  (k=1)                 1         1         0.667     0.5       1
spine_03  (k=2)                 1         1         1         0.75      1
neck / head                     1         1         1         1         1
clavicle_r / arm                1         1         1         1         0（排除）
thigh / calf                    0         0         0         0         0
""",
"图 5-3": r"""
[Locomotion 状态机] → [Save Cached Pose "Loco"]
[Use "Loco"] ──────── Base Pose ─────────► [Layered blend per bone            ] → [Aim Offset] → [Foot IK / Control Rig] → [Output Pose]
[Use "Loco"] → [Slot 'UpperBody'] ─ Blend Pose 0 ─► [ spine_01 / depth 3 / Mesh Space ✔]
顺序：先合成全身姿势 → 再叠加瞄准（网格空间加性）→ 最后做 IK 修正
""",
"图 6-1": r"""
游戏线程 ① USkeletalMeshComponent::TickComponent
           TickAnimation()
             UAnimInstance::UpdateAnimation()
               PreUpdateAnimation（Proxy 拷数据）
               NativeUpdateAnimation / EventGraph   （不开多线程：在这里 Update 整张图）
           RefreshBoneTransforms() → 派发任务 ──┐        （派发后游戏线程继续 Tick 其他 Actor）
工作线程 ② ParallelAnimationEvaluation  ◄──────┘
           Proxy: UpdateAnimation   → 各节点 Update_AnyThread
           Proxy: EvaluateAnimation → 各节点 Evaluate_AnyThread
           FillComponentSpaceTransforms（局部 → 组件空间）──┐
游戏线程 ③ CompleteParallelAnimationEvaluation  ◄───────────┘
           交换缓冲、PostAnimEvaluation：派发 Notify；更新物理 / Socket、通知渲染线程
""",
"图 6-2": r"""
             Update（自顶向下：传 DeltaTime 和权重）  ───────►
[Output Pose] ── [Layered Blend] ──┬── [State Machine] ── [Blend Space]
                                   └── [Slot] ─────────── [Sequence Player]
             ◄─────── Evaluate（自底向上：叶子采样出姿势，逐级混合返回）
偶发：Initialize_AnyThread（首次 / 重新相关）· CacheBones_AnyThread（所需骨骼变化时解析 FBoneReference）
每帧：Update_AnyThread → Evaluate_AnyThread；组件空间节点用 EvaluateComponentSpace_AnyThread
""",
"图 7-1": r"""
 ● Entry → (Idle) ─速度>10─► (Start) ─快播完─► (Cycle 混合空间) ─无输入─► (Stop)
             ▲ ◄──输入取消──┘                                              │
             └──────────────────── Stop 播完 → Idle（自动规则）─────────────┘
 State Alias（任意地面状态）→ ◇Conduit ─跳起─► (JumpStart) ─播完─► (FallLoop) ─落地─► (Land) ─► 回到地面状态
""",
"图 7-3": r"""
AM_Attack（Slot：UpperBody）
Sections : |   Combo1   |   Combo2   |   Combo3   | Recover |
Notifies :    ▲伤害判定(State)  ▲连招窗口(Branching Point)  ▲  ▲脚步声
Blend    :  /‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾\
          Blend In                                   Blend Out（Trigger Time 前开始淡出）
Combo1 ──Montage_SetNextSection / JumpToSection──► Combo2
""",
"图 8-1": r"""
            ○ Pole / Joint Target
            ┆
            B（肘 / 膝）
           ╱  ╲            1. d = clamp(|T−A|, |L1−L2|, L1+L2)
       L1 ╱    ╲ L2        2. cos α = (L1² + d² − L2²) / (2·L1·d)
         ╱      ╲          3. bend = Pole 去掉沿 dir 的分量（归一化）
        ╱ α      ╲            B = A + dir·L1·cos α + bend·L1·sin α
       A ‐‐‐‐‐‐‐‐‐ C=T     4. 上臂转到 A→B，前臂转到 B→T
          d = |T−A|
""",
}


# ---------------------------------------------------------------- 数据宏 → Markdown
def md_bars(d):
    unit = d.get("unit", "")
    rows = [f"| {it[0]} | {it[3] if len(it) > 3 else f'{it[1]:g} {unit}'.strip()} |" for it in d["items"]]
    return f"**{d.get('title', '')}**\n\n| 项 | 值 |\n|---|---|\n" + "\n".join(rows)


def md_bspace(d):
    S = d["samples"]; T = B.delaunay(S)
    out = [f"采样（{len(S)} 个）：" + "、".join(f"{n}({x:g},{y:g})" for n, x, y in S),
           "", f"Delaunay 三角剖分：{len(T)} 个三角形 —— " + "；".join("△" + "-".join(S[i][0] for i in t) for t in T), "",
           "| 查询点 | 参与混合的采样与权重 | 备注 |", "|---|---|---|"]
    for q in d.get("query", []):
        w, tri, (cx, cy) = B.bs_weights(S, T, q[0], q[1])
        ws = " · ".join(f"{S[i][0]} {v:.3f}" for i, v in sorted(w.items(), key=lambda kv: -kv[1]) if v > 1e-6)
        out.append(f"| {q[2]} ({q[0]:g}, {q[1]:g}) | {ws} | {'在三角网外，投影到边界 (' + f'{cx:.0f}, {cy:.0f}' + ')' if tri is None else '三角形内'} |")
    return "\n".join(out)


def md_sync(d):
    (wl, wL), (rl, rL) = d["walk"], d["run"]; T = d.get("T", 3.0); gl = 0.5 * wl + 0.5 * rl; N = 60
    foot = lambda ph, pl: "L" if ((ph - pl) % 1.0) < 0.5 else "R"
    names = {"none": "不同步", "norm": "同步组：归一化时间", "marker": "同步组：同步标记"}
    lines = ["```text", f"时间轴 0 ~ {T:g} s，每格 {T / N * 1000:.0f} ms；L = 左脚支撑，R = 右脚支撑，^ = 两条动画支撑脚不同"]
    for m in d["modes"]:
        a = b = mk = ""
        for k in range(N):
            t = T * (k + 0.5) / N
            if m == "none": pw, pr = (t / wl) % 1, (t / rl) % 1
            else:
                pw = (t / gl) % 1
                pr = pw if m == "norm" else (rL + (pw - wL) % 1) % 1
            fa, fb = foot(pw, wL), foot(pr, rL); a += fa; b += fb; mk += "^" if fa != fb else " "
        lines += ["", names[m], "  Walk |" + a + "|", "  Run  |" + b + "|", "       " + mk + f"  不一致 {100 * mk.count('^') / N:.0f}%"]
    lines.append("```")
    return "\n".join(lines)


def md_curves(d):
    a = lambda t: 50 * math.sin(2 * math.pi * t / 1.0)
    b = lambda t: 10 + 20 * math.sin(2 * math.pi * t / 0.8 + 1)
    t0, T = d.get("t0", 0.4), d.get("T", 0.2); dt = 1 / 120
    x0 = a(t0) - b(t0); v0 = ((a(t0) - a(t0 - dt)) - (b(t0) - b(t0 - dt))) / dt
    def res(m, t):
        if t < t0: return a(t)
        al = max(0, min(1, (t - t0) / T))
        if m == "cut": return b(t)
        if m == "linear": return (1 - al) * a(t) + al * b(t)
        if m == "smooth": s_ = al * al * (3 - 2 * al); return (1 - s_) * a(t) + s_ * b(t)
        return b(t) + B.inertial_offset(x0, v0, T, t - t0)
    ts = [t0 + k * 0.025 for k in range(-2, 11)]
    head = "| t (s) | 源 A | 目标 B | 硬切 | 线性淡化 | SmoothStep | 惯性化 |\n|---|---|---|---|---|---|---|"
    rows = [f"| {t:.3f}{' ← 切换' if abs(t - t0) < 1e-9 else ''} | {a(t):.1f} | {b(t):.1f} | " + " | ".join(f"{res(m, t):.1f}" for m in ("cut", "linear", "smooth", "inertial")) + " |" for t in ts]
    return f"输出角度（度），切换时刻 t0 = {t0} s、过渡 {T} s：\n\n" + head + "\n" + "\n".join(rows)


def md_dials(d):
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
            w.append(0.0 if p != fb or depth < 0 else 1.0 if depth == 0 else min(1.0, (k + 1) / depth))
        return w
    base, aim = d["base"], d["aim"]
    out = ["组件空间朝向（度，俯视，0° = 正前，顺时针为正）：", "", "| 模式 | " + " | ".join(names[i] for i in show) + " |", "|---|" + "---|" * len(show)]
    for lab, mode, depth in d["rows"]:
        if mode == "base": o = comp(base)
        elif mode == "aim": o = comp(aim)
        else:
            w = weights(d["filter"], depth)
            if mode == "local": o = comp([base[i] + (aim[i] - base[i]) * w[i] for i in range(len(par))])
            else:
                cb, ca = comp(base), comp(aim); o = [cb[i] + (ca[i] - cb[i]) * w[i] for i in range(len(par))]
        out.append(f"| {lab} | " + " | ".join(f"{o[i]:+.1f}" for i in show) + " |")
    return "\n".join(out)


MD_MACRO = {"bars": md_bars, "bspace": md_bspace, "sync": md_sync, "curves": md_curves, "dials": md_dials}

LAB_NAMES = {"quatlab": "两个 Yaw 角的混合：欧拉插值 vs 四元数", "bslab": "2D 混合空间：拖动输入点看权重", "synclab": "同步组与同步标记",
             "layerlab": "分层骨骼混合：Blend Depth 与 Mesh Space", "translab": "状态过渡：硬切 / 交叉淡化 / 惯性化", "iklab": "两骨骼 IK",
             "qaxislab": "轴角 → 四元数（调轴和角度，看 w/x/y/z 与 −q）", "gimballab": "欧拉角与万向节锁（pitch 拉到 90° 后拖 yaw 和 roll）"}


# ---------------------------------------------------------------- 极简 DOM
class Node:
    def __init__(self, tag, attrs, parent=None):
        self.tag, self.attrs, self.parent, self.kids = tag, dict(attrs), parent, []
    def cls(self): return self.attrs.get("class", "").split()


VOID = {"br", "img", "input", "hr", "meta", "link"}


class P(HTMLParser):
    def __init__(self):
        super().__init__(convert_charrefs=True); self.root = Node("root", {}); self.cur = self.root
    def handle_starttag(self, tag, attrs):
        n = Node(tag, attrs, self.cur); self.cur.kids.append(n)
        if tag not in VOID: self.cur = n
    def handle_endtag(self, tag):
        c = self.cur
        while c is not self.root and c.tag != tag: c = c.parent
        if c is not self.root: self.cur = c.parent
    def handle_data(self, data): self.cur.kids.append(data)


def inline(n):
    if isinstance(n, str): return re.sub(r"\s+", " ", n)
    t = "".join(inline(k) for k in n.kids)
    if n.tag in ("strong", "b"): return f"**{t.strip()}**" if t.strip() else ""
    if n.tag == "code": return f"`{t}`"
    if n.tag == "em": return f"*{t}*"
    if n.tag == "br": return "  \n"
    return t


def find(n, pred):
    for k in n.kids:
        if isinstance(k, Node):
            if pred(k): return k
            r = find(k, pred)
            if r: return r
    return None


def table(n):
    rows = []
    for tr in [k for k in walk(n) if isinstance(k, Node) and k.tag == "tr"]:
        rows.append([inline(c).strip().replace("|", "\\|") for c in tr.kids if isinstance(c, Node) and c.tag in ("td", "th")])
    if not rows: return ""
    w = max(len(r) for r in rows)
    rows = [r + [""] * (w - len(r)) for r in rows]
    return "\n".join(["| " + " | ".join(rows[0]) + " |", "|" + "---|" * w] + ["| " + " | ".join(r) + " |" for r in rows[1:]])


def walk(n):
    for k in n.kids:
        yield k
        if isinstance(k, Node): yield from walk(k)


def lst(n, depth=0):
    out = []; i = 0
    for li in [k for k in n.kids if isinstance(k, Node) and k.tag == "li"]:
        i += 1
        txt = "".join(inline(k) for k in li.kids if not (isinstance(k, Node) and k.tag in ("ul", "ol"))).strip()
        out.append("  " * depth + (f"{i}. " if n.tag == "ol" else "- ") + txt)
        for sub in [k for k in li.kids if isinstance(k, Node) and k.tag in ("ul", "ol")]:
            out.append(lst(sub, depth + 1))
    return "\n".join(out)


def quote(s): return "\n".join("> " + l if l.strip() else ">" for l in s.strip().split("\n"))


def block(n):
    if isinstance(n, str):
        s = n.strip()
        m = re.fullmatch(r"§§(\d+)§§", s)
        if m: return PH[int(m.group(1))] + "\n\n"
        return (re.sub(r"\s+", " ", s) + "\n\n") if s else ""
    c = n.cls(); tag = n.tag
    if tag in ("h1", "h2", "h3", "h4"): return "#" * {"h1": 1, "h2": 2, "h3": 3, "h4": 4}[tag] + " " + inline(n).strip() + "\n\n"
    if tag == "p": return inline(n).strip() + "\n\n"
    if tag in ("ul", "ol"): return lst(n) + "\n\n"
    if tag == "table": return table(n) + "\n\n"
    if "tbl" in c: return table(n) + "\n\n"
    if "part-head" in c:
        return "---\n\n# " + inline(find(n, lambda k: k.tag == "h1")).strip() + "\n\n> " + inline(find(n, lambda k: k.tag == "p")).strip() + "\n\n"
    if "cover" in c:
        return ""
    if "ch-head" in c:
        no = inline(find(n, lambda k: "no" in k.cls())).strip()
        if no.startswith("CHAPTER"): no = no.replace("CHAPTER ", "第 ") + " 章"
        elif no.startswith("BASICS"): no = "白话 " + no.split("·")[-1].strip()
        elif no.startswith("APPENDIX"): no = "附录 " + no.split("·")[-1].strip()
        h = inline(find(n, lambda k: k.tag == "h1")).strip(); lead = find(n, lambda k: "lead" in k.cls())
        return f"# {no} · {h}\n\n*{inline(lead).strip() if lead else ''}*\n\n"
    if "goals" in c: return "**本章目标**\n\n" + lst(find(n, lambda k: k.tag == "ul")) + "\n\n"
    if "co" in c:
        h = find(n, lambda k: "h" in k.cls() and k.tag == "span"); title = inline(h).strip() if h else ""
        body = "".join(block(k) for k in n.kids if k is not h)
        return quote(f"**{title}**\n\n{body}") + "\n\n"
    if "recap" in c:
        t = find(n, lambda k: "t" in k.cls())
        return quote(f"**{inline(t).strip()}**\n\n" + "".join(block(k) for k in n.kids if k is not t)) + "\n\n"
    if tag == "details":
        s = find(n, lambda k: k.tag == "summary"); body = "".join(inline(k) for k in n.kids if k is not s and isinstance(k, Node)).strip()
        return f"**{inline(s).strip()}**\n\n> 答：{body}\n\n"
    if "cards" in c:
        out = []
        for card in [k for k in n.kids if isinstance(k, Node) and "card" in k.cls()]:
            t = find(card, lambda k: "t" in k.cls()); p = find(card, lambda k: k.tag == "p")
            out.append(f"- **{inline(t).strip()}**：{inline(p).strip() if p else ''}")
        return "\n".join(out) + "\n\n"
    if "ans" in c:
        out = ""
        for k in n.kids:
            if isinstance(k, Node) and "q" in k.cls(): out += f"**{inline(k).strip()}**\n\n"
            else: out += block(k)
        return out
    if "goals" in c or "lab" in c: return ""
    return "".join(block(k) for k in n.kids)


PH = []


def pre(t):
    def code(m):
        lang, title, body = m.group(1), m.group(2).strip(), m.group(3).strip("\n")
        PH.append(f"*{title}*\n\n```{'cpp' if lang == 'cpp' else 'text'}\n{body}\n```"); return f"\n§§{len(PH) - 1}§§\n"
    t = B.CODE_RE.sub(code, t)
    def mac(m):
        kind, d = m.group(1), json.loads(m.group(2)); cap = re.sub(r"<[^>]+>", "", d.get("cap", ""))
        body = MD_MACRO[kind](d) if kind in MD_MACRO else ""
        PH.append(f"{body}\n\n*{html.unescape(cap)}*"); return f"\n§§{len(PH) - 1}§§\n"
    t = B.MACRO_RE.sub(mac, t)
    def figure(m):
        cap = re.search(r"<figcaption>(.*?)</figcaption>", m.group(0), re.S); cap = html.unescape(re.sub(r"<[^>]+>", "", cap.group(1))) if cap else ""
        key = re.match(r"(图 [A-Z]?\d+-\d+)", cap.strip()); art = ASCII.get(key.group(1)) if key else None
        if art is None: print("[warn] 图没有 ASCII 版：", cap[:30])
        PH.append((f"```text{art}```\n\n" if art else "") + f"*{cap.strip()}*"); return f"\n§§{len(PH) - 1}§§\n"
    t = re.sub(r"<figure>[\s\S]*?</figure>", figure, t)
    def lab(m):
        k = next((k for k in LAB_NAMES if k in m.group(1)), None)
        PH.append(f"> 🧪 **交互实验（仅电子书）**：{LAB_NAMES.get(k, '')}——在 `UE_Animation.html` 中可拖动 / 调参。"); return f"\n§§{len(PH) - 1}§§\n"
    t = re.sub(r'<div class="lab ([^"]*)" id="[^"]*"></div><!--/lab-->', lab, t)
    return t


HEADER = """# UE 动画系统：从骨骼到动画蓝图 —— 游戏客户端秋招面试指南

> 配套电子书：`UE_Animation.html`（含 8 个交互实验）/ `UE_Animation.pdf`；实测代码：`demos/anim_demo.cpp`，输出：`demos/run_output.txt`。
> 本文档与电子书由同一份源文件（`src/chapters/*.html`）生成，数据图在构建时用与 demo 相同的算法计算。

## 提问记录

| 时间 | 原始提问 | 回答对应章节 |
|---|---|---|
| 2026-10-12 10:27 | 秋招面试的岗位背景是游戏客户端，你需要详细回答我的问题，并做出深层次的技术解答，并将每一次提问的内容和回答的内容都落盘为md文档。适当控制深度，增可读性，并点出面试需要注意的问题。详细解析一下 UE的动画系统，包含骨骼、动画蓝图、分层骨骼混合、混合空间等等，可以稍微详细一点，实现本次提问请以md+电子书的形式实现 | 全文：第 0 章（30 秒答案）· 第 1~2 章（骨骼、蒙皮、压缩）· 第 3 章（混合数学）· 第 4 章（混合空间、同步组）· 第 5 章（分层骨骼混合）· 第 6~8 章（动画蓝图、状态机、惯性化、蒙太奇、IK）· 第 9 章（性能）· 第 10 章（网络、排错、40 题、速记卡） |
| 2026-10-12 14:54 | 你需要进一步直白的细化，例如解释什么什么是四元数。你可以进一步参考之前的学习资料：@"UE 动画 notion  原笔记/UE 动画系统 33946c2849a280ac895bc13e503a828a.md" | 新增**前置篇**：白话 B1（骨骼动画怎么让模型动起来）· B2（变换、坐标空间、FK 手算、矩阵约定、逆绑定）· **B3（什么是四元数：欧拉角三大问题、复数旋转、轴角公式、q·v·q⁻¹、q 与 −q、乘法顺序、漂移、UE 接口）** · B4（插值、权重、加性、重心坐标）· B5（名词白话词典）；新增**附录 A**（对照 Notion 原笔记的 20 条修正）；新增实测 E11 / E12；修正第 1 章虚拟骨骼描述 |

## 目录

{TOC}

"""


def main():
    parts = []
    for f in sorted((ROOT / "chapters").glob("*.html")):
        t = pre(f.read_text(encoding="utf-8"))
        p = P(); p.feed(t)
        parts.append(block(p.root))
    body = "".join(parts)
    body = re.sub(r"\n{3,}", "\n\n", body)
    toc = []
    for m in re.finditer(r"^(#{1,2}) (.+)$", body, re.M):
        lvl, title = len(m.group(1)), m.group(2).strip()
        if lvl == 1 and ("部分" in title or title.startswith("前置篇")): toc.append(f"- **{title}**")
        elif lvl == 1: toc.append(f"  - {title}")
    md = HEADER.replace("{TOC}", "\n".join(toc)) + body
    left = re.findall(r"<[a-zA-Z/][^>]*>", md)
    if left: print("[warn] 残留标签：", left[:8])
    OUT.write_text(md, encoding="utf-8")
    print(f"OK -> {OUT} ({OUT.stat().st_size / 1024:.1f} KB, lines={md.count(chr(10))}, code={md.count('```') // 2})")


if __name__ == "__main__":
    main()
