"""插图 / 交互实验截图校验：python shots.py [图号...] [labs]  -> ../_shots/*.png"""
import pathlib
import re
import shutil
import subprocess
import sys

ROOT = pathlib.Path(__file__).resolve().parent.parent
OUT = ROOT / "_shots"
CHROME = r"C:\Program Files\Google\Chrome\Application\chrome.exe"

html = (ROOT / "Network_TCP_UDP.html").read_text(encoding="utf-8")
head = html.split("<main>")[0]
defs = re.search(r'<svg width="0" height="0"[\s\S]*?</svg>', html).group(0)
tail = html[html.index("<script>"):]
figs = re.findall(r"<figure>[\s\S]*?</figure>", html)
hide = "<style>#side,#menuBtn{display:none}body{margin:0}</style>"

if OUT.exists():
    shutil.rmtree(OUT)
OUT.mkdir()

args = sys.argv[1:]
pages = {}
want = [int(x) for x in args if x.isdigit()] or ([] if "labs" in args else range(len(figs)))
for i in want:
    pages[f"f{i:02d}"] = head + hide + defs + '<div style="width:960px;padding:10px">' + figs[i] + "</div></body></html>"
if "labs" in args:
    labs = re.findall(r'<div class="lab" id="lab-[\s\S]*?<!--/lab-->', html)
    for k, lab in enumerate(labs):
        pages[f"lab{k}"] = head + hide + defs + '<div style="width:900px;padding:10px">' + lab + "</div>" + tail

for name, page in pages.items():
    src = OUT / f"{name}.html"
    src.write_text(page, encoding="utf-8")
    subprocess.run([CHROME, "--headless=new", "--disable-gpu", "--hide-scrollbars",
                    f"--screenshot={OUT / (name + '.png')}", "--window-size=990,720",
                    src.as_uri()], stderr=subprocess.DEVNULL, stdout=subprocess.DEVNULL, timeout=60)
print("figs:", len(figs), "shots:", sorted(p.name for p in OUT.glob("*.png")))
