"""第 11 章手撕代码：PPO-clip 损失（含梯度）、value clipping、approx KL（k3）、动作掩码的 log-softmax。
书中代码块与本文件逐字一致；运行时用有限差分检查四种情形下的梯度、掩码动作的梯度为 0。纯 Python。
"""
import math
import random


# ---------------------------------------------------------------- 书中代码块（开始）
def masked_log_softmax(logits, mask):
    """mask[k] = 1 表示合法动作。非法动作的 logit 视为 −∞：概率严格为 0，也不参与归一化。"""
    m = max(z for z, ok in zip(logits, mask) if ok)
    s = sum(math.exp(z - m) for z, ok in zip(logits, mask) if ok)
    return [(z - m - math.log(s)) if ok else -math.inf for z, ok in zip(logits, mask)]


def ppo_sample_loss(logits, mask, a, logp_old, adv, eps=0.2):
    """单个样本的 PPO-clip 损失 −min(r·A, clip(r, 1−ε, 1+ε)·A) 及其对 logits 的梯度。"""
    logp = masked_log_softmax(logits, mask)
    r = math.exp(logp[a] - logp_old)
    surr1, surr2 = r * adv, max(1 - eps, min(1 + eps, r)) * adv
    loss = -min(surr1, surr2)
    # 只有 min 取到“未截断”那一项时才有梯度：A>0 且 r<1+ε，或 A<0 且 r>1−ε
    active = (adv >= 0 and r < 1 + eps) or (adv < 0 and r > 1 - eps)
    p = [math.exp(x) if x > -math.inf else 0.0 for x in logp]
    grad = [(-adv * r * ((1.0 if k == a else 0.0) - p[k])) if active else 0.0 for k in range(len(logits))]
    return loss, grad, r


def value_loss_clipped(v_new, v_old, ret, eps=0.2):
    """PPO 常见的 value clipping：新价值离旧价值超过 ε 的部分不再奖励，取两者的较大损失。"""
    v_clip = v_old + max(-eps, min(eps, v_new - v_old))
    return 0.5 * max((v_new - ret) ** 2, (v_clip - ret) ** 2)


def approx_kl_k3(logp_new, logp_old):
    """KL(π_old ‖ π_new) 的 k3 估计：(r − 1) − log r，r = π_new/π_old，按 π_old 采样。无偏且每项 ≥ 0。"""
    log_r = logp_new - logp_old
    return (math.exp(log_r) - 1.0) - log_r
# ---------------------------------------------------------------- 书中代码块（结束）


def main():
    rng = random.Random(11)
    h, worst = 1e-6, 0.0
    cases = {}
    for _ in range(4000):
        K = 5
        logits = [rng.uniform(-2, 2) for _ in range(K)]
        mask = [1, 1, 0, 1, 1]
        a = rng.choice([0, 1, 3, 4])
        logp_cur = masked_log_softmax(logits, mask)[a]
        logp_old = logp_cur - rng.uniform(-0.6, 0.6)          # 让 r 落在区间内外各种位置
        adv = rng.choice([-1, 1]) * rng.uniform(0.1, 2.0)
        loss, g, r = ppo_sample_loss(logits, mask, a, logp_old, adv)
        if abs(r - 1.2) < 1e-4 or abs(r - 0.8) < 1e-4:
            continue                                           # 跳过不可导的折点
        zone = "r∈(1−ε,1+ε)" if 0.8 < r < 1.2 else ("r≥1+ε" if r >= 1.2 else "r≤1−ε")
        key = ("A>0," if adv > 0 else "A<0,") + zone + ("" if any(g) else "→梯度0")
        cases[key] = cases.get(key, 0) + 1
        for k in range(K):
            lp = logits[:]; lp[k] += h
            lm = logits[:]; lm[k] -= h
            num = (ppo_sample_loss(lp, mask, a, logp_old, adv)[0] - ppo_sample_loss(lm, mask, a, logp_old, adv)[0]) / (2 * h)
            worst = max(worst, abs(num - g[k]))
        assert g[2] == 0.0                                     # 掩码动作没有梯度
    print(f"[1] PPO-clip 梯度 vs 有限差分（4000 个随机样本，含掩码动作）：最大差 {worst:.1e}")
    print("    六种情形的样本数：" + "；".join(f"{k} {cases[k]}" for k in sorted(cases)))
    # 2) 掩码：非法动作概率为 0，合法动作概率和为 1
    lp = masked_log_softmax([0.5, 9.0, -1.0], [1, 0, 1])
    probs = [math.exp(x) if x > -math.inf else 0.0 for x in lp]
    print(f"[2] 掩码 softmax：logit 9.0 的非法动作概率 = {probs[1]}，合法动作概率和 = {sum(probs):.12f}")
    # 3) k3 非负、无偏
    pold = [0.5, 0.3, 0.2]
    pnew = [0.4, 0.35, 0.25]
    kl = sum(p * math.log(p / q) for p, q in zip(pold, pnew))
    est = [approx_kl_k3(math.log(pnew[i]), math.log(pold[i])) for i in range(3)]
    mean_k3 = sum(p * e for p, e in zip(pold, est))
    print(f"[3] k3：精确期望 {mean_k3:.10f}，真 KL {kl:.10f}，每项最小值 {min(est):.4f}（≥ 0）")
    # 4) value clipping
    print(f"[4] value clipping：v_old=1.0, ret=2.0, v_new=1.5 → {value_loss_clipped(1.5, 1.0, 2.0):.4f}（取截断版 (1.2−2)²/2 = 0.32）")
    ok = worst < 1e-6 and probs[1] == 0.0 and abs(mean_k3 - kl) < 1e-12 and min(est) >= 0 and abs(value_loss_clipped(1.5, 1.0, 2.0) - 0.32) < 1e-12
    print("全部通过" if ok else "有不一致！")


if __name__ == "__main__":
    main()
