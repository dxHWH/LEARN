"""第 19 章手撕代码：V-trace 目标（倒序递推）、IMPALA 的策略梯度优势、learner 饱和时的策略滞后估算（Little 定律）。
书中代码块与本文件逐字一致；运行时检查：① 递推 = 按定义求和；② ρ = c = 1 时退化为 n 步自举回报；
③ 用 ch19_output.json 中 C++ 实测的滞后与 Little 定律估算对比。纯 Python。
"""
import json
import math
import pathlib
import random


# ---------------------------------------------------------------- 书中代码块（开始）
def vtrace(rewards, values, bootstrap, log_rhos, discounts, rho_bar=1.0, c_bar=1.0):
    """一段长度 T 的 rollout（行为策略 μ 采样）。
    values[t] = V(x_t)，bootstrap = V(x_T)；log_rhos[t] = log π(a_t|x_t) − log μ(a_t|x_t)；
    discounts[t] = γ·(1 − terminated_t)。返回 (vs, pg_adv)：
      vs[t]     = V(x_t) + Σ_{k≥t} γ^{k−t} (Π_{i=t}^{k−1} c_i) ρ_k δ_k   —— critic 的回归目标
      pg_adv[t] = ρ_t (r_t + γ vs[t+1] − V(x_t))                      —— 策略梯度的优势"""
    T = len(rewards)
    rhos = [min(rho_bar, math.exp(l)) for l in log_rhos]
    cs = [min(c_bar, math.exp(l)) for l in log_rhos]
    next_v = values[1:] + [bootstrap]
    vs_minus_v = [0.0] * T
    acc = 0.0
    for t in reversed(range(T)):                      # 倒序：acc = (vs − V)_{t+1}
        delta = rhos[t] * (rewards[t] + discounts[t] * next_v[t] - values[t])
        acc = delta + discounts[t] * cs[t] * acc
        vs_minus_v[t] = acc
    vs = [v + d for v, d in zip(values, vs_minus_v)]
    vs_next = vs[1:] + [bootstrap]
    pg_adv = [rhos[t] * (rewards[t] + discounts[t] * vs_next[t] - values[t]) for t in range(T)]
    return vs, pg_adv


def lag_when_learner_bound(queue_cap, n_actors, rollouts_per_update):
    """learner 是瓶颈时队列总是满的：在系统中的 rollout 数 ≈ 队列容量 + 正在生产的 n_actors 段，
    每次更新消耗 rollouts_per_update 段。Little 定律：平均等待（以“更新次数”计）≈ 在系统中的数量 / 消耗速率。"""
    return (queue_cap + n_actors) / rollouts_per_update
# ---------------------------------------------------------------- 书中代码块（结束）


def main():
    rng = random.Random(19)
    T, g = 12, 0.95
    r = [rng.uniform(-1, 1) for _ in range(T)]
    V = [rng.uniform(-1, 1) for _ in range(T)]
    boot = rng.uniform(-1, 1)
    lr = [rng.uniform(-1.5, 1.5) for _ in range(T)]
    disc = [g] * T
    disc[6] = 0.0                                          # 第 6 步终止
    vs, adv = vtrace(r, V, boot, lr, disc, 1.0, 1.0)
    # 1) 按定义求和
    nv = V[1:] + [boot]
    rho = [min(1.0, math.exp(x)) for x in lr]
    c = [min(1.0, math.exp(x)) for x in lr]
    err = 0.0
    for s in range(T):
        tot, coef = 0.0, 1.0
        for t in range(s, T):
            tot += coef * rho[t] * (r[t] + disc[t] * nv[t] - V[t])
            coef *= disc[t] * c[t]
        err = max(err, abs(V[s] + tot - vs[s]))
    print(f"[1] V-trace 倒序递推 vs 按定义求和：最大差 {err:.1e}")
    # 2) on-policy（log ρ = 0）时 = n 步自举回报
    vs_on, _ = vtrace(r, V, boot, [0.0] * T, disc)
    nstep, G = [0.0] * T, boot
    for t in reversed(range(T)):
        G = r[t] + disc[t] * G
        nstep[t] = G
    e2 = max(abs(a - b) for a, b in zip(vs_on, nstep))
    print(f"[2] ρ = c = 1（on-policy）时 vs 与 n 步自举回报最大差 {e2:.1e}——V-trace 是 n 步回报的 off-policy 推广")
    # 3) Little 定律 vs C++ 实测
    d = json.loads((pathlib.Path(__file__).parent / "ch19_output.json").read_text(encoding="utf-8"))["e2"]
    worst = 0.0
    for row in d["qrows"]:
        est = lag_when_learner_bound(row["q"], 16, 4)
        worst = max(worst, abs(est - row["lag"]))
        print(f"[3] 16 个 actor、队列容量 {row['q']:2.0f}：Little 定律估算滞后 {est:5.2f}，C++ 实测 {row['lag']:5.2f}")
    print("全部通过" if err < 1e-12 and e2 < 1e-12 and worst < 0.5 else "有不一致！")


if __name__ == "__main__":
    main()
