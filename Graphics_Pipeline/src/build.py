"""拼装《图形学渲染管线》电子书：src/chapters/*.html -> ../Graphics_Pipeline.html
代码块：<!--code:cpp:标题-->  原始代码（无需转义） <!--/code-->
渲染图：<!--img:文件名:标签;文件名:标签|图注-->   （图片来自 ../demos/out/png/*.png，base64 内嵌，单文件离线可读）
"""
import base64
import html
import pathlib
import re
import sys
import xml.etree.ElementTree as ET

ROOT = pathlib.Path(__file__).resolve().parent
OUT = ROOT.parent / "Graphics_Pipeline.html"
PNG = ROOT.parent / "demos" / "out" / "png"

CODE_RE = re.compile(r"<!--code:(\w+):(.*?)-->\n?(.*?)\n?<!--/code-->", re.S)
IMG_RE = re.compile(r"<!--img:(.*?)\|(.*?)-->", re.S)
missing = []


def code_block(m):
    lang, title, body = m.group(1), m.group(2).strip(), m.group(3).strip("\n")
    return ('<div class="code"><div class="bar"><span>%s</span><button>复制</button></div>'
            '<pre data-lang="%s">%s</pre></div>' % (html.escape(title or lang), lang, html.escape(body)))


def img_block(m):
    cells = []
    for item in m.group(1).split(";"):
        name, _, label = item.strip().partition(":")
        f = PNG / (name + ".png")
        if not f.exists():
            missing.append(name); continue
        b64 = base64.b64encode(f.read_bytes()).decode()
        cells.append('<div><img alt="%s" src="data:image/png;base64,%s"><span>%s</span></div>' % (html.escape(name), b64, label))
    return '<figure class="shots"><div class="row">%s</div><figcaption>%s</figcaption></figure>' % ("".join(cells), m.group(2))


def main():
    shell = (ROOT / "shell.html").read_text(encoding="utf-8")
    parts = []
    for f in sorted((ROOT / "chapters").glob("*.html")):
        t = f.read_text(encoding="utf-8")
        t = CODE_RE.sub(code_block, t)
        t = IMG_RE.sub(img_block, t)
        parts.append(t)
    content = "\n".join(parts)
    bad = 0
    for i, svg in enumerate(re.findall(r"<svg[\s\S]*?</svg>", content)):
        try:
            ET.fromstring(svg)
        except ET.ParseError as e:
            bad += 1
            print(f"[SVG#{i}] 解析失败: {e}\n{svg[:200]}...", file=sys.stderr)
    ids = re.findall(r'id="([^"]+)"', content)
    dup = {x for x in ids if ids.count(x) > 1}
    if dup:
        print("重复 id:", dup, file=sys.stderr)
    if missing:
        print("缺少图片:", missing, file=sys.stderr)
    labs = (ROOT / "labs.js").read_text(encoding="utf-8") if (ROOT / "labs.js").exists() else ""
    OUT.write_text(shell.replace("{{CONTENT}}", content).replace("{{SCRIPTS}}", labs), encoding="utf-8")
    print(f"OK -> {OUT}  ({OUT.stat().st_size/1024:.1f} KB, svg={len(re.findall('<svg', content))}, bad_svg={bad}, "
          f"imgs={content.count('data:image/png')}, dup_id={len(dup)}, missing_img={len(missing)})")
    return 1 if bad or missing else 0


if __name__ == "__main__":
    sys.exit(main())
