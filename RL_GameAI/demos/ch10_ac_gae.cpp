// 第 10 章实测：Actor-Critic 与 GAE
//   E1 GAE 逐步计算样例：16 步 rollout，含一次终止（t=5）、一次截断（t=11）、rollout 在 t=15 切断；
//      输出每一步的 δ、优势、critic 目标；并给出“把截断当终止”的错误版本作对比（Python 端 ch10_interview.py 对拍）
//   E2 GAE 的偏差-方差：随机 MDP（6 状态 × 3 动作，γ=0.95），精确 critic 与带误差 critic 两种情形，
//      λ 从 0 到 1，统计 t=0 处优势估计的“与梯度相关的偏差²”（去掉每个状态的平移）和方差
//   E3 CartPole 上同步 A2C（8 个环境 × 16 步），GAE λ ∈ {0, 0.9, 0.95, 1}，各 5 个种子并行
// 输出：stdout + ch10_output.json
// 编译：cl /nologo /std:c++latest /EHsc /O2 /utf-8 ch10_ac_gae.cpp
#include "rlcommon.h"
using namespace std;

// ---------------------------------------------------------------- GAE（与书中 Python 版逐行对应）
// r[t]、v[t] = V(s_t)、vnext[t] = V(s_{t+1})（截断时是“截断那一刻的观测”的价值，不是重置后的新局）
// term[t]：真正终止，不自举；end[t] = term || trunc：优势递推在这里切断
static vector<double> gae(const vector<double>& r, const vector<double>& v, const vector<double>& vnext,
                          const vector<int>& term, const vector<int>& trunc, double gamma, double lam, vector<double>* delta = nullptr) {
    int T = (int)r.size();
    vector<double> adv(T); double a = 0;
    if (delta) delta->assign(T, 0);
    for (int t = T - 1; t >= 0; --t) {
        double d = r[t] + gamma * (1 - term[t]) * vnext[t] - v[t];
        int end = term[t] || trunc[t];
        a = d + gamma * lam * (1 - end) * a;
        adv[t] = a;
        if (delta) (*delta)[t] = d;
    }
    return adv;
}

static JObj e1() {
    const int T = 16; const double gamma = 0.9, lam = 0.8;
    Rng r(10);
    vector<double> rw(T), v(T), vn(T); vector<int> term(T, 0), trunc(T, 0);
    term[5] = 1; trunc[11] = 1;
    for (int t = 0; t < T; ++t) { rw[t] = (double)(r.below(5) - 1) * 0.5; v[t] = r.uni(0, 2); }
    for (int t = 0; t < T; ++t) {
        if (term[t]) vn[t] = 0;                    // 不会被用到
        else if (trunc[t]) vn[t] = r.uni(0, 2);    // 截断那一刻观测的价值（需要单独存）
        else if (t == T - 1) vn[t] = r.uni(0, 2);  // rollout 末尾：用最后一个观测自举
        else vn[t] = v[t + 1];
    }
    vector<double> d, d_bad;
    auto adv = gae(rw, v, vn, term, trunc, gamma, lam, &d);
    auto adv0 = gae(rw, v, vn, term, trunc, gamma, 0.0);
    auto adv1 = gae(rw, v, vn, term, trunc, gamma, 1.0);
    vector<int> term_bad = term; term_bad[11] = 1;
    auto adv_bad = gae(rw, v, vn, term_bad, trunc, gamma, lam, &d_bad);
    // 检查：λ=0 等于 δ；λ=1 等于“折扣回报（截断 / 末尾自举）− V”
    double e0 = 0, e1v = 0;
    for (int t = 0; t < T; ++t) e0 = max(e0, fabs(adv0[t] - d[t]));
    for (int t = 0; t < T; ++t) {
        double g = 0, disc = 1; int k = t;
        for (;; ++k) {
            g += disc * rw[k];
            if (term[k]) break;
            if (trunc[k] || k == T - 1) { g += disc * gamma * vn[k]; break; }
            disc *= gamma;
        }
        e1v = max(e1v, fabs(adv1[t] - (g - v[t])));
    }
    printf("[E1] γ=%.2f λ=%.2f 的 16 步样例：λ=0 与 δ 最大差 %.1e；λ=1 与“回报 − V”最大差 %.1e；截断步错当终止：A[11] %.4f → %.4f\n",
           gamma, lam, e0, e1v, adv[11], adv_bad[11]);
    vector<string> rows;
    for (int t = 0; t < T; ++t)
        rows.push_back(JObj().num("t", t).num("r", rw[t], 6).num("v", v[t], 6).num("vn", vn[t], 6).num("term", term[t]).num("trunc", trunc[t])
            .num("delta", d[t], 8).num("adv", adv[t], 10).num("ret", adv[t] + v[t], 8).num("adv_bad", adv_bad[t], 8).str());
    return JObj().num("gamma", gamma).num("lam", lam).add("rows", jlist(rows)).num("err_l0", e0, 3).num("err_l1", e1v, 3)
        .add("r", ja(rw, 17)).add("v", ja(v, 17)).add("vn", ja(vn, 17)).add("term", jai(term)).add("trunc", jai(trunc)).add("adv", ja(adv, 17));
}

// ---------------------------------------------------------------- E2：随机 MDP 上 GAE 的偏差与方差
const int S = 6, A = 3;
struct MDP { double P[S][A][S]; double R[S][A]; double sig[S][A]; };
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
static JObj e2() {
    const double gamma = 0.95;
    MDP m; Rng r(12);
    for (int s = 0; s < S; ++s) for (int a = 0; a < A; ++a) {
        double z = 0; for (int t = 0; t < S; ++t) { m.P[s][a][t] = pow(r.u(), 3.0); z += m.P[s][a][t]; }
        for (int t = 0; t < S; ++t) m.P[s][a][t] /= z;
        m.R[s][a] = r.uni(-1, 1); m.sig[s][a] = 0.3;   // 奖励带 N(0, 0.3²) 噪声
    }
    vector<vector<double>> pi(S, vector<double>(A));
    for (int s = 0; s < S; ++s) { vector<double> z(A); for (auto& x : z) x = r.uni(-1, 1); pi[s] = softmax(z); }
    vector<vector<double>> M(S, vector<double>(S)); vector<double> rp(S, 0);
    for (int s = 0; s < S; ++s) { for (int t = 0; t < S; ++t) M[s][t] = (s == t); for (int a = 0; a < A; ++a) { rp[s] += pi[s][a] * m.R[s][a]; for (int t = 0; t < S; ++t) M[s][t] -= gamma * pi[s][a] * m.P[s][a][t]; } }
    auto V = solve(M, rp);
    vector<vector<double>> Adv(S, vector<double>(A));
    double vstd = stdev(V);
    for (int s = 0; s < S; ++s) for (int a = 0; a < A; ++a) { double q = m.R[s][a]; for (int t = 0; t < S; ++t) q += gamma * m.P[s][a][t] * V[t]; Adv[s][a] = q - V[s]; }
    vector<double> eps(S); for (auto& e : eps) e = r.normal();
    double es = stdev(eps); for (auto& e : eps) e *= vstd / es;    // critic 误差：标准差与 V 的标准差相同
    vector<double> lams = {0, 0.3, 0.5, 0.7, 0.8, 0.9, 0.95, 0.97, 0.99, 1.0};
    const int N = 60000, H = 250, B = 64;
    vector<string> out;
    for (int noisy = 0; noisy < 2; ++noisy) {
        vector<double> Vc(S); for (int s = 0; s < S; ++s) Vc[s] = V[s] + (noisy ? eps[s] : 0);
        vector<double> b2v, varv, msev;
        for (double lam : lams) {
            vector<vector<double>> sum(S, vector<double>(A, 0)), sq(S, vector<double>(A, 0)), cnt(S, vector<double>(A, 0));
            Rng rr(777);   // 公共随机数：每个 λ 用同一批轨迹
            for (int n = 0; n < N; ++n) {
                int s0 = n % S, s = s0; int a0 = -1;
                double adv = 0, coef = 1;
                for (int t = 0; t < H; ++t) {
                    int a = rr.categorical(pi[s]); if (t == 0) a0 = a;
                    double rew = m.R[s][a] + m.sig[s][a] * rr.normal();
                    int s2 = rr.categorical(vector<double>(m.P[s][a], m.P[s][a] + S));
                    double d = rew + gamma * Vc[s2] - Vc[s];
                    adv += coef * d; coef *= gamma * lam;
                    s = s2;
                    if (coef < 1e-12) break;
                }
                sum[s0][a0] += adv; sq[s0][a0] += adv * adv; cnt[s0][a0] += 1;
            }
            // 偏差：解析计算 E[Â | s0, a0] = Σ_t (γλ)^t E[δ_t | s0, a0]（采样只用来估方差）
            vector<double> dvec(S);   // E[δ | s] 在策略下的期望：rπ + γ Pπ Vc − Vc
            for (int s = 0; s < S; ++s) { double x = -Vc[s]; for (int a = 0; a < A; ++a) { x += pi[s][a] * m.R[s][a]; for (int t2 = 0; t2 < S; ++t2) x += gamma * pi[s][a] * m.P[s][a][t2] * Vc[t2]; } dvec[s] = x; }
            vector<vector<double>> EA(S, vector<double>(A));
            for (int s = 0; s < S; ++s) for (int a = 0; a < A; ++a) {
                double d0 = m.R[s][a] - Vc[s]; for (int t2 = 0; t2 < S; ++t2) d0 += gamma * m.P[s][a][t2] * Vc[t2];
                vector<double> dist(m.P[s][a], m.P[s][a] + S); double tot = d0, c = gamma * lam;
                for (int k = 1; k < 3000 && c > 1e-14; ++k) {
                    double ed = 0; for (int x = 0; x < S; ++x) ed += dist[x] * dvec[x];
                    tot += c * ed; c *= gamma * lam;
                    vector<double> nd(S, 0);
                    for (int x = 0; x < S; ++x) for (int b = 0; b < A; ++b) for (int y = 0; y < S; ++y) nd[y] += dist[x] * pi[x][b] * m.P[x][b][y];
                    dist = nd;
                }
                EA[s][a] = tot;
            }
            double b2 = 0, var = 0;
            for (int s = 0; s < S; ++s) {
                vector<double> e(A); double shift = 0;
                for (int a = 0; a < A; ++a) { e[a] = EA[s][a] - Adv[s][a]; shift += pi[s][a] * e[a]; }
                for (int a = 0; a < A; ++a) {
                    double mu = sum[s][a] / cnt[s][a];
                    b2 += pi[s][a] * (e[a] - shift) * (e[a] - shift) / S;
                    var += pi[s][a] * (sq[s][a] / cnt[s][a] - mu * mu) / S;
                }
            }
            b2v.push_back(b2); varv.push_back(var); msev.push_back(b2 + var / B);
        }
        int best = (int)(min_element(msev.begin(), msev.end()) - msev.begin());
        printf("[E2] %s critic：", noisy ? "带误差" : "精确");
        for (size_t i = 0; i < lams.size(); ++i) printf(" λ=%.2f(偏差² %.5f, 方差 %.4f)", lams[i], b2v[i], varv[i]);
        printf("\n     batch=%d 时 MSE = 偏差² + 方差/B 最小在 λ = %.2f\n", B, lams[best]);
        vector<double> varB; for (double x : varv) varB.push_back(x / B);
        out.push_back(JObj().add("b2", jpts(lams, b2v)).add("var", jpts(lams, varv)).add("varB", jpts(lams, varB)).num("mse_best", *min_element(msev.begin(), msev.end()), 5).add("mse", jpts(lams, msev)).num("best", lams[best])
            .num("b2_0", b2v.front(), 5).num("b2_1", b2v.back(), 5).num("var_0", varv.front(), 5).num("var_1", varv.back(), 5).str());
    }
    return JObj().add("exact", out[0]).add("noisy", out[1]).num("B", B).num("N", N).num("vstd", vstd, 4);
}

// ---------------------------------------------------------------- E3：CartPole 上的同步 A2C
struct AcCurve { vector<double> step, ret; double final = 0; };
static AcCurve a2c(double lam, int seed) {
    const int NE = 8, NS = 16, total = 300000; const double gamma = 0.99, lr = 1e-3, ent = 0.01, vcoef = 0.5;
    Rng rng(seed * 977 + 5);
    MLP pi({4, 64, 64, 2}, rng, 0.01), vf({4, 64, 64, 1}, rng);
    vector<CartPole> env(NE); vector<vector<double>> obs(NE);
    for (int e = 0; e < NE; ++e) obs[e] = env[e].reset(rng);
    vector<double> lens; AcCurve c; double acc = 0; int accn = 0;
    int steps = 0, next_log = 10000;
    while (steps < total) {
        vector<vector<double>> S_(NE * NS); vector<int> Aa(NE * NS), term(NE * NS), trunc(NE * NS);
        vector<double> R(NE * NS), Vv(NE * NS), Vn(NE * NS);
        for (int t = 0; t < NS; ++t) for (int e = 0; e < NE; ++e) {
            int i = e * NS + t;   // 每个环境一段连续的 rollout
            S_[i] = obs[e];
            auto p = softmax(pi.forward(obs[e]));
            int a = rng.u() < p[1] ? 1 : 0;
            Vv[i] = vf.forward(obs[e])[0];
            bool te, tu; R[i] = env[e].step(a, te, tu); Aa[i] = a; term[i] = te; trunc[i] = tu;
            auto o2 = env[e].obs();
            Vn[i] = te ? 0.0 : vf.forward(o2)[0];   // 截断时用截断那一刻的观测
            if (te || tu) { lens.push_back(env[e].t); acc += env[e].t; ++accn; obs[e] = env[e].reset(rng); }
            else obs[e] = o2;
            ++steps;
        }
        for (int e = 0; e < NE; ++e) {
            vector<double> r(R.begin() + e * NS, R.begin() + (e + 1) * NS), v(Vv.begin() + e * NS, Vv.begin() + (e + 1) * NS), vn(Vn.begin() + e * NS, Vn.begin() + (e + 1) * NS);
            vector<int> tm(term.begin() + e * NS, term.begin() + (e + 1) * NS), tr(trunc.begin() + e * NS, trunc.begin() + (e + 1) * NS);
            auto adv = gae(r, v, vn, tm, tr, gamma, lam);
            for (int t = 0; t < NS; ++t) {
                int i = e * NS + t;
                auto p = softmax(pi.forward(S_[i]));
                double H = 0; for (double x : p) H -= x * log(max(x, 1e-12));
                vector<double> g(2);
                for (int k = 0; k < 2; ++k) g[k] = (p[k] - (k == Aa[i])) * adv[t] + ent * p[k] * (log(max(p[k], 1e-12)) + H);
                pi.backward(g);
                double vv = vf.forward(S_[i])[0];
                vf.backward({vcoef * 2 * (vv - (adv[t] + v[t]))});
            }
        }
        pi.adam(lr, 1.0 / (NE * NS), 0.5);
        vf.adam(lr, 1.0 / (NE * NS), 0.5);
        if (steps >= next_log) {
            c.step.push_back(next_log); c.ret.push_back(accn ? acc / accn : (c.ret.empty() ? 0 : c.ret.back()));
            acc = 0; accn = 0; next_log += 10000;
        }
    }
    double f = 0; for (size_t i = c.ret.size() - 5; i < c.ret.size(); ++i) f += c.ret[i] / 5; c.final = f;
    return c;
}
static JObj e3() {
    vector<double> lams = {0.0, 0.9, 0.95, 1.0};
    const int L = 4, SEEDS = 5;
    vector<AcCurve> res(L * SEEDS);
    Timer tm;
    parallel_for(L * SEEDS, [&](int i) { res[i] = a2c(lams[i / SEEDS], i % SEEDS + 1); });
    vector<string> out;
    for (int l = 0; l < L; ++l) {
        size_t n = res[l * SEEDS].step.size(); vector<double> mu(n, 0), fin;
        for (int sd = 0; sd < SEEDS; ++sd) { auto& c = res[l * SEEDS + sd]; fin.push_back(c.final); for (size_t i = 0; i < n; ++i) mu[i] += c.ret[i] / SEEDS; }
        printf("[E3] A2C λ=%.2f：最后 5 万步训练回合平均长度 %.1f（种子 %.0f/%.0f/%.0f/%.0f/%.0f）\n", lams[l], mean(fin), fin[0], fin[1], fin[2], fin[3], fin[4]);
        out.push_back(JObj().num("lam", lams[l]).add("mean", jpts(res[l * SEEDS].step, mu, 5)).num("final", mean(fin), 4).add("fin", ja(fin, 4)).str());
    }
    printf("[E3] 20 个训练并行，耗时 %.1f s\n", tm.sec());
    return JObj().add("runs", jlist(out));
}

int main() {
    JObj root;
    root.add("e1", e1().str());
    root.add("e2", e2().str());
    root.add("e3", e3().str());
    write_file("ch10_output.json", root.str());
    printf("写出 ch10_output.json\n");
}
