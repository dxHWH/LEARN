"""第 1 章实测：RL 数学工具速查（纯 Python，无第三方依赖，随机数用 random.Random 固定种子）。
  E1 全期望 / 全方差公式：两级随机变量（先抽英雄类型，再抽伤害）的解析值 vs 20 万次蒙特卡洛
  E2 重要性采样：目标 π = N(μ, 1)、行为 b = N(0, 1)，权重二阶矩 e^{μ²}；普通 vs 加权 IS 的估计方差与有效样本量
  E3 KL 不对称：用单个高斯 q 拟合双峰混合 p；前向 KL(p‖q)（覆盖所有峰）vs 反向 KL(q‖p)（锁定一个峰）
  E4 得分函数估计 vs 重参数化：∇_μ E_{x~N(μ,1)}[x²] = 2μ 的单样本方差（含基线）
  E5 优化器：病态二次函数 f = ½(x² + 50 y²) 上 SGD / Momentum / Adam 的轨迹与损失曲线
书中代码块（工具函数）与本文件逐字一致。输出：stdout + ch01_output.json（build.py 自动读取）。
"""
import json
import math
import pathlib
import random


# ---------------------------------------------------------------- 书中代码块（开始）
def kl_categorical(p, q):
    """KL(p‖q) = Σ p log(p/q)；p 为 0 的项贡献 0；q 为 0 而 p > 0 时为 +∞（覆盖性要求）。"""
    s = 0.0
    for pi, qi in zip(p, q):
        if pi > 0:
            if qi <= 0:
                return math.inf
            s += pi * math.log(pi / qi)
    return s


def importance_estimates(xs, f, logp_target, logp_behavior):
    """样本 xs 来自行为分布 b。返回 (普通 IS, 加权 IS, 有效样本量 ESS = (Σw)² / Σw²)。"""
    w = [math.exp(logp_target(x) - logp_behavior(x)) for x in xs]
    sw = sum(w)
    ordinary = sum(wi * f(x) for wi, x in zip(w, xs)) / len(xs)
    weighted = sum(wi * f(x) for wi, x in zip(w, xs)) / sw
    ess = sw * sw / sum(wi * wi for wi in w)
    return ordinary, weighted, ess


def cross_entropy_loss(logits, label):
    """−log softmax(z)[y]，用 log-sum-exp 保证数值稳定；= 对该样本的负对数似然。"""
    m = max(logits)
    lse = m + math.log(sum(math.exp(z - m) for z in logits))
    return lse - logits[label]


def adam_step(w, g, state, lr=1e-3, b1=0.9, b2=0.999, eps=1e-8, weight_decay=0.0):
    """AdamW：一阶 / 二阶矩的指数平均 + 偏差修正；weight_decay 直接作用在参数上（与梯度解耦）。"""
    state["t"] += 1
    t = state["t"]
    out = []
    for i, (wi, gi) in enumerate(zip(w, g)):
        state["m"][i] = b1 * state["m"][i] + (1 - b1) * gi
        state["v"][i] = b2 * state["v"][i] + (1 - b2) * gi * gi
        mh = state["m"][i] / (1 - b1 ** t)
        vh = state["v"][i] / (1 - b2 ** t)
        out.append(wi - lr * (mh / (math.sqrt(vh) + eps) + weight_decay * wi))
    return out


def clip_by_global_norm(g, max_norm):
    n = math.sqrt(sum(x * x for x in g))
    return [x * max_norm / n for x in g] if n > max_norm else list(g)
# ---------------------------------------------------------------- 书中代码块（结束）


def e1(rng):
    # 英雄类型 Y：坦克 / 战士 / 法师，概率 0.3 / 0.5 / 0.2；伤害 X | Y ~ N(μ_y, σ_y²)
    py, mu, sd = [0.3, 0.5, 0.2], [200.0, 450.0, 700.0], [40.0, 90.0, 150.0]
    ex = sum(p * m for p, m in zip(py, mu))                                    # E[E[X|Y]]
    var_within = sum(p * s * s for p, s in zip(py, sd))                        # E[Var(X|Y)]
    var_between = sum(p * (m - ex) ** 2 for p, m in zip(py, mu))              # Var(E[X|Y])
    N = 200000
    xs = []
    for _ in range(N):
        y = rng.choices(range(3), weights=py)[0]
        xs.append(rng.gauss(mu[y], sd[y]))
    m = sum(xs) / N
    v = sum((x - m) ** 2 for x in xs) / N
    print(f"[E1] E[X] = E[E[X|Y]] = {ex:.1f}（MC {m:.1f}）；Var(X) = E[Var(X|Y)] + Var(E[X|Y]) = {var_within:.0f} + {var_between:.0f} = {var_within + var_between:.0f}（MC {v:.0f}）")
    return {"ex": ex, "vw": var_within, "vb": var_between, "var": var_within + var_between, "mc_mean": m, "mc_var": v, "N": N}


def e2(rng):
    mus = [0.0, 0.5, 1.0, 1.5, 2.0, 2.5, 3.0]
    n, R = 100, 2000
    rows = []
    for mu in mus:
        lp_t = lambda x, mu=mu: -0.5 * (x - mu) ** 2
        lp_b = lambda x: -0.5 * x * x
        ords, wts, esss = [], [], []
        for _ in range(R):
            xs = [rng.gauss(0, 1) for _ in range(n)]
            o, w, e = importance_estimates(xs, lambda x: x, lp_t, lp_b)
            ords.append(o); wts.append(w); esss.append(e)
        def mse(v): return sum((x - mu) ** 2 for x in v) / len(v)
        rows.append({"mu": mu, "w2": math.exp(mu * mu), "kl": 0.5 * mu * mu, "mse_ord": mse(ords), "mse_w": mse(wts),
                     "bias_w": sum(wts) / R - mu, "ess": sum(esss) / R, "ess_th": n / math.exp(mu * mu)})
        print(f"[E2] μ = {mu:.1f}（KL = {0.5 * mu * mu:.2f}）：E_b[w²] = e^μ² = {math.exp(mu * mu):8.1f}；n = {n} 时 MSE 普通 {rows[-1]['mse_ord']:.4f} / 加权 {rows[-1]['mse_w']:.4f}（加权偏差 {rows[-1]['bias_w']:+.4f}），平均 ESS {rows[-1]['ess']:.1f}")
    return {"rows": rows, "n": n, "R": R,
            "ord": [[r["mu"], r["mse_ord"]] for r in rows], "wtd": [[r["mu"], r["mse_w"]] for r in rows], "th": [[r["mu"], (r["w2"] * (1 + 4 * r["mu"] ** 2) - r["mu"] ** 2) / n] for r in rows]}


def e3():
    # p：0.5·N(−2, 0.6²) + 0.5·N(2, 0.6²)；q = N(m, s²)。数值积分在 [−8, 8] 上做
    xs = [-8 + 16 * i / 4000 for i in range(4001)]
    dx = xs[1] - xs[0]
    npdf = lambda x, m, s: math.exp(-0.5 * ((x - m) / s) ** 2) / (s * math.sqrt(2 * math.pi))
    p = [0.5 * npdf(x, -2, 0.6) + 0.5 * npdf(x, 2, 0.6) for x in xs]

    def kl(a, b):
        return sum(ai * math.log(ai / bi) * dx for ai, bi in zip(a, b) if ai > 1e-300 and bi > 1e-300)
    # 前向 KL：最小化 −E_p[log q] ⇒ 矩匹配
    mf = sum(x * pi * dx for x, pi in zip(xs, p))
    sf = math.sqrt(sum((x - mf) ** 2 * pi * dx for x, pi in zip(xs, p)))
    # 反向 KL：网格搜索
    best = (1e9, 0, 1)
    for i in range(81):
        m = -4 + 8 * i / 80
        for j in range(1, 41):
            s = 0.05 * j
            q = [npdf(x, m, s) for x in xs]
            v = kl(q, p)
            if v < best[0]:
                best = (v, m, s)
    _, mr, sr = best
    qf = [npdf(x, mf, sf) for x in xs]
    qr = [npdf(x, mr, sr) for x in xs]
    out = {"mf": round(mf, 10) + 0.0, "sf": sf, "mr": mr, "sr": sr,
           "fwd_f": kl(p, qf), "rev_f": kl(qf, p), "fwd_r": kl(p, qr), "rev_r": kl(qr, p)}
    print(f"[E3] 前向 KL 最优 q = N({mf:.2f}, {sf:.3f}²)：KL(p‖q) = {out['fwd_f']:.3f}，KL(q‖p) = {out['rev_f']:.3f}")
    print(f"     反向 KL 最优 q = N({mr:.2f}, {sr:.2f}²)：KL(p‖q) = {out['fwd_r']:.3f}，KL(q‖p) = {out['rev_r']:.3f}")
    step = 20
    out["p"] = [[xs[i], p[i]] for i in range(0, len(xs), step)]
    out["qf"] = [[xs[i], qf[i]] for i in range(0, len(xs), step)]
    out["qr"] = [[xs[i], qr[i]] for i in range(0, len(xs), step)]
    return out


def e4(rng):
    mus = [0.0, 0.5, 1.0, 2.0, 3.0]
    N = 100000
    rows = []
    for mu in mus:
        sf, sfb, rp = [], [], []
        b = mu * mu + 1                                       # 基线 = E[f(x)]
        for _ in range(N):
            eps = rng.gauss(0, 1)
            x = mu + eps
            sf.append(x * x * (x - mu))                       # 得分函数：f(x)·∂log p/∂μ
            sfb.append((x * x - b) * (x - mu))                # 减基线
            rp.append(2 * x)                                  # 重参数化：∂f(μ+ε)/∂μ
        def mv(v):
            m = sum(v) / len(v)
            return m, sum((t - m) ** 2 for t in v) / len(v)
        (m1, v1), (m2, v2), (m3, v3) = mv(sf), mv(sfb), mv(rp)
        rows.append({"mu": mu, "true": 2 * mu, "sf": m1, "sf_var": v1, "sfb": m2, "sfb_var": v2, "rp": m3, "rp_var": v3})
        print(f"[E4] μ = {mu:.1f}（真梯度 {2 * mu:.1f}）：得分函数 均值 {m1:+.3f} 方差 {v1:8.2f} | 加基线 {m2:+.3f} 方差 {v2:7.2f} | 重参数化 {m3:+.3f} 方差 {v3:.2f}")
    return {"rows": rows, "N": N}


def e5():
    A = [1.0, 50.0]                                            # f = ½(x² + 50 y²)，条件数 50
    f = lambda w: 0.5 * (A[0] * w[0] ** 2 + A[1] * w[1] ** 2)
    grad = lambda w: [A[0] * w[0], A[1] * w[1]]
    runs = {}
    T = 200
    # SGD（lr 受最大曲率限制：lr < 2/50）
    for name, cfg in [("sgd", 0.035), ("mom", 0.035), ("adam", 0.1)]:
        w = [-4.0, 1.0]
        path, loss = [list(w)], [f(w)]
        v = [0.0, 0.0]
        st = {"t": 0, "m": [0.0, 0.0], "v": [0.0, 0.0]}
        for _ in range(T):
            g = grad(w)
            if name == "sgd":
                w = [wi - cfg * gi for wi, gi in zip(w, g)]
            elif name == "mom":
                v = [0.9 * vi + gi for vi, gi in zip(v, g)]
                w = [wi - cfg * 0.2 * vi for wi, vi in zip(w, v)]       # 有效步长 0.007，长期等效 0.07
            else:
                w = adam_step(w, g, st, lr=cfg)
            path.append(list(w))
            loss.append(max(f(w), 1e-300))
        runs[name] = {"path": path[:80], "loss": [[i, l] for i, l in enumerate(loss)], "end": loss[-1], "l50": loss[50]}
        print(f"[E5] {name:4s}：第 50 步损失 {loss[50]:.3e}，第 {T} 步 {loss[-1]:.3e}")
    g = clip_by_global_norm([3.0, 4.0], 1.0)
    print(f"[E5] 梯度裁剪：(3, 4) 按全局范数 1 裁剪 → ({g[0]:.1f}, {g[1]:.1f})")
    return runs


def main():
    rng = random.Random(1)
    out = {"e1": e1(rng), "e2": e2(rng), "e3": e3(), "e4": e4(rng), "e5": e5()}
    # 工具函数自检
    print(f"[自检] KL([0.5,0.5]‖[0.9,0.1]) = {kl_categorical([0.5, 0.5], [0.9, 0.1]):.4f}，反向 {kl_categorical([0.9, 0.1], [0.5, 0.5]):.4f}；"
          f"交叉熵(logits=[2,1,0], y=0) = {cross_entropy_loss([2.0, 1.0, 0.0], 0):.4f}")
    out["kl_demo"] = {"pq": kl_categorical([0.5, 0.5], [0.9, 0.1]), "qp": kl_categorical([0.9, 0.1], [0.5, 0.5])}
    (pathlib.Path(__file__).parent / "ch01_output.json").write_text(json.dumps(out, ensure_ascii=False), encoding="utf-8")


if __name__ == "__main__":
    main()
