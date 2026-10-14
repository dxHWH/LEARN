"""扫描 RPGDemo：脚本 guid -> 场景 / Prefab 引用次数，并导出场景里每个 MonoBehaviour 的序列化字段。"""
import os, re, sys, json, glob
ROOT = os.path.join(os.path.dirname(__file__), '..', 'repo', 'Assets')
ROOT = os.path.abspath(ROOT)

def guid_of(meta):
    with open(meta, encoding='utf-8') as f:
        for line in f:
            if line.startswith('guid:'):
                return line.split()[1]

scripts = {}
for meta in glob.glob(os.path.join(ROOT, 'Script', '**', '*.cs.meta'), recursive=True):
    scripts[guid_of(meta)] = os.path.basename(meta)[:-8]

def load(p):
    with open(p, encoding='utf-8', errors='replace') as f:
        return f.read()

scene = load(os.path.join(ROOT, 'Scenes', 'SampleScene.unity'))
prefabs = {os.path.basename(p)[:-7]: load(p) for p in glob.glob(os.path.join(ROOT, 'Prefabs', '*.prefab'))}

mode = sys.argv[1] if len(sys.argv) > 1 else 'refs'

if mode == 'refs':
    for g, n in sorted(scripts.items(), key=lambda kv: kv[1]):
        sc = scene.count(g)
        pf = [k for k, v in prefabs.items() if g in v]
        print(f'{n:34s} scene={sc:2d} prefabs={pf}')

# 解析 YAML 文档块
def docs(text):
    parts = re.split(r'^--- !u!(\d+) &(-?\d+)(?: stripped)?\s*$', text, flags=re.M)
    out = {}
    for i in range(1, len(parts), 3):
        out[parts[i + 1]] = (int(parts[i]), parts[i + 2])
    return out

if mode in ('mono', 'monoprefab'):
    src = scene if mode == 'mono' else prefabs[sys.argv[2]]
    D = docs(src)
    # GameObject 名
    names = {}
    for fid, (cls, body) in D.items():
        if cls == 1:
            m = re.search(r'm_Name: (.*)', body)
            names[fid] = m.group(1).strip() if m else '?'
    # Transform 父子
    want = sys.argv[3] if len(sys.argv) > 3 else (sys.argv[2] if mode == 'mono' and len(sys.argv) > 2 else None)
    for fid, (cls, body) in D.items():
        if cls != 114:
            continue
        m = re.search(r'm_Script: \{fileID: 11500000, guid: (\w+)', body)
        if not m:
            continue
        sn = scripts.get(m.group(1), m.group(1)[:8])
        go = re.search(r'm_GameObject: \{fileID: (-?\d+)\}', body).group(1)
        if want and want not in sn:
            continue
        print(f'==== [{sn}] on GameObject "{names.get(go, go)}" (fid {fid})')
        lines = body.split('\n')
        keep = []
        started = False
        for ln in lines:
            if ln.startswith('  m_EditorClassIdentifier'):
                started = True
                continue
            if started:
                keep.append(ln)
        print('\n'.join(keep).rstrip())

if mode == 'gos':
    D = docs(scene)
    for fid, (cls, body) in D.items():
        if cls == 1:
            m = re.search(r'm_Name: (.*)', body)
            act = re.search(r'm_IsActive: (\d)', body)
            comps = re.findall(r'component: \{fileID: (-?\d+)\}', body)
            kinds = []
            for c in comps:
                if c in D:
                    k, b = D[c]
                    if k == 114:
                        g = re.search(r'guid: (\w+)', b)
                        kinds.append(scripts.get(g.group(1), 'MB:' + g.group(1)[:6]) if g else 'MB')
                    else:
                        kinds.append(str(k))
            print(f'{m.group(1).strip() if m else "?":30s} active={act.group(1) if act else "?"} {kinds}')
