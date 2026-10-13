"""第 8 章手撕代码：sum-tree（PER）、分层采样 + IS 权重、DQN / Double DQN 目标。
书中代码块与本文件逐字一致；运行时与线性扫描、手算结果对拍。纯 Python，无第三方依赖。
"""
import random


# ---------------------------------------------------------------- 书中代码块（开始）
class SumTree:
    """完全二叉树存优先级：t[1] 是根（总和），叶子在 t[n .. 2n-1]。更新、采样都是 O(log N)。"""
    def __init__(self, cap):
        self.n = 1
        while self.n < cap:
            self.n <<= 1
        self.t = [0.0] * (2 * self.n)

    def update(self, i, p):            # 把第 i 条经验的优先级改成 p，沿路径把差值加到所有祖先
        k = i + self.n
        d = p - self.t[k]
        while k >= 1:
            self.t[k] += d
            k >>= 1

    def total(self):
        return self.t[1]

    def find(self, u):                 # u ∈ [0, total)：小于左子树和就往左，否则减掉左子树和往右
        k = 1
        while k < self.n:
            if u < self.t[2 * k]:
                k = 2 * k
            else:
                u -= self.t[2 * k]
                k = 2 * k + 1
        return k - self.n


def per_sample(tree, batch, beta, size, rng):
    """分层采样：把 [0, total) 等分成 batch 段，每段抽一个；返回下标与按最大值归一化的 IS 权重。"""
    seg = tree.total() / batch
    idx, w = [], []
    for j in range(batch):
        i = tree.find(rng.uniform(seg * j, seg * (j + 1)))
        P = tree.t[i + tree.n] / tree.total()
        idx.append(i)
        w.append((size * P) ** (-beta))
    m = max(w)
    return idx, [x / m for x in w]


def dqn_targets(r, q_next_tgt, q_next_online, terminated, gamma, double=True):
    """r / terminated: [B]；q_next_*: [B][A]。截断（truncated）不算 terminated，照常自举。"""
    ys = []
    for b in range(len(r)):
        if double:   # 在线网络选动作，目标网络估值
            a = max(range(len(q_next_online[b])), key=lambda k: q_next_online[b][k])
            boot = q_next_tgt[b][a]
        else:        # 目标网络既选又估：max 带来高估
            boot = max(q_next_tgt[b])
        ys.append(r[b] + gamma * (1.0 - terminated[b]) * boot)
    return ys
# ---------------------------------------------------------------- 书中代码块（结束）


def main():
    rng = random.Random(8)
    # 1) find 与线性前缀扫描一致
    N = 1000
    pr = [rng.random() ** 2 + 1e-3 for _ in range(N)]
    st = SumTree(N)
    for i, p in enumerate(pr):
        st.update(i, p)
    bad = 0
    for _ in range(20000):
        u = rng.random() * st.total()
        c, j = 0.0, 0
        while j < N - 1 and u >= c + pr[j]:
            c += pr[j]
            j += 1
        bad += st.find(u) != j
    print(f"[1] sum-tree find vs 线性扫描：20000 次查询，不一致 {bad} 次；根 = {st.total():.6f}，直接求和 = {sum(pr):.6f}")
    # 2) 改优先级后根仍等于总和
    for _ in range(500):
        i = rng.randrange(N)
        pr[i] = rng.random()
        st.update(i, pr[i])
    print(f"[2] 500 次随机更新后：根 − 直接求和 = {st.total() - sum(pr):.2e}")
    # 3) 分层采样：频率 ≈ P(i)，权重最大为 1
    cnt = [0] * N
    reps, B = 4000, 32
    wmax = 0.0
    for _ in range(reps):
        idx, w = per_sample(st, B, 0.4, N, rng)
        wmax = max(wmax, max(w))
        for i in idx:
            cnt[i] += 1
    tv = 0.5 * sum(abs(cnt[i] / (reps * B) - pr[i] / st.total()) for i in range(N))
    print(f"[3] 分层采样 {reps}×{B} 次：与 P(i) 的总变差距离 {tv:.4f}，归一化权重最大值 {wmax:.3f}")
    # 4) DQN / Double DQN 目标：手算
    r, term, g = [1.0, 1.0, 0.0], [0.0, 1.0, 0.0], 0.9
    qt = [[2.0, 5.0], [3.0, 1.0], [4.0, 0.5]]
    qo = [[3.0, 1.0], [0.0, 9.0], [1.0, 2.0]]
    y1 = dqn_targets(r, qt, qo, term, g, double=False)
    y2 = dqn_targets(r, qt, qo, term, g, double=True)
    exp1 = [1 + 0.9 * 5.0, 1.0, 0.9 * 4.0]
    exp2 = [1 + 0.9 * 2.0, 1.0, 0.9 * 0.5]
    print(f"[4] DQN 目标 {[round(v, 4) for v in y1]}（应为 {exp1}）；Double DQN 目标 {[round(v, 4) for v in y2]}（应为 {[round(v, 4) for v in exp2]}）")
    ok = bad == 0 and abs(st.total() - sum(pr)) < 1e-9 and tv < 0.06 and abs(wmax - 1) < 1e-12 \
        and all(abs(a - b) < 1e-12 for a, b in zip(y1, exp1)) and all(abs(a - b) < 1e-12 for a, b in zip(y2, exp2))
    print("全部通过" if ok else "有不一致！")


if __name__ == "__main__":
    main()
