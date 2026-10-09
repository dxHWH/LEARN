"""把 out/*.ppm 转为 out/png/*.png（纯标准库：zlib + struct）"""
import pathlib, struct, zlib, sys
OUT = pathlib.Path(__file__).resolve().parent / "out"
def read_ppm(p):
    b = p.read_bytes(); parts = []; i = 0
    while len(parts) < 4:
        while b[i:i+1].isspace(): i += 1
        j = i
        while not b[j:j+1].isspace(): j += 1
        parts.append(b[i:j]); i = j
    i += 1
    w, h = int(parts[1]), int(parts[2]); return w, h, b[i:i + w * h * 3]
def write_png(p, w, h, rgb):
    raw = b"".join(b"\x00" + rgb[y * w * 3:(y + 1) * w * 3] for y in range(h))
    def chunk(t, d): return struct.pack(">I", len(d)) + t + d + struct.pack(">I", zlib.crc32(t + d) & 0xffffffff)
    p.write_bytes(b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 2, 0, 0, 0)) + chunk(b"IDAT", zlib.compress(raw, 9)) + chunk(b"IEND", b""))
(OUT / "png").mkdir(exist_ok=True)
for f in sorted(OUT.glob("*.ppm")):
    w, h, d = read_ppm(f); write_png(OUT / "png" / (f.stem + ".png"), w, h, d)
print("converted", len(list(OUT.glob("*.ppm"))))
