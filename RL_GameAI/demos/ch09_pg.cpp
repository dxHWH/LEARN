// 第 9 章实测：策略梯度
//   E1 随机小 MDP（5 状态 × 3 动作，γ=0.9，表格 softmax 策略）：有限差分 ∇J vs 策略梯度定理 Σ d^γ(s) π(a|s) A(s,a)
//      并比较“丢掉 γ^t”的常见实现（用未折扣的访问分布）与真梯度的夹角
//   E2 同一 MDP 上 4 种梯度估计量的偏差与方差：整条回报 / reward-to-go / reward-to-go + 基线 / 去掉 γ^t 的实用版
//   E3 CartPole 上 REINFORCE：整条回报 vs reward-to-go vs reward-to-go + 学习的 V(s) 基线（各 5 个种子，并行）
// 输出：stdout + ch09_output.json
// 编译：cl /nologo /std:c++latest /EHsc /O2 /utf-8 ch09_pg.cpp
#include "rlcommon.h"
using namespace std;

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
static vector<double> solve(vector<vector<double>> M, vector<double> b) {   // 高斯消元（列主元）
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
struct Eval { vector<vector<double>> pi, Q; vector<double> V, d, dstat; double J; };
static Eval evaluate(const MDP& m, const vector<double>& th, double gamma) {
    Eval e; e.pi.assign(S, vector<double>(A)); e.Q.assign(S, vector<double>(A));
    for (int s = 0; s < S; ++s) { vector<double> z(th.begin() + s * A, th.begin() + s * A + A); e.pi[s] = softmax(z); }
    vector<vector<double>> Pp(S, vector<double>(S, 0)); vector<double> rp(S, 0);
    for (int s = 0; s < S; ++s) for (int a = 0; a < A; ++a) { rp[s] += e.pi[s][a] * m.R[s][a]; for (int t = 0; t < S; ++t) Pp[s][t] += e.pi[s][a] * m.P[s][a][t]; }
    vector<vector<double>> M(S, vector<double>(S));
    for (int i = 0; i < S; ++i) for (int j = 0; j < S; ++j) M[i][j] = (i == j) - gamma * Pp[i][j];
    e.V = solve(M, rp);
    for (int s = 0; s < S; ++s) for (int a = 0; a < A; ++a) { double q = m.R[s][a]; for (int t = 0; t < S; ++t) q += gamma * m.P[s][a][t] * e.V[t]; e.Q[s][a] = q; }
    e.J = 0; for (int s = 0; s < S; ++s) e.J += m.rho[s] * e.V[s];
    // 折扣访问分布 d^γ = ρᵀ (I − γP)⁻¹（未归一化，总和 1/(1−γ)）
    vector<vector<double>> MT(S, vector<double>(S));
    for (int i = 0; i < S; ++i) for (int j = 0; j < S; ++j) MT[i][j] = M[j][i];
    e.d = solve(MT, vector<double>(m.rho, m.rho + S));
    // P_π 的平稳分布（幂迭代）
    e.dstat.assign(S, 1.0 / S);
    for (int k = 0; k < 2000; ++k) { vector<double> nd(S, 0); for (int i = 0; i < S; ++i) for (int j = 0; j < S; ++j) nd[j] += e.dstat[i] * Pp[i][j]; e.dstat = nd; }
    return e;
}
static double cosine(const vector<double>& a, const vector<double>& b) {
    double ab = 0, aa = 0, bb = 0; for (size_t i = 0; i < a.size(); ++i) { ab += a[i] * b[i]; aa += a[i] * a[i]; bb += b[i] * b[i]; }
    return ab / sqrt(aa * bb);
}

static JObj e1e2() {
    MDP m = make_mdp();
    Rng r(21);
    vector<double> th(S * A); for (auto& x : th) x = r.uni(-1.5, 1.5);
    Eval e = evaluate(m, th, G);
    // 有限差分
    vector<double> fd(S * A), pgt(S * A), prac(S * A);
    const double h = 1e-6;
    for (int i = 0; i < S * A; ++i) {
        auto tp = th, tm = th; tp[i] += h; tm[i] -= h;
        fd[i] = (evaluate(m, tp, G).J - evaluate(m, tm, G).J) / (2 * h);
    }
    double maxdiff = 0;
    for (int s = 0; s < S; ++s) for (int a = 0; a < A; ++a) {
        double adv = e.Q[s][a] - e.V[s];
        pgt[s * A + a] = e.d[s] * e.pi[s][a] * adv;
        prac[s * A + a] = e.dstat[s] / (1 - G) * e.pi[s][a] * adv;   // 用平稳分布代替折扣访问分布（同样的总质量）
        maxdiff = max(maxdiff, fabs(pgt[s * A + a] - fd[s * A + a]));
    }
    double cs = cosine(prac, fd), gn = 0; for (double x : fd) gn += x * x; gn = sqrt(gn);
    printf("[E1] J(θ) = %.6f，‖∇J‖ = %.4f；策略梯度定理 vs 有限差分 最大差 %.2e\n", e.J, gn, maxdiff);
    printf("     用平稳分布代替折扣访问分布（“丢掉 γ^t”）：与真梯度夹角余弦 %.4f\n", cs);

    // E2：采样估计量
    const int N = 20000, H = 100;
    const int K = 4;
    const char* names[K] = {"整条回报 G₀", "reward-to-go", "reward-to-go + 基线 V(s)", "去掉 γ^t（实用版）"};
    vector<vector<double>> sum(K, vector<double>(S * A, 0)), sq(K, vector<double>(S * A, 0));
    vector<double> mse(K, 0);
    Rng rr(33);
    for (int n = 0; n < N; ++n) {
        int s = rr.categorical(vector<double>(m.rho, m.rho + S));
        vector<int> ss(H), aa(H); vector<double> rw(H);
        for (int t = 0; t < H; ++t) {
            int a = rr.categorical(e.pi[s]);
            ss[t] = s; aa[t] = a; rw[t] = m.R[s][a];
            s = rr.categorical(vector<double>(m.P[s][a], m.P[s][a] + S));
        }
        vector<double> Gt(H + 1, 0); for (int t = H - 1; t >= 0; --t) Gt[t] = rw[t] + G * Gt[t + 1];
        vector<vector<double>> est(K, vector<double>(S * A, 0));
        double disc = 1;
        for (int t = 0; t < H; ++t) {
            int st = ss[t], at = aa[t];
            for (int b = 0; b < A; ++b) {
                double score = (b == at) - e.pi[st][b];   // ∂ log π(a_t|s_t) / ∂θ_{s_t,b}
                est[0][st * A + b] += score * Gt[0];
                est[1][st * A + b] += disc * score * Gt[t];
                est[2][st * A + b] += disc * score * (Gt[t] - e.V[st]);
                est[3][st * A + b] += score * (Gt[t] - e.V[st]);
            }
            disc *= G;
        }
        for (int k = 0; k < K; ++k) for (int i = 0; i < S * A; ++i) {
            sum[k][i] += est[k][i]; sq[k][i] += est[k][i] * est[k][i];
            mse[k] += (est[k][i] - fd[i]) * (est[k][i] - fd[i]);
        }
    }
    vector<string> rows, nm; vector<double> vars, cosv;
    for (int k = 0; k < K; ++k) {
        double bias2 = 0, var = 0;
        for (int i = 0; i < S * A; ++i) { double mu = sum[k][i] / N; bias2 += (mu - fd[i]) * (mu - fd[i]); var += sq[k][i] / N - mu * mu; }
        vector<double> mu(S * A); for (int i = 0; i < S * A; ++i) mu[i] = sum[k][i] / N;
        double c = cosine(mu, fd);
        printf("[E2] %-26s ‖偏差‖ %.4f（标准误约 %.4f）  总方差 %.3f  与真梯度余弦 %.4f\n", names[k], sqrt(bias2), sqrt(var / N), var, c);
        nm.push_back(js(names[k])); vars.push_back(var); cosv.push_back(c);
        rows.push_back(JObj().add("name", js(names[k])).num("bias", sqrt(bias2), 4).num("se", sqrt(var / N), 4).num("var", var, 5).num("cos", c, 4).str());
    }
    return JObj().num("J", e.J, 8).num("gnorm", gn, 6).num("maxdiff", maxdiff, 3).num("cos_prac", cs, 5).add("fd", ja(fd, 8)).add("pgt", ja(pgt, 8))
        .add("est", jlist(rows)).add("names", jlist(nm)).add("vars", ja(vars, 5)).add("short", "[\"G₀\",\"reward-to-go\",\"+ 基线\",\"去掉 γ^t\"]").num("N", N).num("H", H);
}

// ================================================================ E3 CartPole REINFORCE
struct PgCurve { vector<double> ep, ret; double final = 0; };
static PgCurve reinforce(int variant, int seed) {   // 0 整条回报，1 reward-to-go，2 reward-to-go + V 基线
    const int updates = 300, batch = 5; const double gamma = 0.99, lr = 0.01, lrv = 0.01;
    Rng rng(seed * 31 + 7);
    MLP pi({4, 32, 2}, rng, 0.01), vf({4, 32, 1}, rng);
    PgCurve c; double win = 0; int winn = 0;
    for (int u = 1; u <= updates; ++u) {
        double avg = 0; int nsteps = 0;
        for (int b = 0; b < batch; ++b) {
            CartPole env; auto s = env.reset(rng);
            vector<vector<double>> ss; vector<int> as; vector<double> rs;
            bool term = false, trunc = false;
            while (!term && !trunc) {
                auto p = softmax(pi.forward(s));
                int a = rng.u() < p[1] ? 1 : 0;
                ss.push_back(s); as.push_back(a); rs.push_back(env.step(a, term, trunc)); s = env.obs();
            }
            int T = (int)rs.size(); avg += T; nsteps += T;
            double boot = 0;
            if (trunc && variant == 2) boot = vf.forward(s)[0];   // 截断：用 V 自举剩余回报
            vector<double> Gt(T + 1, 0); Gt[T] = boot;
            for (int t = T - 1; t >= 0; --t) Gt[t] = rs[t] + gamma * Gt[t + 1];
            for (int t = 0; t < T; ++t) {
                double psi;
                if (variant == 0) psi = Gt[0];
                else if (variant == 1) psi = Gt[t];
                else {
                    double v = vf.forward(ss[t])[0];
                    vf.backward({v - Gt[t]});           // V 回归到 reward-to-go
                    psi = Gt[t] - v;
                }
                auto p = softmax(pi.forward(ss[t]));
                vector<double> g(2); for (int k = 0; k < 2; ++k) g[k] = (p[k] - (k == as[t])) * psi;   // ∂(−log π · ψ)/∂logits
                pi.backward(g);
            }
        }
        pi.adam(lr, 1.0 / batch, 0.0);
        if (variant == 2) vf.adam(lrv, 1.0 / nsteps);
        avg /= batch; win += avg; ++winn;
        if (u % 10 == 0) { c.ep.push_back(u * batch); c.ret.push_back(win / winn); win = 0; winn = 0; }
    }
    double f = 0; for (size_t i = c.ret.size() - 5; i < c.ret.size(); ++i) f += c.ret[i] / 5; c.final = f;
    return c;
}
static JObj e3() {
    const int V = 3, SEEDS = 5;
    vector<PgCurve> res(V * SEEDS);
    Timer tm;
    parallel_for(V * SEEDS, [&](int i) { res[i] = reinforce(i / SEEDS, i % SEEDS + 1); });
    const char* names[V] = {"整条回报 G₀", "reward-to-go", "reward-to-go + V 基线"};
    vector<string> out;
    for (int v = 0; v < V; ++v) {
        size_t n = res[v * SEEDS].ep.size();
        vector<double> mu(n, 0), lo(n, 1e9), hi(n, -1e9), fin;
        for (int sd = 0; sd < SEEDS; ++sd) {
            auto& c = res[v * SEEDS + sd]; fin.push_back(c.final);
            for (size_t i = 0; i < n; ++i) { mu[i] += c.ret[i] / SEEDS; lo[i] = min(lo[i], c.ret[i]); hi[i] = max(hi[i], c.ret[i]); }
        }
        int hit = -1; for (size_t i = 0; i < n; ++i) if (mu[i] >= 195) { hit = (int)res[0].ep[i]; break; }
        printf("[E3] %-22s 最后 250 回合平均回报 %.1f（种子 %.0f/%.0f/%.0f/%.0f/%.0f），平均回报首次 ≥195 在第 %d 回合\n",
               names[v], mean(fin), fin[0], fin[1], fin[2], fin[3], fin[4], hit);
        out.push_back(JObj().add("name", js(names[v])).add("mean", jpts(res[0].ep, mu, 5)).add("lo", jpts(res[0].ep, lo, 5)).add("hi", jpts(res[0].ep, hi, 5))
            .num("final", mean(fin), 4).add("fin", ja(fin, 4)).num("hit", hit).str());
    }
    printf("[E3] 15 个训练并行，耗时 %.1f s\n", tm.sec());
    return JObj().add("runs", jlist(out));
}

int main() {
    JObj root;
    root.add("e1", e1e2().str());
    root.add("e3", e3().str());
    write_file("ch09_output.json", root.str());
    printf("写出 ch09_output.json\n");
}
