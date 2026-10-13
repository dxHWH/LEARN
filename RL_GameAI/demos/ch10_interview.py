"""第 10 章手撕代码：GAE（区分终止与截断）、critic 目标、softmax 熵的梯度。
书中代码块与本文件逐字一致；运行时读取 ch10_output.json 里 C++ 的 16 步样例逐项对拍，并做有限差分检查。
"""
import json
import math
import pathlib


# ---------------------------------------------------------------- 书中代码块（开始）
def compute_gae(rews, values, next_values, terminated, truncated, gamma=0.99, lam=0.95):
    """rews / values / next_values / terminated / truncated 都是长度 T 的列表（单个环境、按时间顺序）。
    next_values[t] = V(s_{t+1})；截断时必须是“截断那一刻的观测”的价值，而不是重置后新局第一帧的价值。
    返回 (advantages, returns)，returns = advantages + values 作为 critic 的回归目标（即 λ-回报）。"""
    T = len(rews)
    adv = [0.0] * T
    last = 0.0
    for t in reversed(range(T)):
        not_term = 1.0 - terminated[t]                     # 终止：没有未来价值
        not_end = 1.0 - max(terminated[t], truncated[t])   # 终止或截断：优势递推都在这里切断
        delta = rews[t] + gamma * not_term * next_values[t] - values[t]
        last = delta + gamma * lam * not_end * last
        adv[t] = last
    returns = [a + v for a, v in zip(adv, values)]
    return adv, returns


def entropy_and_grad(logits):
    """softmax 策略的熵 H = −Σ p log p，以及 dH/dlogits_k = −p_k (log p_k + H)。"""
    m = max(logits)
    e = [math.exp(z - m) for z in logits]
    s = sum(e)
    p = [x / s for x in e]
    H = -sum(pi * math.log(pi) for pi in p)
    return H, [-pk * (math.log(pk) + H) for pk in p]
# ---------------------------------------------------------------- 书中代码块（结束）


def main():
    d = json.loads((pathlib.Path(__file__).parent / "ch10_output.json").read_text(encoding="utf-8"))["e1"]
    adv, ret = compute_gae(d["r"], d["v"], d["vn"], d["term"], d["trunc"], d["gamma"], d["lam"])
    e1 = max(abs(a - b) for a, b in zip(adv, d["adv"]))
    print(f"[1] Python GAE vs C++ GAE（16 步样例，γ={d['gamma']}，λ={d['lam']}）：最大差 {e1:.1e}")
    # 截断当终止的错误写法
    bad_term = list(d["term"])
    bad_term[11] = 1
    adv_bad, _ = compute_gae(d["r"], d["v"], d["vn"], bad_term, d["trunc"], d["gamma"], d["lam"])
    print(f"[2] 把 t=11 的截断当成终止：A[11] 从 {adv[11]:.4f} 变成 {adv_bad[11]:.4f}（差值 = γ·V(截断观测) = {d['gamma'] * d['vn'][11]:.4f}）")
    # λ = 1 时 returns = 折扣回报（遇到截断 / rollout 末尾自举）
    _, ret1 = compute_gae(d["r"], d["v"], d["vn"], d["term"], d["trunc"], d["gamma"], 1.0)
    g, brute = 0.0, [0.0] * 16
    for t in reversed(range(16)):
        if d["term"][t]:
            g = d["r"][t]
        elif d["trunc"][t] or t == 15:
            g = d["r"][t] + d["gamma"] * d["vn"][t]
        else:
            g = d["r"][t] + d["gamma"] * g
        brute[t] = g
    e3 = max(abs(a - b) for a, b in zip(ret1, brute))
    print(f"[3] λ=1 时 returns 与按定义倒推的折扣回报：最大差 {e3:.1e}")
    # 熵梯度 vs 有限差分
    z, h = [0.3, -1.1, 2.0, 0.5], 1e-6
    H, gH = entropy_and_grad(z)
    e4 = max(abs((entropy_and_grad([x + (h if i == k else 0) for i, x in enumerate(z)])[0]
                  - entropy_and_grad([x - (h if i == k else 0) for i, x in enumerate(z)])[0]) / (2 * h) - gH[k]) for k in range(4))
    print(f"[4] 熵 H = {H:.4f}，dH/dlogits 与有限差分最大差 {e4:.1e}")
    print("全部通过" if max(e1, e3, e4) < 1e-8 else "有不一致！")


if __name__ == "__main__":
    main()
