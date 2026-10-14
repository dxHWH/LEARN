"""解析 SampleScene.unity 里的 Grid / Tilemap / TilemapRenderer / TilemapCollider2D / CompositeCollider2D，
输出 tools/tilemap_dump.json（书中第 18 章的关卡图、碰撞体统计都从这里读）。"""
import glob
import json
import os
import re

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), '..', 'repo', 'Assets'))
scene = open(os.path.join(ROOT, 'Scenes', 'SampleScene.unity'), encoding='utf-8').read()
parts = re.split(r'^--- !u!(\d+) &(-?\d+)( stripped)?\s*$', scene, flags=re.M)
D = {parts[i + 1]: (int(parts[i]), parts[i + 3]) for i in range(1, len(parts), 4)}
names = {k: re.search(r'm_Name: (.*)', b).group(1).strip() for k, (c, b) in D.items() if c == 1}
layers = {k: int(re.search(r'm_Layer: (\d+)', b).group(1)) for k, (c, b) in D.items() if c == 1}
guid = {}
for m in glob.glob(os.path.join(ROOT, '**', '*.meta'), recursive=True):
    g = re.search(r'guid: (\w+)', open(m, encoding='utf-8', errors='ignore').read())
    if g:
        guid[g.group(1)] = m[:-5].replace('\\', '/')


def owner(b):
    m = re.search(r'm_GameObject: \{fileID: (-?\d+)\}', b)
    return m.group(1) if m else None


out = {"tilemaps": {}, "markers": {}}
for k, (c, b) in D.items():
    go = owner(b)
    if c == 156049354:   # Grid
        out["grid"] = {"cell": re.search(r'm_CellSize: (.*)', b).group(1), "layout": int(re.search(r'm_CellLayout: (\d)', b).group(1))}
    if c == 1839735485:  # Tilemap
        sec = b.split('m_Tiles:')[1].split('m_AnimatedTiles:')[0]
        tiles = re.findall(r'first: \{x: (-?\d+), y: (-?\d+), z: (-?\d+)\}[^-]*?m_TileIndex: (\d+)', sec)
        assets = re.findall(r'm_RefCount: (\d+)\s+m_Data: \{fileID: (-?\d+), guid: (\w+), type: 2\}',
                            b.split('m_TileAssetArray:')[1].split('m_TileSpriteArray:')[0])
        sprites = re.findall(r'm_RefCount: (\d+)\s+m_Data: \{fileID: (-?\d+), guid: (\w+), type: 3\}',
                             b.split('m_TileSpriteArray:')[1].split('m_TileMatrixArray:')[0])
        tm = out["tilemaps"].setdefault(names[go], {})
        tm.update(tiles=[[int(t[0]), int(t[1]), int(t[3])] for t in tiles],
                  assets=[os.path.basename(guid.get(a[2], a[2]))[:-6] for a in assets],
                  assetRefs=[int(a[0]) for a in assets], sprites=len(sprites), layer=layers[go])
    if c == 483693784:   # TilemapRenderer
        tm = out["tilemaps"].setdefault(names[go], {})
        tm["renderMode"] = int(re.search(r'm_Mode: (\d)', b).group(1))
        tm["chunk"] = re.search(r'm_ChunkSize: (.*)', b).group(1)
        tm["order"] = int(re.search(r'm_SortingOrder: (-?\d+)', b).group(1))
    if c == 19719996:    # TilemapCollider2D
        tm = out["tilemaps"].setdefault(names[go], {})
        tm["collider"] = {"usedByComposite": int(re.search(r'm_UsedByComposite: (\d)', b).group(1))}
    if c == 66:          # CompositeCollider2D
        tm = out["tilemaps"].setdefault(names[go], {})
        paths = []
        for p in re.split(r'\n    - - ', b.split('m_Paths:')[1].split('m_VertexDistance')[0])[1:]:
            pts = re.findall(r'\{x: (-?[\d.e-]+), y: (-?[\d.e-]+)\}', '- ' + p)
            paths.append([[round(float(x), 3), round(float(y), 3)] for x, y in pts])
        tm["composite"] = {"geometry": int(re.search(r'm_GeometryType: (\d)', b).group(1)),
                           "generation": int(re.search(r'm_GenerationType: (\d)', b).group(1)),
                           "paths": paths}
    if c == 50:          # Rigidbody2D
        if names.get(go) in out["tilemaps"] or names.get(go) == 'Ground':
            out["tilemaps"].setdefault(names[go], {})["bodyType"] = int(re.search(r'm_BodyType: (\d)', b).group(1))
    if c == 4 and names.get(go) in ('player', 'Enenmy_Skeleton'):
        p = re.search(r'm_LocalPosition: \{x: (-?[\d.e-]+), y: (-?[\d.e-]+)', b)
        out["markers"][names[go]] = [float(p.group(1)), float(p.group(2))]
# 瓦片资源的碰撞类型 / 贴图导入设置
tiles_dir = os.path.join(ROOT, 'Graphics', 'Surroundings', 'Medieval_Castle', 'Tiles')
ct = {}
for p in glob.glob(os.path.join(tiles_dir, 'Tile Assets', '*.asset')):
    t = open(p, encoding='utf-8').read()
    k = re.search(r'm_ColliderType: (\d)', t).group(1)
    ct[k] = ct.get(k, 0) + 1
imp = {}
for m in glob.glob(os.path.join(tiles_dir, '*.png.meta')):
    t = open(m, encoding='utf-8').read()
    key = (re.search(r'spritePixelsToUnits: (\S+)', t).group(1), re.search(r'filterMode: (\S+)', t).group(1),
           re.search(r'textureCompression: (\S+)', t).group(1))
    imp['|'.join(key)] = imp.get('|'.join(key), 0) + 1
out["tileAssets"] = {"count": sum(ct.values()), "colliderTypes": ct, "importSettings": imp,
                     "pngCount": len(glob.glob(os.path.join(tiles_dir, '*.png.meta')))}
dst = os.path.join(os.path.dirname(__file__), 'tilemap_dump.json')
json.dump(out, open(dst, 'w', encoding='utf-8'), ensure_ascii=False, indent=1)
for n, tm in out["tilemaps"].items():
    print(n, {k: (v if k != 'tiles' else len(v)) for k, v in tm.items()})
print(out["grid"], out["markers"], out["tileAssets"])
