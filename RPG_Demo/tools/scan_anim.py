"""解析 AnimatorController 状态/转换/参数 + AnimationClip 事件，输出 JSON 与文本摘要。"""
import os, re, glob, json, sys
ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), '..', 'repo', 'Assets'))

def guid_of(meta):
    with open(meta, encoding='utf-8') as f:
        for line in f:
            if line.startswith('guid:'):
                return line.split()[1]

clips = {}
for p in glob.glob(os.path.join(ROOT, 'Animation', '**', '*.anim'), recursive=True):
    g = guid_of(p + '.meta')
    txt = open(p, encoding='utf-8').read()
    name = os.path.basename(p)[:-5]
    stop = re.search(r'm_StopTime: ([\d.e-]+)', txt)
    loop = re.search(r'm_LoopTime: (\d)', txt)
    sr = re.search(r'm_SampleRate: ([\d.]+)', txt)
    evs = []
    m = re.search(r'm_Events:\n((?:  - .*\n(?:    .*\n)*)*)', txt)
    if m:
        for e in re.finditer(r'  - time: ([\d.e-]+)\n    functionName: (\S+)', m.group(1)):
            evs.append((round(float(e.group(1)), 4), e.group(2)))
    clips[g] = dict(name=name, stop=float(stop.group(1)) if stop else None,
                    loop=int(loop.group(1)) if loop else None, events=evs,
                    rate=float(sr.group(1)) if sr else None)

def docs(text):
    parts = re.split(r'^--- !u!(\d+) &(-?\d+)\s*$', text, flags=re.M)
    out = {}
    for i in range(1, len(parts), 3):
        out[parts[i + 1]] = (int(parts[i]), parts[i + 2])
    return out

MODES = {1: 'If', 2: 'IfNot', 3: 'Greater', 4: 'Less', 6: 'Equals', 7: 'NotEqual'}
for p in sorted(glob.glob(os.path.join(ROOT, 'Animation', 'Controller', '*.controller'))):
    D = docs(open(p, encoding='utf-8').read())
    print('=' * 20, os.path.basename(p))
    states = {}
    for fid, (cls, body) in D.items():
        if cls == 1102:
            n = re.search(r'm_Name: (.*)', body).group(1).strip()
            mg = re.search(r'm_Motion: \{fileID: \d+, guid: (\w+)', body)
            spd = re.search(r'm_Speed: ([\d.]+)', body)
            states[fid] = dict(name=n, clip=clips.get(mg.group(1), {}).get('name') if mg else None,
                               trans=re.findall(r'- \{fileID: (-?\d+)\}', body.split('m_Transitions:')[1].split('m_StateMachineBehaviours')[0]) if 'm_Transitions:' in body else [],
                               speed=spd.group(1) if spd else None)
        if cls == 91:
            params = re.findall(r'm_Name: (\S+)\n    m_Type: (\d)', body)
            print('params:', [(a, {'1': 'Float', '3': 'Int', '4': 'Bool', '9': 'Trigger'}[b]) for a, b in params])
    def trans_desc(tid):
        cls, body = D[tid]
        dst = re.search(r'm_DstState: \{fileID: (-?\d+)\}', body).group(1)
        conds = re.findall(r'm_ConditionMode: (\d+)\n\s+m_ConditionEvent: (\S+)\n\s+m_EventTreshold: ([-\d.e]+)', body)
        isexit = re.search(r'm_IsExit: (\d)', body)
        he = re.search(r'm_HasExitTime: (\d)', body).group(1)
        et = re.search(r'm_ExitTime: ([\d.e-]+)', body).group(1)
        dur = re.search(r'm_TransitionDuration: ([\d.e-]+)', body).group(1)
        cs = ' & '.join(f'{c[1]} {MODES.get(int(c[0]), c[0])} {c[2]}' if int(c[0]) > 2 else (c[1] if c[0] == '1' else '!' + c[1]) for c in conds)
        dname = 'EXIT' if (isexit and isexit.group(1)=='1') else states.get(dst, {'name': '?' + dst})['name']
        return f"-> {dname} [{cs or '无条件'}] exitTime={'Y' if he == '1' else 'N'}({et}) dur={dur}"
    for fid, s in states.items():
        c = None
        for g, cl in clips.items():
            if cl['name'] == s['clip']:
                c = cl
        info = f"  clip={s['clip']}" + (f" len={c['stop']:.3f}s loop={c['loop']} events={c['events']}" if c else '')
        print(f"[{s['name']}]{info}")
        for t in s['trans']:
            if t in D:
                print('     ', trans_desc(t))
    # AnyState transitions
    for fid, (cls, body) in D.items():
        if cls == 1107:
            m = re.search(r'm_AnyStateTransitions:\n((?:  - .*\n)*)', body)
            if m:
                for t in re.findall(r'\{fileID: (-?\d+)\}', m.group(1)):
                    print('  [AnyState]', trans_desc(t))
            ent = re.search(r'm_EntryTransitions:\n((?:  - .*\n)*)', body)
            dflt = re.search(r'm_DefaultState: \{fileID: (-?\d+)\}', body)
            if dflt and dflt.group(1) in states:
                print('  [Default]', states[dflt.group(1)]['name'])
            if ent:
                for t in re.findall(r'\{fileID: (-?\d+)\}', ent.group(1)):
                    cls2, b2 = D[t]
                    dst = re.search(r'm_DstState: \{fileID: (-?\d+)\}', b2).group(1)
                    conds = re.findall(r'm_ConditionMode: (\d+)\n\s+m_ConditionEvent: (\S+)\n\s+m_EventTreshold: ([-\d.e]+)', b2)
                    print('  [Entry] ->', states.get(dst, {'name': dst})['name'], conds)
print()
print('==== 所有 clip')
for g, c in sorted(clips.items(), key=lambda kv: kv[1]['name']):
    print(f"{c['name']:30s} len={c['stop']} loop={c['loop']} rate={c['rate']} events={c['events']}")
