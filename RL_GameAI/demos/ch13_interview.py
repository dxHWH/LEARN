"""第 13 章手撕代码：Bradley-Terry 奖励模型损失、DPO 损失、GRPO 组内优势、k3 KL 惩罚。
书中代码块与本文件逐字一致；运行时用有限差分检查梯度，并数值验证 KL 正则目标的闭式最优解。纯 Python。
"""
import math
import random


# ---------------------------------------------------------------- 书中代码块（开始）
def log_sigmoid(z):
    return -math.log1p(math.exp(-z)) if z >= 0 else z - math.log1p(math.exp(z))


def bt_loss(r_chosen, r_rejected):
    """奖励模型：−log σ(r_w − r_l)。返回损失和对 (r_w, r_l) 的梯度。"""
    z = r_chosen - r_rejected
    g = -(1.0 - 1.0 / (1.0 + math.exp(-z)))        # d/dz = σ(z) − 1
    return -log_sigmoid(z), g, -g


def dpo_loss(logp_w, logp_l, ref_logp_w, ref_logp_l, beta=0.1):
    """DPO：−log σ(β[(log π_w − log π_ref,w) − (log π_l − log π_ref,l)])。
    logp_* 是整条回答的对数概率（各 token 的 log-prob 之和）。返回损失和对 (logp_w, logp_l) 的梯度。"""
    z = beta * ((logp_w - ref_logp_w) - (logp_l - ref_logp_l))
    s = 1.0 / (1.0 + math.exp(-z))
    return -log_sigmoid(z), -beta * (1 - s), beta * (1 - s)


def grpo_advantages(rewards, eps=1e-8):
    """同一个提示采 G 个回答：优势 = (r − 组均值) / 组标准差。组内全对或全错时优势全为 0。"""
    G = len(rewards)
    mu = sum(rewards) / G
    sd = math.sqrt(sum((r - mu) ** 2 for r in rewards) / G)
    return [(r - mu) / (sd + eps) for r in rewards]


def k3_kl_penalty(logp, ref_logp):
    """GRPO 的 KL 项（每个 token）：π_ref/π − log(π_ref/π) − 1 ≥ 0，按 π 采样时是 KL(π‖π_ref) 的无偏估计。
    返回值和对 logp 的梯度 1 − π_ref/π。"""
    d = ref_logp - logp
    return math.exp(d) - d - 1.0, 1.0 - math.exp(d)
# ---------------------------------------------------------------- 书中代码块（结束）


def main():
    rng = random.Random(13)
    h, worst = 1e-6, 0.0
    for _ in range(300):
        a, b, c, d = (rng.uniform(-5, 5) for _ in range(4))
        L, gw, gl = bt_loss(a, b)
        worst = max(worst, abs((bt_loss(a + h, b)[0] - bt_loss(a - h, b)[0]) / (2 * h) - gw),
                    abs((bt_loss(a, b + h)[0] - bt_loss(a, b - h)[0]) / (2 * h) - gl))
        L, gw, gl = dpo_loss(a, b, c, d, 0.3)
        worst = max(worst, abs((dpo_loss(a + h, b, c, d, 0.3)[0] - dpo_loss(a - h, b, c, d, 0.3)[0]) / (2 * h) - gw),
                    abs((dpo_loss(a, b + h, c, d, 0.3)[0] - dpo_loss(a, b - h, c, d, 0.3)[0]) / (2 * h) - gl))
        v, g = k3_kl_penalty(a / 5, c / 5)
        worst = max(worst, abs((k3_kl_penalty(a / 5 + h, c / 5)[0] - k3_kl_penalty(a / 5 - h, c / 5)[0]) / (2 * h) - g))
    print(f"[1] BT / DPO / k3 的梯度 vs 有限差分（300 组随机输入）：最大差 {worst:.1e}")
    adv = grpo_advantages([1, 0, 0, 1, 1, 0, 0, 0])
    print(f"[2] GRPO：8 个回答中 3 个正确 → 正确的优势 {adv[0]:+.4f}，错误的 {adv[1]:+.4f}；全部正确 → {grpo_advantages([1] * 4)}")
    # 3) 闭式解：π* ∝ π_ref·exp(r/β) 使 E_π[r] − β·KL(π‖π_ref) 最大
    K, beta = 6, 0.5
    r = [rng.gauss(0, 1) for _ in range(K)]
    ref = [rng.random() + 0.1 for _ in range(K)]
    s = sum(ref)
    ref = [x / s for x in ref]
    w = [p * math.exp(x / beta) for p, x in zip(ref, r)]
    Z = sum(w)
    star = [x / Z for x in w]

    def obj(p):
        return sum(pi * ri for pi, ri in zip(p, r)) - beta * sum(pi * math.log(pi / qi) for pi, qi in zip(p, ref))
    best_other = -1e9
    for _ in range(20000):
        z = [math.log(x) + rng.gauss(0, 0.3) for x in star]
        m = max(z)
        e = [math.exp(t - m) for t in z]
        se = sum(e)
        best_other = max(best_other, obj([t / se for t in e]))
    print(f"[3] 闭式 π* 的目标值 {obj(star):.6f}，= β·log Z = {beta * math.log(Z):.6f}；20000 个随机扰动策略的最好目标值 {best_other:.6f}")
    k3_mean = sum(star[i] * k3_kl_penalty(math.log(star[i]), math.log(ref[i]))[0] for i in range(K))
    kl = sum(p * math.log(p / q) for p, q in zip(star, ref))
    print(f"[4] k3 按 π 精确取期望 {k3_mean:.10f}，真实 KL(π‖π_ref) {kl:.10f}")
    ok = worst < 1e-6 and abs(obj(star) - beta * math.log(Z)) < 1e-12 and best_other < obj(star) and abs(k3_mean - kl) < 1e-12
    print("全部通过" if ok else "有不一致！")


if __name__ == "__main__":
    main()
