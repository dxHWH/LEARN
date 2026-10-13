"""第 12 章手撕代码：tanh 压缩高斯的采样与 log-prob、TD3 / SAC 的目标值、软更新、温度 α 的损失。
书中代码块与本文件逐字一致；运行时做数值检查（密度积分为 1、目标值手算、α 梯度方向）。纯 Python。
"""
import math
import random


# ---------------------------------------------------------------- 书中代码块（开始）
def squashed_gaussian_sample(mu, log_std, rng):
    """重参数化：u = μ + σ·ε，a = tanh(u)。返回动作和 log π(a)（含 tanh 雅可比修正）。"""
    std = math.exp(log_std)
    eps = rng.gauss(0.0, 1.0)
    u = mu + std * eps
    a = math.tanh(u)
    logp_u = -0.5 * eps * eps - log_std - 0.5 * math.log(2 * math.pi)
    # 数值稳定写法：log(1 − tanh²u) = 2·(log 2 − u − softplus(−2u))
    log_det = 2.0 * (math.log(2.0) - u - math.log1p(math.exp(-2.0 * u)))
    return a, logp_u - log_det


def td3_target(r, terminated, q1_next, q2_next, gamma=0.99):
    """截断双 Q：取两个目标 critic 的较小值（q*_next 已在“加噪声的目标动作”上求值）。"""
    return r + gamma * (1.0 - terminated) * min(q1_next, q2_next)


def sac_target(r, terminated, q1_next, q2_next, logp_next, alpha, gamma=0.99):
    """soft Bellman 目标：r + γ(1−d)·[min Q(s′,a′) − α·log π(a′|s′)]，a′ 从当前策略采样。"""
    return r + gamma * (1.0 - terminated) * (min(q1_next, q2_next) - alpha * logp_next)


def soft_update(target, online, tau=0.005):
    return [t + tau * (o - t) for t, o in zip(target, online)]


def alpha_loss_and_grad(log_alpha, logps, target_entropy):
    """J(α) = E[−α·(log π + H̄)]，对 log α 做梯度下降：熵低于目标时 α 增大，高于目标时 α 减小。"""
    alpha = math.exp(log_alpha)
    m = sum(lp + target_entropy for lp in logps) / len(logps)
    return -alpha * m, -alpha * m          # dJ/dlog α = −α·m
# ---------------------------------------------------------------- 书中代码块（结束）


def main():
    rng = random.Random(12)
    mu, log_std = 0.4, math.log(0.9)
    # 1) log π(a) 在 (−1, 1) 上积分为 1（用解析式按 a 积分）
    std = math.exp(log_std)
    n, integ = 200000, 0.0
    for k in range(n):
        a = -1 + (k + 0.5) * 2 / n
        u = math.atanh(a)
        integ += math.exp(-0.5 * ((u - mu) / std) ** 2 - log_std - 0.5 * math.log(2 * math.pi) - math.log(1 - a * a)) * 2 / n
    # 2) 采样函数返回的 log π 与直接公式一致
    worst = 0.0
    for _ in range(1000):
        a, lp = squashed_gaussian_sample(mu, log_std, rng)
        u = math.atanh(a)
        direct = -0.5 * ((u - mu) / std) ** 2 - log_std - 0.5 * math.log(2 * math.pi) - math.log(1 - a * a)
        if abs(a) < 0.999999:
            worst = max(worst, abs(direct - lp))
    print(f"[1] tanh 高斯密度在 (−1,1) 上的积分 = {integ:.6f}；采样函数的 log π 与直接公式最大差 {worst:.1e}")
    # 3) 目标值手算
    y1 = td3_target(1.0, 0.0, 5.0, 4.0, 0.9)
    y2 = sac_target(1.0, 0.0, 5.0, 4.0, -1.5, 0.2, 0.9)
    y3 = sac_target(1.0, 1.0, 5.0, 4.0, -1.5, 0.2, 0.9)
    print(f"[2] TD3 目标 {y1:.4f}（= 1 + 0.9·4）；SAC 目标 {y2:.4f}（= 1 + 0.9·(4 + 0.2·1.5)）；终止时 SAC 目标 {y3:.4f}")
    # 4) 软更新与 α 梯度方向
    tgt = soft_update([0.0, 1.0], [1.0, 1.0], 0.1)
    _, g_low = alpha_loss_and_grad(math.log(0.2), [0.5, 0.7], -1.0)     # 熵估计 −mean(log π) = −0.6，高于目标 −1
    _, g_high = alpha_loss_and_grad(math.log(0.2), [1.5, 1.3], -1.0)    # 熵 ≈ −1.4 低于目标 −1
    print(f"[3] 软更新 τ=0.1：{tgt}；熵高于目标时 dJ/dlogα = {g_low:+.3f}（下降 → α 变小），熵低于目标时 = {g_high:+.3f}（α 变大）")
    ok = abs(integ - 1) < 1e-4 and worst < 1e-9 and abs(y1 - 4.6) < 1e-12 and abs(y2 - 4.87) < 1e-12 and y3 == 1.0 and g_low > 0 > g_high
    print("全部通过" if ok else "有不一致！")


if __name__ == "__main__":
    main()
