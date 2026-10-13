"""第 18 章实测：游戏 Bot 的多头 / 自回归动作头 + 动作掩码，以及决策频率与等效折扣。
  E1 动作空间：类型头（移动 / 普攻 / 技能1 / 技能2 / 回城），目标头（3 个敌方单位，仅普攻和技能1 需要，按类型条件化），
     方向头（8 方向，仅移动和技能2 需要）。各头有状态相关的掩码（技能冷却、目标不在射程内）。
     验证：① 联合 log-prob = 用到的各头 log-prob 之和；② 联合熵 = H(类型) + Σ p(类型)·H(子头 | 类型)；
           ③ 20 万次采样的频率与联合概率一致，非法动作从未被采到；④ PPO 风格损失对 logits 的梯度与有限差分一致。
  E2 决策频率：30 FPS 游戏、每 k 帧决策一次时，要保持“约 T 秒的视界”需要的 γ，以及固定 γ = 0.99 时视界随 k 的变化。
书中代码块与本文件逐字一致。输出：stdout + ch18_output.json（build.py 自动读取）。纯 Python。
"""
import json
import math
import pathlib
import random


# ---------------------------------------------------------------- 书中代码块（开始）
NEG = -1e9   # 代替 −∞，避免 NaN；半精度推理时要换成该精度能表示的大负数


def masked_softmax(logits, mask):
    z = [x if m else NEG for x, m in zip(logits, mask)]
    mx = max(z)
    e = [math.exp(x - mx) if m else 0.0 for x, m in zip(z, mask)]
    s = sum(e)
    return [x / s for x in e]


TYPES = ["移动", "普攻", "技能1", "技能2", "回城"]
NEED_TARGET = {1, 2}        # 普攻、技能1 需要选目标
NEED_DIR = {0, 3}           # 移动、技能2 需要选方向（技能2 是指向性技能）


def action_dist(net_out, obs_mask):
    """net_out: {'type': [5], 'target': {t: [3]}, 'dir': {t: [8]}}，目标 / 方向头按类型条件化（自回归）。
    obs_mask: {'type': [5], 'target': {t: [3]}}，来自游戏状态（冷却、射程、视野）。返回各头的分布。"""
    pt = masked_softmax(net_out["type"], obs_mask["type"])
    ptar = {t: masked_softmax(net_out["target"][t], obs_mask["target"][t]) for t in NEED_TARGET}
    pdir = {t: masked_softmax(net_out["dir"][t], [1] * 8) for t in NEED_DIR}
    return pt, ptar, pdir


def joint_logp(dists, action):
    """action = (类型, 目标或 None, 方向或 None)。只把“这个类型用到的头”的 log-prob 加起来。"""
    pt, ptar, pdir = dists
    t, tar, d = action
    lp = math.log(pt[t])
    if t in NEED_TARGET:
        lp += math.log(ptar[t][tar])
    if t in NEED_DIR:
        lp += math.log(pdir[t][d])
    return lp


def joint_entropy(dists):
    """链式法则：H(T, X) = H(T) + Σ_t p(t)·H(X | T = t)。没用到的头不计入（它们不影响动作）。"""
    pt, ptar, pdir = dists
    H = lambda p: -sum(x * math.log(x) for x in p if x > 0)
    h = H(pt)
    for t, p in enumerate(pt):
        if t in NEED_TARGET:
            h += p * H(ptar[t])
        if t in NEED_DIR:
            h += p * H(pdir[t])
    return h


def sample_action(dists, rng):
    pt, ptar, pdir = dists
    t = rng.choices(range(len(pt)), weights=pt)[0]
    tar = rng.choices(range(3), weights=ptar[t])[0] if t in NEED_TARGET else None
    d = rng.choices(range(8), weights=pdir[t])[0] if t in NEED_DIR else None
    return t, tar, d


def gamma_for_horizon(seconds, fps=30, frame_skip=4):
    """每 frame_skip 帧决策一次，希望有效视界约 seconds 秒：1/(1−γ) = seconds·fps/frame_skip。"""
    steps = seconds * fps / frame_skip
    return 1.0 - 1.0 / steps
# ---------------------------------------------------------------- 书中代码块（结束）


def enumerate_joint(dists):
    pt, ptar, pdir = dists
    out = {}
    for t, p in enumerate(pt):
        if p == 0:
            continue
        tars = range(3) if t in NEED_TARGET else [None]
        dirs = range(8) if t in NEED_DIR else [None]
        for a in tars:
            for d in dirs:
                q = p * (ptar[t][a] if a is not None else 1) * (pdir[t][d] if d is not None else 1)
                if q > 0:
                    out[(t, a, d)] = q
    return out


def main():
    rng = random.Random(18)
    net = {"type": [rng.gauss(0, 1) for _ in range(5)],
           "target": {t: [rng.gauss(0, 1) for _ in range(3)] for t in NEED_TARGET},
           "dir": {t: [rng.gauss(0, 1) for _ in range(8)] for t in NEED_DIR}}
    mask = {"type": [1, 1, 1, 0, 1],                       # 技能2 冷却中
            "target": {1: [1, 1, 0], 2: [1, 0, 0]}}         # 敌人 3 不在普攻射程，技能1 只够得到敌人 1
    d = action_dist(net, mask)
    J = enumerate_joint(d)
    total = sum(J.values())
    # ① log-prob
    e1 = max(abs(math.log(q) - joint_logp(d, a)) for a, q in J.items())
    # ② 熵
    hj = -sum(q * math.log(q) for q in J.values())
    e2 = abs(hj - joint_entropy(d))
    # ③ 采样
    M = 200000
    cnt = {}
    illegal = 0
    for _ in range(M):
        a = sample_action(d, rng)
        cnt[a] = cnt.get(a, 0) + 1
        if a not in J:
            illegal += 1
    tv = 0.5 * sum(abs(cnt.get(a, 0) / M - q) for a, q in J.items())
    # ④ 梯度：L = −logπ(a)·A − β·H，对类型头 logits 的有限差分
    act, A, beta, h = (1, 1, None), 0.7, 0.01, 1e-6

    def loss(nt):
        dd = action_dist(dict(net, type=nt), mask)
        return -joint_logp(dd, act) * A - beta * joint_entropy(dd)
    pt = d[0]
    Ht, Hcond = joint_entropy(d), 0.0
    # 解析梯度：−A·(1[k=a]−p_k) − β·∂H/∂z_k，其中 H = H(T) + Σ p_t·H_t，∂H/∂z_k = −p_k(log p_k + H(T)) + p_k(H_k − Σ p_t H_t)
    Hsub = [0.0] * 5
    for t in range(5):
        if t in NEED_TARGET:
            Hsub[t] += -sum(x * math.log(x) for x in d[1][t] if x > 0)
        if t in NEED_DIR:
            Hsub[t] += -sum(x * math.log(x) for x in d[2][t] if x > 0)
    HT = -sum(x * math.log(x) for x in pt if x > 0)
    mean_sub = sum(p * hs for p, hs in zip(pt, Hsub))
    grad = []
    for k in range(5):
        if pt[k] == 0:
            grad.append(0.0)
            continue
        dH = -pt[k] * (math.log(pt[k]) + HT) + pt[k] * (Hsub[k] - mean_sub)
        grad.append(-A * ((1.0 if k == act[0] else 0.0) - pt[k]) - beta * dH)
    e4 = 0.0
    for k in range(5):
        zp = list(net["type"]); zp[k] += h
        zm = list(net["type"]); zm[k] -= h
        e4 = max(e4, abs((loss(zp) - loss(zm)) / (2 * h) - grad[k]))
    print(f"[E1] 合法联合动作 {len(J)} 个（不做分解时笛卡尔积为 5×3×8 = 120），概率和 {total:.12f}")
    print(f"     ① 联合 log-prob 与“用到的头相加”最大差 {e1:.1e}；② 联合熵 {hj:.6f}，链式分解 {joint_entropy(d):.6f}，差 {e2:.1e}")
    print(f"     ③ {M} 次采样：与联合概率的总变差距离 {tv:.4f}，非法动作被采到 {illegal} 次；④ 类型头梯度与有限差分最大差 {e4:.1e}（被掩码的技能2 梯度 = {grad[3]}）")
    # E2：决策频率与 γ
    ks = [1, 2, 4, 8, 15, 30]
    rows = []
    for k in ks:
        rows.append({"k": k, "hz": 30 / k, "g10": gamma_for_horizon(10, 30, k), "g30": gamma_for_horizon(30, 30, k),
                     "h099": k / 30 / (1 - 0.99), "g_equiv": 0.999 ** k})
    for r in rows:
        print(f"[E2] 每 {r['k']:2d} 帧决策（{r['hz']:.1f} 次/秒）：视界 10 秒需 γ = {r['g10']:.4f}，30 秒需 γ = {r['g30']:.4f}；"
              f"固定 γ = 0.99 时视界 {r['h099']:.1f} 秒；逐帧 γ = 0.999 等效于每步 γ^k = {r['g_equiv']:.4f}")
    ok = e1 < 1e-12 and e2 < 1e-12 and illegal == 0 and tv < 0.01 and e4 < 1e-6 and grad[3] == 0.0
    print("全部通过" if ok else "有不一致！")
    probs = {"type": d[0], "t1": d[1][1], "t2": d[1][2]}
    out = {"n_joint": len(J), "e1": e1, "e2": e2, "tv": tv, "illegal": illegal, "e4": e4, "H": hj, "M": M,
           "ptype": d[0], "types": TYPES, "rows": rows, "probs": probs}
    (pathlib.Path(__file__).parent / "ch18_output.json").write_text(json.dumps(out, ensure_ascii=False, indent=1), encoding="utf-8")


if __name__ == "__main__":
    main()
