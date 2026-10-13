// 第 11 章实测：信任域与 PPO
//   E1 表格 softmax 策略（5 状态 × 3 动作，γ=0.9）：
//      (a) 性能差异引理精确验证；替代目标 L_π(π′)（用旧状态分布）随步长的误差
//      (b) 自然梯度 = 优势（Kakade）：共轭梯度解 F x = g 与 A(s,·) 的比较
//      (c) TRPO 步长 β = sqrt(2δ / gᵀF⁻¹g)：实际 KL vs δ；与同样 KL 下普通梯度步的 J 提升对比
//   E2 KL 估计量 k1 = −log r、k2 = ½(log r)²、k3 = (r−1) − log r 的偏差与标准差（r = π_new / π_old，按 π_old 采样）
//   E3 CartPole 上的 PPO 与消融：标准 / 去 clip / 去优势归一化 / λ=0 / λ=1 / 只跑 1 个 epoch，各 5 个种子并行；
//      记录标准 PPO 与去 clip 的训练诊断（approx KL、clipfrac、熵、explained variance）
// 输出：stdout + ch11_output.json
// 编译：cl /nologo /std:c++latest /EHsc /O2 /utf-8 ch11_ppo.cpp
#include "rlcommon.h"
using namespace std;

// ================================================================ E1 表格情形
const int S = 5, A = 3;
const double G = 0.9;
struct MDP { double P[S][A][S]; double R[S][A]; double rho[S]; };
static MDP make_mdp() {
    MDP m; Rng r(9);
    for (int s = 0; s < S; ++s) {
        m.rho[s] = 1.0 / S;
        for (int a = 0; a < A; ++a) {
            double z = 0; for (int t = 0; t < S; ++t) { m.P[s][a][t] = pow(r.u(), 2.0); z += m.P[s][a][t]; }
            for (int t = 0; t < S; ++t) m.P[s][a][t] /= z;
            m.R[s][a] = r.uni(-1, 1);
        }
    }
    return m;
}
static vector<double> solve(vector<vector<double>> M, vector<double> b) {
    int n = (int)b.size();
    for (int c = 0; c < n; ++c) {
        int p = c; for (int i = c + 1; i < n; ++i) if (fabs(M[i][c]) > fabs(M[p][c])) p = i;
        swap(M[c], M[p]); swap(b[c], b[p]);
        for (int i = c + 1; i < n; ++i) { double f = M[i][c] / M[c][c]; for (int j = c; j < n; ++j) M[i][j] -= f * M[c][j]; b[i] -= f * b[c]; }
    }
    vector<double> x(n);
    for (int i = n - 1; i >= 0; --i) { double s = b[i]; for (int j = i + 1; j < n; ++j) s -= M[i][j] * x[j]; x[i] = s / M[i][i]; }
    return x;
}
struct Ev { vector<vector<double>> pi, Q, Ad; vector<double> V, d; double J; };
static Ev evalp(const MDP& m, const vector<double>& th) {
    Ev e; e.pi.assign(S, vector<double>(A)); e.Q = e.pi; e.Ad = e.pi;
    for (int s = 0; s < S; ++s) e.pi[s] = softmax(vector<double>(th.begin() + s * A, th.begin() + s * A + A));
    vector<vector<double>> M(S, vector<double>(S)); vector<double> rp(S, 0);
    for (int s = 0; s < S; ++s) { for (int t = 0; t < S; ++t) M[s][t] = (s == t); for (int a = 0; a < A; ++a) { rp[s] += e.pi[s][a] * m.R[s][a]; for (int t = 0; t < S; ++t) M[s][t] -= G * e.pi[s][a] * m.P[s][a][t]; } }
    e.V = solve(M, rp);
    for (int s = 0; s < S; ++s) for (int a = 0; a < A; ++a) { double q = m.R[s][a]; for (int t = 0; t < S; ++t) q += G * m.P[s][a][t] * e.V[t]; e.Q[s][a] = q; e.Ad[s][a] = q - e.V[s]; }
    e.J = 0; for (int s = 0; s < S; ++s) e.J += m.rho[s] * e.V[s];
    vector<vector<double>> MT(S, vector<double>(S)); for (int i = 0; i < S; ++i) for (int j = 0; j < S; ++j) MT[i][j] = M[j][i];
    e.d = solve(MT, vector<double>(m.rho, m.rho + S));   // 未归一化折扣访问分布，总和 1/(1−γ)
    return e;
}
static double meankl(const Ev& a, const Ev& b) {   // Σ_s d̄_a(s) KL(π_a(·|s) ‖ π_b(·|s))，d̄ 归一化
    double k = 0; for (int s = 0; s < S; ++s) { double ks = 0; for (int x = 0; x < A; ++x) ks += a.pi[s][x] * log(a.pi[s][x] / b.pi[s][x]); k += a.d[s] * (1 - G) * ks; }
    return k;
}
static JObj e1() {
    MDP m = make_mdp(); Rng r(21);
    vector<double> th(S * A); for (auto& x : th) x = r.uni(-1.5, 1.5);
    Ev e = evalp(m, th);
    // 梯度 g = Σ d(s) π (e_a − π) A，Fisher F = Σ d̄(s) [diag π − π πᵀ]（按归一化 d̄，与 meankl 一致）
    vector<double> g(S * A, 0);
    for (int s = 0; s < S; ++s) for (int b = 0; b < A; ++b) g[s * A + b] = e.d[s] * e.pi[s][b] * e.Ad[s][b];
    auto Fmul = [&](const vector<double>& x) {
        vector<double> y(S * A, 0);
        for (int s = 0; s < S; ++s) {
            double px = 0; for (int b = 0; b < A; ++b) px += e.pi[s][b] * x[s * A + b];
            for (int b = 0; b < A; ++b) y[s * A + b] = e.d[s] * (1 - G) * (e.pi[s][b] * x[s * A + b] - e.pi[s][b] * px) + 1e-10 * x[s * A + b];
        }
        return y;
    };
    // 共轭梯度解 F x = g（只用 Fisher-向量积，这正是 TRPO 的做法）
    vector<double> x(S * A, 0), rr = g, p = g; double rs = 0; for (double v : rr) rs += v * v;
    int it = 0;
    for (; it < 200 && rs > 1e-24; ++it) {
        auto Ap = Fmul(p); double pAp = 0; for (int i = 0; i < S * A; ++i) pAp += p[i] * Ap[i];
        double al = rs / pAp; for (int i = 0; i < S * A; ++i) { x[i] += al * p[i]; rr[i] -= al * Ap[i]; }
        double rs2 = 0; for (double v : rr) rs2 += v * v; for (int i = 0; i < S * A; ++i) p[i] = rr[i] + rs2 / rs * p[i]; rs = rs2;
    }
    // 自然梯度应等于 A(s,·)/(1−γ)（每个状态内可差一个常数）：去掉状态内均值后比较
    double nat_err = 0;
    for (int s = 0; s < S; ++s) {
        double mx = 0, ma = 0; for (int b = 0; b < A; ++b) { mx += x[s * A + b] / A; ma += e.Ad[s][b] / (1 - G) / A; }
        for (int b = 0; b < A; ++b) nat_err = max(nat_err, fabs((x[s * A + b] - mx) - (e.Ad[s][b] / (1 - G) - ma)));
    }
    double gFg = 0; for (int i = 0; i < S * A; ++i) gFg += g[i] * x[i];
    printf("[E1b] 共轭梯度 %d 步解 F x = g；自然梯度与 A(s,·)/(1−γ)（去掉状态内常数）最大差 %.2e\n", it, nat_err);
    // (c) TRPO 步长与普通梯度对比
    vector<double> deltas = {0.001, 0.005, 0.01, 0.03, 0.1, 0.3};
    vector<string> rows;
    for (double dl : deltas) {
        double beta = sqrt(2 * dl / gFg);
        vector<double> tn(S * A); for (int i = 0; i < S * A; ++i) tn[i] = th[i] + beta * x[i];
        Ev en = evalp(m, tn);
        double kl = meankl(e, en);
        // 普通梯度：二分步长，使实际 KL 相同
        double lo = 0, hi = 100;
        for (int k = 0; k < 200; ++k) {
            double mid = (lo + hi) / 2; vector<double> tv(S * A); for (int i = 0; i < S * A; ++i) tv[i] = th[i] + mid * g[i];
            if (meankl(e, evalp(m, tv)) < kl) lo = mid; else hi = mid;
        }
        vector<double> tv(S * A); for (int i = 0; i < S * A; ++i) tv[i] = th[i] + lo * g[i];
        Ev ev = evalp(m, tv);
        // 替代目标：用旧状态分布 d_π 和旧优势 A_π 估计改进量
        double sur = 0; for (int s = 0; s < S; ++s) for (int b = 0; b < A; ++b) sur += e.d[s] * en.pi[s][b] * e.Ad[s][b];
        printf("[E1c] δ=%.3f：TRPO 实际 KL %.5f（比值 %.3f），J 提升 %.5f，替代目标 %.5f；同 KL 普通梯度 J 提升 %.5f\n",
               dl, kl, kl / dl, en.J - e.J, sur, ev.J - e.J);
        rows.push_back(JObj().num("delta", dl).num("kl", kl, 6).num("ratio", kl / dl, 4).num("dj_nat", en.J - e.J, 6).num("sur", sur, 6).num("dj_van", ev.J - e.J, 6).str());
    }
    // (a) 性能差异引理：π′ 取若干随机方向 / 步长
    double pdl_err = 0; vector<double> steps, true_dj, sur_dj;
    vector<double> dir(S * A); for (auto& v : dir) v = r.normal();
    for (int k = 0; k <= 20; ++k) {
        double st = 0.25 * k; vector<double> tn(S * A); for (int i = 0; i < S * A; ++i) tn[i] = th[i] + st * x[i] / sqrt(gFg) + 0.3 * st * dir[i];
        Ev en = evalp(m, tn);
        double rhs = 0, sur = 0;
        for (int s = 0; s < S; ++s) for (int b = 0; b < A; ++b) { rhs += en.d[s] * en.pi[s][b] * e.Ad[s][b]; sur += e.d[s] * en.pi[s][b] * e.Ad[s][b]; }
        pdl_err = max(pdl_err, fabs((en.J - e.J) - rhs));
        steps.push_back(st); true_dj.push_back(en.J - e.J); sur_dj.push_back(sur);
    }
    printf("[E1a] 性能差异引理 J(π′)−J(π) = Σ d^{π′}(s) Σ π′(a|s) A_π(s,a)：21 个 π′ 上最大差 %.2e\n", pdl_err);
    return JObj().num("J", e.J, 8).num("cg_iters", it).num("nat_err", nat_err, 3).num("pdl_err", pdl_err, 3).add("trpo", jlist(rows))
        .add("pdl_true", jpts(steps, true_dj)).add("pdl_sur", jpts(steps, sur_dj));
}

// ================================================================ E2 KL 估计量
static JObj e2() {
    // π_old = softmax(z0)，π_new = softmax(z0 + t·u)，按 π_old 采样 a，估计 KL(π_old ‖ π_new)
    const int K = 10, N = 200000; Rng r(5);
    vector<double> z0(K), u(K); for (auto& v : z0) v = r.normal(); for (auto& v : u) v = r.normal();
    vector<double> ts = {0.1, 0.3, 0.6, 1.0, 1.5};
    vector<string> rows;
    for (double t : ts) {
        vector<double> z1(K); for (int i = 0; i < K; ++i) z1[i] = z0[i] + t * u[i];
        auto p = softmax(z0), q = softmax(z1);
        double kl = 0; for (int i = 0; i < K; ++i) kl += p[i] * log(p[i] / q[i]);
        double s[3] = {0, 0, 0}, s2[3] = {0, 0, 0}; int neg1 = 0;
        for (int n = 0; n < N; ++n) {
            int a = r.categorical(p); double ratio = q[a] / p[a], lr = log(ratio);
            double k[3] = {-lr, 0.5 * lr * lr, (ratio - 1) - lr};
            if (k[0] < 0) ++neg1;
            for (int j = 0; j < 3; ++j) { s[j] += k[j]; s2[j] += k[j] * k[j]; }
        }
        double mu[3], sd[3]; for (int j = 0; j < 3; ++j) { mu[j] = s[j] / N; sd[j] = sqrt(s2[j] / N - mu[j] * mu[j]); }
        printf("[E2] 真 KL %.4f：k1 均值 %.4f 标准差 %.4f（负值占 %.0f%%）| k2 均值 %.4f 标准差 %.4f | k3 均值 %.4f 标准差 %.4f\n",
               kl, mu[0], sd[0], 100.0 * neg1 / N, mu[1], sd[1], mu[2], sd[2]);
        rows.push_back(JObj().num("kl", kl, 5).num("k1", mu[0], 5).num("k1sd", sd[0], 4).num("k1neg", 100.0 * neg1 / N, 3)
            .num("k2", mu[1], 5).num("k2sd", sd[1], 4).num("k3", mu[2], 5).num("k3sd", sd[2], 4).str());
    }
    return JObj().add("rows", jlist(rows)).num("N", N);
}

// ================================================================ E3 PPO
static vector<double> gae(const vector<double>& r, const vector<double>& v, const vector<double>& vn,
                          const vector<int>& term, const vector<int>& trunc, double gamma, double lam) {
    int T = (int)r.size(); vector<double> adv(T); double a = 0;
    for (int t = T - 1; t >= 0; --t) {
        double d = r[t] + gamma * (1 - term[t]) * vn[t] - v[t];
        a = d + gamma * lam * (1 - (term[t] || trunc[t])) * a; adv[t] = a;
    }
    return adv;
}
struct PpoCfg { const char* name; double clip = 0.2; bool advnorm = true; double lam = 0.95; int epochs = 10; };
struct PpoOut { vector<double> step, ret, kl, cf, ent, ev; double final = 0; };
static PpoOut ppo(const PpoCfg& cfg, int seed) {
    const int NE = 8, NS = 128, total = 200000, MB = 256; const double gamma = 0.99, lr = 3e-4, vcoef = 0.5;
    Rng rng(seed * 1009 + 3);
    MLP pi({4, 64, 64, 2}, rng, 0.01), vf({4, 64, 64, 1}, rng);
    vector<CartPole> env(NE); vector<vector<double>> obs(NE);
    for (int e = 0; e < NE; ++e) obs[e] = env[e].reset(rng);
    PpoOut o; double acc = 0; int accn = 0; int steps = 0, next_log = 10000;
    const int N = NE * NS;
    while (steps < total) {
        vector<vector<double>> Sx(N); vector<int> Ax(N), term(N), trunc(N); vector<double> R(N), Vv(N), Vn(N), LP(N);
        for (int t = 0; t < NS; ++t) for (int e = 0; e < NE; ++e) {
            int i = e * NS + t;
            Sx[i] = obs[e];
            auto p = softmax(pi.forward(obs[e]));
            int a = rng.u() < p[1] ? 1 : 0; Ax[i] = a; LP[i] = log(p[a]);
            Vv[i] = vf.forward(obs[e])[0];
            bool te, tu; R[i] = env[e].step(a, te, tu); term[i] = te; trunc[i] = tu;
            auto o2 = env[e].obs();
            Vn[i] = te ? 0.0 : vf.forward(o2)[0];
            if (te || tu) { acc += env[e].t; ++accn; obs[e] = env[e].reset(rng); } else obs[e] = o2;
            ++steps;
        }
        vector<double> ADV(N), RET(N);
        for (int e = 0; e < NE; ++e) {
            auto sl = [&](auto& v) { return vector<typename decay_t<decltype(v)>::value_type>(v.begin() + e * NS, v.begin() + (e + 1) * NS); };
            auto adv = gae(sl(R), sl(Vv), sl(Vn), sl(term), sl(trunc), gamma, cfg.lam);
            for (int t = 0; t < NS; ++t) { ADV[e * NS + t] = adv[t]; RET[e * NS + t] = adv[t] + Vv[e * NS + t]; }
        }
        // explained variance（更新前的 critic）
        { vector<double> res(N); for (int i = 0; i < N; ++i) res[i] = RET[i] - Vv[i]; double vr = stdev(RET); o.ev.push_back(vr > 0 ? 1 - pow(stdev(res), 2) / (vr * vr) : 0); }
        vector<double> An = ADV;
        if (cfg.advnorm) { double mu = mean(An), sd = stdev(An) + 1e-8; for (auto& x : An) x = (x - mu) / sd; }
        vector<int> idx(N); iota(idx.begin(), idx.end(), 0);
        double kl = 0, cf = 0, ent = 0;
        for (int ep = 0; ep < cfg.epochs; ++ep) {
            for (int i = N - 1; i > 0; --i) swap(idx[i], idx[rng.below(i + 1)]);
            bool last = ep == cfg.epochs - 1; if (last) { kl = cf = ent = 0; }
            for (int b0 = 0; b0 < N; b0 += MB) {
                for (int j = b0; j < b0 + MB; ++j) {
                    int i = idx[j];
                    auto p = softmax(pi.forward(Sx[i]));
                    double lpn = log(p[Ax[i]]), ratio = exp(lpn - LP[i]), A_ = An[i];
                    // PPO-clip：只有“未被 clip 截住”的样本有梯度
                    bool active = A_ >= 0 ? ratio < 1 + cfg.clip : ratio > 1 - cfg.clip;
                    vector<double> gz(2, 0.0);
                    if (active) for (int k = 0; k < 2; ++k) gz[k] = -A_ * ratio * ((k == Ax[i]) - p[k]);
                    pi.backward(gz);
                    double vv = vf.forward(Sx[i])[0];
                    vf.backward({vcoef * 2 * (vv - RET[i])});
                    if (last) {
                        kl += (ratio - 1) - log(ratio); cf += fabs(ratio - 1) > cfg.clip;
                        for (double x : p) ent -= x * log(max(x, 1e-12));
                    }
                }
                pi.adam(lr, 1.0 / MB, 0.5); vf.adam(lr, 1.0 / MB, 0.5);
            }
        }
        o.kl.push_back(kl / N); o.cf.push_back(cf / N); o.ent.push_back(ent / N);
        while (steps >= next_log) { o.step.push_back(next_log); o.ret.push_back(accn ? acc / accn : (o.ret.empty() ? 0 : o.ret.back())); acc = 0; accn = 0; next_log += 10000; }
    }
    double f = 0; for (size_t i = o.ret.size() - 5; i < o.ret.size(); ++i) f += o.ret[i] / 5; o.final = f;
    return o;
}
static JObj e3() {
    vector<PpoCfg> cfgs(6);
    cfgs[0].name = "标准 PPO";
    cfgs[1].name = "去 clip"; cfgs[1].clip = 1e9;
    cfgs[2].name = "去优势归一化"; cfgs[2].advnorm = false;
    cfgs[3].name = "λ = 0"; cfgs[3].lam = 0.0;
    cfgs[4].name = "λ = 1"; cfgs[4].lam = 1.0;
    cfgs[5].name = "只跑 1 个 epoch"; cfgs[5].epochs = 1;
    const int C = 6, SEEDS = 5;
    vector<PpoOut> res(C * SEEDS);
    Timer tm;
    parallel_for(C * SEEDS, [&](int i) { res[i] = ppo(cfgs[i / SEEDS], i % SEEDS + 1); });
    vector<string> out;
    for (int c = 0; c < C; ++c) {
        size_t n = res[c * SEEDS].step.size(), nu = res[c * SEEDS].kl.size();
        vector<double> mu(n, 0), kl(nu, 0), cf(nu, 0), ent(nu, 0), ev(nu, 0), fin, ux(nu);
        for (size_t u = 0; u < nu; ++u) ux[u] = (u + 1) * 1024.0;
        for (int sd = 0; sd < SEEDS; ++sd) {
            auto& r = res[c * SEEDS + sd]; fin.push_back(r.final);
            for (size_t i = 0; i < n; ++i) mu[i] += r.ret[i] / SEEDS;
            for (size_t u = 0; u < nu; ++u) { kl[u] += r.kl[u] / SEEDS; cf[u] += r.cf[u] / SEEDS; ent[u] += r.ent[u] / SEEDS; ev[u] += r.ev[u] / SEEDS; }
        }
        int hit = -1; for (size_t i = 0; i < n; ++i) if (mu[i] >= 450) { hit = (int)res[0].step[i]; break; }
        double klmax = *max_element(kl.begin(), kl.end()), klmean = mean(kl);
        printf("[E3] %-14s 最后 5 万步平均回合长度 %.1f（种子 %.0f/%.0f/%.0f/%.0f/%.0f），平均首次 ≥450 在 %d 步；approx KL 平均 %.4f 最大 %.4f\n",
               cfgs[c].name, mean(fin), fin[0], fin[1], fin[2], fin[3], fin[4], hit, klmean, klmax);
        JObj j; j.add("name", js(cfgs[c].name)).add("mean", jpts(res[0].step, mu, 5)).num("final", mean(fin), 4).add("fin", ja(fin, 4)).num("hit", hit)
            .num("klmean", klmean, 4).num("klmax", klmax, 4);
        // 诊断曲线每 4 次更新取一个点
        vector<double> sx, skl, scf, sen, sev;
        for (size_t u = 0; u < nu; u += 4) { sx.push_back(ux[u]); skl.push_back(kl[u]); scf.push_back(cf[u]); sen.push_back(ent[u]); sev.push_back(ev[u]); }
        j.add("kl", jpts(sx, skl, 5)).add("cf", jpts(sx, scf, 5)).add("ent", jpts(sx, sen, 5)).add("ev", jpts(sx, sev, 5));
        out.push_back(j.str());
    }
    printf("[E3] 30 个训练并行，耗时 %.1f s\n", tm.sec());
    return JObj().add("runs", jlist(out));
}

int main() {
    JObj root;
    root.add("e1", e1().str());
    root.add("e2", e2().str());
    root.add("e3", e3().str());
    write_file("ch11_output.json", root.str());
    printf("写出 ch11_output.json\n");
}
