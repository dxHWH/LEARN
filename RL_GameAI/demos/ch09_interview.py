"""第 9 章手撕代码：reward-to-go、REINFORCE 对 logits 的梯度（含基线）、高斯策略的 score function。
书中代码块与本文件逐字一致；运行时用有限差分对拍。纯 Python，无第三方依赖。
"""
import math
import random


# ---------------------------------------------------------------- 书中代码块（开始）
def reward_to_go(rews, gamma):
    """G_t = r_t + γ G_{t+1}，倒序一遍 O(T)。"""
    out, g = [0.0] * len(rews), 0.0
    for t in reversed(range(len(rews))):
        g = rews[t] + gamma * g
        out[t] = g
    return out


def softmax(z):
    m = max(z)
    e = [math.exp(x - m) for x in z]
    s = sum(e)
    return [x / s for x in e]


def reinforce_logit_grads(logits, actions, rews, gamma, baseline=None):
    """损失 L = −Σ_t log π(a_t|s_t)·ψ_t，ψ_t = G_t − b(s_t)。返回 dL/dlogits[t][k] = (π_k − 1[k=a_t])·ψ_t。"""
    G = reward_to_go(rews, gamma)
    grads = []
    for t, (z, a) in enumerate(zip(logits, actions)):
        psi = G[t] - (baseline[t] if baseline is not None else 0.0)
        p = softmax(z)
        grads.append([(p[k] - (1.0 if k == a else 0.0)) * psi for k in range(len(z))])
    return grads


def gaussian_logp_and_grads(a, mu, log_std):
    """一维高斯策略：log π(a) 以及对 μ、log σ 的导数（score function）。"""
    std = math.exp(log_std)
    z = (a - mu) / std
    logp = -0.5 * z * z - log_std - 0.5 * math.log(2 * math.pi)
    d_mu = z / std                 # (a − μ) / σ²
    d_log_std = z * z - 1.0        # 对 log σ 求导：((a − μ)² − σ²) / σ²
    return logp, d_mu, d_log_std
# ---------------------------------------------------------------- 书中代码块（结束）


def main():
    rng = random.Random(9)
    T, K, gamma = 12, 4, 0.97
    logits = [[rng.uniform(-2, 2) for _ in range(K)] for _ in range(T)]
    actions = [rng.randrange(K) for _ in range(T)]
    rews = [rng.uniform(-1, 1) for _ in range(T)]
    base = [rng.uniform(-1, 1) for _ in range(T)]
    # 1) reward-to-go vs 暴力定义
    G = reward_to_go(rews, gamma)
    brute = [sum(gamma ** (k - t) * rews[k] for k in range(t, T)) for t in range(T)]
    e1 = max(abs(a - b) for a, b in zip(G, brute))
    # 2) logits 梯度 vs 有限差分（ψ 视为常数，正是策略梯度“替代损失”的写法）
    psi = [G[t] - base[t] for t in range(T)]

    def loss(lg):
        return -sum(math.log(softmax(lg[t])[actions[t]]) * psi[t] for t in range(T))
    g = reinforce_logit_grads(logits, actions, rews, gamma, base)
    h, e2 = 1e-6, 0.0
    for t in range(T):
        for k in range(K):
            lp = [row[:] for row in logits]; lp[t][k] += h
            lm = [row[:] for row in logits]; lm[t][k] -= h
            e2 = max(e2, abs((loss(lp) - loss(lm)) / (2 * h) - g[t][k]))
    # 3) 高斯 score vs 有限差分
    e3 = 0.0
    for _ in range(50):
        a, mu, ls = rng.uniform(-2, 2), rng.uniform(-1, 1), rng.uniform(-1, 0.5)
        lp, dmu, dls = gaussian_logp_and_grads(a, mu, ls)
        n_mu = (gaussian_logp_and_grads(a, mu + h, ls)[0] - gaussian_logp_and_grads(a, mu - h, ls)[0]) / (2 * h)
        n_ls = (gaussian_logp_and_grads(a, mu, ls + h)[0] - gaussian_logp_and_grads(a, mu, ls - h)[0]) / (2 * h)
        e3 = max(e3, abs(n_mu - dmu), abs(n_ls - dls))
    # 4) score 的期望为 0：E_{a~π}[∇ log π] = 0（基线无偏的核心）
    p = softmax(logits[0])
    e4 = max(abs(sum(p[a] * ((1.0 if k == a else 0.0) - p[k]) for a in range(K))) for k in range(K))
    print(f"[1] reward-to-go vs 定义：最大差 {e1:.2e}")
    print(f"[2] REINFORCE logits 梯度 vs 有限差分：最大差 {e2:.2e}")
    print(f"[3] 高斯策略 score（μ、log σ）vs 有限差分：最大差 {e3:.2e}")
    print(f"[4] softmax 策略 E_a[∇ log π(a)] 各分量最大绝对值：{e4:.2e}")
    print("全部通过" if max(e1, e2, e3, e4) < 1e-6 else "有不一致！")


if __name__ == "__main__":
    main()
