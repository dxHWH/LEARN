"""第 6 章手撕代码：ε-greedy、Sarsa / Expected Sarsa / Q-learning 单步更新、Double Q-learning、off-policy 重要性采样比值。
书中代码块与本文件逐字一致；运行时用与 C++ 完全相同的随机数生成器重跑 Cliff Walking 的 run 0（前 100 回合），
与 ch06_output.json 中 C++ 的逐回合奖励逐位对拍。纯 Python，无第三方依赖。
"""
import json
import pathlib

M64 = (1 << 64) - 1


class Rng:
    """xorshift64*，与 demos/rlcommon.h 完全相同。"""
    def __init__(self, seed):
        self.s = (seed * 0x9E3779B97F4A7C15 + 1) & M64

    def u(self):
        s = self.s
        s ^= s >> 12
        s ^= (s << 25) & M64
        s ^= s >> 27
        self.s = s
        return (((s * 2685821657736338717) & M64) >> 11) * (1.0 / 9007199254740992.0)

    def below(self, n):
        k = int(self.u() * n)
        return k if k < n else n - 1


# ---------------------------------------------------------------- 书中代码块（开始）
def egreedy(q, eps, rng):
    """以 ε 的概率均匀随机；否则在并列最大值里均匀随机（并列时固定选第一个会引入系统性偏好）。"""
    if rng.u() < eps:
        return rng.below(len(q))
    m = max(q)
    best = [a for a, v in enumerate(q) if v == m]
    return best[rng.below(len(best))]


def sarsa_update(Q, s, a, r, s2, a2, terminated, alpha, gamma=1.0):
    """on-policy：目标用“下一步实际要执行的动作” a2（由同一个 ε-greedy 选出）。"""
    target = r + (0.0 if terminated else gamma * Q[s2][a2])
    Q[s][a] += alpha * (target - Q[s][a])


def expected_sarsa_target(q_next, eps):
    """对 ε-greedy 策略下的下一动作求期望，消除“下一动作随机”带来的方差。"""
    m = max(q_next)
    k = sum(1 for v in q_next if v == m)
    n = len(q_next)
    return sum((eps / n + ((1 - eps) / k if v == m else 0.0)) * v for v in q_next)


def q_learning_update(Q, s, a, r, s2, terminated, alpha, gamma=1.0):
    """off-policy：目标用 max，评估的是贪心策略，与行为策略无关，所以一步更新不需要重要性采样。"""
    target = r + (0.0 if terminated else gamma * max(Q[s2]))
    Q[s][a] += alpha * (target - Q[s][a])


def double_q_update(QA, QB, s, a, r, s2, terminated, alpha, gamma, rng):
    """随机选一张表更新：用它选 argmax，用另一张表估值。"""
    if rng.u() < 0.5:
        QA, QB = QB, QA
    if terminated:
        boot = 0.0
    else:
        a_star = max(range(len(QA[s2])), key=lambda b: QA[s2][b])
        boot = QB[s2][a_star]
    QA[s][a] += alpha * (r + gamma * boot - QA[s][a])


def is_ratio(pi_probs, b_probs):
    """off-policy MC：回合中从 t 到终止的 ρ = Π π(A_k|S_k) / b(A_k|S_k)。要求 b 覆盖 π（π>0 处 b>0）。"""
    rho = 1.0
    for p, q in zip(pi_probs, b_probs):
        rho *= p / q
    return rho
# ---------------------------------------------------------------- 书中代码块（结束）


# Cliff Walking：4×12，起点左下角，终点右下角，底边中间 10 格是悬崖
H, W = 4, 12
START, GOAL = 3 * W, 3 * W + 11
DR, DC = (-1, 0, 1, 0), (0, 1, 0, -1)


def step(s, a):
    r_, c_ = s // W + DR[a], s % W + DC[a]
    r_, c_ = max(0, min(H - 1, r_)), max(0, min(W - 1, c_))
    s2 = r_ * W + c_
    if r_ == 3 and 1 <= c_ <= 10:
        return START, -100.0
    return s2, -1.0


def run_episode(alg, Q, alpha, eps, rng):
    s, G = START, 0.0
    a = egreedy(Q[s], eps, rng)
    while True:
        s2, r = step(s, a)
        G += r
        term = s2 == GOAL
        if alg == 0:
            a2 = 0 if term else egreedy(Q[s2], eps, rng)
            sarsa_update(Q, s, a, r, s2, a2, term, alpha)
            s, a = s2, a2
        else:
            if alg == 1:
                target = r + (0.0 if term else expected_sarsa_target(Q[s2], eps))
                Q[s][a] += alpha * (target - Q[s][a])
            else:
                q_learning_update(Q, s, a, r, s2, term, alpha)
            s = s2
            if not term:
                a = egreedy(Q[s], eps, rng)
        if term:
            return G


def main():
    out = json.loads((pathlib.Path(__file__).parent / "ch06_output.json").read_text(encoding="utf-8"))["e1"]
    names = ["Sarsa", "Expected Sarsa", "Q-learning"]
    worst = 0.0
    for alg in range(3):
        rng = Rng(1000)
        Q = [[0.0] * 4 for _ in range(H * W)]
        g = [run_episode(alg, Q, 0.5, 0.1, rng) for _ in range(100)]
        cpp = out["runs"][alg]["seed0"]
        d = max(abs(x - y) for x, y in zip(g, cpp))
        worst = max(worst, d)
        print(f"[1] {names[alg]:15s} run 0 前 100 回合奖励：Python vs C++ 最大差 {d:.1e}（第 1 回合 {g[0]:.0f}，第 100 回合 {g[-1]:.0f}）")
    # 2) Expected Sarsa 目标手算：q = [1, 3, 3, 0]，ε = 0.2 → 两个最大值各 (0.8/2 + 0.05)，其余 0.05
    e = expected_sarsa_target([1.0, 3.0, 3.0, 0.0], 0.2)
    print(f"[2] Expected Sarsa 目标 {e:.4f}（= 0.05·1 + 0.45·3 + 0.45·3 + 0.05·0 = 2.75）")
    # 3) 重要性比值：目标策略确定性地选 left（概率 1），行为策略均匀（0.5），3 步 → 8
    print(f"[3] 3 步全选 left 的比值 {is_ratio([1, 1, 1], [0.5, 0.5, 0.5])}；只要有一步选了 π 不会选的动作，比值 = {is_ratio([1, 0], [0.5, 0.5])}")
    ok = worst == 0.0 and abs(e - 2.75) < 1e-12
    print("全部通过" if ok else "有不一致！")


if __name__ == "__main__":
    main()
