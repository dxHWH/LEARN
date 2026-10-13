"""第 7 章手撕代码：增量均值、ε-greedy / UCB1 / Thompson（Beta）选臂、梯度老虎机更新、遗憾计算。
书中代码块与本文件逐字一致；运行时检查：① 梯度老虎机的期望更新 = ∂E[R]/∂H（精确计算）；② 伪遗憾 = Σ Δ_a·N_a；
③ UCB 置信半径由 Hoeffding 不等式反解得到。纯 Python。
"""
import math
import random


# ---------------------------------------------------------------- 书中代码块（开始）
class Bandit:
    def __init__(self, k):
        self.N = [0] * k              # 每个臂被拉的次数
        self.Q = [0.0] * k            # 样本均值
        self.t = 0

    def update(self, a, r):
        self.N[a] += 1
        self.Q[a] += (r - self.Q[a]) / self.N[a]      # 增量均值：新 = 旧 + (目标 − 旧)/n

    def eps_greedy(self, eps, rng):
        if rng.random() < eps:
            return rng.randrange(len(self.Q))
        m = max(self.Q)
        return rng.choice([a for a, q in enumerate(self.Q) if q == m])

    def ucb1(self, c=math.sqrt(2)):
        self.t += 1
        for a, n in enumerate(self.N):                # 先把每个臂各拉一次
            if n == 0:
                return a
        return max(range(len(self.Q)), key=lambda a: self.Q[a] + c * math.sqrt(math.log(self.t) / self.N[a]))


def thompson_beta(succ, fail, rng):
    """伯努利臂：后验 Beta(1+成功, 1+失败)，各采一个样本，选最大的。"""
    return max(range(len(succ)), key=lambda a: rng.betavariate(1 + succ[a], 1 + fail[a]))


def gradient_bandit_step(H, a, r, baseline, alpha):
    """偏好 H → π = softmax(H)；H_k += α (R − 基线)(1[k=a] − π_k)。这是期望奖励的随机梯度上升。"""
    m = max(H)
    e = [math.exp(h - m) for h in H]
    s = sum(e)
    pi = [x / s for x in e]
    return [h + alpha * (r - baseline) * ((1.0 if k == a else 0.0) - pi[k]) for k, h in enumerate(H)], pi


def pseudo_regret(mu, actions):
    """累积（伪）遗憾 = Σ_t (μ* − μ_{a_t}) = Σ_a Δ_a · N_a(T)。"""
    best = max(mu)
    return sum(best - mu[a] for a in actions)
# ---------------------------------------------------------------- 书中代码块（结束）


def main():
    rng = random.Random(7)
    # 1) 梯度老虎机：对固定 H，按 π 和奖励分布精确求期望更新，与 ∂E[R]/∂H 的有限差分比较
    q = [0.2, -0.5, 1.0, 0.3]
    H = [0.1, 0.4, -0.3, 0.0]
    alpha, base = 1.0, 0.7

    def ER(h):
        m = max(h)
        e = [math.exp(x - m) for x in h]
        s = sum(e)
        return sum(x / s * qq for x, qq in zip(e, q))
    _, pi = gradient_bandit_step(H, 0, 0.0, 0.0, 0.0)
    exp_upd = [sum(pi[a] * alpha * (q[a] - base) * ((1.0 if k == a else 0.0) - pi[k]) for a in range(4)) for k in range(4)]
    h = 1e-6
    num = [(ER([x + (h if i == k else 0) for i, x in enumerate(H)]) - ER([x - (h if i == k else 0) for i, x in enumerate(H)])) / (2 * h) for k in range(4)]
    e1 = max(abs(a - b) for a, b in zip(exp_upd, num))
    print(f"[1] 梯度老虎机期望更新 vs ∂E[R]/∂H（基线 = {base}，与基线取值无关）：最大差 {e1:.1e}")
    # 2) 伪遗憾恒等式：一次 UCB1 运行
    mu = [0.7, 0.6, 0.5]
    b = Bandit(3)
    acts = []
    for _ in range(3000):
        a = b.ucb1()
        b.update(a, 1.0 if rng.random() < mu[a] else 0.0)
        acts.append(a)
    R = pseudo_regret(mu, acts)
    R2 = sum((max(mu) - mu[a]) * n for a, n in enumerate(b.N))
    print(f"[2] UCB1 3000 步：伪遗憾 {R:.4f} = Σ Δ_a·N_a = {R2:.4f}；各臂次数 {b.N}")
    # 3) Hoeffding 反解：令 exp(−2 n ε²) = t^−4 → ε = sqrt(2 ln t / n)
    t, n = 1000, 50
    eps = math.sqrt(2 * math.log(t) / n)
    print(f"[3] t = {t}、n = {n}：置信半径 sqrt(2 ln t / n) = {eps:.4f}，对应的 Hoeffding 尾概率 exp(−2nε²) = {math.exp(-2 * n * eps * eps):.3e} = t⁻⁴ = {t ** -4:.3e}")
    # 4) Thompson：一个臂成功率明显高时几乎总被选中
    succ, fail = [80, 30], [20, 70]
    picks = sum(thompson_beta(succ, fail, rng) == 0 for _ in range(2000))
    print(f"[4] Thompson：后验 Beta(81,21) vs Beta(31,71)，2000 次中选第 0 个臂 {picks} 次")
    ok = e1 < 1e-8 and abs(R - R2) < 1e-9 and abs(math.exp(-2 * n * eps * eps) - t ** -4) < 1e-18 and picks == 2000
    print("全部通过" if ok else "有不一致！")


if __name__ == "__main__":
    main()
