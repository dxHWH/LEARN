// 第 12 章实测：连续控制与最大熵 RL
//   E1 tanh 压缩高斯的 log-prob 修正：解析式 log μ(u) − log(1 − tanh²u) vs 200 万样本直方图密度
//   E2 actor 利用 critic 误差导致的高估：单 critic / 另一个 critic 估值（Double 式）/ 两个取 min（TD3 截断双 Q）
//   E3 最大熵策略优化（无状态的连续老虎机，双峰奖励）：重参数化梯度 + tanh 修正，
//      α = 0 / 0.05 / 0.3 / 自动温度（目标熵 −1）四种设置，记录 μ、σ、熵、α 的轨迹与两个峰上的概率质量
// 输出：stdout + ch12_output.json
// 编译：cl /nologo /std:c++latest /EHsc /O2 /utf-8 ch12_sac.cpp
#include "rlcommon.h"
using namespace std;

static double logN(double u, double mu, double sd) { double z = (u - mu) / sd; return -0.5 * z * z - log(sd) - 0.5 * log(2 * PI); }
static double logpi_tanh(double a, double mu, double sd) { double u = atanh(a); return logN(u, mu, sd) - log(1 - a * a); }

static JObj e1() {
    const double mu = 0.4, sd = 0.9; const int N = 2000000, BINS = 40;
    Rng r(12); vector<double> cnt(BINS, 0);
    for (int i = 0; i < N; ++i) { double a = tanh(mu + sd * r.normal()); int b = (int)((a + 1) / 2 * BINS); if (b >= 0 && b < BINS) cnt[b] += 1; }
    double w = 2.0 / BINS, maxrel = 0; vector<double> xs, emp, ana, naive;
    for (int b = 2; b < BINS - 2; ++b) {   // 两端密度变化太快、直方图偏差大，略去
        double a = -1 + (b + 0.5) * w, de = cnt[b] / N / w, da = exp(logpi_tanh(a, mu, sd)), dn = exp(logN(atanh(a), mu, sd));
        maxrel = max(maxrel, fabs(de - da) / da);
        xs.push_back(a); emp.push_back(de); ana.push_back(da); naive.push_back(dn);
    }
    double integ = 0; for (int k = 0; k < 200000; ++k) { double a = -1 + (k + 0.5) * 2.0 / 200000; integ += exp(logpi_tanh(a, mu, sd)) * 2.0 / 200000; }
    double integ_naive = 0; for (int k = 0; k < 200000; ++k) { double a = -1 + (k + 0.5) * 2.0 / 200000; integ_naive += exp(logN(atanh(a), mu, sd)) * 2.0 / 200000; }
    printf("[E1] tanh 高斯 μ=%.1f σ=%.1f：解析密度 vs 直方图最大相对误差 %.3f；解析密度积分 %.6f，不做修正的“密度”积分 %.4f\n", mu, sd, maxrel, integ, integ_naive);
    return JObj().num("mu", mu).num("sd", sd).num("maxrel", maxrel, 3).num("integ", integ, 8).num("integ_naive", integ_naive, 5)
        .add("emp", jpts(xs, emp)).add("ana", jpts(xs, ana)).add("naive", jpts(xs, naive));
}

static JObj e2() {
    const int G = 401, K = 5, TR = 20000;
    vector<double> sig = {0.05, 0.1, 0.2, 0.3, 0.4};
    vector<double> b1, b2, bmin;
    Rng r(3);
    for (double sn : sig) {
        double s1 = 0, s2 = 0, sm = 0;
        for (int t = 0; t < TR; ++t) {
            double c[2][K], d[2][K];
            for (int i = 0; i < 2; ++i) for (int k = 0; k < K; ++k) { c[i][k] = r.normal(); d[i][k] = r.normal(); }
            auto noise = [&](int i, double a) { double s = 0; for (int k = 0; k < K; ++k) s += c[i][k] * cos((k + 1) * PI * a) + d[i][k] * sin((k + 1) * PI * a); return sn * s / sqrt((double)K); };
            double best = -1e9, ab = 0;
            for (int g = 0; g < G; ++g) { double a = -1 + 2.0 * g / (G - 1); double q1 = -(a - 0.3) * (a - 0.3) + noise(0, a); if (q1 > best) { best = q1; ab = a; } }
            double qt = -(ab - 0.3) * (ab - 0.3), q1 = best, q2 = qt + noise(1, ab);
            s1 += q1 - qt; s2 += q2 - qt; sm += min(q1, q2) - qt;
        }
        b1.push_back(s1 / TR); b2.push_back(s2 / TR); bmin.push_back(sm / TR);
        printf("[E2] critic 噪声 σ=%.2f：单 critic 估值偏差 %+.4f，另一 critic 估值 %+.4f，min(Q1,Q2) %+.4f\n", sn, s1 / TR, s2 / TR, sm / TR);
    }
    return JObj().add("single", jpts(sig, b1)).add("other", jpts(sig, b2)).add("min", jpts(sig, bmin))
        .num("s_last", b1.back(), 4).num("o_last", b2.back(), 4).num("m_last", bmin.back(), 4);
}

// 双峰奖励：a = 0.6 处高 1.0，a = −0.5 处高 0.8
static double rew(double a) { return exp(-(a - 0.6) * (a - 0.6) / 0.02) + 0.8 * exp(-(a + 0.5) * (a + 0.5) / 0.02); }
static double drew(double a) { return exp(-(a - 0.6) * (a - 0.6) / 0.02) * (-(a - 0.6) / 0.01) + 0.8 * exp(-(a + 0.5) * (a + 0.5) / 0.02) * (-(a + 0.5) / 0.01); }

static JObj e3() {
    struct Cfg { const char* name; double alpha; bool autoa; };
    vector<Cfg> cfgs = {{"α = 0（无熵）", 0.0, false}, {"α = 0.05", 0.05, false}, {"α = 0.3", 0.3, false}, {"自动温度（目标熵 −1）", 0.2, true}};
    const int IT = 3000, B = 256; const double lr = 0.01, lra = 0.01, Htgt = -1.0;
    vector<string> out;
    for (auto& c : cfgs) {
        double mu = -0.05, ls = log(0.6), la = log(max(c.alpha, 1e-12)), alpha = c.alpha;
        double m1 = 0, v1 = 0, m2 = 0, v2 = 0; int t = 0;
        Rng r(77);
        vector<double> it, tm, sdv, Hv, Rv, Av;
        for (int k = 1; k <= IT; ++k) {
            double sd = exp(ls), gmu = 0, gls = 0, H = 0, R = 0, lpsum = 0;
            for (int b = 0; b < B; ++b) {
                double eps = r.normal(), u = mu + sd * eps, a = tanh(u);
                double lp = logN(u, mu, sd) - log(1 - a * a + 1e-12);
                double dobj_du = drew(a) * (1 - a * a) - alpha * 2 * a;   // d/du [r(tanh u) + α·log(1 − tanh²u)]
                gmu += dobj_du; gls += dobj_du * sd * eps + alpha;          // 熵中 log σ 项的导数是 α
                H -= lp; R += rew(a); lpsum += lp;
            }
            gmu /= B; gls /= B; H /= B; R /= B;
            // Adam（梯度上升）
            ++t; double c1 = 1 - pow(0.9, t), c2 = 1 - pow(0.999, t);
            m1 = 0.9 * m1 + 0.1 * gmu; v1 = 0.999 * v1 + 0.001 * gmu * gmu; mu += lr * (m1 / c1) / (sqrt(v1 / c2) + 1e-8);
            m2 = 0.9 * m2 + 0.1 * gls; v2 = 0.999 * v2 + 0.001 * gls * gls; ls += lr * (m2 / c1) / (sqrt(v2 / c2) + 1e-8);
            ls = max(ls, log(1e-3));
            if (c.autoa) { la += lra * (lpsum / B + Htgt); alpha = exp(la); }   // α 损失 −log α·(log π + H̄) 的梯度下降
            if (k % 30 == 0) { it.push_back(k); tm.push_back(tanh(mu)); sdv.push_back(exp(ls)); Hv.push_back(H); Rv.push_back(R); Av.push_back(alpha); }
        }
        // 两个峰附近的概率质量
        double pr = 0, pl = 0; Rng r2(5); const int M = 200000;
        for (int i = 0; i < M; ++i) { double a = tanh(mu + exp(ls) * r2.normal()); if (fabs(a - 0.6) < 0.2) pr += 1; if (fabs(a + 0.5) < 0.2) pl += 1; }
        pr /= M; pl /= M;
        printf("[E3] %-22s tanh(μ)=%+.3f σ=%.3f 熵 %.3f 平均奖励 %.3f α=%.3f；右峰质量 %.2f 左峰质量 %.2f\n", c.name, tanh(mu), exp(ls), Hv.back(), Rv.back(), alpha, pr, pl);
        out.push_back(JObj().add("name", js(c.name)).add("tmu", jpts(it, tm, 5)).add("sd", jpts(it, sdv, 5)).add("H", jpts(it, Hv, 5)).add("R", jpts(it, Rv, 5)).add("alpha", jpts(it, Av, 5))
            .num("ftmu", tanh(mu), 4).num("fsd", exp(ls), 4).num("fH", Hv.back(), 4).num("fR", Rv.back(), 4).num("falpha", alpha, 4).num("pr", pr, 3).num("pl", pl, 3).num("mu", mu, 6).num("sdraw", exp(ls), 6).str());
    }
    // 奖励函数曲线
    vector<double> ax, ay; for (int i = 0; i <= 200; ++i) { double a = -0.995 + 1.99 * i / 200; ax.push_back(a); ay.push_back(rew(a)); }
    return JObj().add("runs", jlist(out)).add("reward", jpts(ax, ay));
}

int main() {
    JObj root;
    root.add("e1", e1().str());
    root.add("e2", e2().str());
    root.add("e3", e3().str());
    write_file("ch12_output.json", root.str());
    printf("写出 ch12_output.json\n");
}
