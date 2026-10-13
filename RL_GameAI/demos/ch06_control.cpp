// 第 6 章实测：无模型控制（Sarsa / Expected Sarsa / Q-learning / off-policy）
//   E1 Cliff Walking（Sutton 例 6.6，4×12，每步 −1，掉崖 −100 回到起点，γ=1，ε=0.1，α=0.5）：
//      三种算法 500 回合 × 200 次运行的“每回合奖励和”曲线 + run 0 学到的贪心路径；run 0 前 100 回合曲线供 Python 逐位对拍
//   E2 步长扫描（Sutton 图 6.3）：前 100 回合平均（200 次运行）与 1000 回合平均（20 次运行）随 α 的变化
//   E3 GLIE：ε 从 0.1 线性降到 0 后，Sarsa 的贪心路径与在线回报
//   E4 off-policy MC 的重要性采样（Sutton 例 5.5，单状态，目标策略总选 left，行为策略均匀）：普通 IS 方差无穷 vs 加权 IS
//   E5 ε-greedy 策略改进定理：随机 MDP 上从随机 ε-soft 策略出发反复“评估 + ε-greedy 改进”，检查每轮 v 单调不降，
//      收敛点与“最优 ε-soft 策略”的 Bellman 方程解一致，并给出与真正最优 v* 的差距
//   E6 最大化偏差随 B 状态动作数增长（Sutton 例 6.7 的变体）：Q-learning / Expected Sarsa / Double Q 在第 300 回合选 left 的比例
// 随机数：xorshift64*（与 rlcore.py / ch06_interview.py 相同）。输出：stdout + ch06_output.json
// 编译：cl /nologo /std:c++latest /EHsc /O2 /utf-8 ch06_control.cpp
#include "rlcommon.h"
#include <array>
using namespace std;

// ================================================================ Cliff Walking
const int CH = 4, CW = 12, CN = CH * CW, START = 3 * CW, GOAL = 3 * CW + 11;
static const int DR[4] = {-1, 0, 1, 0}, DC[4] = {0, 1, 0, -1};   // 上 右 下 左
using Q4 = vector<array<double, 4>>;
static bool is_cliff(int s) { return s / CW == 3 && s % CW >= 1 && s % CW <= 10; }
static int cliff_step(int s, int a, double& r) {
    int rr = s / CW + DR[a], cc = s % CW + DC[a];
    rr = max(0, min(CH - 1, rr)); cc = max(0, min(CW - 1, cc));
    int s2 = rr * CW + cc;
    if (is_cliff(s2)) { r = -100; return START; }
    r = -1; return s2;
}
// ε-greedy：先抽一次 u 决定是否探索；贪心时在并列最大值中均匀随机（随机数消耗顺序与 Python 版完全一致）
static int egreedy(const array<double, 4>& q, double eps, Rng& r) {
    if (r.u() < eps) return r.below(4);
    double m = q[0]; for (int a = 1; a < 4; ++a) if (q[a] > m) m = q[a];
    int c[4], k = 0; for (int a = 0; a < 4; ++a) if (q[a] == m) c[k++] = a;
    return c[r.below(k)];
}
static double expected_q(const array<double, 4>& q, double eps) {
    double m = q[0]; for (int a = 1; a < 4; ++a) if (q[a] > m) m = q[a];
    int k = 0; for (int a = 0; a < 4; ++a) if (q[a] == m) ++k;
    double e = 0; for (int a = 0; a < 4; ++a) e += (eps / 4 + (q[a] == m ? (1 - eps) / k : 0.0)) * q[a];
    return e;
}
// alg：0 Sarsa，1 Expected Sarsa，2 Q-learning。返回本回合奖励和
static double episode(int alg, Q4& Q, double alpha, double eps, Rng& r) {
    int s = START; double G = 0;
    int a = egreedy(Q[s], eps, r);
    for (int t = 0; t < 100000; ++t) {
        double rw; int s2 = cliff_step(s, a, rw); G += rw;
        bool term = s2 == GOAL;
        if (alg == 0) {
            int a2 = term ? 0 : egreedy(Q[s2], eps, r);
            Q[s][a] += alpha * (rw + (term ? 0.0 : Q[s2][a2]) - Q[s][a]);
            s = s2; a = a2;
        } else {
            double boot = term ? 0.0 : (alg == 1 ? expected_q(Q[s2], eps) : *max_element(Q[s2].begin(), Q[s2].end()));
            Q[s][a] += alpha * (rw + boot - Q[s][a]);
            s = s2;
            if (!term) a = egreedy(Q[s], eps, r);
        }
        if (term) break;
    }
    return G;
}
static vector<int> greedy_path(const Q4& Q) {
    vector<int> p = {START}; int s = START;
    for (int t = 0; t < 60 && s != GOAL; ++t) {
        int a = (int)(max_element(Q[s].begin(), Q[s].end()) - Q[s].begin());
        double rw; s = cliff_step(s, a, rw); p.push_back(s);
        if (rw < -50) break;
    }
    return p;
}

static JObj e1() {
    const int RUNS = 200, EP = 500; const double alpha = 0.5, eps = 0.1;
    const char* names[3] = {"Sarsa", "Expected Sarsa", "Q-learning"};
    vector<string> out;
    for (int alg = 0; alg < 3; ++alg) {
        vector<double> avg(EP, 0), seed0;
        vector<int> path0; int safe_rows = 0; double last = 0;
        vector<double> plen;
        for (int run = 0; run < RUNS; ++run) {
            Rng r(1000 + run); Q4 Q(CN, {0, 0, 0, 0});
            for (int ep = 0; ep < EP; ++ep) {
                double G = episode(alg, Q, alpha, eps, r); avg[ep] += G / RUNS;
                if (run == 0 && ep < 100) seed0.push_back(G);
            }
            auto p = greedy_path(Q); plen.push_back((double)p.size() - 1);
            int minrow = 3; for (int s : p) minrow = min(minrow, s / CW);
            if (minrow == 0) ++safe_rows;
            if (run == 0) path0 = p;
        }
        for (int ep = EP - 100; ep < EP; ++ep) last += avg[ep] / 100;
        vector<double> xs, ys;   // 每 10 回合平滑
        for (int ep = 0; ep < EP; ep += 10) { double m = 0; for (int k = ep; k < ep + 10; ++k) m += avg[k] / 10; xs.push_back(ep + 10); ys.push_back(m); }
        double mlen = mean(plen);
        printf("[E1] %-15s 最后 100 回合平均每回合奖励 %.2f；run 0 贪心路径 %zu 步；200 次运行中贪心路径走最上一行的 %d 次，平均路径长 %.2f\n",
               names[alg], last, path0.size() - 1, safe_rows, mlen);
        out.push_back(JObj().add("name", js(names[alg])).add("curve", jpts(xs, ys, 5)).num("last", last, 5).add("path", jai(path0))
            .num("plen", (double)path0.size() - 1).num("safe", safe_rows).num("mlen", mlen, 4).add("seed0", ja(seed0, 17)).str());
    }
    vector<string> rows;
    for (int r = 0; r < CH; ++r) { string s; for (int c = 0; c < CW; ++c) { int i = r * CW + c; s += i == START ? 'S' : i == GOAL ? 'G' : is_cliff(i) ? 'C' : '.'; } rows.push_back(js(s)); }
    return JObj().add("runs", jlist(out)).add("rows", "[" + [&] { string s; for (size_t i = 0; i < rows.size(); ++i) { if (i) s += ","; s += rows[i]; } return s; }() + "]")
        .num("alpha", alpha).num("eps", eps).num("RUNS", RUNS).num("EP", EP);
}

static JObj e2() {
    vector<double> alphas = {0.1, 0.2, 0.3, 0.4, 0.5, 0.6, 0.7, 0.8, 0.9, 1.0};
    const int NA = (int)alphas.size();
    vector<vector<double>> interim(3, vector<double>(NA)), asym(3, vector<double>(NA));
    parallel_for(3 * NA, [&](int i) {
        int alg = i / NA, k = i % NA; double a = alphas[k];
        double s1 = 0; for (int run = 0; run < 200; ++run) { Rng r(5000 + run); Q4 Q(CN, {0, 0, 0, 0}); for (int ep = 0; ep < 100; ++ep) s1 += episode(alg, Q, a, 0.1, r); }
        double s2 = 0; for (int run = 0; run < 20; ++run) { Rng r(7000 + run); Q4 Q(CN, {0, 0, 0, 0}); for (int ep = 0; ep < 1000; ++ep) s2 += episode(alg, Q, a, 0.1, r); }
        interim[alg][k] = s1 / (200.0 * 100); asym[alg][k] = s2 / (20.0 * 1000);
    });
    const char* names[3] = {"Sarsa", "Expected Sarsa", "Q-learning"};
    vector<string> out;
    for (int alg = 0; alg < 3; ++alg) {
        printf("[E2] %-15s α=0.1…1.0 前 100 回合：", names[alg]); for (double v : interim[alg]) printf(" %.1f", v);
        printf(" | 1000 回合："); for (double v : asym[alg]) printf(" %.1f", v); printf("\n");
        out.push_back(JObj().add("name", js(names[alg])).add("interim", jpts(alphas, interim[alg], 5)).add("asym", jpts(alphas, asym[alg], 5))
            .num("int_a1", interim[alg].back(), 5).num("asym_a1", asym[alg].back(), 5).num("int_a05", interim[alg][4], 5).str());
    }
    return JObj().add("runs", jlist(out));
}

static JObj e3() {
    const int RUNS = 200, EP = 500;
    vector<double> curve(EP, 0); int opt = 0; vector<int> path0;
    for (int run = 0; run < RUNS; ++run) {
        Rng r(9000 + run); Q4 Q(CN, {0, 0, 0, 0});
        for (int ep = 0; ep < EP; ++ep) { double eps = max(0.0, 0.1 * (1.0 - ep / 400.0)); curve[ep] += episode(0, Q, 0.5, eps, r) / RUNS; }
        auto p = greedy_path(Q); if (p.size() - 1 == 13) ++opt; if (run == 0) path0 = p;
    }
    double last = 0; for (int ep = EP - 50; ep < EP; ++ep) last += curve[ep] / 50;
    vector<double> xs, ys; for (int ep = 0; ep < EP; ep += 10) { double m = 0; for (int k = ep; k < ep + 10; ++k) m += curve[k] / 10; xs.push_back(ep + 10); ys.push_back(m); }
    printf("[E3] GLIE Sarsa（ε 在第 400 回合降到 0）：最后 50 回合平均奖励 %.2f；200 次运行中贪心路径为最短路（13 步）的 %d 次；run 0 路径 %zu 步\n", last, opt, path0.size() - 1);
    // E3b：同时满足 GLIE 与 Robbins-Monro：α = 1/N(s,a)，ε = min(0.1, 1/k)，20000 回合；统计贪心路径长度分布与“悬崖边那一行”的访问次数
    const int RB = 40, EPB = 20000; int c13 = 0, c15 = 0, c17 = 0, cother = 0, cfail = 0; double edge_first = 0, edge_last = 0;
    for (int run = 0; run < RB; ++run) {
        Rng r(9000 + run); Q4 Q(CN, {0, 0, 0, 0}); vector<array<int, 4>> N(CN, {0, 0, 0, 0});
        for (int ep = 0; ep < EPB; ++ep) {
            double eps = min(0.1, 1.0 / (ep + 1)); int s = START, a = egreedy(Q[s], eps, r), edge = 0;
            for (int t = 0; t < 100000; ++t) {
                double rw; int s2 = cliff_step(s, a, rw); bool term = s2 == GOAL;
                if (s / CW == 2 && s % CW >= 1 && s % CW <= 10) ++edge;
                int a2 = term ? 0 : egreedy(Q[s2], eps, r); N[s][a]++;
                Q[s][a] += (1.0 / N[s][a]) * (rw + (term ? 0.0 : Q[s2][a2]) - Q[s][a]); s = s2; a = a2; if (term) break;
            }
            if (ep < 1000) edge_first += edge / (1000.0 * RB); if (ep >= EPB - 1000) edge_last += edge / (1000.0 * RB);
        }
        auto gp = greedy_path(Q); size_t L = gp.size() - 1; bool ok = gp.back() == GOAL;
        if (!ok) ++cfail; else if (L == 13) ++c13; else if (L == 15) ++c15; else if (L == 17) ++c17; else ++cother;
    }
    printf("[E3b] α=1/N、ε=min(0.1,1/k)、%d 回合 × %d 次：贪心路径 13 步 %d 次 / 15 步 %d 次 / 17 步 %d 次 / 其他长度 %d 次 / 到不了终点 %d 次；每回合在悬崖边一行的步数：前 1000 回合 %.2f，最后 1000 回合 %.3f\n",
           EPB, RB, c13, c15, c17, cother, cfail, edge_first, edge_last);
    return JObj().add("curve", jpts(xs, ys, 5)).num("last", last, 5).num("opt", opt).add("path", jai(path0)).num("plen", (double)path0.size() - 1)
        .num("rb", RB).num("epb", EPB).num("c13", c13).num("c15", c15).num("c17", c17).num("cother", cother).num("cfail", cfail).num("edge_first", edge_first, 4).num("edge_last", edge_last, 4);
}

static JObj e4() {
    const int RUNS = 10; const long long EP = 1000000;
    vector<double> xs; for (double x = 1; x <= EP; x *= 1.25) xs.push_back(floor(x)); xs.push_back((double)EP);
    xs.erase(unique(xs.begin(), xs.end()), xs.end());
    vector<string> ord, wtd; double ord_end_min = 1e9, ord_end_max = -1e9, maxrho = 0;
    for (int run = 0; run < RUNS; ++run) {
        Rng r(300 + run); double sum = 0, wsum = 0, csum = 0; size_t k = 0; vector<double> yo, yw;
        for (long long n = 1; n <= EP; ++n) {
            double rho = 1, G = 0;
            while (true) {
                int a = r.u() < 0.5 ? 0 : 1;   // 0 = left，1 = right（行为策略均匀）
                if (a == 1) { rho = 0; break; }  // 目标策略从不选 right：比值为 0
                rho *= 2.0;                      // π(left)/b(left) = 1 / 0.5
                if (r.u() < 0.1) { G = 1; break; }
            }
            sum += rho * G; wsum += rho * G; csum += rho; maxrho = max(maxrho, rho);
            while (k < xs.size() && xs[k] == (double)n) { yo.push_back(sum / n); yw.push_back(csum > 0 ? wsum / csum : 0.0); ++k; }
        }
        ord_end_min = min(ord_end_min, yo.back()); ord_end_max = max(ord_end_max, yo.back());
        if (run < 6) { ord.push_back(jpts(xs, yo, 5)); wtd.push_back(jpts(xs, yw, 5)); }
    }
    printf("[E4] 例 5.5（v_π = 1）：10 次运行、每次 10⁶ 个回合后普通 IS 估计在 [%.3f, %.3f]，加权 IS 恒为 1；单个回合最大比值 %.3g\n", ord_end_min, ord_end_max, maxrho);
    return JObj().add("ord", jlist(ord)).add("wtd", jlist(wtd)).num("ord_min", ord_end_min, 5).num("ord_max", ord_end_max, 5).num("maxrho", maxrho, 4).num("EP", (double)EP);
}

// ================================================================ E5 ε-greedy 策略改进定理
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
static JObj e5() {
    const int S = 12, A = 4; const double g = 0.9, eps = 0.2;
    Rng r(66); static double P[S][A][S], R[S][A];
    for (int s = 0; s < S; ++s) for (int a = 0; a < A; ++a) { double z = 0; for (int t = 0; t < S; ++t) { P[s][a][t] = pow(r.u(), 2.0); z += P[s][a][t]; } for (int t = 0; t < S; ++t) P[s][a][t] /= z; R[s][a] = r.uni(-1, 1); }
    auto evalp = [&](const vector<vector<double>>& pi, vector<vector<double>>& Q) {
        vector<vector<double>> M(S, vector<double>(S)); vector<double> b(S, 0);
        for (int s = 0; s < S; ++s) { for (int t = 0; t < S; ++t) M[s][t] = s == t; for (int a = 0; a < A; ++a) { b[s] += pi[s][a] * R[s][a]; for (int t = 0; t < S; ++t) M[s][t] -= g * pi[s][a] * P[s][a][t]; } }
        auto V = solve(M, b); Q.assign(S, vector<double>(A));
        for (int s = 0; s < S; ++s) for (int a = 0; a < A; ++a) { double q = R[s][a]; for (int t = 0; t < S; ++t) q += g * P[s][a][t] * V[t]; Q[s][a] = q; }
        return V;
    };
    vector<vector<double>> pi(S, vector<double>(A));
    for (int s = 0; s < S; ++s) { vector<double> z(A); for (auto& x : z) x = r.normal(); auto p = softmax(z); for (int a = 0; a < A; ++a) pi[s][a] = eps / A + (1 - eps) * p[a]; }   // 随机 ε-soft 初始策略
    vector<vector<double>> Q; auto V = evalp(pi, Q);
    vector<double> v0s; double mind = 1e9; int rounds = 0;
    vector<double> vstart = {V[0]}; vector<double> rx = {0};
    for (int it = 1; it <= 20; ++it) {
        vector<vector<double>> np(S, vector<double>(A, eps / A));
        for (int s = 0; s < S; ++s) { int b = (int)(max_element(Q[s].begin(), Q[s].end()) - Q[s].begin()); np[s][b] += 1 - eps; }
        vector<vector<double>> Q2; auto V2 = evalp(np, Q2);
        bool same = np == pi;
        if (same) { rounds = it - 1; break; }
        double d = 1e9; for (int s = 0; s < S; ++s) d = min(d, V2[s] - V[s]); mind = min(mind, d);
        pi = np; Q = Q2; V = V2; vstart.push_back(V[0]); rx.push_back(it);
    }
    // 最优 ε-soft 策略：v(s) = max_a [(1−ε)·q(s,a)] + ε/|A|·Σ_a q(s,a)，用价值迭代求解
    vector<double> W(S, 0), Vs(S, 0);
    for (int it = 0; it < 5000; ++it) {
        for (int s = 0; s < S; ++s) {
            double mx = -1e18, sm = 0;
            for (int a = 0; a < A; ++a) { double q = R[s][a]; for (int t = 0; t < S; ++t) q += g * P[s][a][t] * W[t]; mx = max(mx, q); sm += q; }
            Vs[s] = (1 - eps) * mx + eps / A * sm;
        }
        W = Vs;
    }
    vector<double> Vstar(S, 0);
    for (int it = 0; it < 5000; ++it) { vector<double> n(S); for (int s = 0; s < S; ++s) { double mx = -1e18; for (int a = 0; a < A; ++a) { double q = R[s][a]; for (int t = 0; t < S; ++t) q += g * P[s][a][t] * Vstar[t]; mx = max(mx, q); } n[s] = mx; } Vstar = n; }
    double diff = 0, gap = 0; for (int s = 0; s < S; ++s) { diff = max(diff, fabs(V[s] - W[s])); gap = max(gap, Vstar[s] - V[s]); }
    printf("[E5] ε=%.1f：%d 轮后策略不再变化；每轮各状态价值的最小增量 %.3e（≥ 0）；收敛点与最优 ε-soft 方程解最大差 %.2e；与真正最优 v* 的最大差距 %.4f\n",
           eps, rounds, mind, diff, gap);
    return JObj().num("rounds", rounds).num("mind", mind, 3).num("diff", diff, 3).num("gap", gap, 5).add("vstart", jpts(rx, vstart, 8)).num("vstar0", Vstar[0], 6).num("eps", eps);
}

// ================================================================ E6 最大化偏差 vs 动作数
static JObj e6() {
    vector<int> nbs = {1, 2, 5, 10, 20, 50};
    const int RUNS = 3000, EPN = 300; const double alpha = 0.1, eps = 0.1;
    const char* names[3] = {"Q-learning", "Expected Sarsa", "Double Q-learning"};
    vector<vector<double>> res(3, vector<double>(nbs.size()));
    parallel_for(3 * (int)nbs.size(), [&](int i) {
        int alg = i / (int)nbs.size(), k = i % (int)nbs.size(), nb = nbs[k];
        double left = 0;
        for (int run = 0; run < RUNS; ++run) {
            Rng r(400 + run);
            vector<double> A1(2, 0), A2(2, 0), B1(nb, 0), B2(nb, 0);
            auto amax = [&](const vector<double>& q) { double m = *max_element(q.begin(), q.end()); vector<int> c; for (int j = 0; j < (int)q.size(); ++j) if (q[j] == m) c.push_back(j); return c[r.below((int)c.size())]; };
            for (int ep = 0; ep < EPN; ++ep) {
                vector<double> qa(2); for (int j = 0; j < 2; ++j) qa[j] = alg == 2 ? A1[j] + A2[j] : A1[j];
                int a = r.u() < eps ? r.below(2) : amax(qa);
                if (ep >= EPN - 50 && a == 0) left += 1;
                if (a == 1) { if (alg == 2 && r.u() < 0.5) A2[1] += alpha * (0 - A2[1]); else A1[1] += alpha * (0 - A1[1]); continue; }
                // A → B（奖励 0）
                if (alg == 0) A1[0] += alpha * (*max_element(B1.begin(), B1.end()) - A1[0]);
                else if (alg == 1) {   // ε-greedy 下的期望
                    double m = *max_element(B1.begin(), B1.end()); int kk = 0; for (double x : B1) kk += x == m; double e = 0;
                    for (double x : B1) e += (eps / nb + (x == m ? (1 - eps) / kk : 0.0)) * x;
                    A1[0] += alpha * (e - A1[0]);
                } else if (r.u() < 0.5) A1[0] += alpha * (B2[amax(B1)] - A1[0]);
                else A2[0] += alpha * (B1[amax(B2)] - A2[0]);
                vector<double> qb(nb); for (int j = 0; j < nb; ++j) qb[j] = alg == 2 ? B1[j] + B2[j] : B1[j];
                int b = r.u() < eps ? r.below(nb) : amax(qb);
                double rew = -0.1 + r.normal();
                if (alg == 2 && r.u() < 0.5) B2[b] += alpha * (rew - B2[b]); else B1[b] += alpha * (rew - B1[b]);
            }
        }
        res[alg][k] = 100.0 * left / (RUNS * 50.0);
    });
    vector<string> out; vector<double> xs(nbs.begin(), nbs.end());
    for (int alg = 0; alg < 3; ++alg) {
        printf("[E6] %-17s 最后 50 回合选 left 的比例（B 的动作数 1/2/5/10/20/50）：", names[alg]); for (double v : res[alg]) printf(" %.1f%%", v); printf("\n");
        out.push_back(JObj().add("name", js(names[alg])).add("pts", jpts(xs, res[alg], 5)).add("v", ja(res[alg], 4)).str());
    }
    return JObj().add("runs", jlist(out));
}

int main() {
    JObj root;
    root.add("e1", e1().str());
    root.add("e2", e2().str());
    root.add("e3", e3().str());
    root.add("e4", e4().str());
    root.add("e5", e5().str());
    root.add("e6", e6().str());
    write_file("ch06_output.json", root.str());
    printf("写出 ch06_output.json\n");
}
