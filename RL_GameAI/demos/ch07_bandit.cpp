// 第 7 章实测：探索与利用
//   E1 10 臂测试平台（Sutton 第 2 章）：q*(a) ~ N(0,1)，奖励 ~ N(q*(a),1)，2000 次运行 × 1000 步；
//      ε-greedy（ε = 0 / 0.01 / 0.1）、乐观初值（Q₀ = 5，α = 0.1，贪心）、UCB（c = 2）、Thompson 采样（高斯先验）、
//      梯度老虎机（α = 0.1，q* 整体 +4，有 / 无基线）的平均奖励与最优动作比例
//   E2 参数研究（Sutton 图 2.6）：前 1000 步平均奖励随各方法超参数的变化
//   E3 伯努利老虎机的累积遗憾（5 臂，T = 20000，500 次运行）：固定 ε / 衰减 ε / UCB1 / Thompson（Beta），对比 Lai-Robbins 下界
//   E4 Hoeffding 不等式的经验检验：Bernoulli(0.5) 的样本均值偏离 ≥ 0.1 的概率 vs exp(−2nε²)
//   E5 深度探索：长度为 N 的链（只有最右端有奖励），ε-greedy / 乐观初值 / 计数奖励 β/√N(s,a) 的 Q-learning，首次到达终点所需回合数
// 输出：stdout + ch07_output.json
// 编译：cl /nologo /std:c++latest /EHsc /O2 /utf-8 ch07_bandit.cpp
#include "rlcommon.h"
#include <array>
using namespace std;

const int K = 10;
// kind: 0 ε-greedy（样本均值），1 乐观初值（常数 α），2 UCB，3 Thompson（高斯），4 梯度老虎机（有基线），5 梯度老虎机（无基线）
struct Agent {
    int kind; double p;   // p：ε / Q₀ / c / （无） / α
    array<double, K> Q{}, H{}, sum{}; array<int, K> N{}; double rbar = 0; int t = 0; vector<double> pi;
    Agent(int k, double p_) : kind(k), p(p_) { if (kind == 1) Q.fill(p); pi.assign(K, 1.0 / K); }
    int act(Rng& r) {
        ++t;
        auto amax = [&](const array<double, K>& v) { double m = v[0]; for (int a = 1; a < K; ++a) m = max(m, v[a]); int c[K], n = 0; for (int a = 0; a < K; ++a) if (v[a] == m) c[n++] = a; return c[r.below(n)]; };
        if (kind == 0) return r.u() < p ? r.below(K) : amax(Q);
        if (kind == 1) return amax(Q);
        if (kind == 2) { for (int a = 0; a < K; ++a) if (N[a] == 0) return a; array<double, K> u; for (int a = 0; a < K; ++a) u[a] = Q[a] + p * sqrt(log((double)t) / N[a]); return amax(u); }
        if (kind == 3) { array<double, K> s; for (int a = 0; a < K; ++a) s[a] = sum[a] / (N[a] + 1) + r.normal() / sqrt(N[a] + 1.0); return amax(s); }
        pi = softmax(vector<double>(H.begin(), H.end())); return r.categorical(pi);
    }
    void learn(int a, double rew) {
        N[a]++; sum[a] += rew;
        if (kind == 0 || kind == 2 || kind == 3) Q[a] += (rew - Q[a]) / N[a];
        else if (kind == 1) Q[a] += 0.1 * (rew - Q[a]);
        else {
            double base = kind == 4 ? rbar : 0.0;
            for (int b = 0; b < K; ++b) H[b] += p * (rew - base) * ((b == a) - pi[b]);
            rbar += (rew - rbar) / t;
        }
    }
};
struct Curve { vector<double> rew, opt; };
static Curve testbed(int kind, double p, int runs, int steps, double shift, uint64_t seed0) {
    Curve c; c.rew.assign(steps, 0); c.opt.assign(steps, 0);
    for (int run = 0; run < runs; ++run) {
        Rng r(seed0 + run); array<double, K> q; for (auto& x : q) x = r.normal() + shift;
        int best = (int)(max_element(q.begin(), q.end()) - q.begin());
        Agent ag(kind, p);
        for (int t = 0; t < steps; ++t) { int a = ag.act(r); double rew = q[a] + r.normal(); ag.learn(a, rew); c.rew[t] += rew / runs; c.opt[t] += (a == best) * 100.0 / runs; }
    }
    return c;
}
static vector<double> smooth(const vector<double>& v, int w, vector<double>& xs) {
    vector<double> o; xs.clear();
    for (size_t i = 0; i < v.size(); i += w) { double s = 0; int n = 0; for (size_t j = i; j < min(v.size(), i + w); ++j) { s += v[j]; ++n; } o.push_back(s / n); xs.push_back((double)i + n); }
    return o;
}

static JObj e1() {
    struct M { const char* name; int kind; double p; double shift; };
    vector<M> ms = {{"贪心（ε = 0）", 0, 0.0, 0}, {"ε = 0.01", 0, 0.01, 0}, {"ε = 0.1", 0, 0.1, 0}, {"乐观初值 Q₀ = 5（贪心，α = 0.1）", 1, 5.0, 0},
                    {"UCB（c = 2）", 2, 2.0, 0}, {"Thompson 采样", 3, 0, 0}, {"梯度老虎机（有基线）", 4, 0.1, 4}, {"梯度老虎机（无基线）", 5, 0.1, 4}};
    const int RUNS = 2000, T = 1000;
    vector<Curve> res(ms.size());
    parallel_for((int)ms.size(), [&](int i) { res[i] = testbed(ms[i].kind, ms[i].p, RUNS, T, ms[i].shift, 100); });
    vector<string> out;
    for (size_t i = 0; i < ms.size(); ++i) {
        vector<double> xs; auto r = smooth(res[i].rew, 10, xs); auto o = smooth(res[i].opt, 10, xs);
        double lr = 0, lo = 0; for (int t = T - 100; t < T; ++t) { lr += res[i].rew[t] / 100; lo += res[i].opt[t] / 100; }
        double ar = mean(res[i].rew);
        printf("[E1] %-30s 前 1000 步平均奖励 %.3f；最后 100 步平均奖励 %.3f，最优动作比例 %.1f%%（第 11 步最优比例 %.1f%%）\n", ms[i].name, ar, lr, lo, res[i].opt[10]);
        out.push_back(JObj().add("name", js(ms[i].name)).add("rew", jpts(xs, r, 5)).add("opt", jpts(xs, o, 5)).num("last_r", lr, 4).num("last_o", lo, 4).num("avg", ar, 4)
            .num("opt11", res[i].opt[10], 4).add("rew_early", jpts(vector<double>{1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16,17,18,19,20,21,22,23,24,25,26,27,28,29,30}, vector<double>(res[i].rew.begin(), res[i].rew.begin() + 30), 5)).str());
    }
    return JObj().add("runs", jlist(out)).num("RUNS", RUNS).num("T", T);
}

static JObj e2() {
    vector<double> P = {1.0 / 128, 1.0 / 64, 1.0 / 32, 1.0 / 16, 1.0 / 8, 1.0 / 4, 1.0 / 2, 1, 2, 4};
    struct M { const char* name; int kind; int lo, hi; double shift; };   // 参数取 P[lo..hi]
    vector<M> ms = {{"ε-greedy（参数 ε）", 0, 0, 5, 0}, {"梯度老虎机（参数 α）", 4, 2, 9, 0}, {"UCB（参数 c）", 2, 3, 9, 0}, {"乐观初值贪心（参数 Q₀）", 1, 5, 9, 0}};
    vector<string> out;
    for (auto& m : ms) {
        vector<double> xs, ys(m.hi - m.lo + 1);
        parallel_for(m.hi - m.lo + 1, [&](int i) { auto c = testbed(m.kind, P[m.lo + i], 2000, 1000, m.shift, 5000); ys[i] = mean(c.rew); });
        for (int i = m.lo; i <= m.hi; ++i) xs.push_back(P[i]);
        int bi = (int)(max_element(ys.begin(), ys.end()) - ys.begin());
        printf("[E2] %-24s 最好参数 %.4g，前 1000 步平均奖励 %.3f\n", m.name, xs[bi], ys[bi]);
        out.push_back(JObj().add("name", js(m.name)).add("pts", jpts(xs, ys, 5)).num("best_p", xs[bi], 4).num("best", ys[bi], 4).str());
    }
    return JObj().add("runs", jlist(out));
}

static JObj e3() {
    const vector<double> mu = {0.7, 0.6, 0.5, 0.4, 0.3}; const int KA = 5, T = 20000, RUNS = 500;
    const double best = 0.7;
    auto kl = [](double p, double q) { return p * log(p / q) + (1 - p) * log((1 - p) / (1 - q)); };
    double lr = 0; for (int a = 1; a < KA; ++a) lr += (best - mu[a]) / kl(mu[a], best);
    const char* names[4] = {"固定 ε = 0.1", "衰减 ε_t = min(1, 5K/t)", "UCB1", "Thompson 采样（Beta）"};
    vector<vector<double>> reg(4, vector<double>(T, 0));
    vector<double> pulls_sub(4, 0);
    parallel_for(4, [&](int m) {
        for (int run = 0; run < RUNS; ++run) {
            Rng r(20000 + run); vector<double> S(KA, 0); vector<int> N(KA, 0); double R = 0;
            for (int t = 1; t <= T; ++t) {
                int a = 0;
                auto amax = [&](const vector<double>& v) { double mx = *max_element(v.begin(), v.end()); vector<int> c; for (int i = 0; i < KA; ++i) if (v[i] == mx) c.push_back(i); return c[r.below((int)c.size())]; };
                vector<double> est(KA); for (int i = 0; i < KA; ++i) est[i] = N[i] ? S[i] / N[i] : 0.0;
                if (m == 0) a = r.u() < 0.1 ? r.below(KA) : amax(est);
                else if (m == 1) a = r.u() < min(1.0, 5.0 * KA / t) ? r.below(KA) : amax(est);
                else if (m == 2) { int z = -1; for (int i = 0; i < KA; ++i) if (!N[i]) { z = i; break; }
                    if (z >= 0) a = z; else { vector<double> u(KA); for (int i = 0; i < KA; ++i) u[i] = est[i] + sqrt(2 * log((double)t) / N[i]); a = amax(u); } }
                else { // Beta(1+S, 1+N−S) 采样：两个 Gamma 之比，用整数参数的 Gamma = −Σ log U
                    vector<double> smp(KA);
                    for (int i = 0; i < KA; ++i) {
                        auto gam = [&](int k) { if (k <= 30) { double g = 0; for (int j = 0; j < k; ++j) g -= log(max(r.u(), 1e-300)); return g; }
                            // 大参数：Marsaglia–Tsang
                            double d = k - 1.0 / 3, c = 1 / sqrt(9 * d);
                            while (true) { double x = r.normal(), v = 1 + c * x; if (v <= 0) continue; v = v * v * v; double u = r.u(); if (log(max(u, 1e-300)) < 0.5 * x * x + d - d * v + d * log(v)) return d * v; } };
                        int s = (int)S[i]; double x = gam(1 + s), y = gam(1 + N[i] - s); smp[i] = x / (x + y);
                    }
                    a = amax(smp);
                }
                double rew = r.u() < mu[a] ? 1.0 : 0.0; S[a] += rew; N[a]++;
                R += best - mu[a]; reg[m][t - 1] += R / RUNS;
            }
            for (int i = 1; i < KA; ++i) pulls_sub[m] += (double)N[i] / RUNS;
        }
    });
    vector<double> xs; for (double x = 10; x <= T; x *= 1.15) xs.push_back(floor(x)); xs.push_back(T);
    xs.erase(unique(xs.begin(), xs.end()), xs.end());
    vector<string> out;
    for (int m = 0; m < 4; ++m) {
        vector<double> ys; for (double x : xs) ys.push_back(reg[m][(int)x - 1]);
        printf("[E3] %-22s T=%d 累积遗憾 %.1f（t=1000 时 %.1f），次优臂共被拉 %.0f 次\n", names[m], T, reg[m][T - 1], reg[m][999], pulls_sub[m]);
        out.push_back(JObj().add("name", js(names[m])).add("pts", jpts(xs, ys, 5)).num("end", reg[m][T - 1], 5).num("r1000", reg[m][999], 5).num("pulls", pulls_sub[m], 5).str());
    }
    vector<double> lb; for (double x : xs) lb.push_back(lr * log(x));
    printf("[E3] Lai-Robbins 下界常数 Σ Δ/KL = %.2f，T=%d 时 %.1f\n", lr, T, lr * log((double)T));
    return JObj().add("runs", jlist(out)).add("lb", jpts(xs, lb, 5)).num("lr", lr, 4).num("lb_end", lr * log((double)T), 4).num("T", T).num("RUNS", RUNS);
}

static JObj e4() {
    const double eps = 0.1; const int R = 200000; vector<double> ns, emp, bound;
    Rng r(44);
    for (int n = 5; n <= 150; n += 5) {
        int hit = 0;
        for (int k = 0; k < R; ++k) { int s = 0; for (int i = 0; i < n; ++i) s += r.u() < 0.5; if ((double)s / n - 0.5 >= eps - 1e-12) ++hit; }
        ns.push_back(n); emp.push_back((double)hit / R); bound.push_back(exp(-2.0 * n * eps * eps));
    }
    bool ok = true; for (size_t i = 0; i < ns.size(); ++i) ok &= emp[i] <= bound[i];
    printf("[E4] Hoeffding：n = 50 时 P(均值 − 0.5 ≥ 0.1) 经验 %.4f ≤ 上界 %.4f；n = 150 时 %.5f ≤ %.5f；全部满足：%s\n", emp[9], bound[9], emp.back(), bound.back(), ok ? "是" : "否");
    return JObj().add("emp", jpts(ns, emp, 5)).add("bound", jpts(ns, bound, 5)).num("e50", emp[9], 4).num("b50", bound[9], 4).num("ok", ok);
}

// E5：链式 MDP 的深度探索。状态 0..N−1，从 0 出发，动作 左 / 右，只有走到 N−1 才得 +1（终止），每回合最多 N+9 步。
//   方法 0：ε-greedy（ε = 0.1，Q₀ = 0，α = 0.1）
//   方法 1：UCB 式动作选择 argmax Q + c/√(N(s,a)+1)（只用本地计数，不通过 TD 传播，α = 0.1）
//   方法 2：计数奖励 r + β/√N(s,a) 作为内在奖励，通过 TD 传播（ε = 0.1，α = 0.5，β = 0.1）
//   方法 3：乐观初值 Q₀ = 1（贪心，α = 0.1）
static JObj e5() {
    vector<int> Ns = {4, 8, 12, 16, 20, 25, 30, 40, 50};
    const int SEEDS = 20, CAP = 5000; const double g = 0.99;
    const char* names[4] = {"ε-greedy（ε = 0.1）", "UCB 式动作选择（本地计数）", "计数奖励 β/√N(s,a)（经 TD 传播）", "乐观初值 Q₀ = 1（贪心）"};
    const int NM = 4, NK = (int)Ns.size();
    vector<vector<double>> med(NM, vector<double>(NK)), succ(NM, vector<double>(NK));
    parallel_for(NM * NK, [&](int i) {
        int m = i / NK, k = i % NK, N = Ns[k];
        double alpha = m == 2 ? 0.5 : 0.1;
        vector<double> firsts; int ok = 0;
        for (int sd = 0; sd < SEEDS; ++sd) {
            Rng r(600 + sd); double q0 = m == 3 ? 1.0 : 0.0;
            vector<array<double, 2>> Q(N, {q0, q0}); vector<array<int, 2>> C(N, {0, 0});
            int first = CAP + 1;
            for (int ep = 1; ep <= CAP; ++ep) {
                int s = 0; bool reached = false;
                for (int t = 0; t < N + 9; ++t) {
                    int a;
                    if (m == 1) { double u0 = Q[s][0] + 0.5 / sqrt(C[s][0] + 1.0), u1 = Q[s][1] + 0.5 / sqrt(C[s][1] + 1.0); a = u0 == u1 ? r.below(2) : (u1 > u0 ? 1 : 0); }
                    else {
                        bool explore = (m == 0 || m == 2) && r.u() < 0.1;
                        if (explore) a = r.below(2);
                        else { double m0 = max(Q[s][0], Q[s][1]); int c[2], n = 0; for (int b = 0; b < 2; ++b) if (Q[s][b] == m0) c[n++] = b; a = c[r.below(n)]; }
                    }
                    int s2 = a == 1 ? s + 1 : max(0, s - 1);
                    double rew = s2 == N - 1 ? 1.0 : 0.0; C[s][a]++;
                    if (m == 2) rew += 0.1 / sqrt((double)C[s][a]);
                    bool term = s2 == N - 1;
                    Q[s][a] += alpha * (rew + (term ? 0.0 : g * max(Q[s2][0], Q[s2][1])) - Q[s][a]);
                    s = s2; if (term) { reached = true; break; }
                }
                if (reached) { first = ep; break; }
            }
            firsts.push_back(first); ok += first <= CAP;
        }
        sort(firsts.begin(), firsts.end()); med[m][k] = (firsts[SEEDS / 2 - 1] + firsts[SEEDS / 2]) / 2; succ[m][k] = (double)ok / SEEDS;
    });
    vector<string> out; vector<double> xs(Ns.begin(), Ns.end());
    for (int m = 0; m < NM; ++m) {
        printf("[E5] %-30s 首次到达终点所需回合数（中位数，N = 4…50）：", names[m]); for (int k = 0; k < NK; ++k) printf(" %s", med[m][k] > CAP ? ">5000" : to_string((int)med[m][k]).c_str());
        printf("；5000 回合内到达的种子比例："); for (int k = 0; k < NK; ++k) printf(" %.2f", succ[m][k]); printf("\n");
        out.push_back(JObj().add("name", js(names[m])).add("pts", jpts(xs, med[m], 5)).add("succ", jpts(xs, succ[m], 4)).add("med", ja(med[m], 5)).add("sv", ja(succ[m], 4)).str());
    }
    return JObj().add("runs", jlist(out)).num("CAP", CAP).add("Ns", jai(Ns));
}

int main() {
    JObj root;
    root.add("e1", e1().str());
    root.add("e2", e2().str());
    root.add("e3", e3().str());
    root.add("e4", e4().str());
    root.add("e5", e5().str());
    write_file("ch07_output.json", root.str());
    printf("写出 ch07_output.json\n");
}
