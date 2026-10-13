"""第 15 章手撕代码：Elo 期望胜率与更新、零和矩阵博弈的可利用度、虚拟自博弈（FSP）、QMIX 单调混合与 IGM 检查。
书中代码块与本文件逐字一致；运行时与第 15 章 C++ demo 的结论交叉验证。纯 Python。
"""
import itertools
import math
import random


# ---------------------------------------------------------------- 书中代码块（开始）
def elo_expect(ra, rb):
    """A 对 B 的期望得分：1 / (1 + 10^((R_B − R_A)/400))，即以 400/ln10 为尺度的 logistic 函数。"""
    return 1.0 / (1.0 + 10 ** ((rb - ra) / 400.0))


def elo_update(ra, rb, score_a, k=16):
    """score_a ∈ {1, 0.5, 0}。两人的分数变化大小相同、方向相反（零和）。"""
    d = k * (score_a - elo_expect(ra, rb))
    return ra + d, rb - d


def exploitability(A, p):
    """对称零和矩阵博弈（A 为行玩家收益，A = −Aᵀ）：最优响应对 p 能拿到的收益，纳什均衡处为 0。"""
    return max(sum(A[i][j] * p[j] for j in range(len(p))) for i in range(len(p)))


def fictitious_play(A, p0, iters):
    """每轮对“历史平均策略”做最优响应，再把这个纯策略并入平均。返回平均策略。"""
    avg = list(p0)
    n = len(avg)
    for k in range(1, iters + 1):
        u = [sum(A[i][j] * avg[j] for j in range(n)) for i in range(n)]
        b = max(range(n), key=lambda i: u[i])
        avg = [x + ((1.0 if i == b else 0.0) - x) / (k + 1) for i, x in enumerate(avg)]
    return avg


def qmix_monotone(qs, w, b):
    """最简单的单调混合：Q_tot = Σ |w_i|·Q_i + b。权重取绝对值保证 ∂Q_tot/∂Q_i ≥ 0。
    （真实 QMIX 用以全局状态为输入的超网络生成非负权重，并有一层 ELU 非线性。）"""
    return sum(abs(wi) * q for wi, q in zip(w, qs)) + b
# ---------------------------------------------------------------- 书中代码块（结束）


def main():
    rng = random.Random(15)
    A = [[0, -1, 1], [1, 0, -1], [-1, 1, 0]]
    # 1) RPS：纳什 (1/3,1/3,1/3) 的可利用度为 0；无差异条件：对手均衡时三个动作收益相同
    nash = [1 / 3] * 3
    u = [sum(A[i][j] * nash[j] for j in range(3)) for i in range(3)]
    print(f"[1] 纳什策略下三个动作的收益 {u}（无差异）；可利用度 {exploitability(A, nash):.2e}")
    avg = fictitious_play(A, [0.5, 0.3, 0.2], 3000)
    print(f"[2] FSP 3000 轮平均策略 {[round(x, 3) for x in avg]}，可利用度 {exploitability(A, avg):.4f}（C++ demo 同设置）")
    # 3) Elo：100 分差对应 64% 胜率；对称性
    e = elo_expect(1600, 1500)
    ra, rb = elo_update(1500, 1500, 1.0, k=32)
    print(f"[3] 分差 100 → 期望胜率 {e:.4f}；两个 1500 分的人对局后 → {ra:.0f} / {rb:.0f}；E_A + E_B = {elo_expect(1600, 1500) + elo_expect(1500, 1600):.12f}")
    # Elo 期望 = logistic 回归：σ(Δ·ln10/400)
    sig = 1 / (1 + math.exp(-100 * math.log(10) / 400))
    print(f"    logistic 形式 σ(100·ln10/400) = {sig:.4f}")
    # 4) QMIX：单调混合满足 IGM（各自 argmax 的组合就是联合 argmax）；非单调混合可能违反
    viol_mono, viol_non = 0, 0
    for _ in range(2000):
        Qi = [[rng.gauss(0, 1) for _ in range(3)] for _ in range(3)]   # 3 个智能体 × 3 个动作
        w = [rng.gauss(0, 1) for _ in range(3)]
        greedy = tuple(max(range(3), key=lambda a, q=q: q[a]) for q in Qi)
        joint_m = max(itertools.product(range(3), repeat=3), key=lambda js: qmix_monotone([Qi[i][js[i]] for i in range(3)], w, 0.0))
        viol_mono += joint_m != greedy
        joint_n = max(itertools.product(range(3), repeat=3), key=lambda js: sum(w[i] * Qi[i][js[i]] for i in range(3)))
        viol_non += joint_n != greedy
    print(f"[4] 2000 个随机实例：单调混合违反 IGM {viol_mono} 次；允许负权重的线性混合违反 {viol_non} 次")
    ok = exploitability(A, nash) < 1e-12 and exploitability(A, avg) < 0.02 and abs(e - sig) < 1e-12 and viol_mono == 0 and viol_non > 0
    print("全部通过" if ok else "有不一致！")


if __name__ == "__main__":
    main()
