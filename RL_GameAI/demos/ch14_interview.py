"""第 14 章手撕代码：DAgger 主循环、GAIL 判别器损失与最优解、CQL 保守项、RL+BC 混合损失的权重调度。
书中代码块与本文件逐字一致；运行时在一个小链式环境上跑 DAgger，并数值验证 D* 与 CQL 梯度。纯 Python。
"""
import math
import random


# ---------------------------------------------------------------- 书中代码块（开始）
def dagger(expert, rollout, fit, n_iters, beta0=1.0):
    """expert(s) → 专家动作；rollout(policy) → 该策略访问到的状态列表；fit(data) → 新的学生策略。
    第 i 轮用 β_i·专家 + (1−β_i)·学生 的混合策略采样，所有访问到的状态都请专家标注后并入数据集。"""
    data, student = [], None
    for i in range(n_iters):
        beta = beta0 * (0.5 ** i)
        if student is None:
            policy = expert
        else:
            policy = lambda s, st=student, b=beta: expert(s) if random.random() < b else st(s)
        for s in rollout(policy):
            data.append((s, expert(s)))        # 关键：标签永远来自专家，状态来自（混合）学生
        student = fit(data)
    return student


def gail_disc_loss(d_expert, d_policy):
    """判别器 D(s,a) ∈ (0,1) 判断“来自专家”的概率：最大化 E_E[log D] + E_π[log(1−D)]（这里返回负值作为损失）。
    生成器（策略）用奖励 r = −log(1 − D(s,a)) 做 RL。"""
    le = sum(math.log(d) for d in d_expert) / len(d_expert)
    lp = sum(math.log(1 - d) for d in d_policy) / len(d_policy)
    return -(le + lp)


def cql_penalty(q_values, a_data):
    """CQL(H) 的保守项：logsumexp_a Q(s,a) − Q(s, a_data)。压低所有动作（尤其是 Q 大的数据外动作），抬高数据里的动作。
    返回值和对 q_values 的梯度 softmax(Q) − onehot(a_data)。"""
    m = max(q_values)
    lse = m + math.log(sum(math.exp(q - m) for q in q_values))
    p = [math.exp(q - lse) for q in q_values]
    return lse - q_values[a_data], [p[k] - (1.0 if k == a_data else 0.0) for k in range(len(q_values))]


def update_bc_weight(w, win_rate, target=0.55, step=0.05, w_min=0.0, w_max=1.0):
    """RL + BC 混合损失 L = L_RL + w·L_BC 的规则化调权：胜率高于目标就加大 w（更像人），低于目标就减小 w（先保强度）。"""
    w = w + step if win_rate > target else w - step
    return min(w_max, max(w_min, w))
# ---------------------------------------------------------------- 书中代码块（结束）


def main():
    random.seed(14)
    # 1) 链式环境：位置 0..N，专家永远往目标（N）走；学生是表格多数投票，没见过的状态随机
    N, T, slip = 30, 40, 0.15

    def expert(s):
        return 1

    def rollout(policy):
        s, seen = 0, []
        for _ in range(T):
            seen.append(s)
            a = policy(s)
            if random.random() < slip:
                a = random.choice([0, 1])
            s = max(0, min(N, s + (1 if a == 1 else -1)))
            if s == N:
                break
        return seen

    def fit(data):
        table = {}
        for s, a in data:
            table.setdefault(s, []).append(a)
        major = {s: max(set(v), key=v.count) for s, v in table.items()}
        return lambda s: major.get(s, random.choice([0, 1]))

    def success(pol, n=2000):
        ok = 0
        for _ in range(n):
            s = 0
            for _ in range(T):
                a = pol(s)
                if random.random() < slip:
                    a = random.choice([0, 1])
                s = max(0, min(N, s + (1 if a == 1 else -1)))
                if s == N:
                    ok += 1
                    break
        return ok / n
    clean = fit([(s, 1) for s in range(0, N, 2)])        # “干净示范”只覆盖偶数格（模拟示范覆盖不全）
    stu = dagger(expert, rollout, fit, n_iters=6)
    print(f"[1] 链式环境（{N} 格，滑步 {slip}）：只覆盖一半状态的 BC 成功率 {success(clean):.3f}；DAgger 6 轮后 {success(stu):.3f}")
    # 2) GAIL 判别器最优解 D* = ρ_E / (ρ_E + ρ_π)
    rho_e, rho_p = [0.5, 0.3, 0.2], [0.2, 0.2, 0.6]

    def obj(D):
        return sum(e * math.log(d) + p * math.log(1 - d) for e, p, d in zip(rho_e, rho_p, D))
    dstar = [e / (e + p) for e, p in zip(rho_e, rho_p)]
    best = max(obj([min(0.999, max(0.001, d + random.gauss(0, 0.05))) for d in dstar]) for _ in range(20000))
    js = 0.5 * sum(e * math.log(2 * e / (e + p)) + p * math.log(2 * p / (e + p)) for e, p in zip(rho_e, rho_p))
    print(f"[2] D* = {[round(d, 4) for d in dstar]}，目标值 {obj(dstar):.6f}；随机扰动最好 {best:.6f}；−log4 + 2·JS = {-math.log(4) + 2 * js:.6f}")
    l_bal = gail_disc_loss([0.5] * 4, [0.5] * 4)
    print(f"[3] 判别器无法区分（D ≡ 0.5）时损失 = {l_bal:.6f} = log 4 = {math.log(4):.6f}")
    # 4) CQL 梯度 vs 有限差分
    q, a, h = [1.0, 3.0, -0.5, 0.2], 0, 1e-6
    v, g = cql_penalty(q, a)
    num = [(cql_penalty([x + (h if i == k else 0) for i, x in enumerate(q)], a)[0] - cql_penalty([x - (h if i == k else 0) for i, x in enumerate(q)], a)[0]) / (2 * h) for k in range(4)]
    err = max(abs(x - y) for x, y in zip(num, g))
    print(f"[4] CQL 保守项 {v:.4f}；梯度 {[round(x, 4) for x in g]}（数据外的 Q=3.0 被压得最多）；与有限差分最大差 {err:.1e}")
    w = 0.5
    for wr in [0.6, 0.62, 0.5, 0.58]:
        w = update_bc_weight(w, wr)
    print(f"[5] 胜率序列 0.60/0.62/0.50/0.58 后 BC 权重 w = {w:.2f}")
    ok = abs(obj(dstar) - (-math.log(4) + 2 * js)) < 1e-12 and best <= obj(dstar) and err < 1e-8 and abs(l_bal - math.log(4)) < 1e-12
    print("全部通过" if ok else "有不一致！")


if __name__ == "__main__":
    main()
