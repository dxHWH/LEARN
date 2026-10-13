"""第 4 章「面试手撕版」价值迭代 / 策略迭代：与书中代码块逐字一致，并在对线 MDP 与地牢网格上对拍精确解。
运行：python ch04_interview.py   （需要 ../src/rlcore.py 提供 MDP 数据与精确解）
"""
import pathlib
import sys

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent.parent / "src"))
import rlcore as C  # noqa: E402


# ---------------------------------------------------------------- 书中代码块（开始）
def q(P, R, V, gamma, s, a):
    return R[s][a] + gamma * sum(p * V[s2] for s2, p in P[s][a])


def value_iteration(P, R, gamma, tol=1e-8):
    # P[s][a] = [(s2, prob), ...]；R[s][a] = 期望即时奖励；终止状态 P[s] == []
    V = [0.0] * len(P)
    while True:
        delta = 0.0
        for s in range(len(P)):
            if not P[s]:
                continue
            best = max(q(P, R, V, gamma, s, a) for a in range(len(P[s])))
            delta = max(delta, abs(best - V[s]))
            V[s] = best                                   # 原地更新（Gauss-Seidel）
        if delta < tol:
            break
    pi = [max(range(len(P[s])), key=lambda a: q(P, R, V, gamma, s, a)) if P[s] else None
          for s in range(len(P))]
    return V, pi


def policy_iteration(P, R, gamma, tol=1e-10):
    n = len(P)
    pi = [0 if P[s] else None for s in range(n)]
    V = [0.0] * n
    while True:
        while True:                                       # ① 策略评估：原地迭代，热启动上一轮的 V
            delta = 0.0
            for s in range(n):
                if P[s]:
                    v = q(P, R, V, gamma, s, pi[s])
                    delta, V[s] = max(delta, abs(v - V[s])), v
            if delta < tol:
                break
        stable = True                                     # ② 策略改进：严格更好才换动作
        for s in range(n):
            if P[s]:
                best = max(range(len(P[s])), key=lambda a: q(P, R, V, gamma, s, a))
                if q(P, R, V, gamma, s, best) > q(P, R, V, gamma, s, pi[s]) + 1e-9:
                    pi[s], stable = best, False
        if stable:
            return V, pi
# ---------------------------------------------------------------- 书中代码块（结束）


def check(name, m):
    Vstar, _, _ = C.value_iteration(m, 1e-12)
    V1, pi1 = value_iteration(m.P, m.R, m.gamma)
    V2, pi2 = policy_iteration(m.P, m.R, m.gamma)
    e1 = max(abs(a - b) for a, b in zip(V1, Vstar))
    e2 = max(abs(a - b) for a, b in zip(V2, Vstar))
    ev = lambda pi: max(abs(a - b) for a, b in zip(C.eval_exact(m, C.det(m, [x if x is not None else 0 for x in pi])), Vstar))
    e3, e4 = ev(pi1), ev(pi2)
    diff = sum(1 for a, b in zip(pi1, pi2) if a != b)
    print(f"{name}: |S|={m.n}  手撕 VI 与 V* 最大差 {e1:.2e}，手撕 PI 与 V* 最大差 {e2:.2e}；"
          f"两份策略有 {diff} 个状态选了不同动作（平局），但各自精确评估后与 V* 的最大差为 {e3:.2e} / {e4:.2e} → 都是最优策略")
    return e1, e2, e3 < 1e-6 and e4 < 1e-6


if __name__ == "__main__":
    r = [check("对线 MDP γ=0.9", C.lane(0.9)),
         check("地牢 7×9 滑步 0%", C.Grid(C.DUNGEON, 0.95, 0.0, -0.1, 10, -10).mdp),
         check("地牢 7×9 滑步 20%", C.Grid(C.DUNGEON, 0.95, 0.2, -0.1, 10, -10).mdp)]
    ok = all(e1 < 1e-6 and e2 < 1e-6 and same for e1, e2, same in r)
    print("全部通过" if ok else "存在差异")
