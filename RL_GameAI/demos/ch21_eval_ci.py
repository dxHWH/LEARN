"""第 21 章实测：AI 评测的统计学（纯 Python，固定随机种子）。
  E1 胜率置信区间的覆盖率（精确计算，不是模拟）：Wald / Wilson / Clopper-Pearson，n = 10…1000，p = 0.5 / 0.9
  E2 要打多少局：区间半宽 vs 局数；检测“胜率 50% → 50% + Δ”所需局数（α = 0.05 双侧，功效 80%）的公式与模拟验证
  E3 “边打边看”的陷阱：每 20 局检查一次、显著就停 → 假阳性率；SPRT（序贯概率比检验）的假阳性率、功效与平均局数
  E4 比较新旧版本：独立抽种子 vs 公共种子 + 双方换边（配对设计），胜率差估计的标准差与“等效局数”
书中代码块与本文件逐字一致。输出：stdout + ch21_output.json。
"""
import json
import math
import pathlib
import random


# ---------------------------------------------------------------- 书中代码块（开始）
def z_quantile(p):
    """标准正态分位数（Acklam 有理逼近，相对误差 < 1.2e-9）。"""
    a = [-3.969683028665376e+01, 2.209460984245205e+02, -2.759285104469687e+02, 1.383577518672690e+02, -3.066479806614716e+01, 2.506628277459239e+00]
    b = [-5.447609879822406e+01, 1.615858368580409e+02, -1.556989798598866e+02, 6.680131188771972e+01, -1.328068155288572e+01]
    c = [-7.784894002430293e-03, -3.223964580411365e-01, -2.400758277161838e+00, -2.549732539343734e+00, 4.374664141464968e+00, 2.938163982698783e+00]
    d = [7.784695709041462e-03, 3.224671290700398e-01, 2.445134137142996e+00, 3.754408661907416e+00]
    if p < 0.02425:
        q = math.sqrt(-2 * math.log(p))
        return (((((c[0] * q + c[1]) * q + c[2]) * q + c[3]) * q + c[4]) * q + c[5]) / ((((d[0] * q + d[1]) * q + d[2]) * q + d[3]) * q + 1)
    if p > 1 - 0.02425:
        return -z_quantile(1 - p)
    q = p - 0.5
    r = q * q
    return (((((a[0] * r + a[1]) * r + a[2]) * r + a[3]) * r + a[4]) * r + a[5]) * q / (((((b[0] * r + b[1]) * r + b[2]) * r + b[3]) * r + b[4]) * r + 1)


def wald_interval(k, n, alpha=0.05):
    z = z_quantile(1 - alpha / 2)
    p = k / n
    h = z * math.sqrt(p * (1 - p) / n)
    return max(0.0, p - h), min(1.0, p + h)


def wilson_interval(k, n, alpha=0.05):
    """反解 |p̂ − p| ≤ z·sqrt(p(1−p)/n)（方差用未知的 p 而不是 p̂），小样本和极端胜率下比 Wald 可靠得多。"""
    z = z_quantile(1 - alpha / 2)
    p = k / n
    den = 1 + z * z / n
    centre = (p + z * z / (2 * n)) / den
    h = z * math.sqrt(p * (1 - p) / n + z * z / (4 * n * n)) / den
    return centre - h, centre + h


def games_needed(delta, p0=0.5, alpha=0.05, power=0.8):
    """单样本检验 H0: p = p0 vs H1: p = p0 + delta（双侧 α、功效 1−β）所需局数。"""
    za, zb = z_quantile(1 - alpha / 2), z_quantile(power)
    p1 = p0 + delta
    return math.ceil(((za * math.sqrt(p0 * (1 - p0)) + zb * math.sqrt(p1 * (1 - p1))) / delta) ** 2)


def sprt_step(llr, win, p0, p1, alpha=0.05, beta=0.05):
    """序贯概率比检验：每局累加对数似然比，越过上界接受 H1，越过下界接受 H0，否则继续。"""
    llr += math.log(p1 / p0) if win else math.log((1 - p1) / (1 - p0))
    upper, lower = math.log((1 - beta) / alpha), math.log(beta / (1 - alpha))
    return llr, ("H1" if llr >= upper else "H0" if llr <= lower else None)
# ---------------------------------------------------------------- 书中代码块（结束）


def betainc(a, b, x):
    """正则化不完全 Beta 函数 I_x(a, b)（Lentz 连分式），用于 Clopper-Pearson 精确区间。"""
    if x <= 0:
        return 0.0
    if x >= 1:
        return 1.0
    lbt = math.lgamma(a + b) - math.lgamma(a) - math.lgamma(b) + a * math.log(x) + b * math.log(1 - x)
    if x > (a + 1) / (a + b + 2):
        return 1.0 - betainc(b, a, 1 - x)
    tiny = 1e-300
    c, d = 1.0, 1.0 - (a + b) * x / (a + 1)
    d = 1.0 / (d if abs(d) > tiny else tiny)
    f = d
    for m in range(1, 300):
        m2 = 2 * m
        aa = m * (b - m) * x / ((a + m2 - 1) * (a + m2))
        d = 1 + aa * d
        d = 1 / (d if abs(d) > tiny else tiny)
        c = 1 + aa / c
        c = c if abs(c) > tiny else tiny
        f *= d * c
        aa = -(a + m) * (a + b + m) * x / ((a + m2) * (a + m2 + 1))
        d = 1 + aa * d
        d = 1 / (d if abs(d) > tiny else tiny)
        c = 1 + aa / c
        c = c if abs(c) > tiny else tiny
        delta = d * c
        f *= delta
        if abs(delta - 1) < 1e-14:
            break
    return math.exp(lbt) * f / a


def beta_ppf(q, a, b):
    lo, hi = 0.0, 1.0
    for _ in range(60):
        mid = (lo + hi) / 2
        if betainc(a, b, mid) < q:
            lo = mid
        else:
            hi = mid
    return (lo + hi) / 2


def clopper_pearson(k, n, alpha=0.05):
    lo = 0.0 if k == 0 else beta_ppf(alpha / 2, k, n - k + 1)
    hi = 1.0 if k == n else beta_ppf(1 - alpha / 2, k + 1, n - k)
    return lo, hi


def binom_pmf_all(n, p):
    lp, lq = math.log(p), math.log(1 - p)
    return [math.exp(math.lgamma(n + 1) - math.lgamma(k + 1) - math.lgamma(n - k + 1) + k * lp + (n - k) * lq) for k in range(n + 1)]


def e1():
    ns = [10, 20, 30, 50, 75, 100, 150, 200, 300, 500, 1000]
    out = {}
    for p in (0.5, 0.9):
        rows = []
        for n in ns:
            pmf = binom_pmf_all(n, p)
            cov = {}
            for name, f in (("wald", wald_interval), ("wilson", wilson_interval), ("cp", clopper_pearson)):
                cov[name] = sum(pm for k, pm in enumerate(pmf) if f(k, n)[0] <= p <= f(k, n)[1])
            rows.append({"n": n, **cov})
            print(f"[E1] p = {p}，n = {n:4d}：95% 区间的实际覆盖率 Wald {cov['wald']:.3f} | Wilson {cov['wilson']:.3f} | Clopper-Pearson {cov['cp']:.3f}")
        out[str(p)] = {"rows": rows, **{k: [[r["n"], r[k]] for r in rows] for k in ("wald", "wilson", "cp")}}
    # 例子：100 局赢 60 局
    w1, w2, w3 = wald_interval(60, 100), wilson_interval(60, 100), clopper_pearson(60, 100)
    w4 = wald_interval(19, 20)
    print(f"[E1] 100 局赢 60 局：Wald [{w1[0]:.3f}, {w1[1]:.3f}]，Wilson [{w2[0]:.3f}, {w2[1]:.3f}]，CP [{w3[0]:.3f}, {w3[1]:.3f}]；20 局赢 19 局的 Wald 上界 {w4[1]:.3f}")
    out["ex"] = {"wald": w1, "wilson": w2, "cp": w3, "wald19": wald_interval(19, 20), "wilson19": wilson_interval(19, 20)}
    return out


def e2(rng):
    half = [[n, 1.96 * math.sqrt(0.25 / n)] for n in [20, 50, 100, 200, 400, 1000, 2000, 5000, 10000]]
    deltas = [0.01, 0.02, 0.03, 0.05, 0.08, 0.10]
    rows = []
    for d in deltas:
        n = games_needed(d)
        # 模拟验证功效：真实胜率 0.5 + d，打 n 局，双侧检验 |p̂ − 0.5| / sqrt(0.25/n) > 1.96
        R = 2000 if n < 5000 else 400
        hits = 0
        for _ in range(R):
            k = sum(1 for _ in range(n) if rng.random() < 0.5 + d)
            hits += abs(k / n - 0.5) / math.sqrt(0.25 / n) > 1.96
        rows.append({"delta": d, "n": n, "power": hits / R, "R": R})
        print(f"[E2] 胜率 50% → {50 + 100 * d:.0f}%：需要 {n} 局（模拟 {R} 次，实际功效 {hits / R:.3f}）")
    for n, h in half:
        print(f"[E2] {n:5d} 局：胜率 50% 时 95% 区间半宽 ±{100 * h:.1f}%")
    return {"half": half, "rows": rows}


def e3(rng):
    R, MAXN, STEP = 4000, 1000, 20
    fp = 0
    stop_at = []
    for _ in range(R):
        k = 0
        for n in range(1, MAXN + 1):
            k += rng.random() < 0.5
            if n % STEP == 0 and abs(k / n - 0.5) / math.sqrt(0.25 / n) > 1.96:
                fp += 1
                stop_at.append(n)
                break
    fixed_fp = 0
    for _ in range(R):
        k = sum(1 for _ in range(MAXN) if rng.random() < 0.5)
        fixed_fp += abs(k / MAXN - 0.5) / math.sqrt(0.25 / MAXN) > 1.96
    print(f"[E3] 真实胜率 50%，最多打 {MAXN} 局：固定打满再检验的假阳性率 {fixed_fp / R:.3f}；每 {STEP} 局看一次、显著就停的假阳性率 {fp / R:.3f}")
    # SPRT：H0 p = 0.5，H1 p = 0.55
    res = {}
    for truth in (0.5, 0.55, 0.6):
        acc1, lens = 0, []
        for _ in range(R):
            llr, n, dec = 0.0, 0, None
            while dec is None and n < 5000:
                n += 1
                llr, dec = sprt_step(llr, rng.random() < truth, 0.5, 0.55)
            acc1 += dec == "H1"
            lens.append(n)
        lens.sort()
        res[str(truth)] = {"h1": acc1 / R, "mean": sum(lens) / R, "median": lens[R // 2], "p90": lens[int(R * 0.9)]}
        print(f"[E3] SPRT（H0: 50% vs H1: 55%，α = β = 0.05），真实胜率 {truth}：判为 H1 的比例 {acc1 / R:.3f}，平均 {sum(lens) / R:.0f} 局（中位数 {lens[R // 2]}，90% 分位 {lens[int(R * 0.9)]}）")
    n_fixed = games_needed(0.05, alpha=0.10, power=0.95)      # 双侧 0.10 = 单侧 0.05，与 SPRT 的设定一致
    print(f"[E3] 对照：固定局数的单侧检验（α = 0.05、50% vs 55% 的功效 95%）需要 {n_fixed} 局")
    return {"fp_peek": fp / R, "fp_fixed": fixed_fp / R, "R": R, "step": STEP, "maxn": MAXN, "sprt": res, "n_fixed": n_fixed}


def e4(rng):
    sig = lambda x: 1 / (1 + math.exp(-x))
    theta_old, theta_new, side_adv, seed_sd = 0.0, 0.2, 0.8, 1.5
    N, R = 200, 3000                       # 每个版本 N 局
    est_ind, est_pair = [], []
    for _ in range(R):
        # (a) 独立：每局随机抽种子（地图 / 开局随机性）和阵营
        w_new = sum(rng.random() < sig(theta_new + side_adv * rng.choice((-1, 1)) + rng.gauss(0, seed_sd)) for _ in range(N))
        w_old = sum(rng.random() < sig(theta_old + side_adv * rng.choice((-1, 1)) + rng.gauss(0, seed_sd)) for _ in range(N))
        est_ind.append((w_new - w_old) / N)
        # (b) 配对：N/2 个种子，每个种子两个版本都打、每个版本两边各打一次
        wn = wo = 0
        for _ in range(N // 2):
            u = rng.gauss(0, seed_sd)
            for s in (-1, 1):
                wn += rng.random() < sig(theta_new + side_adv * s + u)
                wo += rng.random() < sig(theta_old + side_adv * s + u)
        est_pair.append((wn - wo) / N)

    def sd(v):
        m = sum(v) / len(v)
        return math.sqrt(sum((x - m) ** 2 for x in v) / len(v)), m
    s1, m1 = sd(est_ind)
    s2, m2 = sd(est_pair)
    print(f"[E4] 新版本真实优势 θ = 0.2（logit），每个版本 {N} 局：独立设计 Δ 估计均值 {m1:.4f}、标准差 {s1:.4f}；配对设计（公共种子 + 换边）均值 {m2:.4f}、标准差 {s2:.4f}")
    print(f"     方差比 {s1 * s1 / (s2 * s2):.2f} → 独立设计要打约 {N * s1 * s1 / (s2 * s2):.0f} 局才能达到配对设计 {N} 局的精度")
    return {"sd_ind": s1, "sd_pair": s2, "m_ind": m1, "m_pair": m2, "ratio": s1 * s1 / (s2 * s2), "N": N, "R": R, "equiv": N * s1 * s1 / (s2 * s2)}


def main():
    rng = random.Random(21)
    out = {"e1": e1(), "e2": e2(rng), "e3": e3(rng), "e4": e4(rng)}
    print(f"[自检] z_0.975 = {z_quantile(0.975):.6f}；I_0.3(2, 5) = {betainc(2, 5, 0.3):.6f}（精确值 0.579825）")
    (pathlib.Path(__file__).parent / "ch21_output.json").write_text(json.dumps(out, ensure_ascii=False), encoding="utf-8")


if __name__ == "__main__":
    main()
