"""插图 / 交互实验截图校验 -> ../_shots/*.png
python shots.py 3 5 labs        单张截图（图序号从 0 开始）
python shots.py page 12-15      把第 12~15 张图竖着拼成一页截一张长图（省得一张张看）
python shots.py list            列出所有图的序号与图注
"""
import pathlib
import re
import subprocess
import sys

ROOT = pathlib.Path(__file__).resolve().parent.parent
OUT = ROOT / "_shots"
CHROME = r"C:\Program Files\Google\Chrome\Application\chrome.exe"

html = (ROOT / "UE_Coop_Shooter.html").read_text(encoding="utf-8")
head = html.split("<main>")[0]
defs = re.search(r'<svg width="0" height="0"[\s\S]*?</svg>', html).group(0)
tail = html[html.index("<script>"):]
figs = re.findall(r"<figure>[\s\S]*?</figure>", html)
hide = "<style>#side,#menuBtn{display:none}body{margin:0}</style>"
args = sys.argv[1:]

if args and args[0] == "list":
    for i, f in enumerate(figs):
        cap = re.search(r"<figcaption>(.*?)</figcaption>", f, re.S)
        print(i, re.sub(r"<[^>]+>", "", cap.group(1))[:50] if cap else "")
    sys.exit(0)

OUT.mkdir(exist_ok=True)
pages = {}
if args and args[0] == "sect":  # python shots.py sect 名字 "起始文字" "结束文字" [高度]：截正文里一段（表格 / 卡片）
    a, b = html.index(args[2]), html.index(args[3])
    a = html.rindex("<h", 0, a)
    pages["s_" + args[1]] = (head + hide + defs + '<div style="width:960px;padding:10px"><section class="chapter">' + html[a:b] + "</section></div></body></html>", 990, int(args[4]) if len(args) > 4 else 2400)
elif args and args[0] == "page":
    for rg in args[1:]:
        a, _, b = rg.partition("-")
        a, b = int(a), int(b or a)
        body = "".join(figs[i] for i in range(a, b + 1))
        pages[f"p{a:02d}_{b:02d}"] = (head + hide + defs + '<div style="width:960px;padding:10px">' + body + "</div></body></html>", 990, 300 + 760 * (b - a + 1))
else:
    want = [int(x) for x in args if x.isdigit()] or ([] if "labs" in args else range(len(figs)))
    for i in want:
        pages[f"f{i:02d}"] = (head + hide + defs + '<div style="width:960px;padding:10px">' + figs[i] + "</div></body></html>", 990, 900)
    if "labs" in args:
        labs = re.findall(r'<div class="lab[^"]*" id="lab-[\s\S]*?<!--/lab-->', html)
        for k, lab in enumerate(labs):
            pages[f"lab{k}"] = (head + hide + defs + '<div style="width:900px;padding:10px">' + lab + "</div>" + tail, 990, 720)

for name, (page, w, h) in pages.items():
    src = OUT / f"{name}.html"
    src.write_text(page, encoding="utf-8")
    subprocess.run([CHROME, "--headless=new", "--disable-gpu", "--hide-scrollbars",
                    f"--screenshot={OUT / (name + '.png')}", f"--window-size={w},{h}",
                    src.as_uri()], stderr=subprocess.DEVNULL, stdout=subprocess.DEVNULL, timeout=90)
print("figs:", len(figs), "shots:", sorted(pages))
