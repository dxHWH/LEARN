"""表格型 RL 的确定性计算（纯 Python，无第三方依赖），第 3、4 章的图与数字都由它在构建时算出。
同样的 MDP / 网格在 demos/ch03_mdp.cpp、demos/ch04_dp.cpp 里用 C++ 再实现一遍，build.py 会对拍两边的结果。

约定（与 C++ 完全一致，保证迭代次数也能逐位对上）：
  - MDP.P[s][a] = [(s2, prob), ...]，按“首次出现”的顺序合并重复的后继；R[s][a] = 期望即时奖励
  - 终止状态没有动作，价值恒为 0
  - 同步更新：V_{k+1} 全部由 V_k 算出；原地更新：按状态编号顺序就地覆盖
  - 停止准则：max_s |V_{k+1}(s) - V_k(s)| < tol
"""
import math

# ------------------------------------------------------------------ 线性代数
def solve(A, b):
    n = len(A)
    M = [row[:] + [b[i]] for i, row in enumerate(A)]
    for c in range(n):
        p = max(range(c, n), key=lambda r: abs(M[r][c]))
        M[c], M[p] = M[p], M[c]
        for r in range(n):
            if r != c and M[r][c] != 0:
                f = M[r][c] / M[c][c]
                M[r] = [x - f * y for x, y in zip(M[r], M[c])]
    return [M[i][n] / M[i][i] for i in range(n)]


class MDP:
    def __init__(self, S, A, P, R, gamma, term=()):
        self.S, self.A, self.P, self.R, self.gamma = S, A, P, R, gamma
        self.term = set(term)

    @property
    def n(self):
        return len(self.S)


# ------------------------------------------------------------------ 第 3 章：对线 MDP
LANE_S = ["对线", "团战", "回城"]
LANE_A = [["稳健补刀", "激进压线"], ["后撤拉扯", "强行开团"], ["快速上线", "补满状态"]]
# 两个动作取平均 = 第 0 章样张里的 MRP（均匀随机策略下的 P_π、R_π），所以 V_均匀 与图 0-4 完全相同
LANE_P = [
    [[0.9, 0.1, 0.0], [0.3, 0.5, 0.2]],
    [[0.4, 0.6, 0.0], [0.2, 0.2, 0.6]],
    [[1.0, 0.0, 0.0], [0.8, 0.0, 0.2]],
]
LANE_R = [[1.2, 0.8], [1.5, 2.5], [-1.2, -0.8]]


def lane(gamma=0.9):
    P = [[[(j, p) for j, p in enumerate(row) if p > 0] for row in st] for st in LANE_P]
    return MDP(LANE_S, LANE_A, P, [r[:] for r in LANE_R], gamma)


# ------------------------------------------------------------------ 策略 / 评估 / 控制
def uniform(m):
    return [[1.0 / len(m.A[s])] * len(m.A[s]) if m.A[s] else [] for s in range(m.n)]


def det(m, acts):
    return [[1.0 if a == acts[s] else 0.0 for a in range(len(m.A[s]))] for s in range(m.n)]


def mrp(m, pi):
    n = m.n
    P = [[0.0] * n for _ in range(n)]
    R = [0.0] * n
    for s in range(n):
        for a, pa in enumerate(pi[s]):
            R[s] += pa * m.R[s][a]
            for s2, p in m.P[s][a]:
                P[s][s2] += pa * p
    return P, R


def eval_exact(m, pi):
    P, R = mrp(m, pi)
    n = m.n
    A = [[(1.0 if i == j else 0.0) - m.gamma * P[i][j] for j in range(n)] for i in range(n)]
    return solve(A, R)


def q_sa(m, V, s, a):
    return m.R[s][a] + m.gamma * sum(p * V[s2] for s2, p in m.P[s][a])


def eval_iter(m, pi, tol=1e-10, inplace=False, maxit=10 ** 6, keep=False, V0=None):
    V = list(V0) if V0 else [0.0] * m.n
    hist = [V[:]] if keep else None
    for k in range(1, maxit + 1):
        src = V if inplace else V[:]
        new = V if inplace else [0.0] * m.n
        delta = 0.0
        for s in range(m.n):
            if not m.A[s]:
                new[s] = 0.0
                continue
            v = 0.0
            for a, pa in enumerate(pi[s]):
                if pa:
                    v += pa * (m.R[s][a] + m.gamma * sum(p * src[s2] for s2, p in m.P[s][a]))
            delta = max(delta, abs(v - V[s]) if inplace else abs(v - src[s]))
            new[s] = v
        V = new
        if keep:
            hist.append(V[:])
        if delta < tol:
            return V, k, hist
    return V, maxit, hist


def value_iteration(m, tol=1e-10, inplace=False, maxit=10 ** 6, keep=False):
    V = [0.0] * m.n
    hist = [V[:]] if keep else None
    for k in range(1, maxit + 1):
        src = V if inplace else V[:]
        new = V if inplace else [0.0] * m.n
        delta = 0.0
        for s in range(m.n):
            if not m.A[s]:
                new[s] = 0.0
                continue
            best = -math.inf
            for a in range(len(m.A[s])):
                q = m.R[s][a] + m.gamma * sum(p * src[s2] for s2, p in m.P[s][a])
                if q > best:
                    best = q
            delta = max(delta, abs(best - V[s]) if inplace else abs(best - src[s]))
            new[s] = best
        V = new
        if keep:
            hist.append(V[:])
        if delta < tol:
            return V, k, hist
    return V, maxit, hist


def greedy_sets(m, V, eps=1e-9):
    out = []
    for s in range(m.n):
        if not m.A[s]:
            out.append([])
            continue
        qs = [q_sa(m, V, s, a) for a in range(len(m.A[s]))]
        mx = max(qs)
        out.append([a for a, q in enumerate(qs) if q >= mx - eps])
    return out


def policy_iteration(m, acts0, exact=True, tol=1e-10, eps=1e-9):
    """返回每一轮的 (策略, V_π)。改进时只有“严格更好”才换动作，避免平局来回切换。"""
    acts = list(acts0)
    rounds = []
    while True:
        pi = det(m, acts)
        V = eval_exact(m, pi) if exact else eval_iter(m, pi, tol, inplace=True)[0]
        rounds.append((acts[:], V))
        stable = True
        for s in range(m.n):
            if not m.A[s]:
                continue
            qs = [q_sa(m, V, s, a) for a in range(len(m.A[s]))]
            b = max(range(len(qs)), key=lambda a: qs[a])
            if qs[b] > qs[acts[s]] + eps:
                acts[s] = b
                stable = False
        if stable:
            return rounds
        if len(rounds) > 1000:
            raise RuntimeError("PI 不收敛")


# ------------------------------------------------------------------ 第 4 章：网格世界
DIRS = [(-1, 0, "↑"), (0, 1, "→"), (1, 0, "↓"), (0, -1, "←")]


class Grid:
    """rows：'.' 空地  '#' 墙  'S' 出生点  'G' 目标(终止,+goal)  'X' 陷阱(终止,trap)  'T' 普通终止(0)"""

    def __init__(self, rows, gamma=0.9, slip=0.0, step=-1.0, goal=10.0, trap=-10.0):
        self.rows, self.H, self.W = rows, len(rows), len(rows[0])
        self.cells = [(r, c) for r in range(self.H) for c in range(self.W) if rows[r][c] != "#"]
        self.idx = {rc: i for i, rc in enumerate(self.cells)}
        S, A, P, R, term = [], [], [], [], []
        bonus = {"G": goal, "X": trap}
        for i, (r, c) in enumerate(self.cells):
            ch = rows[r][c]
            S.append(f"{r},{c}")
            if ch in "GXT":
                A.append([]); P.append([]); R.append([]); term.append(i)
                continue
            A.append([d[2] for d in DIRS])
            Ps, Rs = [], []
            for a in range(4):
                outs = [(a, 1.0 - slip)] if slip == 0 else [(a, 1.0 - slip), ((a + 1) % 4, slip / 2), ((a + 3) % 4, slip / 2)]
                acc, er = [], 0.0
                for d, p in outs:
                    j = self.move(r, c, d)
                    er += p * (step + bonus.get(rows[self.cells[j][0]][self.cells[j][1]], 0.0))
                    for e in acc:
                        if e[0] == j:
                            e[1] += p
                            break
                    else:
                        acc.append([j, p])
                Ps.append([(j, p) for j, p in acc])
                Rs.append(er)
            P.append(Ps); R.append(Rs)
        self.mdp = MDP(S, A, P, R, gamma, term)

    def move(self, r, c, d):
        dr, dc, _ = DIRS[d]
        r2, c2 = r + dr, c + dc
        if 0 <= r2 < self.H and 0 <= c2 < self.W and self.rows[r2][c2] != "#":
            return self.idx[(r2, c2)]
        return self.idx[(r, c)]

    def start(self):
        for i, (r, c) in enumerate(self.cells):
            if self.rows[r][c] == "S":
                return i
        return 0


# ------------------------------------------------------------------ 第 3 章汇总（build.py 调用，结果放进 VALS["lane"]）
def lane_all():
    out = {}
    m = lane(0.9)
    u = uniform(m)
    Vu = eval_exact(m, u)
    out["Vu"] = Vu
    Q = [[q_sa(m, Vu, s, a) for a in range(2)] for s in range(3)]
    out["Qu"] = Q
    out["Au"] = [[Q[s][a] - Vu[s] for a in range(2)] for s in range(3)]
    _, k_eval, _ = eval_iter(m, u, 1e-10)
    out["k_eval"] = k_eval
    Vs, k_vi, hist = value_iteration(m, 1e-10, keep=True)
    out["Vstar"] = Vs
    out["k_vi"] = k_vi
    out["Qstar"] = [[q_sa(m, Vs, s, a) for a in range(2)] for s in range(3)]
    gs = greedy_sets(m, Vs)
    out["pistar"] = [g[0] for g in gs]
    out["pistar_name"] = [LANE_A[s][gs[s][0]] for s in range(3)]
    out["vi_err"] = [[k, max(abs(h[i] - Vs[i]) for i in range(3))] for k, h in enumerate(hist[:41])]
    # 8 个确定性策略
    names, V0, V1, V2, best = [], [], [], [], None
    allV = []
    for code in range(8):
        acts = [(code >> 2) & 1, (code >> 1) & 1, code & 1]
        V = eval_exact(m, det(m, acts))
        allV.append(V)
        names.append("·".join(LANE_A[s][acts[s]][:2] for s in range(3)))
        V0.append(V[0]); V1.append(V[1]); V2.append(V[2])
        if acts == out["pistar"]:
            best = code
    dom = all(all(allV[best][s] >= allV[c][s] - 1e-12 for s in range(3)) for c in range(8))
    out["brute"] = {"names": names, "V0": V0, "V1": V1, "V2": V2, "best": best, "dominates": dom}
    # 每个状态单独的最优者
    out["brute"]["argmax"] = [max(range(8), key=lambda c: allV[c][s]) for s in range(3)]
    # γ 对最优策略的影响
    gs_list = []
    for g in [0.3, 0.5, 0.6, 0.7, 0.8, 0.85, 0.9, 0.95, 0.99]:
        mg = lane(g)
        Vg, _, _ = value_iteration(mg, 1e-12)
        act = [greedy_sets(mg, Vg)[s][0] for s in range(3)]
        gs_list.append({"gamma": g, "acts": [LANE_A[s][act[s]] for s in range(3)], "V": Vg})
    out["gamma_sweep"] = gs_list
    # 对“均匀策略的价值”做一次贪心改进 ≠ 最优（说明策略迭代通常要多轮）
    g1 = greedy_sets(m, Vu)
    out["greedy_u"] = [LANE_A[s][g1[s][0]] for s in range(3)]
    out["V_greedy_u"] = eval_exact(m, det(m, [g1[s][0] for s in range(3)]))
    out["pi_rounds"] = [{"acts": [LANE_A[s][a] for s, a in enumerate(acts)], "V": V}
                        for acts, V in policy_iteration(m, [1, 1, 1])]
    out["pi_names"] = [f"π{i}\n" + "·".join(a[:2] for a in r["acts"]) for i, r in enumerate(out["pi_rounds"])]
    out["pi_V"] = [[r["V"][s] for r in out["pi_rounds"]] for s in range(3)]
    out["pi_last"] = len(out["pi_rounds"]) - 1
    out["pi_n"] = len(out["pi_rounds"])
    for row in gs_list:
        row["a0"], row["a1"], row["a2"] = row["acts"]
        row["V0"] = row["V"][0]
    # 截断视界
    Rmax = max(abs(r) for row in LANE_R for r in row)
    out["Rmax"] = Rmax
    out["H"] = math.ceil(math.log(1e-6 * (1 - 0.9) / Rmax) / math.log(0.9))
    return out


DUNGEON = [".........", ".........", ".#XXXXX#.", "S.......G", ".#XXXXX#.", ".........", "........."]
SUTTON = ["T...", "....", "....", "...T"]


def dungeon_all():
    """第 4 章地牢地图（γ=0.95、每步 −0.1、目标 +10、陷阱 −10），与 demos/ch04_dp.cpp 对拍。"""
    out = {}
    for tag, slip in (("s0", 0.0), ("s2", 0.2)):
        gw = Grid(DUNGEON, gamma=0.95, slip=slip, step=-0.1, goal=10, trap=-10)
        m = gw.mdp
        V, k, _ = value_iteration(m, 1e-10)
        Vi, ki, _ = value_iteration(m, 1e-10, inplace=True)
        rounds = policy_iteration(m, [1] * m.n)
        out[tag] = {"Vstart": V[gw.start()], "sweeps": k, "sweeps_inplace": ki, "pi_rounds": len(rounds),
                    "n": m.n}
    gs = Grid(SUTTON, gamma=1.0, slip=0.0, step=-1.0)
    Vx = eval_exact(gs.mdp, uniform(gs.mdp))
    out["sutton_V"] = Vx
    return out


# ------------------------------------------------------------------ 第 5 章：随机游走（与 demos/ch05_random_walk.cpp 逐位一致）
M64 = (1 << 64) - 1


class Rng:
    """xorshift64*，与 C++ 版完全相同（seed → 状态的映射、取 53 位尾数）。"""

    def __init__(self, seed):
        self.s = (seed * 0x9E3779B97F4A7C15 + 1) & M64

    def u(self):
        s = self.s
        s ^= s >> 12
        s ^= (s << 25) & M64
        s ^= s >> 27
        self.s = s
        return (((s * 2685821657736338717) & M64) >> 11) * (1.0 / 9007199254740992.0)


def rw_gen(n, rng, left_r, right_r):
    s = (n + 1) // 2
    st, rw = [s], []
    while s not in (0, n + 1):
        s += -1 if rng.u() < 0.5 else 1
        st.append(s)
        rw.append(left_r if s == 0 else (right_r if s == n + 1 else 0.0))
    return st, rw


def rw_rms(V, tv, n):
    return math.sqrt(sum((V[i] - tv[i]) ** 2 for i in range(1, n + 1)) / n)


def rw_td0(V, ep, a, n):
    st, rw = ep
    for t in range(len(rw)):
        s, s2 = st[t], st[t + 1]
        v2 = 0.0 if s2 in (0, n + 1) else V[s2]
        V[s] += a * (rw[t] + v2 - V[s])


def rw_mc(V, ep, a):
    st, rw = ep
    G, Gs = 0.0, [0.0] * len(rw)
    for t in range(len(rw) - 1, -1, -1):
        G = rw[t] + G
        Gs[t] = G
    for t in range(len(rw)):
        V[st[t]] += a * (Gs[t] - V[st[t]])


def rw_nstep(V, ep, nn, a, n):
    st, rw = ep
    T = len(rw)
    for tau in range(T):
        G = 0.0
        for i in range(tau + 1, min(tau + nn, T) + 1):
            G += rw[i - 1]
        if tau + nn < T:
            G += V[st[tau + nn]]
        V[st[tau]] += a * (G - V[st[tau]])


def rw_td_lambda(V, ep, lam, a, n, online):
    st, rw = ep
    z, dV = [0.0] * (n + 2), [0.0] * (n + 2)
    for t in range(len(rw)):
        s, s2 = st[t], st[t + 1]
        v2 = 0.0 if s2 in (0, n + 1) else V[s2]
        delta = rw[t] + v2 - V[s]
        for i in range(1, n + 1):
            z[i] *= lam
        z[s] += 1.0
        for i in range(1, n + 1):
            if online:
                V[i] += a * delta * z[i]
            else:
                dV[i] += a * delta * z[i]
    if not online:
        for i in range(1, n + 1):
            V[i] += dV[i]


def rw_batch_mc(eps, n, init):
    sm, cnt = [0.0] * (n + 2), [0.0] * (n + 2)
    for st, rw in eps:
        G = 0.0
        for t in range(len(rw) - 1, -1, -1):
            G = rw[t] + G
            sm[st[t]] += G
            cnt[st[t]] += 1
    return [0.0] + [sm[i] / cnt[i] if cnt[i] else init for i in range(1, n + 1)] + [0.0]


def rw_batch_td(eps, n, init):
    """确定性等价：用经验转移频率与平均奖励建模，再精确求解 V = R̂ + P̂V。"""
    C = [[0.0] * (n + 2) for _ in range(n + 2)]
    Rs, cnt = [0.0] * (n + 2), [0.0] * (n + 2)
    for st, rw in eps:
        for t in range(len(rw)):
            s = st[t]
            cnt[s] += 1
            Rs[s] += rw[t]
            C[s][st[t + 1]] += 1
    A, b = [], []
    for i in range(1, n + 1):
        row = [0.0] * n
        row[i - 1] = 1.0
        if cnt[i] == 0:
            A.append(row); b.append(init)
            continue
        for j in range(1, n + 1):
            row[j - 1] -= C[i][j] / cnt[i]
        A.append(row); b.append(Rs[i] / cnt[i])
    return [0.0] + solve(A, b) + [0.0]


def rw_seeds():
    """run 0 的若干学习曲线，build.py 用它和 C++ 输出逐位对拍。"""
    out = {}
    n, tv = 5, [0.0] + [i / 6 for i in range(1, 6)] + [0.0]
    rng = Rng(1)
    eps = [rw_gen(n, rng, 0.0, 1.0) for _ in range(100)]
    for key, fn in (("seed_td", lambda V, e: rw_td0(V, e, 0.1, n)), ("seed_mc", lambda V, e: rw_mc(V, e, 0.02))):
        V = [0.0] + [0.5] * n + [0.0]
        cur = [rw_rms(V, tv, n)]
        for e in eps:
            fn(V, e)
            cur.append(rw_rms(V, tv, n))
        out[key] = cur
    out["batch_td"] = [rw_rms(rw_batch_td(eps[:k], n, 0.5), tv, n) for k in range(1, 31)]
    out["batch_mc"] = [rw_rms(rw_batch_mc(eps[:k], n, 0.5), tv, n) for k in range(1, 31)]
    n, tv = 19, [0.0] + [-1 + 2 * i / 20 for i in range(1, 20)] + [0.0]
    rng = Rng(10001)
    eps = [rw_gen(n, rng, -1.0, 1.0) for _ in range(10)]
    V, V2, a, b = [0.0] * 21, [0.0] * 21, [], []
    for e in eps:
        rw_nstep(V, e, 4, 0.4, n); a.append(rw_rms(V, tv, n))
        rw_td_lambda(V2, e, 0.8, 0.4, n, False); b.append(rw_rms(V2, tv, n))
    out["seed_n4"], out["seed_lam08"] = a, b
    return out


def rw_var(n=5):
    """5 状态随机游走、V 取真值时，MC 目标 G_t 与 TD 目标 R+V(S') 在各状态的方差（γ=1）。"""
    tv = [0.0] + [i / (n + 1) for i in range(1, n + 1)] + [0.0]
    rows = []
    for i in range(1, n + 1):
        p = tv[i]                                   # G_t ∈ {0, 1}，P(G=1) = 真值
        outs = []
        for s2 in (i - 1, i + 1):
            r = 1.0 if s2 == n + 1 else 0.0
            outs.append(r + (0.0 if s2 in (0, n + 1) else tv[s2]))
        m = sum(outs) / 2
        td = sum((o - m) ** 2 for o in outs) / 2
        rows.append({"s": i, "v": p, "mc": p * (1 - p), "td": td, "ratio": p * (1 - p) / td})
    return rows


def ab_example():
    """Sutton 例 6.4：8 个回合 A,0,B,0 / B,1 ×6 / B,0。批量 MC 与批量 TD（确定性等价）给出不同的 V(A)。"""
    eps = [[("A", 0.0), ("B", 0.0)]] + [[("B", 1.0)]] * 6 + [[("B", 0.0)]]
    ret = {"A": [], "B": []}
    for e in eps:
        G = 0.0
        for st, r in reversed(e):
            G += r
            ret[st].append(G)
    mc = {k: sum(v) / len(v) for k, v in ret.items()}
    # 确定性等价模型：A → B 概率 1、奖励 0；B → 终止，平均奖励 = 6/8
    rB = sum(e[-1][1] for e in eps) / len(eps)
    nA = sum(1 for e in eps if e[0][0] == "A")
    td = {"B": rB, "A": 0.0 + rB}
    return {"n": len(eps), "mcA": mc["A"], "mcB": mc["B"], "tdA": td["A"], "tdB": td["B"], "nA": nA}


def trace_demo(visits=(2, 5, 6, 7, 15), T=22, gl=0.8):
    """单个状态的资格迹随时间的变化（累积迹 / 替换迹），画成阶梯折线的点列。"""
    acc, rep, za, zr = [], [], 0.0, 0.0
    for t in range(T + 1):
        if t:
            acc.append([t - 1e-6, za]); rep.append([t - 1e-6, zr])  # 衰减前的值（画竖直跳变）
        za *= gl; zr *= gl
        if t in visits:
            za += 1.0; zr = 1.0
        acc.append([t, za]); rep.append([t, zr])
    return {"acc": acc, "rep": rep, "visits": list(visits), "gl": gl, "acc_max": max(p[1] for p in acc)}


if __name__ == "__main__":
    import json
    print(json.dumps(lane_all(), ensure_ascii=False, indent=1)[:3000])
