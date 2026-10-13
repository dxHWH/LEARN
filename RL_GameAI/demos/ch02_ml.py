"""第 2 章实测：ML / DL 面试速查（纯 Python，无第三方依赖，固定随机种子）。
  E1 偏差-方差分解：y = sin(2πx) + N(0, 0.3²)，每次 20 个训练点，多项式次数 0…12（带极小 ridge），300 个数据集
  E2 L1 vs L2：10 维线性回归、只有 3 个特征有效；Lasso（坐标下降 + 软阈值）与 Ridge 随 λ 的零系数个数
  E3 梯度消失：20 层、宽 32 的 MLP，sigmoid / tanh / ReLU / ReLU + 残差，在初始化时各层权重梯度的范数
  E4 注意力缩放：q、k ~ N(0, I_d)，点积方差 = d；16 个 key 的 softmax 在不缩放 / 除以 √d 时的最大概率与熵
  E5 BN vs LN：同一个样本放进不同的 batch，BN 输出随 batch 变化而 LN 不变
  E6 AUC：按排序计算的 AUC = 随机正样本得分高于随机负样本的概率，与梯形法 ROC 面积一致
书中代码块与本文件逐字一致。输出：stdout + ch02_output.json。
"""
import json
import math
import pathlib
import random


# ---------------------------------------------------------------- 书中代码块（开始）
def softmax(z):
    m = max(z)
    e = [math.exp(x - m) for x in z]
    s = sum(e)
    return [x / s for x in e]


def softmax_ce_grad(logits, label):
    """L = −log softmax(z)[y] 对 logits 的梯度 = p − onehot(y)。"""
    p = softmax(logits)
    return [pk - (1.0 if k == label else 0.0) for k, pk in enumerate(p)]


def layer_norm(x, eps=1e-5):
    """对单个样本的特征维度归一化：与 batch 里还有谁无关（RL / Transformer 常用）。"""
    m = sum(x) / len(x)
    v = sum((t - m) ** 2 for t in x) / len(x)
    return [(t - m) / math.sqrt(v + eps) for t in x]


def batch_norm_train(batch, eps=1e-5):
    """对每个特征在 batch 维度归一化：输出依赖同 batch 的其他样本；推理时要换成滑动统计量。"""
    n, d = len(batch), len(batch[0])
    mu = [sum(b[j] for b in batch) / n for j in range(d)]
    var = [sum((b[j] - mu[j]) ** 2 for b in batch) / n for j in range(d)]
    return [[(b[j] - mu[j]) / math.sqrt(var[j] + eps) for j in range(d)] for b in batch]


def scaled_dot_attention(q, keys, values):
    d = len(q)
    scores = [sum(a * b for a, b in zip(q, k)) / math.sqrt(d) for k in keys]   # 除以 √d：点积方差从 d 拉回 1
    w = softmax(scores)
    return [sum(wi * v[j] for wi, v in zip(w, values)) for j in range(len(values[0]))], w


def auc_by_rank(scores, labels):
    """AUC = P(随机正样本得分 > 随机负样本得分)，平局算 1/2。O(n log n) 的秩和写法（Mann-Whitney U）。"""
    order = sorted(range(len(scores)), key=lambda i: scores[i])
    ranks = [0.0] * len(scores)
    i = 0
    while i < len(order):
        j = i
        while j + 1 < len(order) and scores[order[j + 1]] == scores[order[i]]:
            j += 1
        for k in range(i, j + 1):
            ranks[order[k]] = (i + j) / 2 + 1          # 平局取平均秩
        i = j + 1
    npos = sum(labels)
    nneg = len(labels) - npos
    rpos = sum(r for r, y in zip(ranks, labels) if y == 1)
    return (rpos - npos * (npos + 1) / 2) / (npos * nneg)


def soft_threshold(z, lam):
    """Lasso 坐标下降的闭式解：|z| ≤ λ 时直接置 0，这就是 L1 产生稀疏解的原因。"""
    return math.copysign(max(abs(z) - lam, 0.0), z)
# ---------------------------------------------------------------- 书中代码块（结束）


def solve(A, b):
    n = len(b)
    M = [row[:] + [b[i]] for i, row in enumerate(A)]
    for c in range(n):
        p = max(range(c, n), key=lambda r: abs(M[r][c]))
        M[c], M[p] = M[p], M[c]
        for r in range(c + 1, n):
            f = M[r][c] / M[c][c]
            for k in range(c, n + 1):
                M[r][k] -= f * M[c][k]
    x = [0.0] * n
    for r in range(n - 1, -1, -1):
        x[r] = (M[r][n] - sum(M[r][k] * x[k] for k in range(r + 1, n))) / M[r][r]
    return x


def polyfit(xs, ys, deg, lam=1e-8):
    # 用 Legendre 基（x 映射到 [−1, 1]）以免高次多项式病态
    def basis(x):
        t = 2 * x - 1
        P = [1.0, t]
        for k in range(2, deg + 1):
            P.append(((2 * k - 1) * t * P[-1] - (k - 1) * P[-2]) / k)
        return P[:deg + 1]
    Phi = [basis(x) for x in xs]
    d = deg + 1
    A = [[sum(r[i] * r[j] for r in Phi) + (lam if i == j else 0.0) for j in range(d)] for i in range(d)]
    b = [sum(r[i] * y for r, y in zip(Phi, ys)) for i in range(d)]
    w = solve(A, b)
    return lambda x: sum(wi * bi for wi, bi in zip(w, basis(x)))


def e1(rng):
    f = lambda x: math.sin(2 * math.pi * x)
    sig, n, R = 0.3, 20, 300
    grid = [i / 100 for i in range(101)]
    rows = []
    for deg in range(13):
        preds = [[0.0] * len(grid) for _ in range(R)]
        test_mse = 0.0
        train_mse = 0.0
        r2 = random.Random(1000)              # 每个次数用同一批数据集（公共随机数）
        for k in range(R):
            xs = [r2.random() for _ in range(n)]
            ys = [f(x) + r2.gauss(0, sig) for x in xs]
            g = polyfit(xs, ys, deg)
            preds[k] = [g(x) for x in grid]
            train_mse += sum((g(x) - y) ** 2 for x, y in zip(xs, ys)) / n / R
        mean = [sum(preds[k][i] for k in range(R)) / R for i in range(len(grid))]
        bias2 = sum((mean[i] - f(x)) ** 2 for i, x in enumerate(grid)) / len(grid)
        var = sum(sum((preds[k][i] - mean[i]) ** 2 for k in range(R)) / R for i in range(len(grid))) / len(grid)
        test_mse = bias2 + var + sig * sig
        rows.append({"deg": deg, "bias2": bias2, "var": var, "noise": sig * sig, "test": test_mse, "train": train_mse})
    best = min(rows, key=lambda r: r["test"])
    print("[E1] 偏差-方差（次数 0…12）：" + "，".join(f"{r['deg']}:{r['bias2']:.3f}/{r['var']:.3f}" for r in rows))
    print(f"     测试误差最小在次数 {best['deg']}（{best['test']:.3f}）；次数 12 时训练误差 {rows[12]['train']:.3f}、测试误差 {rows[12]['test']:.3f}")
    return {"rows": rows, "best": best["deg"], "best_test": best["test"],
            "bias2": [[r["deg"], r["bias2"]] for r in rows], "var": [[r["deg"], r["var"]] for r in rows],
            "test": [[r["deg"], r["test"]] for r in rows], "train": [[r["deg"], r["train"]] for r in rows], "noise": sig * sig}


def e2(rng):
    d, n = 10, 50
    wtrue = [3.0, -2.0, 1.5] + [0.0] * 7
    X = [[rng.gauss(0, 1) for _ in range(d)] for _ in range(n)]
    y = [sum(a * b for a, b in zip(x, wtrue)) + rng.gauss(0, 0.5) for x in X]
    lams = [0.01, 0.03, 0.1, 0.3, 1.0, 3.0, 10.0, 30.0]
    rows = []
    for lam in lams:
        # Lasso：min 1/(2n)‖y − Xw‖² + λ‖w‖₁，坐标下降
        w = [0.0] * d
        for _ in range(300):
            for j in range(d):
                rj = [y[i] - sum(X[i][k] * w[k] for k in range(d) if k != j) for i in range(n)]
                zj = sum(X[i][j] * rj[i] for i in range(n)) / n
                aj = sum(X[i][j] ** 2 for i in range(n)) / n
                w[j] = soft_threshold(zj, lam) / aj
        # Ridge：min 1/(2n)‖y − Xw‖² + λ/2‖w‖²
        A = [[sum(X[i][a] * X[i][b] for i in range(n)) / n + (lam if a == b else 0.0) for b in range(d)] for a in range(d)]
        bb = [sum(X[i][a] * y[i] for i in range(n)) / n for a in range(d)]
        wr = solve(A, bb)
        rows.append({"lam": lam, "lasso_zero": sum(1 for v in w if abs(v) < 1e-10), "ridge_zero": sum(1 for v in wr if abs(v) < 1e-10),
                     "lasso_noise": max(abs(v) for v in w[3:]), "ridge_noise": max(abs(v) for v in wr[3:]), "lasso_w0": w[0], "ridge_w0": wr[0]})
        print(f"[E2] λ = {lam:5.2f}：Lasso 零系数 {rows[-1]['lasso_zero']} 个（噪声特征最大 |w| {rows[-1]['lasso_noise']:.3f}，w₀ = {w[0]:.2f}）| Ridge 零系数 {rows[-1]['ridge_zero']} 个（噪声特征最大 |w| {rows[-1]['ridge_noise']:.3f}，w₀ = {wr[0]:.2f}）")
    return {"rows": rows}


def e3(rng):
    L, W = 20, 32
    acts = {"sigmoid": (lambda z: 1 / (1 + math.exp(-z)), lambda a: a * (1 - a), 1.0),
            "tanh": (math.tanh, lambda a: 1 - a * a, 1.0),
            "relu": (lambda z: max(0.0, z), lambda a: 1.0 if a > 0 else 0.0, 2.0)}
    out = {}
    for name, resid, rs in [("sigmoid", False, 1.0), ("tanh", False, 1.0), ("relu", False, 1.0), ("relu", True, 1.0), ("relu", True, 0.2)]:
        f, df, gain = acts[name]
        r = random.Random(3)
        Ws = [[[r.gauss(0, math.sqrt(gain / W)) for _ in range(W)] for _ in range(W)] for _ in range(L)]   # Xavier / He 初始化
        x = [r.gauss(0, 1) for _ in range(W)]
        hs, zs = [x], []
        for l in range(L):
            h = hs[-1]
            z = [sum(Ws[l][i][j] * h[j] for j in range(W)) for i in range(W)]
            a = [f(v) for v in z]
            if resid:
                a = [rs * ai + hi for ai, hi in zip(a, h)]       # 残差：h + rs·f(Wh)，rs < 1 相当于 ReZero / Fixup 式的分支缩放
            zs.append(z)
            hs.append(a)
        g = [1.0 / math.sqrt(W)] * W                  # 损失对最后一层输出的梯度（固定方向）
        norms = [0.0] * L
        for l in range(L - 1, -1, -1):
            h_in, a_out = hs[l], hs[l + 1]
            fa = [f(v) for v in zs[l]]
            gz = [gi * df(ai) * (rs if resid else 1.0) for gi, ai in zip(g, fa)]
            norms[l] = math.sqrt(sum((gz[i] * h_in[j]) ** 2 for i in range(W) for j in range(W)))     # ‖∂L/∂W_l‖
            gin = [sum(Ws[l][i][j] * gz[i] for i in range(W)) for j in range(W)]
            if resid:
                gin = [a + b for a, b in zip(gin, g)]
            g = gin
        key = name + (("_res" if rs == 1.0 else "_res02") if resid else "")
        hn = math.sqrt(sum(v * v for v in hs[-1]) / W)
        out[key] = {"pts": [[l + 1, max(nv, 1e-30)] for l, nv in enumerate(norms)], "first": norms[0], "last": norms[-1], "ratio": norms[0] / norms[-1], "hrms": hn}
        print(f"[E3] {key:11s}：第 1 层梯度范数 {norms[0]:.2e}，第 20 层 {norms[-1]:.2e}，比值 {norms[0] / norms[-1]:.2e}；最后一层激活 RMS {hn:.2e}")
    return out


def e4(rng):
    ds = [1, 4, 16, 64, 256, 1024]
    rows = []
    for d in ds:
        R = 300
        dots, pm_raw, pm_sc, H_raw, H_sc = [], 0.0, 0.0, 0.0, 0.0
        for _ in range(R):
            q = [rng.gauss(0, 1) for _ in range(d)]
            ks = [[rng.gauss(0, 1) for _ in range(d)] for _ in range(16)]
            s = [sum(a * b for a, b in zip(q, k)) for k in ks]
            dots.extend(s)
            p1, p2 = softmax(s), softmax([x / math.sqrt(d) for x in s])
            pm_raw += max(p1) / R
            pm_sc += max(p2) / R
            H_raw += -sum(x * math.log(x) for x in p1 if x > 0) / R
            H_sc += -sum(x * math.log(x) for x in p2 if x > 0) / R
        m = sum(dots) / len(dots)
        v = sum((x - m) ** 2 for x in dots) / len(dots)
        rows.append({"d": d, "var": v, "pmax_raw": pm_raw, "pmax_sc": pm_sc, "H_raw": H_raw, "H_sc": H_sc})
        print(f"[E4] d = {d:4d}：点积方差 {v:8.1f}；不缩放 softmax 最大概率 {pm_raw:.3f} 熵 {H_raw:.3f} | 除以 √d 最大概率 {pm_sc:.3f} 熵 {H_sc:.3f}（均匀分布熵 {math.log(16):.3f}）")
    return {"rows": rows, "pmax_raw": [[r["d"], r["pmax_raw"]] for r in rows], "pmax_sc": [[r["d"], r["pmax_sc"]] for r in rows],
            "H_raw": [[r["d"], r["H_raw"]] for r in rows], "H_sc": [[r["d"], r["H_sc"]] for r in rows], "Hmax": math.log(16)}


def e5(rng):
    x = [1.0, 2.0, 3.0, 4.0]
    others_a = [[rng.gauss(0, 1) for _ in range(4)] for _ in range(7)]
    others_b = [[rng.gauss(5, 3) for _ in range(4)] for _ in range(7)]
    bn_a = batch_norm_train([x] + others_a)[0]
    bn_b = batch_norm_train([x] + others_b)[0]
    ln = layer_norm(x)
    diff_bn = max(abs(a - b) for a, b in zip(bn_a, bn_b))
    print(f"[E5] 同一个样本 x = {x}：BN（batch A）→ {[round(v, 3) for v in bn_a]}，BN（batch B）→ {[round(v, 3) for v in bn_b]}，最大差 {diff_bn:.3f}；LN → {[round(v, 3) for v in ln]}（与 batch 无关）")
    one = batch_norm_train([x])[0]
    print(f"     batch 大小为 1 时 BN 输出 {[round(v, 3) for v in one]}（方差为 0，信息全部丢失）")
    return {"bn_a": bn_a, "bn_b": bn_b, "ln": ln, "diff": diff_bn, "one": one}


def e6(rng):
    scores, labels = [], []
    for _ in range(400):
        y = 1 if rng.random() < 0.3 else 0
        scores.append(round(rng.gauss(1.0 if y else 0.0, 1.0), 1))      # 保留一位小数，制造平局
        labels.append(y)
    a1 = auc_by_rank(scores, labels)
    pos = [s for s, y in zip(scores, labels) if y]
    neg = [s for s, y in zip(scores, labels) if not y]
    a2 = sum((1.0 if p > q else 0.5 if p == q else 0.0) for p in pos for q in neg) / (len(pos) * len(neg))
    th = sorted(set(scores), reverse=True)
    roc = [(0.0, 0.0)]
    for t in th:
        tp = sum(1 for p in pos if p >= t) / len(pos)
        fp = sum(1 for q in neg if q >= t) / len(neg)
        roc.append((fp, tp))
    a3 = sum((roc[i][0] - roc[i - 1][0]) * (roc[i][1] + roc[i - 1][1]) / 2 for i in range(1, len(roc)))
    th_auc = 0.5 * (1 + math.erf(1.0 / 2))      # 两个单位方差高斯均值差 1：Φ(1/√2)
    print(f"[E6] AUC：秩和公式 {a1:.6f}，两两比较 {a2:.6f}，ROC 梯形面积 {a3:.6f}（理论值 Φ(1/√2) = {th_auc:.4f}）")
    return {"rank": a1, "pair": a2, "trap": a3, "theory": th_auc, "roc": [[a, b] for a, b in roc]}


def main():
    rng = random.Random(2)
    out = {"e1": e1(rng), "e2": e2(rng), "e3": e3(rng), "e4": e4(rng), "e5": e5(rng), "e6": e6(rng)}
    # 自检：softmax + CE 梯度 vs 有限差分
    z, y, h = [0.5, -1.2, 2.0, 0.1], 2, 1e-6

    def ce(v):
        m = max(v)
        return m + math.log(sum(math.exp(t - m) for t in v)) - v[y]
    g = softmax_ce_grad(z, y)
    err = max(abs((ce([t + (h if i == k else 0) for i, t in enumerate(z)]) - ce([t - (h if i == k else 0) for i, t in enumerate(z)])) / (2 * h) - g[k]) for k in range(4))
    _, w = scaled_dot_attention([1.0, 0.0], [[1.0, 0.0], [0.0, 1.0]], [[1.0], [0.0]])
    print(f"[自检] softmax+CE 梯度 p − y 与有限差分最大差 {err:.1e}；梯度和 = {sum(g):.1e}；2 维注意力权重 {[round(t, 4) for t in w]}")
    out["ce_err"] = err
    (pathlib.Path(__file__).parent / "ch02_output.json").write_text(json.dumps(out, ensure_ascii=False), encoding="utf-8")


if __name__ == "__main__":
    main()
