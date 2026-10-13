"""第 16 章手撕代码：势函数塑形、HER 目标重标记、options 的 SMDP Q 更新。
书中代码块与本文件逐字一致；运行时在小链式 MDP 上用价值迭代验证 Q′* = Q* − Φ，并检查 HER 重标记结果。纯 Python。
"""
import random


# ---------------------------------------------------------------- 书中代码块（开始）
def shaped_reward(r, s, s2, terminated, phi, gamma):
    """势函数塑形：r + γΦ(s′) − Φ(s)。终止状态的势必须取 0（否则会改变最优策略）。"""
    phi_next = 0.0 if terminated else phi(s2)
    return r + gamma * phi_next - phi(s)


def her_relabel(episode, reward_fn, k=4, rng=random):
    """episode: [(s, a, s2, achieved_goal_after_step, desired_goal)]。
    “future” 策略：对每一步再抽 k 个本回合之后实际达到的状态当作目标，重新计算奖励。返回扩充后的转移列表。"""
    out = []
    T = len(episode)
    for t, (s, a, s2, ag, g) in enumerate(episode):
        out.append((s, a, s2, g, reward_fn(ag, g)))                  # 原始目标
        for _ in range(k):
            fut = episode[rng.randrange(t, T)][3]                    # 本回合 t 之后真实达到过的某个状态
            out.append((s, a, s2, fut, reward_fn(ag, fut)))          # 假装“本来就想去那里”
    return out


def smdp_q_update(Q, s, option, rewards, s_end, terminated, options_at, gamma=0.99, alpha=0.1):
    """option 从 s 执行了 τ = len(rewards) 步到达 s_end：目标 = Σ γ^k r_k + γ^τ max_o′ Q(s_end, o′)。"""
    tau = len(rewards)
    ret = sum(gamma ** k * r for k, r in enumerate(rewards))
    boot = 0.0 if terminated else max(Q[(s_end, o)] for o in options_at(s_end))
    Q[(s, option)] += alpha * (ret + gamma ** tau * boot - Q[(s, option)])
    return Q[(s, option)]
# ---------------------------------------------------------------- 书中代码块（结束）


def main():
    rng = random.Random(16)
    # 1) 链式 MDP：0..N，N 为终点（+1），每步 −0.05；动作 左 / 右，20% 滑到反方向
    N, gamma, slip = 10, 0.9, 0.2

    def trans(s, a):
        right = [(min(N, s + 1), 1 - slip), (max(0, s - 1), slip)]
        left = [(max(0, s - 1), 1 - slip), (min(N, s + 1), slip)]
        return right if a == 1 else left

    def solve(rfun):
        V = [0.0] * (N + 1)
        for _ in range(3000):
            for s in range(N):
                V[s] = max(sum(p * (rfun(s, s2) + (0 if s2 == N else gamma * V[s2])) for s2, p in trans(s, a)) for a in (0, 1))
        return [[sum(p * (rfun(s, s2) + (0 if s2 == N else gamma * V[s2])) for s2, p in trans(s, a)) for a in (0, 1)] for s in range(N)]
    base = lambda s, s2: (1.0 if s2 == N else 0.0) - 0.05
    phi = lambda s: rng_phi[s]
    rng_phi = [rng.uniform(-3, 3) for _ in range(N + 1)]            # 任意势函数都行
    Q0 = solve(base)
    Q1 = solve(lambda s, s2: shaped_reward(base(s, s2), s, s2, s2 == N, phi, gamma))
    err = max(abs(Q1[s][a] - (Q0[s][a] - phi(s))) for s in range(N) for a in (0, 1))
    same = all((Q0[s][1] > Q0[s][0]) == (Q1[s][1] > Q1[s][0]) for s in range(N))
    print(f"[1] 链式 MDP + 随机势函数：max|Q′* − (Q* − Φ)| = {err:.2e}；各状态最优动作一致：{same}")
    # 终止状态的势不取 0 会怎样：等价于给“到达终点”额外加了 −Φ(终点)，最优策略可能改变
    bad = solve(lambda s, s2: base(s, s2) + gamma * phi(s2) - phi(s))
    print(f"    若终点的势不置 0（Φ(终点) = {phi(N):.2f}）：Q′ − (Q − Φ) 在终点旁的状态上偏差 {bad[N - 1][1] - (Q0[N - 1][1] - phi(N - 1)):+.3f}")
    # 2) HER：一条没到达目标 (9,9) 的轨迹，重标记后出现正奖励
    ep = [((i, 0), 1, (i + 1, 0), (i + 1, 0), (9, 9)) for i in range(5)]
    rf = lambda ag, g: 1.0 if ag == g else 0.0
    data = her_relabel(ep, rf, k=4, rng=rng)
    pos = sum(1 for d in data if d[4] > 0)
    print(f"[2] HER：5 步轨迹原本 0 个正奖励，重标记后 {len(data)} 条转移里有 {pos} 条正奖励")
    # 3) SMDP 更新：一个执行了 3 步的 option
    Q = {(0, "去兵线"): 0.0, (5, "去兵线"): 2.0, (5, "回城"): 1.0}
    v = smdp_q_update(Q, 0, "去兵线", [0.1, 0.1, 0.5], 5, False, lambda s: ["去兵线", "回城"], gamma=0.9, alpha=1.0)
    print(f"[3] SMDP：Σγ^k r_k + γ³·max Q(s_end) = 0.1 + 0.09 + 0.405 + 0.729·2 = {v:.4f}")
    ok = err < 1e-9 and same and pos > 0 and abs(v - (0.1 + 0.09 + 0.405 + 0.729 * 2)) < 1e-12
    print("全部通过" if ok else "有不一致！")


if __name__ == "__main__":
    main()
