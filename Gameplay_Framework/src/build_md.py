"""把电子书章节转换成 Markdown：../GameplayFramework_源码解析与面试指南.md
- 正文、表格、代码、提示框、自测题与电子书一致（同一份 chapters/*.html）
- 数据图（bars / slotmap）用 build.py 里的同一套算法生成表格或字符图，数字一致
- 手绘 SVG 图用下面 ASCII 字典里的字符图替代
- 末尾附 Phase 1 仓库审计报告原文（src/phase1_audit.md）
"""
import html
import json
import math
import pathlib
import re
from html.parser import HTMLParser

import build as B

ROOT = pathlib.Path(__file__).resolve().parent
OUT = ROOT.parent / "GameplayFramework_源码解析与面试指南.md"

ASCII = {
"图 11-1": r"""
L3 业务     [SurvivalGameMode 注册池·预热·SpawnActor] [CombatPawn 死亡→DestroyActor] [SimpleAIController 失去肉体→DestroyActor]
               │SpawnActor                           │DestroyActor                   │DestroyActor
L2 World    [World：唯一的出生/死亡入口 · SpawnActor<T>(key,pos) · DestroyActor(a,delay) · LateUpdate 帧末统一回收] → [TimerSystem：delay>0 时计时器捕获借条]
               │Take / Return（按 key 找池）
L1 ActorPool  [LightPawn]  [HeavyPawn]  [AIController]   闲置挂在 [Pool] 节点；工厂先失活再挂组件；maxIdle 之外直接销毁
               │Acquire / Release(handle)
L0 纯 C#    [ObjectPool<T>：Slot{Item, Generation, InUse}[] + Stack<int> 空闲槽 → PoolHandle(Index, Generation)，generation 从 1 开始]
""",
"图 11-2": r"""
工厂创建（先 SetActive(false) 再挂 AActor）
   ▼
[闲置 Pooled] ──Take：OnTakeFromPool → RegisterAActor──► [待开始 Pending] ──下一次 World.Update：BeginPlay──► [游戏中 Playing]
 SetActive(false)                                         可以 Possess                                       Tick·碰撞·计时器
 不在 World 名单                                                                                                  │ DestroyActor(a)
      ▲                                                                                                           │（delay>0 先经 TimerSystem，捕获借条）
      │ 否：入池，generation++                                                                                    ▼
      └──────────────────────── ◇闲置已满？ ◄──LateUpdate：EndPlay → 注销 → OnReturnToPool → SetActive(false)── [待回收 PendingKill]
                                   │是                                                                         不 Tick、不参与结算
                                   ▼
                              [Destroy 销毁]
""",
"图 11-3": r"""
时间(s)     0      0.5           1.0      1.5                         3.5
池中对象   [玩家肉体X (0,1)][尸体   ][闲置][红队 AI 肉体 Y (0,2) 正在场上 ………………]
             t=0.5 抛弃 X，安排 3 秒后回收（借条 (0,1)）
                            t=1.0 X 被提前回收：generation 1→2
                                     t=1.5 刷怪：LIFO 拿回同一个对象
朴素：捕获对象引用 → 到期检查“对象活跃吗？”= 是 → 回收了 Y ✘
新设计：捕获借条 (0,1) → 到期检查 IsAlive((0,1)) = 否 → 忽略 ✔
""",
"图 1-1": r"""
┌ GameInstance · DontDestroyOnLoad（只在 MainMenuScene；不持有 World）──────────────────────────┐
└──────────────────────────────────────────────────────────────────────────────────────────────┘
┌ 每个场景各一套（切场景即销毁）· 四个独立 Update，顺序未定义 ─────────────────────────────────┐
│  [World 懒创建] [TimerSystem] [EventSystem] ──事件──► [UI 普通 MonoBehaviour]                    │
│       │                ▲ UI 每帧 GetTimeRemaining(handle) ───────────────┘                       │
│       ▼ pending → BeginPlay → 倒序 Tick                                                          │
│  ┌ AActor（都是 MonoBehaviour 组件）──────────────────────────────────────────────┐           │
│  │ [GameMode] ──生成+担保──► [GameState] ──Trigger──► EventSystem                    │           │
│  │     ▲                          ▲                    [Controller]                  │           │
│  │     │ EndMatch（Pawn 直接调用） │ AddScore（直接写）    ▲ │ 双向引用              │           │
│  │     └───────────────────────────┴─────────────────── [Pawn]                       │           │
│  └──────────────────────────────────────────────────────────────────────────────────┘           │
└──────────────────────────────────────────────────────────────────────────────────────────────┘
""",
"图 9-1": r"""
外部持有 Handle(1,0)
   │ ① _generations[1] == 0 ?        稀疏侧（按槽位号）  下标:   0   1   2   3
   ├────────────────────────────►     _generations              0  [0]  0   0
   │ ② _handleToIndex[1] = 1          _handleToIndex            0  [1]  2   3
   └────────────────────────────►     _freeIndices             （空）
                                          │ ③
                                          ▼
                                     紧凑侧（按 dense 下标，每帧 0..count-1 遍历）
                                      _timers                   A  [B]  C   D
                                      _indexToHandle            0  [1]  2   3
不变式：h2i[i2h[k]] == k（k < count）；i2h[h2i[s]] == s（s 在用）
""",
}


# ---------------------------------------------------------------- 数据宏 → Markdown
def md_bars(d):
    unit = d.get("unit", "")
    rows = [f"| {it[0]} | {it[3] if len(it) > 3 else f'{it[1]:g} {unit}'.strip()} |" for it in d["items"]]
    return f"**{d.get('title', '')}**\n\n| 项 | 值 |\n|---|---|\n" + "\n".join(rows)


def md_slotmap(d):
    steps = B.slotmap_run(d)
    out = ["```text"]
    for k in d.get("show", list(range(len(steps)))):
        desc, s = steps[k]
        cell = lambda v, dead: (f"({v})" if dead else f" {v} ").center(5)
        n = max(len(s["gen"]), len(s["dense"]))
        out.append(f"第 {k + 1} 步  {desc}")
        out.append("  下标             " + "".join(str(i).center(5) for i in range(n)))
        out.append("  dense _timers    " + "".join(cell(s["dense"][i] if i < len(s["dense"]) else "", i >= s["count"]) for i in range(n)) + f"   _timerCount={s['count']}")
        out.append("  _indexToHandle   " + "".join(cell(s["i2h"][i], i >= s["count"]) for i in range(len(s["i2h"]))))
        out.append("  _handleToIndex   " + "".join(cell(s["h2i"][i], i in s["free"]) for i in range(len(s["h2i"]))))
        out.append("  _generations     " + "".join(cell(v, False) for v in s["gen"]))
        out.append("  _freeIndices     " + (" ← ".join(map(str, s["free"])) or "（空）"))
        out.append("")
    out.append("（括号）= 逻辑上无效、但内存里还留着的值")
    out.append("```")
    return "\n".join(out)


MD_MACRO = {"bars": md_bars, "slotmap": md_slotmap}

LAB_NAMES = {"slotlab": "Slot Map：新建 / 停止 / 旧句柄 / 默认句柄（可切换 generation 从 1 开始）", "chaselab": "卡顿一帧之后 interval 与 loop 计时器各触发几次（可调 interval、卡顿时长、追赶模式、maximumDeltaTime）"}


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


def inline(n, code=False):
    if isinstance(n, str):
        t = re.sub(r"\s+", " ", n)
        return t if code else t.replace("<", "&lt;").replace(">", "&gt;")
    t = "".join(inline(k, code or n.tag == "code") for k in n.kids)
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
        PH.append(f"*{title}*\n\n```{ {'cpp': 'cpp', 'cs': 'csharp'}.get(lang, 'text') }\n{body}\n```"); return f"\n§§{len(PH) - 1}§§\n"
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
        PH.append(f"> 🧪 **交互实验（仅电子书）**：{LAB_NAMES.get(k, '')}——在 `Gameplay_Framework.html` 中可操作。"); return f"\n§§{len(PH) - 1}§§\n"
    t = re.sub(r'<div class="lab ([^"]*)" id="[^"]*"></div><!--/lab-->', lab, t)
    return t


HEADER = """# 《从 Unity Gameplay Framework 到游戏客户端面试》—— 源码解析 + 面试追问

> 目标仓库：<https://github.com/dxHWH/MyGamePlayArchitecture>　源码快照：`repo/`（commit `427d7c1`，2026-03-11）
> 配套电子书：`Gameplay_Framework.html`（含 2 个交互实验）/ `Gameplay_Framework.pdf`；实测：`demos/audit_verify.cpp` → `demos/run_output.txt`（E1~E14），`demos/infra_verify.cpp` → `demos/infra_output.txt`（G1~G10：事件派发 / 环形队列 / 锁与无锁 / false sharing / 对象池 / 日志 IO），`demos/pool_design_verify.cpp` → `demos/pool_design_output.txt`（H1~H5：对象池落地设计），`demos/timer_bench.cpp` → `demos/bench_output.txt`（B1~B2），`demos/framework_verify.cpp` → `demos/framework_output.txt`（F1~F5：World 注册复杂度 / Tick 中修改列表 10 种情况 / Possess 不变式 / 双 GameMode / 世界时间漂移；Unity 生命周期规则按 R1~R4 建模）。
> 本文档与电子书由同一份源文件（`src/chapters/*.html`）生成；数据图在构建时用逐行复刻源码的算法计算。文末附 Phase 1 仓库审计报告原文。

## 提问记录

| 时间 | 原始提问（摘要） | 回答对应章节 |
|---|---|---|
| 2026-10-12 15:25 | 基于 Git 仓库源码，为 Gameplay Framework 项目制作「源码解析 + 面试追问」电子书；本次只执行 **Phase 1：Repository Audit**（Fact Sheet / 源码地图 / 真实架构图 / 10 个主题 / 缺陷与债务 / 20 个攻击问题 / 重排目录） | 文末附录「Phase 1 仓库审计报告」 |
| 2026-10-12 15:55 | 1. 基本认可这个目录，强化面试可能会问到的问题，并适当发散衍生。你还需要介绍为了测试这个框架，我们写的一个简单的 SoulDecay 的 Demo　2. 可以不用给出代码 | 第 0 章（读前须知 + **全书面试地图**：每章高频问题 / 发散方向 / 为什么会追过去）· 第 1 章（项目全景、两个 Demo 的验证分工、30 秒/1 分钟/3 分钟介绍）· 第 9 章（TimerSystem 结构：Slot Map、generation、四个句柄漏洞、cache 实测）· 第 10 章（Tick 两阶段、重入、掉帧语义、scaled/unscaled、float 精度、GC 审计、Code Review）· **第 15 章（SoulDecay Demo 完整拆解）**；每章含四层答案、追问树、错误回答、发散题；按要求不给修复代码 |
| 2026-10-12 21:10 | 继续未完成的任务（Phase 3 第二批：核心框架章节） | **第 2 章**（Unity 生命周期 → Gameplay 生命周期、BeginPlay 至少晚一帧、先 Possess 后 BeginPlay、Destroy 延迟、无 EndPlay、与 UE 对照）· **第 3 章**（World 统一 Tick、倒序遍历 10 种情况实测、IsTickEnabled 脱钩、O(N²) 注册、顺序与 TickGroup）· **第 4 章**（Controller/Pawn 分离、Possess 五步与回调顺序、双向引用不变式 6 种情况实测、fake null 与 is）· **第 5 章**（GameMode/GameState 分工、Authority 只校验注册、双 GameMode 实测、ServerWorldTimeSeconds 漂移、联网延伸）· **第 6 章**（MonoSingleton、漏调 base.Awake、Awake/OnEnable 跨对象顺序隐患、Domain Reload、替代方案与 UE Subsystem）；沿用四层答案 / 追问树 / 错误回答 / 发散，不给修复代码 |
| 2026-10-12 21:59 | 继续未完成的任务（Phase 3 第三批：基础设施章节） | **第 7 章**（EventSystem：双缓冲延迟派发、立即 vs 延迟、四个隐藏行为——运行时类型分发 / 快照语义 / 多播异常三层影响 / 事件对象延迟读取、Priority 与饥饿、溢出策略表、闭包分配（无监听者也分配）、ThreadSafeEventSystem、与 UE 委托对比）· **第 8 章**（RingBuffer：满空三种判别、出队清槽漏掉含引用结构体、取模 vs 位与实测、与 Queue/链表对比、零测试覆盖）· **第 11 章**（ObjectPool：未启用的事实、LIFO/扩容/容量 0、借条无 generation 的三类问题、池化收益与代价、与 UnityEngine.Pool 对比、接入生命周期冲突的根因）· **第 12 章**（ThreadSafeRingBuffer：lock-based ≠ lock-free、contention 实测、SPSC 无锁要点与内存序、MPSC/MPMC 难点、false sharing 实测、压力测试三缺陷、Unity 工作线程边界）· **第 13 章**（Log：[Conditional] vs #if、宏只对 Standalone 定义、缓冲落盘收益与崩溃丢失、启动删除旧日志、线上排查）；新增实测 G1~G10（`demos/infra_verify.cpp` → `infra_output.txt`）；不给修复代码 |
| 2026-10-13 15:00 | 进一步，请你分析如果要实现代码中我是先到一半的对象池，具体应该怎么设计，怎么配这个框架最合适，如何在 demo 中使用，图文并茂，并配合原来的代码讲清楚这一部分 | **第 11B 章 · 对象池落地设计**：半成品四个缺口（对照原 SurvivalGameMode / GameObjectWrapper / ObjectPool 代码）· 池放在哪一层（三方案对比，推荐 World 拥有机制、GameMode 配置策略）· 四层结构图 · L0 带 generation 的 ObjectPool 骨架与逐行对比 · Actor 池化生命周期（EndPlay / OnReturnToPool / OnTakeFromPool / IsPendingKill，状态机图）· World.SpawnActor / DestroyActor 与帧末回收 · 延迟回收的 ABA（时间线图）· ActorPool · SoulDecay 逐文件改动表 · 夺舍 / 死亡调用链 · 收益模拟 · 替代方案 · 四步迁移 · 测试清单 · UE 对照 · 追问树；新增实测 H1~H5（`demos/pool_design_verify.cpp` → `pool_design_output.txt`）；本章按问题需要给出 C# 关键代码骨架（未在 Unity 中编译） |

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
        if lvl == 1 and ("部分" in title or title.startswith("附录")): toc.append(f"- **{title}**")
        elif lvl == 1: toc.append(f"  - {title}")
    audit = (ROOT / "phase1_audit.md").read_text(encoding="utf-8")
    toc.append("- **附录 · Phase 1 仓库审计报告（原文）**")
    md = HEADER.replace("{TOC}", "\n".join(toc)) + body + "\n---\n\n# 附录 · Phase 1 仓库审计报告（原文）\n\n" + audit
    left = re.findall(r"<[a-zA-Z/][^>]*>", re.sub(r"```[\s\S]*?```|`[^`\n]*`|<https?://[^>]+>", "", md))
    if left: print("[warn] 残留标签：", left[:8], [md[max(0, md.find(x) - 40):md.find(x) + 10] for x in left[:3]])
    OUT.write_text(md, encoding="utf-8")
    print(f"OK -> {OUT} ({OUT.stat().st_size / 1024:.1f} KB, lines={md.count(chr(10))}, code={md.count('```') // 2})")


if __name__ == "__main__":
    main()
