"""拼装《从 Unity Gameplay Framework 到游戏客户端面试》电子书：src/chapters/*.html -> ../Gameplay_Framework.html

章节里可用的“宏”（构建时展开）：
  <!--code:cs|cpp|text:标题-->  原始代码（无需转义） <!--/code-->
  <!--bars {json} -->      横向柱状图（实测数据）
  <!--slotmap {json} -->   在构建时用 Python 逐行复刻 TimerSystem.CreateTimer / StopTimer，渲染每一步后 5 个数组的真实状态
所有宏都会被包进 <figure>，json 里的 "cap" 作为图注。
"""
import html
import json
import pathlib
import re
import sys
import xml.etree.ElementTree as ET
from collections import deque

ROOT = pathlib.Path(__file__).resolve().parent
OUT = ROOT.parent / "Gameplay_Framework.html"

CODE_RE = re.compile(r"<!--code:(\w+):(.*?)-->\n?(.*?)\n?<!--/code-->", re.S)
MACRO_RE = re.compile(r"<!--(bars|slotmap)\s+(\{.*?\})\s*-->", re.S)

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


# ============================================================ slotmap：TimerSystem 的 Python 复刻（与 TimerSystem.cs 逐行一致）
class TS:
    def __init__(self, gen0=0):
        self.dense = []            # 模拟 _timers 数组（长度 = 曾经达到的最大容量，尾部保留残留值）
        self.count = 0             # _timerCount
        self.h2i, self.i2h, self.gen = [], [], []
        self.free = deque()        # Queue<int>：FIFO
        self.gen0 = gen0

    def valid(self, h):
        i, g = h
        return 0 <= i < len(self.gen) and self.gen[i] == g

    def create(self, name):
        from_free = bool(self.free)
        if self.free:
            hi = self.free.popleft()
        else:
            hi = len(self.h2i)
            self.h2i.append(-1); self.i2h.append(-1); self.gen.append(self.gen0)
        h = (hi, self.gen[hi])
        real = self.count
        if real < len(self.dense): self.dense[real] = name
        else: self.dense.append(name)
        self.count += 1
        self.h2i[hi] = real
        if real >= len(self.i2h): self.i2h.append(hi)
        else: self.i2h[real] = hi
        return h, f"{name} 放进 dense[{real}]，槽位 {hi}{'（从 free list 取出，代数沿用 ' + str(h[1]) + '）' if from_free else '（新开槽位，代数 ' + str(h[1]) + '）'}"

    def stop(self, h):
        if not self.valid(h):
            return False, "IsHandleValid=false → 直接 return"
        hi = h[0]; real = self.h2i[hi]; last = self.count - 1
        victim = self.dense[real]
        msg = f"删除 dense[{real}]={victim}；"
        if real != last:
            self.dense[real] = self.dense[last]
            lh = self.i2h[last]
            self.i2h[real] = lh; self.h2i[lh] = real
            msg += f"把末尾 {self.dense[last]}（dense[{last}]）搬到 dense[{real}]，改它的两条映射"
        else:
            msg += "它就在末尾，只需 count−1"
        self.count -= 1
        self.gen[hi] += 1
        self.free.append(hi)
        return True, msg + f"；generation[{hi}]→{self.gen[hi]}，槽位 {hi} 入 free list"

    def snap(self):
        return dict(dense=list(self.dense), count=self.count, h2i=list(self.h2i), i2h=list(self.i2h),
                    gen=list(self.gen), free=list(self.free))


def slotmap_run(d):
    ts = TS(d.get("gen0", 0))
    handles = {}          # name -> 最近一次发出的句柄
    first = {}            # name -> 第一次发出的句柄（用来演示旧句柄）
    steps = []
    for op in d["ops"]:
        kind = op[0]
        if kind == "c":
            h, msg = ts.create(op[1]); handles[op[1]] = h; first.setdefault(op[1], h)
            desc = f"Create {op[1]} → 句柄 ({h[0]},{h[1]})：{msg}"
        elif kind == "s":
            h = handles[op[1]]; ok, msg = ts.stop(h)
            desc = f"Stop {op[1]}({h[0]},{h[1]})：{msg}"
        elif kind == "old":
            h = first[op[1]]
            desc = f"用 {op[1]} 的旧句柄 ({h[0]},{h[1]}) 访问：IsHandleValid={'true（危险）' if ts.valid(h) else 'false，安全拒绝'}"
        elif kind == "h":
            h = (op[1], op[2]); ok, msg = ts.stop(h)
            desc = f"StopTimer(({h[0]},{h[1]}))（{op[3] if len(op) > 3 else '手工构造的句柄'}）：{msg}"
        elif kind == "def":
            h = (0, 0)
            desc = f"default(TimerHandle)=(0,0)：IsHandleValid={'true → 会命中 dense[' + str(ts.h2i[0]) + ']=' + ts.dense[ts.h2i[0]] if ts.valid(h) else 'false'}"
        else:
            raise SystemExit("slotmap: 未知操作 " + str(op))
        steps.append((desc, ts.snap()))
    return steps


def _diff(prev, cur, key, i):
    if prev is None: return False
    a, b = prev[key], cur[key]
    return i >= len(a) or a[i] != b[i]


def slotmap(d):
    steps = slotmap_run(d)
    show = d.get("show", list(range(len(steps))))
    W = 860; L = 150; cw = 50; ch = 26; out = []; y = 6
    for k in show:
        desc, s = steps[k]
        prev = steps[k - 1][1] if k > 0 else None
        out.append(f'<text x="8" y="{y + 15}" class="b sm" style="fill:#1E3A8A">{esc(f"第 {k + 1} 步　" + desc)}</text>')
        y += 24
        slots = len(s["gen"])
        rows = [("dense _timers", "dense", max(slots, len(s["dense"])), lambda i: i >= s["count"]),
                ("_indexToHandle", "i2h", slots, lambda i: i >= s["count"]),
                ("_handleToIndex", "h2i", slots, lambda i: i in s["free"]),
                ("_generations", "gen", slots, lambda i: False)]
        for lab, key, n, dead in rows:
            out.append(f'<text x="{L - 10}" y="{y + 17}" text-anchor="end" class="sm mono">{esc(lab)}</text>')
            arr = s[key]
            for i in range(n):
                v = arr[i] if i < len(arr) else ""
                x = L + i * cw
                isdead = dead(i)
                ch_ = _diff(prev, s, key, i) and not isdead
                fill, stroke = ("#F3F4F6", "#D1D5DB") if isdead else (("#FFEDD5", "#C2410C") if ch_ else ("#FFFFFF", "#94A3B8"))
                out.append(f'<rect x="{x + 1}" y="{y}" width="{cw - 2}" height="{ch}" rx="4" fill="{fill}" stroke="{stroke}" stroke-width="{1.6 if ch_ else 1}"/>')
                tc = "#9CA3AF" if isdead else "#111827"
                out.append(f'<text x="{x + cw / 2}" y="{y + 18}" text-anchor="middle" class="mono b" style="fill:{tc};font-size:13px">{esc(v)}</text>')
            if key == "dense":
                out.append(f'<text x="{L + n * cw + 12}" y="{y + 17}" class="xs">_timerCount = {s["count"]}（灰色 = count 之外的残留数据）</text>')
            if key == "h2i":
                out.append(f'<text x="{L + n * cw + 12}" y="{y + 17}" class="xs">灰色 = 空闲槽的残留映射（未清 −1）</text>')
            y += ch + 6
        out.append(f'<text x="{L - 10}" y="{y + 17}" text-anchor="end" class="sm mono">_freeIndices</text>')
        if s["free"]:
            for i, v in enumerate(s["free"]):
                x = L + i * cw
                out.append(f'<rect x="{x + 1}" y="{y}" width="{cw - 2}" height="{ch}" rx="13" fill="#EAF7EF" stroke="#15803D"/>')
                out.append(f'<text x="{x + cw / 2}" y="{y + 18}" text-anchor="middle" class="mono b" style="font-size:13px">{v}</text>')
            out.append(f'<text x="{L + len(s["free"]) * cw + 12}" y="{y + 17}" class="xs">← 队头先出（FIFO）</text>')
        else:
            out.append(f'<text x="{L}" y="{y + 17}" class="xs">（空）</text>')
        y += ch + 6
        out.append(f'<text x="{L - 10}" y="{y + 10}" text-anchor="end" class="xs">下标</text>')
        for i in range(max(len(s["gen"]), len(s["dense"]))):
            out.append(f'<text x="{L + i * cw + cw / 2}" y="{y + 10}" text-anchor="middle" class="xs mono">{i}</text>')
        y += 26
    out.append(f'<rect x="{L}" y="{y}" width="14" height="14" rx="3" fill="#FFEDD5" stroke="#C2410C"/><text x="{L + 20}" y="{y + 12}" class="xs">本步被修改的格子</text>')
    out.append(f'<rect x="{L + 150}" y="{y}" width="14" height="14" rx="3" fill="#F3F4F6" stroke="#D1D5DB"/><text x="{L + 170}" y="{y + 12}" class="xs">逻辑上无效、但内存里还留着的值</text>')
    y += 22
    svg = f'<svg viewBox="0 0 {W} {y}" xmlns="http://www.w3.org/2000/svg">' + "".join(out) + "</svg>"
    return fig(svg, d.get("cap"))


def macro(m: re.Match) -> str:
    kind, body = m.group(1), m.group(2)
    try:
        d = json.loads(body)
    except json.JSONDecodeError as e:
        raise SystemExit(f"[{kind}] JSON 错误: {e}\n{body[:200]}")
    return {"bars": bars, "slotmap": slotmap}[kind](d)


def code_block(m: re.Match) -> str:
    lang, title, body = m.group(1), m.group(2).strip(), m.group(3)
    body = body.strip("\n")
    return (
        '<div class="code"><div class="bar"><span>%s</span><button>复制</button></div>'
        '<pre data-lang="%s">%s</pre></div>' % (html.escape(title or lang), "cpp" if lang == "cs" else lang, html.escape(body))
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
    left = re.findall(r"<!--(bars|slotmap|code)", content)
    if left:
        print("未展开的宏:", left, file=sys.stderr)
    scripts = (ROOT / "labs.js").read_text(encoding="utf-8") if (ROOT / "labs.js").exists() else ""
    OUT.write_text(shell.replace("{{CONTENT}}", content).replace("{{SCRIPTS}}", scripts), encoding="utf-8")
    print(f"OK -> {OUT}  ({OUT.stat().st_size / 1024:.1f} KB, figure={content.count('<figure>')}, "
          f"svg={len(re.findall('<svg', content))}, bad_svg={bad}, dup_id={len(dup)}, lab={content.count('class=\"lab')})")
    return 1 if bad or left else 0


if __name__ == "__main__":
    sys.exit(main())
