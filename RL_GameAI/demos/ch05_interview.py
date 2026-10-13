"""第 5 章「面试手撕版」：MC / TD(0) / 资格迹 TD(λ) / λ-回报（GAE 式倒序递推）。
书中代码块与本文件逐字一致；运行时与 ../src/rlcore.py 的参考实现、以及按定义暴力计算的 λ-回报对拍。
运行：python ch05_interview.py
"""
import pathlib
import random
import sys

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent.parent / "src"))
import rlcore as C  # noqa: E402


# ---------------------------------------------------------------- 书中代码块（开始）
def mc_update(V, states, rewards, alpha, gamma):
    # states[t] = S_t (t = 0..T-1)，rewards[t] = R_{t+1}；回合结束后从后往前算回报
    G, returns = 0.0, [0.0] * len(rewards)
    for t in reversed(range(len(rewards))):
        G = rewards[t] + gamma * G
        returns[t] = G
    for t, s in enumerate(states):                         # 每次访问 MC；同一状态多次出现时按时间顺序依次更新
        V[s] += alpha * (returns[t] - V[s])


def td0_step(V, s, r, s2, done, alpha, gamma):
    target = r + (0.0 if done else gamma * V[s2])          # 终止时不自举（截断时应自举，见 5.2 节注意框）
    V[s] += alpha * (target - V[s])


def td_lambda_episode(V, states, rewards, alpha, gamma, lam):
    # 后向视角（在线、累积迹）；states 比 rewards 多一个元素：最后一个是终止状态
    z = {}
    for t in range(len(rewards)):
        s, s2 = states[t], states[t + 1]
        done = t == len(rewards) - 1
        delta = rewards[t] + (0.0 if done else gamma * V[s2]) - V[s]
        for k in z:
            z[k] *= gamma * lam                            # 所有迹先衰减
        z[s] = z.get(s, 0.0) + 1.0                         # 当前状态的迹 +1
        for k, e in z.items():
            V[k] += alpha * delta * e                      # 一个 δ 同时更新所有“最近来过”的状态


def lambda_returns(values, rewards, gamma, lam):
    # values[t] = V(S_t)，长度 T+1（values[T] = 终止状态的价值 0，截断时填 V(S_T)）
    # 倒序递推：G^λ_t = R_{t+1} + γ[(1-λ)V(S_{t+1}) + λ G^λ_{t+1}]，与 GAE 同一个结构
    T = len(rewards)
    out = [0.0] * T
    nxt = values[T]
    for t in reversed(range(T)):
        nxt = rewards[t] + gamma * ((1 - lam) * values[t + 1] + lam * nxt)
        out[t] = nxt
    return out
# ---------------------------------------------------------------- 书中代码块（结束）


def lambda_returns_by_definition(values, rewards, gamma, lam):
    """按定义：G^λ_t = (1-λ) Σ_{n=1}^{T-t-1} λ^{n-1} G^{(n)}_t + λ^{T-t-1} G_t。"""
    T = len(rewards)
    out = []
    for t in range(T):
        acc, Gn, disc = 0.0, 0.0, 1.0
        for n in range(1, T - t + 1):
            Gn += disc * rewards[t + n - 1]
            disc *= gamma
            if t + n < T:
                acc += (1 - lam) * lam ** (n - 1) * (Gn + disc * values[t + n])
        G = sum(gamma ** k * rewards[t + k] for k in range(T - t))
        out.append(acc + lam ** (T - t - 1) * G)
    return out


def main():
    ok = True
    # 1) MC / TD(0) / 在线 TD(λ)：与 rlcore 参考实现在 19 状态随机游走上对拍（γ = 1）
    n = 19
    rng = C.Rng(10001)
    eps = [C.rw_gen(n, rng, -1.0, 1.0) for _ in range(10)]
    pairs = {"MC α=0.05": ([0.0] * 21, [0.0] * 21), "TD(0) α=0.4": ([0.0] * 21, [0.0] * 21),
             "TD(λ=0.8) α=0.3": ([0.0] * 21, [0.0] * 21)}
    for st, rw in eps:
        a, b = pairs["MC α=0.05"]
        mc_update(a, st[:-1], rw, 0.05, 1.0); C.rw_mc(b, (st, rw), 0.05)
        a, b = pairs["TD(0) α=0.4"]
        for t in range(len(rw)):
            td0_step(a, st[t], rw[t], st[t + 1], st[t + 1] in (0, n + 1), 0.4, 1.0)
        C.rw_td0(b, (st, rw), 0.4, n)
        a, b = pairs["TD(λ=0.8) α=0.3"]
        td_lambda_episode(a, st, rw, 0.3, 1.0, 0.8); C.rw_td_lambda(b, (st, rw), 0.8, 0.3, n, True)
    for name, (a, b) in pairs.items():
        d = max(abs(x - y) for x, y in zip(a, b))
        ok &= d < 1e-12
        print(f"{name:<18} 与参考实现最大差 = {d:.1e}")
    # 2) λ-回报倒序递推 vs 定义（随机长度、随机奖励与价值，γ < 1；λ=0 → TD 目标，λ=1 → MC 回报）
    random.seed(5)
    worst = 0.0
    for _ in range(300):
        T = random.randint(1, 40)
        rw = [random.uniform(-1, 1) for _ in range(T)]
        vals = [random.uniform(-2, 2) for _ in range(T)] + [random.choice([0.0, random.uniform(-2, 2)])]
        g, lam = random.uniform(0.8, 1.0), random.choice([0.0, 0.5, 0.95, 1.0])
        if vals[T] != 0.0:          # 截断：定义里把尾部的 V(S_T) 看作 R_T 之后的自举
            continue
        a = lambda_returns(vals, rw, g, lam)
        b = lambda_returns_by_definition(vals, rw, g, lam)
        worst = max(worst, max(abs(x - y) for x, y in zip(a, b)))
    ok &= worst < 1e-10
    print(f"λ-回报 倒序递推 vs 定义  最大差 = {worst:.1e}")
    print("全部通过" if ok else "存在不一致！")


if __name__ == "__main__":
    main()
