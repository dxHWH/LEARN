"""拼装 GAS 电子书：src/chapters/*.html -> ../UE5_GAS_Deep_Dive.html
代码块写法：<!--code:cpp:标题-->  原始代码（无需转义） <!--/code-->
"""
import html
import pathlib
import re
import sys
import xml.etree.ElementTree as ET

ROOT = pathlib.Path(__file__).resolve().parent
OUT = ROOT.parent / "UE5_GAS_Deep_Dive.html"

CODE_RE = re.compile(r"<!--code:(\w+):(.*?)-->\n?(.*?)\n?<!--/code-->", re.S)


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
        parts.append(CODE_RE.sub(code_block, f.read_text(encoding="utf-8")))
    content = "\n".join(parts)

    # 校验：每个内联 SVG 必须是合法 XML
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
    OUT.write_text(shell.replace("{{CONTENT}}", content).replace("{{SCRIPTS}}", ""), encoding="utf-8")
    print(f"OK -> {OUT}  ({OUT.stat().st_size/1024:.1f} KB, svg={len(re.findall('<svg', content))}, bad_svg={bad}, dup_id={len(dup)})")
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
