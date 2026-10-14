"""把电子书章节转换成 Markdown：../UE联机射击Demo_面试复盘.md
- 正文、表格、代码、提示框、自测题与电子书同源（chapters/*.html）
- 数据宏（bars / lanes / plot / dtab）和 {{v:...}} 用 build.py 的同一份 figdata.json 生成，数字一致
- 手绘 SVG 用下面 ASCII 字典里的字符图替代
"""
import html
import json
import pathlib
import re
from html.parser import HTMLParser

import build as B

ROOT = pathlib.Path(__file__).resolve().parent
OUT = ROOT.parent / "UE联机射击Demo_面试复盘.md"

ASCII = {
"图 1-1": r"""
 每一端各跑一份                    服务器（主机）                         复制到客户端后
 ┌ 关卡蓝图 ──────────────┐  Server_   ┌ GM.AddReadyPlayer ─────────────┐
 │ 主菜单 / AnyKey 准备    │ ReportReady│ 人数到齐 → 每个 Pawn 上         │
 │ ✗ Server_SetStart 被丢弃│ ─────────► │   Multicast_StartGame          │
 └────────────────────────┘            └────────────────────────────────┘
 ┌ UMG：InGame/Pause/结算 ┐            ┌ BP_Weapon.Server_Fire(Reliable)┐      ┌ 武器 / 子弹副本 ─────────┐
 │ 倒计时各算各的          │            │ ① Multicast_Fire（动画）        │ ───► │ 弹药无条件复制           │
 │ ✗ SetGamePaused/OpenLevel│           │ ② SpawnBullet → CheckCanFire   │      │ 子弹晚 RTT/2 出现         │
 └────────────────────────┘    ┌────► │   生成复制子弹、扣弹、后坐力     │      └──────────────────────────┘
 ┌ BP_Player 输入（本机）──┐    │       └────────────────────────────────┘
 │ 开火：计时器→Server_Fire├────┘       ┌ Server_Change(Reliable) ───────┐
 │ 瞄准：只改本机 AnimBP   │            │ 当前=0 → 多播 → Delay           │
 │ 冲刺：Server RPC 只改服务器│         │ ✗ 当前 = 剩余子弹计算()（返回0） │
 └────────────────────────┘            └────────────────────────────────┘
 ┌ ✗ BP_Bullet.ReceiveHit（每一端）┐    ┌ BP_Zombie（AI 在服务器）────────┐      ┌ 僵尸副本 ─────────────────┐
 │ 弹孔/音效/粒子：没问题        │ ──► │ ReceiveAnyDamage → 血量 −= 伤害 │ ───► │ ✗ 死亡组播里的补刷每端都执行│
 │ ApplyDamage：客户端被引擎拦下 │     │ → HitReact / Death 多播          │      │ ✗ 重新生成 → 本地幽灵僵尸   │
 └──────────────────────────────┘     │ ✗ Death → 每端都调用 重新生成    │      └───────────────────────────┘
                                      └─────────────────────────────────┘
""",
"图 3-1": r"""
 UNetConnection ◄──属于── PlayerController ◄──Owner── PlayerState（复制给所有人：分数/是否准备）
 （玩家 2 的连接）          ▲ 服务器 + 玩家 2 本机
                            │ Owner（Possess 时设置）
                         Character（玩家 2 本机：AutonomousProxy） - - ► 本地专属：输入映射、HUD、1P 手臂
                            ▲
                            │ Owner（生成参数里设置；Instigator = Pawn）
                         Weapon（bNetUseOwnerRelevancy） - - ► 受益：ServerFire 可从玩家 2 发出；OwnerOnly 只发给玩家 2
""",
"图 4-2": r"""
 ① 本机（t=0）                        ② 服务器（t≈RTT/2）                     ③ 另一端
 IA_Fire → StartFire                   校验：存活/未换弹/射速/弹药/起点/方向
 预测扣一发，Seq=++LocalSeq     ┌───► 扣弹 · AckSeq=Seq · ++BurstCounter ── 复制 ──► OnRep_BurstCounter：3P 开火、曳光
 立刻播表现（1P 动画/火焰/声音/后坐力）│ 判定命中（PvE 容差校验 / PvP 回溯）
 本地射线（只播命中特效）       │     ApplyPointDamage(Instigator, Causer)
 ServerFireShot(Seq,起点,方向,时间) ──┘ 僵尸 TakeDamage（仅服务器）─────── 复制 ──► OnRep_Health/LastHit：受击
 ⑤ OwnerOnly 弹药(含 AckSeq) ◄── 复制 ── Health≤0 → bIsDead、停AI、加分、补刷 ── 复制 ──► OnRep_IsDead：布娃娃
    显示 = 服务器弹数 −（LocalSeq − AckSeq）
""",
"图 5-2": r"""
            ServerStartReload（备弹>0 且 未满 且 存活）          计时器到点
 ( Idle ) ─────────────────────────────────► ( Reloading ) ───────────────► [ ReloadWeapon：take=min(空位,备弹) ]
    ▲   本机：立即播 1P 换弹动画                 │ bPendingReload=true（SkipOwner）              │
    │◄─ ─ 取消：切枪/死亡/冲刺打断 → ClearTimer，弹药不变 ─ ┘                                 │
    └──────────────────────── 复位 bPendingReload = false（复制）◄────────────────────────────┘
 另一端：OnRep_PendingReload(true/false) 播 / 停 3P 换弹动画
""",
"图 7-1": r"""
 ┌ UGameInstance（进程级，切地图不销毁，不复制）┐   ┌ UWorld（当前地图，切地图时整体重建）───────────────────────────┐
 │ ULocalPlayer ×1：视口、Enhanced Input 子系统   │──►│ PersistentLevel（所有 Actor 的 Outer）                       │
 │ Subsystems：会话、存档、设置                   │   │   WorldSettings · LevelScriptActor · 摆放的 Actor · 生成的 Actor │
 │ TimerManager（World 的计时器由它管理）         │   │ AuthorityGameMode（仅服务器）                                 │
 │ 跨地图的本地数据（每台机器各一份）             │   │ GameState（复制）→ PlayerArray → PlayerState ×2               │
 │ GameViewportClient：UMG 层，只在本机           │   │ PlayerController ×2：Player = ULocalPlayer 或 UNetConnection  │
 └────────────────────────────────────────────────┘   │ NetDriver → UNetConnection ×1（远端玩家）                     │
                                                       │ Pawn：BP_CoopCharacter ×2（└ BP_WeaponBase）、BP_Zombie ×N     │
                                                       │       （└ 被 AIC_Zombie Possess，AIController 只在服务器）     │
                                                       └───────────────────────────────────────────────────────────────┘
""",
"图 7-6": r"""
            ┌ 表现层（每端本地，不复制）：WBP_HUD · Lobby / Result · AnimBP · 特效音效 ┐
 请求向下   ├ 请求层（本地控制端 → 服务器）：PC_Coop 准备/重开/暂停菜单 · 角色输入        ┤   通知向上
 Server RPC ├ 玩法层（服务器权威，状态复制）：Character · Weapon · AC_Health · Zombie+AI    ┤   复制 / OnRep
 函数调用 ▼ └ 规则与全局状态层：GM_Coop（仅服务器）· GS_Coop · PS_Coop（复制）            ┘ ▲ 事件分发器
 禁止：武器直接改 HUD · GameMode 建界面 · UI 里算倒计时 · 僵尸 Cast 玩家 PC · 客户端调用 GameMode · 多播里改复制变量 · Tick 里每帧 Cast
""",

}


# ---------------------------------------------------------------- 数据宏 → Markdown
def md_bars(d):
    rows = []
    for it in d["items"]:
        if it[0].startswith("—"):
            rows.append(f"| **{it[0][1:]}** | |")
            continue
        v = B.resolve(it[1]) if isinstance(it[1], str) else it[1]
        txt = it[3] if len(it) > 3 else f"{format(v, d.get('fmt', 'g'))} {d.get('unit', '')}".strip()
        rows.append(f"| {it[0]} | {txt} |")
    return f"**{d.get('title', '')}**\n\n| 项 | 值 |\n|---|---|\n" + "\n".join(rows)


def md_lanes(d):
    sc = d["scheme"]; evs = B.lane_events(sc); t1 = d.get("t1", 1.5)
    inp = {e["v"]: e["t"] for e in evs if e["kind"] == "input"}
    srv = {e["v"]: e for e in evs if e["lane"] == 1}
    oks = sorted([e for e in evs if e["lane"] == 1 and e["kind"] == "ok"], key=lambda e: e["t"])
    own, rem = {}, {}
    for e in evs:
        if e["kind"] not in ("fx", "ghost"):
            continue
        if sc == 0:
            (own if e["lane"] == 0 else rem)[e["v"]] = (e["t"], e["kind"])
        elif e["lane"] == 0:
            own[e["v"]] = (e["t"], "fx")
        elif e["lane"] == 2 and e["v"] <= len(oks):
            rem[oks[e["v"] - 1]["v"]] = (e["t"], "fx")
    ms = lambda t: f"{t * 1000:.0f}"
    out = [f"**{d.get('title', '')}**（时刻单位 ms）", "", "| 第几发 | 本机按下 | 服务器处理 | 本机看到开火 | 另一端看到开火 |", "|---|---|---|---|---|"]
    for s in sorted(inp):
        if inp[s] > t1:
            continue
        sv = srv.get(s)
        st = f"{ms(sv['t'])}（{'接受' if sv['kind'] == 'ok' else '拒绝'}）" if sv else "—"
        f = lambda m: (f"{ms(m[s][0])}" + ("（空放）" if m[s][1] == "ghost" else "")) if s in m else "—"
        out.append(f"| {s} | {ms(inp[s])} | {st} | {f(own)} | {f(rem)} |")
    reps = [f"{ms(e['t'])}:{e['v']}" for e in evs if e["kind"] == "rep" and e["t"] <= t1]
    dry = [ms(e["t"]) for e in evs if e["kind"] == "dry"]
    out += ["", "本机收到的弹药复制（时刻:弹数）：" + " ".join(reps), "", "本机空仓停火：" + ("、".join(dry) + " ms" if dry else "—")]
    return "\n".join(out)


def md_plot(d):
    (x0, x1) = d["x"]; n = d.get("xticks", 8)
    xs = [x0 + (x1 - x0) * k / n for k in range(n + 1)]
    sers = [(lab, B.resolve(path)) for lab, path, *_ in d["series"]]
    def at(pts, x):
        last = pts[0][1]
        for px, py in pts:
            if px > x + 1e-9:
                break
            last = py
        return last
    head = "| " + d.get("xlab", "x") + " | " + " | ".join(lab for lab, _ in sers) + " |"
    rows = [head, "|---|" + "---|" * len(sers)]
    for x in xs:
        rows.append(f"| {x:g} | " + " | ".join(f"{at(p, x):g}" for _, p in sers) + " |")
    return f"{d.get('ylab', '')}（按横轴采样）：\n\n" + "\n".join(rows)


def md_dtab(d):
    head, rows = B.dtab_rows(d)
    t = ["| " + " | ".join(head) + " |", "|" + "---|" * len(head)] + ["| " + " | ".join(r) + " |" for r in rows]
    return "\n".join(t)


MD_MACRO = {"bars": md_bars, "lanes": md_lanes, "plot": md_plot, "dtab": md_dtab, **B.BM.MD_MACROS}
LAB_NAMES = {"firelab": "开火时序：切换方案、拖动 RTT 看三端的事件", "lagcomplab": "延迟补偿：拖动 RTT / 第几枪，看本机看到的目标与服务器判定的差距"}


# ---------------------------------------------------------------- 极简 DOM
class Node:
    def __init__(self, tag, attrs, parent=None):
        self.tag, self.attrs, self.parent, self.kids = tag, dict(attrs), parent, []
    def cls(self): return (self.attrs.get("class") or "").split()


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
    if n.tag == "span" and "tag" in n.cls(): return f"【{t.strip()}】"
    return t


def find(n, pred):
    for k in n.kids:
        if isinstance(k, Node):
            if pred(k): return k
            r = find(k, pred)
            if r: return r
    return None


def walk(n):
    for k in n.kids:
        yield k
        if isinstance(k, Node): yield from walk(k)


def table(n):
    rows = []
    for tr in [k for k in walk(n) if isinstance(k, Node) and k.tag == "tr"]:
        rows.append([inline(c).strip().replace("|", "\\|") for c in tr.kids if isinstance(c, Node) and c.tag in ("td", "th")])
    if not rows: return ""
    w = max(len(r) for r in rows)
    rows = [r + [""] * (w - len(r)) for r in rows]
    return "\n".join(["| " + " | ".join(rows[0]) + " |", "|" + "---|" * w] + ["| " + " | ".join(r) + " |" for r in rows[1:]])


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
        out = ""
        for part in re.split(r"(§§\d+§§)", s):
            mm = re.fullmatch(r"§§(\d+)§§", part.strip())
            if mm: out += PH[int(mm.group(1))] + "\n\n"
            elif part.strip(): out += re.sub(r"\s+", " ", part.strip()) + "\n\n"
        return out
    c = n.cls(); tag = n.tag
    if tag in ("h1", "h2", "h3", "h4"): return "#" * {"h1": 1, "h2": 2, "h3": 3, "h4": 4}[tag] + " " + inline(n).strip() + "\n\n"
    if tag == "p":
        t = inline(n).strip()
        return (f"*{t}*" if "tcap" in c else t) + "\n\n"
    if tag in ("ul", "ol"): return lst(n) + "\n\n"
    if tag == "table" or "tbl" in c: return table(n) + "\n\n"
    if "part-head" in c:
        return "---\n\n# " + inline(find(n, lambda k: k.tag == "h1")).strip() + "\n\n> " + inline(find(n, lambda k: k.tag == "p")).strip() + "\n\n"
    if "cover" in c:
        sub = find(n, lambda k: "sub" in k.cls())
        return "> " + inline(sub).strip() + "\n\n" if sub else ""
    if "ch-head" in c:
        no = inline(find(n, lambda k: "no" in k.cls())).strip()
        if no.startswith("CHAPTER"): no = no.replace("CHAPTER ", "第 ") + " 章"
        elif no.startswith("APPENDIX"): no = "附录 " + no.split("·")[-1].strip()
        elif no.startswith("BRIEFING"): no = "口径篇 " + no.split("·")[-1].strip()
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
        s = find(n, lambda k: k.tag == "summary")
        body = "".join(inline(k) for k in n.kids if k is not s).strip()
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
    if "state" in c and tag == "div":
        sl = find(n, lambda k: "sl" in k.cls())
        parts = []
        for k in n.kids:
            if k is sl: continue
            if isinstance(k, Node) and k.tag == "div" and not find(k, lambda x: x.tag in ("p", "ul", "ol", "table", "div")):
                parts.append(inline(k).strip())
            else:
                parts.append(block(k).strip())
        body = "\n\n".join(x for x in parts if x)
        return quote(f"**【{inline(sl).strip()}】** {body}") + "\n\n"
    if "pit" in c:
        pn = find(n, lambda k: "pn" in k.cls()); pt = find(n, lambda k: "pt" in k.cls()); pc = find(n, lambda k: "pc" in k.cls())
        out = f"### {inline(pn).strip()} · {inline(pt).strip()}\n\n" + (f"*{inline(pc).strip()}*\n\n" if pc else "")
        for r in [k for k in n.kids if isinstance(k, Node) and "pr" in k.cls()]:
            pl = find(r, lambda k: "pl" in k.cls())
            body = "".join(block(k) for k in r.kids if k is not pl).strip()
            out += (f"**{inline(pl).strip()}**：{body}" if "\n" not in body else f"**{inline(pl).strip()}**\n\n{body}") + "\n\n"
        return out
    if "pitch" in c:
        out = ""
        for k in n.kids:
            if isinstance(k, Node) and "q" in k.cls(): out += f"**{inline(k).strip()}**\n\n"
            else: out += block(k)
        return out
    if "lab" in c: return ""
    return "".join(block(k) for k in n.kids)


PH = []


def pre(t):
    def code(m):
        lang, title, body = m.group(1), m.group(2).strip(), m.group(3).strip("\n")
        PH.append(f"*{title}*\n\n```{ {'cpp': 'cpp', 'bash': 'bash'}.get(lang, 'text') }\n{body}\n```"); return f"\n§§{len(PH) - 1}§§\n"
    t = B.CODE_RE.sub(code, t)
    def mac(m):
        kind, d = m.group(1), json.loads(m.group(2))
        cap = html.unescape(re.sub(r"<[^>]+>", "", B.VAL_RE.sub(B.val_sub, d.get("cap", ""))))
        if d.get("state"):
            m2 = re.match(r"^((?:图|表) [A-Z]?\d+[A-Z]?-\d+[a-z]?\s*)", cap)
            tg = f"【{B.STATE_NAME[d['state']]}】"
            cap = (m2.group(1) + tg + cap[m2.end():]) if m2 else tg + cap
        PH.append(f"{B.VAL_RE.sub(B.val_sub, MD_MACRO[kind](d))}\n\n*{cap}*"); return f"\n§§{len(PH) - 1}§§\n"
    t = B.MACRO_RE.sub(mac, t)
    t = B.VAL_RE.sub(B.val_sub, t)
    def figure(m):
        cap = re.search(r"<figcaption>(.*?)</figcaption>", m.group(0), re.S); cap = html.unescape(re.sub(r"<[^>]+>", "", cap.group(1))) if cap else ""
        key = re.match(r"(图 [A-Z]?\d+[A-Z]?-\d+)", cap.strip()); art = ASCII.get(key.group(1)) if key else None
        cap = re.sub(r"^(图 [A-Z]?\d+[A-Z]?-\d+\s*)(当前做法|理想方案|最小修复)", r"\1【\2】", cap.strip())
        if art is None: print("[warn] 图没有 ASCII 版：", cap[:30])
        PH.append((f"```text{art}```\n\n" if art else "") + f"*{cap.strip()}*"); return f"\n§§{len(PH) - 1}§§\n"
    t = re.sub(r"<figure>[\s\S]*?</figure>", figure, t)
    def lab(m):
        k = next((k for k in LAB_NAMES if k in m.group(1)), None)
        PH.append(f"> 🧪 **交互实验（仅电子书）**：{LAB_NAMES.get(k, '')}——在 `UE_Coop_Shooter.html` 中可调参。"); return f"\n§§{len(PH) - 1}§§\n"
    t = re.sub(r'<div class="lab ([^"]*)" id="[^"]*"></div><!--/lab-->', lab, t)
    return t


HEADER = """# UE 双人联机射击 Demo：面试复盘与正确实现 —— 游戏客户端秋招面试指南

> 配套电子书：`UE_Coop_Shooter.html`（含 40+ 张蓝图风格事件图表与 2 个交互实验）/ `UE_Coop_Shooter.pdf`；网络行为模拟：`demos/coop_net_demo.cpp`（E1~E7）与 `demos/projectile_demo.cpp`（E8~E10，实体子弹），输出：`demos/run_output.txt`、`demos/figdata.json`；
> 原工程审计：`repo/`（dxHWH/-demo@d7e5c75 的逻辑资产快照）→ `audit/*.txt`（由 `tools/bpdump.py` 从 .uasset 导出的蓝图伪代码）。
> 本文档与电子书由同一份源文件（`src/chapters/*.html`）生成，所有数字来自同一份实测数据。

## 提问记录

| 时间 | 原始提问 | 回答对应章节 |
|---|---|---|
| 2026-10-12 15:51 | 秋招面试的岗位背景是游戏客户端，你需要详细回答我的问题，并做出深层次的技术解答，并将每一次提问的内容和回答的内容都落盘为md文档。适当控制深度，增可读性，并点出面试需要注意的问题。需求如下：https://github.com/dxHWH/-demo（这是一个极度失败的demo，这是早期从单人游戏在不熟悉UE网络系的前提下改到的双人游戏的）用一本新的电子书或者md回答面试中针对这个demo的回答与解析。（你需要用UE双人游戏中更合理的方式去实现，上面的链接只是一个不合适的实现，并且可能带有BUG），有以下问题供你参考：①描述游戏从开火到击中僵尸的工程中，发生了什么？②描述一下控制的角色、控制器、武器应该怎么绑定 ③如果弱网造成卡顿，应该怎么优化？④怎么实现子弹同步、换弹问题的？等等 | 第 0 章（四问 30 秒答案 + 怎么介绍项目）· 第 1 章（原 Demo 审计、问题清单）· 第 2 章（网络基础）· **第 3 章（问②绑定）· 第 4 章（问①开火到击中）· 第 5 章（问④子弹同步与换弹）· 第 6 章（问③弱网优化）** · 第 16 章（面试题 1~40 + 速记卡）· 附录 A（问题全清单、审计方法、实测总表） |
| 2026-10-12 21:08 | 下一步，进一步优化重做的部分，并尽可能详细，最好文图并茂结合类似蓝图逻辑说明每一个类的事件图表，并再次集合UEGameplay框架说明（OOP：world 、 controller  、 player_BP等等）讲清楚一个合格的DEMO应该是什么这样的状态以及技术细节，增加可读性，并做好面试问题追问防守。 | 重写第三部分：**第 7 章（Gameplay 框架 OOP：World / GameInstance / Controller / Player BP 的运行时结构、继承树、五种关系、在哪端、生命周期、启动时序）· 第 8 章（合格 Demo 的 20 条验收标准、类清单、16 个复制变量与 7 个 RPC 总表、工程结构、蓝图 vs C++）· 第 9 章（GameInstance / GameMode / GameState / PlayerState / PlayerController 事件图表）· 第 10 章（BP_CoopCharacter / AC_Health / AnimBP 事件图表）· 第 11 章（BP_WeaponBase 五张事件图表 + 射速令牌桶）· 第 12 章（僵尸 / AIController / 行为树）· 第 13 章（UI）· 第 14 章（测试、调试、演示、README）**；新增第四部分：**第 15 章（12 条追问链 + 危险说法对照）**· 第 16 章扩充到 50 题；勘误：原 Demo 死亡时 DisableInput 实际不生效（第 1、3、10 章）；新增问题 #21、#22（僵尸随机各端不一致） |
| 2026-10-13 10:23 | 在流程图那一部分，你还需要进一步明确 复制的属性、哪些时间是运行在服务器上、哪些是组播、哪些变量打开了复制。你需要讲解清楚 | 流程图全部补网络标注：**第 9 章 1.1~1.2（执行端标签怎么读、图 9-1a 判断事件在哪端执行、表 9-1 引擎事件分端、复制标注怎么读）**；第 9~12 章每个类开头新增“网络规格”面板（Class Defaults 复制开关 / 每个变量的 Replication 与 Condition / 每个事件的 Replicates、Reliable、在哪执行）；每张事件图表的 Set 节点标出 ⇄ RepNotify / ⇄ Replicated / ○ 不复制，入口节点标出执行端，图下附“本图网络摘要”（执行端 / 写入的复制变量 / 本端变量 / RPC / 组播）；第 13 章 UI 标出“⇠ 读复制值”并新增表 13-1；时序图（图 7-4、7-5、9-2、11-7）按进程分组；**第 8 章新增：打开复制的三层开关、表 8-4 复制接收矩阵（双人局每个变量发给谁）、表 8-3 RPC 与组播总表（8 RPC + 2 组播，含在哪调用 / 在哪执行）、5.1 组播去哪了（原 8 个组播逐个替换）、表 8-6 执行端速查**；第 11 章 OnRep_BurstCounter 在哪几台机器执行（蓝图 vs C++）；第 12 章 4.5 全书仅有的两个组播；第 16 章新增第 51~56 题 |
| 2026-10-14 11:11 | 你要知道面试官是看不到这个DEMO的，我们需要自己对demo进行描述上的优化并对齐口径，并理清楚这种功能/需求场景下，更好的实现是什么？讲清楚实现方法而非代码。你可以把我们目前犯的问题看作是容易踩的坑，然后再描述如何避免和正确实现。更新一波电子书 | 新增**第零部分 · 口径篇**（排在全书最前）：**K1 把 Demo 讲清楚**（30 秒答案、讲 Demo 四段结构图、项目档案表、一局流程与“原版 / 重构后谁在管”、10 秒 / 1 分钟 / 3 分钟讲法与时间分配、做过 / 验证过 / 设计的三种时态口径、统一数字表 / 统一术语表 / 易前后不一的统一答案、白板图“一发子弹的三段”、讲 Demo 六个失误、追问入口表）；**K2 十四个功能场景**（六类根因图、场景总览表、14 张场景卡：需求 → 容易踩的坑（原版做法）→ 为什么会踩 → 玩家会看到 → 怎么避免 → 正确实现步骤 → 面试这样说；通用落地模板图；模板例外；七问自查表；拾取道具 / 开门两道迁移题）。**口径对齐（勘误）**：“SkipOwner 的 OnRep 不在服务器触发”改为“C++ 的 OnRep 不在服务器执行、蓝图 RepNotify 在服务器赋值时执行”（第 4、5、15 章）；第 15 章、第 8 章、第 0 章把“我做了 / 我测过”按时态改写；第 0 章 1 分钟口径与 K1 一致；RPC 名统一为 Server_FireShot / Server_StartReload；第 15 章危险说法表新增 3 行；第 16 章速记卡新增 2 张 |
| 2026-10-14 14:32 | 你需要在电子书中区分当前做法和理想重构方案的做法，例如实际上我们没有做射线检测，而是使用了在服务端生成子弹actor并同步的方法。你需要说明清楚当前状态和理想状态，让读者能更清晰的认识。更新电子书 | 新增**口径篇 K0 · 先分清：当前做法 vs 理想方案**（三种标记 当前做法 / 最小修复 / 理想方案；类名对照表 K0-1；20 项功能总对照表 K0-2（当前 / 最小修复 / 理想）；**第 3 节子弹专题**：原 Demo 没有射线检测、服务器生成复制子弹 Actor 的完整时序图 K0-2、玩家可感知的问题表、理想路线 A 命中扫描 / 路线 B 做对的抛射物对照表 K0-3、面试口径；读书地图 K0-4）。全书标注：第 0 章四问每题先给〔当前做法〕再给〔理想方案〕；第 1 章整章标当前；第 3~6 章章首当前 / 理想横幅、章末“当前做法 vs 理想方案”对照表（第 4 章命中扫描与抛射物表、第 5 章补“子弹本身”一行）；第三部分 part-head 声明全部为理想方案，第 8~13 章章首横幅 + 原 Demo 对照表，所有事件图表 / 网络规格面板 / 时序图图注带〔理想方案〕标签；第 14 章 README 分 A（重构完成后）/ B（当前状态）两版；第 15 章 7 条追问链先交代当前做法；第 16 章新增第 57~60 题；K2 场景卡改为“当前做法（坑）/ 最小修复 / 理想实现”，7 个场景新增最小修复。**勘误**：原书“子弹在每一端都扣僵尸血”不准确——Apply Damage 与 Event AnyDamage 为 Authority Only，客户端那次不执行，僵尸只在服务器扣血；问题 #2 由 P0 降为 P2（P0 共 7 条），第 0、1、2、8、16 章与 K1、K2、附录 A 同步修改；真正在每端执行的是死亡组播里的补刷 |
| 2026-10-14 15:23 | 你还需要点出当前做法存在的问题，更新电子书 | 全书每处〔当前做法〕之后补〔存在的问题〕（红色横幅 / ⚠ 行 / 红色列，带级别与附录 A 编号 A#n 和实测编号 En）：**K0 新增第 4 节“当前做法存在的问题：23 条按后果分五类”**（图 K0-3 问题分布、表 K0-4 分类表、30 秒答法），K0-1 改为“当前的主要问题”列，K0-2 二十个功能每行加 ⚠ 问题；第 0 章四问每题补问题段；第 1 章问题总数横幅；第 3~7 章章首“存在的问题”横幅；第 3、5 章章末与第 9~12 章章首对照表新增“存在的问题”列，第 13 章原 Demo 列加 ⚠；第 14 章补当前测试方式及其问题；第 15 章 7 条追问链补问题；K1 项目档案补问题；K2 场景卡“玩家会看到”改为“存在的问题”并注明对应附录 A 编号。**勘误**：原书“开局逻辑每端跑两遍”不准确——关卡蓝图只把 Start_Logic 绑在本地角色的委托上，每端只执行一次；真正的问题是依赖“关卡 BeginPlay 时本地角色已存在”的初始化时序（A#16），已同步修改 K2 场景 02、第 8 章表 8-5、K0 表 K0-2 |
| 2026-10-14 15:41 | 再添加分析，如果不用射线检测，硬用实体子弹应该怎么做？ | 新增**第 5B 章“追问：不用射线检测，硬要用实体子弹，联机该怎么做”**：章首当前做法（原 Demo 就是最朴素的实体子弹）+ 存在的问题；图 5B-1 选方案（命中扫描 / 预测抛射物 / 数据化弹道 / 复制移动）；表 5B-1 实体子弹的五个问题（谁生成、时间怎么对齐、谁判命中、别人怎么看到、什么时候销毁）；**方案 A 预测抛射物**：图 5B-2 一发完整时序（本机假弹 → 服务器校验、快进、生成真弹 → 首包 → 射手隐藏 / 交接 → 命中只认服务器 → ImpactInfo 复制）、图 5B-3 BP_Projectile 网络规格、图 5B-4 武器发射事件图表（快进 = 先射线再在快进点生成）、图 5B-5 子弹命中与隐藏事件图表（假弹在本机也是 Authority，要用 bIsFake）、表 5B-2 四个关键设置、交接还是隐藏；**第 4 节 快进不等于延迟补偿**（图 5B-6/5B-7、表 5B-3/5B-4）；第 5 节远端前推与 PvP 公平性（表 5B-5/5B-6）；**方案 B 数据化弹道**（图 5B-8）；第 7 节带宽（图 5B-9）；表 5B-7 原 Demo 怎么改（当前 / 问题 / 最小修复 / 理想）；八个坑；3 道自测。新增 demo `demos/projectile_demo.cpp`（E8 视觉时序、E9 命中一致性、E10 带宽；输出 run_output_proj.txt / figdata_proj.json，不重跑旧实验）。同步：第 0 章问④、K0 第 3 节与读书地图、K1 统一数字、K2 场景 05、第 4/5 章指向、第 15 章链 2 追问 4、第 16 章 Q58 与新增 Q61~62（共 62 题）、附录 A.3 新增 E8~E10 |

## 目录

{TOC}

"""


def main():
    parts = []
    for f in sorted((ROOT / "chapters").glob("*.html")):
        t = pre(f.read_text(encoding="utf-8"))
        p = P(); p.feed(t)
        parts.append(block(p.root))
    body = re.sub(r"\n{3,}", "\n\n", "".join(parts))
    toc = []
    for m in re.finditer(r"^(#{1,2}) (.+)$", re.sub(r"```[\s\S]*?```", "", body), re.M):
        lvl, title = len(m.group(1)), m.group(2).strip()
        if lvl == 1 and "部分" in title: toc.append(f"- **{title}**")
        elif lvl == 1: toc.append(f"  - {title}")
    md = HEADER.replace("{TOC}", "\n".join(toc)) + body
    left = re.findall(r"<[a-zA-Z/][^>]*>", re.sub(r"`[^`\n]*`", "", re.sub(r"```[\s\S]*?```", "", md)))
    if left: print("[warn] 残留标签：", left[:8])
    if "{{v:" in md or "§§" in md: print("[warn] 未替换的占位符")
    OUT.write_text(md, encoding="utf-8")
    print(f"OK -> {OUT} ({OUT.stat().st_size / 1024:.1f} KB, lines={md.count(chr(10))}, code={md.count('```') // 2})")


if __name__ == "__main__":
    main()
