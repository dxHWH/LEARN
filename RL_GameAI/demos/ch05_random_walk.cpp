// 第 5 章实测：Random Walk 上的蒙特卡洛与时序差分
//   E1 5 状态随机游走（Sutton 例 6.2）：TD(0) / 常数步长 MC 的 RMS 学习曲线（100 次独立运行平均）+ TD 价值快照
//   E2 批量更新：批量 TD（= 确定性等价解）vs 批量 MC（= 回报样本均值），并用迭代批量 TD 验证不动点
//   E3 19 状态随机游走：n 步 TD、离线 λ-回报（= 离线 TD(λ)）、在线 TD(λ) 的 α-RMS 曲线（前 10 回合、100 次运行平均）
//   E4 恒等式数值验证：TD 误差累加 = MC 误差；λ-回报 = V + Σ(γλ)^k δ；离线前向 / 后向视角逐回合相等
// 随机数：xorshift64*（与 src/rlcore.py 完全相同），同一 run 的回合序列对所有算法相同（公共随机数），
//         build.py 会用 Python 重跑 run 0 的若干曲线逐位对拍。
// 输出：stdout + ch05_output.json
// 编译：cl /nologo /std:c++latest /EHsc /O2 /utf-8 ch05_random_walk.cpp
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>
using namespace std;

struct Rng {
    uint64_t s;
    explicit Rng(uint64_t seed) : s(seed * 0x9E3779B97F4A7C15ULL + 1) {}
    double u() {
        s ^= s >> 12; s ^= s << 25; s ^= s >> 27;
        return (double)((s * 2685821657736338717ULL) >> 11) * (1.0 / 9007199254740992.0);
    }
};

// 一条回合：states[0..T]，states[T] 是终止（0 或 n+1）；rewards[t] = R_{t+1}
struct Episode { vector<int> st; vector<double> rw; };

Episode gen(int n, Rng& rng, double left_r, double right_r) {
    Episode e;
    int s = (n + 1) / 2;
    e.st.push_back(s);
    while (s != 0 && s != n + 1) {
        s += rng.u() < 0.5 ? -1 : 1;
        e.st.push_back(s);
        e.rw.push_back(s == 0 ? left_r : (s == n + 1 ? right_r : 0.0));
    }
    return e;
}

double rms(const vector<double>& V, const vector<double>& tv, int n) {
    double acc = 0;
    for (int i = 1; i <= n; ++i) acc += (V[i] - tv[i]) * (V[i] - tv[i]);
    return sqrt(acc / n);
}

void td0(vector<double>& V, const Episode& e, double a, int n) {
    for (size_t t = 0; t + 1 < e.st.size(); ++t) {
        int s = e.st[t], s2 = e.st[t + 1];
        double v2 = (s2 == 0 || s2 == n + 1) ? 0.0 : V[s2];
        V[s] += a * (e.rw[t] + v2 - V[s]);
    }
}

void mc(vector<double>& V, const Episode& e, double a) {  // γ = 1，每次访问，常数步长
    double G = 0;
    vector<double> Gs(e.rw.size());
    for (int t = (int)e.rw.size() - 1; t >= 0; --t) { G = e.rw[t] + G; Gs[t] = G; }
    for (size_t t = 0; t < e.rw.size(); ++t) { int s = e.st[t]; V[s] += a * (Gs[t] - V[s]); }
}

void nstep(vector<double>& V, const Episode& e, int nn, double a, int n) {
    int T = (int)e.rw.size();
    for (int tau = 0; tau < T; ++tau) {
        double G = 0;
        int end = min(tau + nn, T);
        for (int i = tau + 1; i <= end; ++i) G += e.rw[i - 1];
        if (tau + nn < T) G += V[e.st[tau + nn]];
        int s = e.st[tau];
        V[s] += a * (G - V[s]);
    }
}
// 注：在线 n 步 TD 在时刻 t 更新 τ = t−n+1；由于更新顺序与 τ 的顺序一致、且 S_{τ+n} 的价值在 τ 时刻之前只会被更早的 τ 改动，
//     按 τ 依次执行与“边走边更新”的结果完全相同（回合已整条生成，动力学不依赖 V）。

void td_lambda(vector<double>& V, const Episode& e, double lam, double a, int n, bool online) {
    int T = (int)e.rw.size();
    vector<double> z(n + 2, 0.0), dV(n + 2, 0.0);
    for (int t = 0; t < T; ++t) {
        int s = e.st[t], s2 = e.st[t + 1];
        double v2 = (s2 == 0 || s2 == n + 1) ? 0.0 : V[s2];
        double delta = e.rw[t] + v2 - V[s];
        for (int i = 1; i <= n; ++i) z[i] *= lam;   // γ = 1
        z[s] += 1.0;                                // 累积迹
        for (int i = 1; i <= n; ++i) {
            if (online) V[i] += a * delta * z[i];
            else dV[i] += a * delta * z[i];
        }
    }
    if (!online) for (int i = 1; i <= n; ++i) V[i] += dV[i];
}

// ---------------------------------------------------------------- 批量解
// 批量 MC（每次访问）：V(s) = 所有访问 s 的回报的平均；未访问的状态保持初值
// 批量 TD：V = R̂ + P̂V（确定性等价：用经验转移频率建模后的精确价值），高斯消元求解
vector<double> batch_mc(const vector<Episode>& eps, int n, double init) {
    vector<double> sum(n + 2, 0.0), cnt(n + 2, 0.0), V(n + 2, init);
    for (auto& e : eps) {
        double G = 0;
        for (int t = (int)e.rw.size() - 1; t >= 0; --t) { G = e.rw[t] + G; sum[e.st[t]] += G; cnt[e.st[t]] += 1; }
    }
    for (int i = 1; i <= n; ++i) if (cnt[i] > 0) V[i] = sum[i] / cnt[i];
    V[0] = V[n + 1] = 0;
    return V;
}

vector<double> batch_td(const vector<Episode>& eps, int n, double init) {
    vector<vector<double>> C(n + 2, vector<double>(n + 2, 0.0));
    vector<double> Rs(n + 2, 0.0), cnt(n + 2, 0.0);
    for (auto& e : eps)
        for (size_t t = 0; t < e.rw.size(); ++t) { int s = e.st[t]; cnt[s] += 1; Rs[s] += e.rw[t]; C[s][e.st[t + 1]] += 1; }
    // 未访问的状态：方程 V(s) = init
    int m = n;
    vector<vector<double>> A(m, vector<double>(m + 1, 0.0));
    for (int i = 1; i <= n; ++i) {
        auto& row = A[i - 1];
        row[i - 1] = 1.0;
        if (cnt[i] == 0) { row[m] = init; continue; }
        row[m] = Rs[i] / cnt[i];
        for (int j = 1; j <= n; ++j) row[j - 1] -= C[i][j] / cnt[i];
    }
    for (int c = 0; c < m; ++c) {
        int p = c;
        for (int r = c; r < m; ++r) if (fabs(A[r][c]) > fabs(A[p][c])) p = r;
        swap(A[c], A[p]);
        for (int r = 0; r < m; ++r) {
            if (r == c || A[r][c] == 0) continue;
            double f = A[r][c] / A[c][c];
            for (int k = c; k <= m; ++k) A[r][k] -= f * A[c][k];
        }
    }
    vector<double> V(n + 2, 0.0);
    for (int i = 0; i < m; ++i) V[i + 1] = A[i][m] / A[i][i];
    return V;
}

// ---------------------------------------------------------------- JSON 小工具
string J(double v) { if (!isfinite(v) || v > 99.0) v = 99.0; char b[40]; snprintf(b, sizeof b, "%.17g", v); return b; }
string Jv(const vector<double>& v, int lo, int hi) {
    string s = "[";
    for (int i = lo; i <= hi; ++i) { s += J(v[i]); if (i < hi) s += ","; }
    return s + "]";
}
string Jpts(const vector<pair<double, double>>& p) {
    string s = "[";
    for (size_t i = 0; i < p.size(); ++i) { s += "[" + J(p[i].first) + "," + J(p[i].second) + "]"; if (i + 1 < p.size()) s += ","; }
    return s + "]";
}

int main() {
    const int RUNS = 100;
    FILE* js = fopen("ch05_output.json", "w");
    fprintf(js, "{\n");

    // ============================================================ E1
    {
        const int n = 5, EP = 100;
        vector<double> tv(n + 2, 0.0);
        for (int i = 1; i <= n; ++i) tv[i] = i / 6.0;
        printf("E1 5 状态随机游走（左端 0、右端 +1，γ=1，V0=0.5），%d 次运行 × %d 回合\n", RUNS, EP);
        printf("   真值 V = (1/6, 2/6, 3/6, 4/6, 5/6)\n");
        struct M { const char* m; double a; };
        vector<M> ms = {{"TD", 0.05}, {"TD", 0.1}, {"TD", 0.15}, {"MC", 0.01}, {"MC", 0.02}, {"MC", 0.03}, {"MC", 0.04}};
        vector<vector<double>> curve(ms.size(), vector<double>(EP + 1, 0.0));
        vector<vector<double>> snap;  // run 0, TD α=0.1 在第 0/1/10/100 回合后的价值
        vector<double> seed_td, seed_mc;
        for (int r = 0; r < RUNS; ++r) {
            Rng rng(1 + r);
            vector<Episode> eps;
            for (int k = 0; k < EP; ++k) eps.push_back(gen(n, rng, 0.0, 1.0));
            for (size_t j = 0; j < ms.size(); ++j) {
                vector<double> V(n + 2, 0.5); V[0] = V[n + 1] = 0;
                curve[j][0] += rms(V, tv, n) / RUNS;
                bool keep_snap = r == 0 && string(ms[j].m) == "TD" && ms[j].a == 0.1;
                if (keep_snap) snap.push_back(V);
                if (r == 0 && j == 1) seed_td.push_back(rms(V, tv, n));
                if (r == 0 && j == 4) seed_mc.push_back(rms(V, tv, n));
                for (int k = 0; k < EP; ++k) {
                    if (string(ms[j].m) == "TD") td0(V, eps[k], ms[j].a, n); else mc(V, eps[k], ms[j].a);
                    double e = rms(V, tv, n);
                    curve[j][k + 1] += e / RUNS;
                    if (keep_snap && (k + 1 == 1 || k + 1 == 10 || k + 1 == 100)) snap.push_back(V);
                    if (r == 0 && j == 1) seed_td.push_back(e);
                    if (r == 0 && j == 4) seed_mc.push_back(e);
                }
            }
        }
        fprintf(js, "\"rw5\":{\"true\":%s,\"snap\":[", Jv(tv, 1, n).c_str());
        for (size_t i = 0; i < snap.size(); ++i) fprintf(js, "%s%s", Jv(snap[i], 1, n).c_str(), i + 1 < snap.size() ? "," : "");
        fprintf(js, "],\"curves\":[");
        printf("   %-10s %10s %10s %10s %10s\n", "方法", "第 10 回合", "第 25 回合", "第 50 回合", "第 100 回合");
        for (size_t j = 0; j < ms.size(); ++j) {
            vector<pair<double, double>> p;
            for (int k = 0; k <= EP; ++k) p.push_back({(double)k, curve[j][k]});
            fprintf(js, "{\"m\":\"%s\",\"a\":%g,\"rms10\":%s,\"rms25\":%s,\"rms100\":%s,\"min\":%s,\"pts\":%s}%s", ms[j].m, ms[j].a,
                    J(curve[j][10]).c_str(), J(curve[j][25]).c_str(), J(curve[j][100]).c_str(),
                    J(*min_element(curve[j].begin(), curve[j].end())).c_str(), Jpts(p).c_str(), j + 1 < ms.size() ? "," : "");
            printf("   %s α=%-5g %10.4f %10.4f %10.4f %10.4f\n", ms[j].m, ms[j].a, curve[j][10], curve[j][25], curve[j][50], curve[j][100]);
        }
        fprintf(js, "],\"seed_td\":%s,\"seed_mc\":%s},\n", Jv(seed_td, 0, EP).c_str(), Jv(seed_mc, 0, EP).c_str());
        printf("   run 0 / TD α=0.1 的价值：第 100 回合后 = (%.3f, %.3f, %.3f, %.3f, %.3f)\n\n",
               snap.back()[1], snap.back()[2], snap.back()[3], snap.back()[4], snap.back()[5]);
    }

    // ============================================================ E2
    {
        const int n = 5, EP = 100;
        vector<double> tv(n + 2, 0.0);
        for (int i = 1; i <= n; ++i) tv[i] = i / 6.0;
        vector<double> ctd(EP + 1, 0.0), cmc(EP + 1, 0.0), seed_td, seed_mc;
        for (int r = 0; r < RUNS; ++r) {
            Rng rng(1 + r);
            vector<Episode> eps;
            for (int k = 1; k <= EP; ++k) {
                eps.push_back(gen(n, rng, 0.0, 1.0));
                double et = rms(batch_td(eps, n, 0.5), tv, n), em = rms(batch_mc(eps, n, 0.5), tv, n);
                ctd[k] += et / RUNS; cmc[k] += em / RUNS;
                if (r == 0) { seed_td.push_back(et); seed_mc.push_back(em); }
            }
        }
        // 迭代批量 TD（Sutton 6.3 的做法：α 很小，整批增量累加后一起更新，直到收敛）→ 与闭式解比较
        Rng rng(1);
        vector<Episode> eps;
        for (int k = 0; k < 20; ++k) eps.push_back(gen(n, rng, 0.0, 1.0));
        vector<double> V(n + 2, 0.5); V[0] = V[n + 1] = 0;
        int sweeps = 0;
        for (; sweeps < 1000000; ) {
            vector<double> d(n + 2, 0.0);
            for (auto& e : eps)
                for (size_t t = 0; t < e.rw.size(); ++t) {
                    int s = e.st[t], s2 = e.st[t + 1];
                    double v2 = (s2 == 0 || s2 == n + 1) ? 0.0 : V[s2];
                    d[s] += 0.01 * (e.rw[t] + v2 - V[s]);
                }
            double mx = 0;
            for (int i = 1; i <= n; ++i) { V[i] += d[i]; mx = max(mx, fabs(d[i])); }
            ++sweeps;
            if (mx < 1e-13) break;
        }
        vector<double> ce = batch_td(eps, n, 0.5);
        double diff = 0;
        for (int i = 1; i <= n; ++i) diff = max(diff, fabs(V[i] - ce[i]));
        vector<pair<double, double>> pt, pm;
        for (int k = 1; k <= EP; ++k) { pt.push_back({(double)k, ctd[k]}); pm.push_back({(double)k, cmc[k]}); }
        fprintf(js, "\"batch\":{\"td\":%s,\"mc\":%s,\"td100\":%s,\"mc100\":%s,\"td20\":%s,\"mc20\":%s,\"seed_td\":%s,\"seed_mc\":%s,\"iter_sweeps\":%d,\"iter_diff\":%s,\"ce20\":%s},\n",
                Jpts(pt).c_str(), Jpts(pm).c_str(), J(ctd[100]).c_str(), J(cmc[100]).c_str(), J(ctd[20]).c_str(), J(cmc[20]).c_str(),
                Jv(seed_td, 0, EP - 1).c_str(), Jv(seed_mc, 0, EP - 1).c_str(), sweeps, J(diff).c_str(), Jv(ce, 1, n).c_str());
        printf("E2 批量更新（每多一个回合就在全部已有数据上求批量解），%d 次运行平均 RMS\n", RUNS);
        printf("   回合数 %4s %10s %10s\n", "", "批量 TD", "批量 MC");
        for (int k : {1, 5, 10, 20, 50, 100}) printf("   %6d %4s %10.4f %10.4f\n", k, "", ctd[k], cmc[k]);
        printf("   迭代批量 TD（α=0.01，20 个回合）%d 次整批扫描后与确定性等价闭式解的最大差 = %.2e\n\n", sweeps, diff);
    }

    // ============================================================ E3
    {
        const int n = 19, EP = 10;
        vector<double> tv(n + 2, 0.0);
        for (int i = 1; i <= n; ++i) tv[i] = -1.0 + 2.0 * i / 20.0;
        vector<double> alphas;
        for (int i = 1; i <= 20; ++i) alphas.push_back(i * 0.05);
        vector<int> ns = {1, 2, 4, 8, 16, 32};
        vector<double> lams = {0.0, 0.4, 0.8, 0.9, 0.95, 0.975, 1.0};
        // 预生成所有 run 的回合（公共随机数）
        vector<vector<Episode>> all(RUNS);
        for (int r = 0; r < RUNS; ++r) { Rng rng(10001 + r); for (int k = 0; k < EP; ++k) all[r].push_back(gen(n, rng, -1.0, 1.0)); }
        auto score = [&](auto&& upd) {  // 前 10 个回合每个回合结束后的 RMS 的平均，再对 run 平均
            double tot = 0;
            for (int r = 0; r < RUNS; ++r) {
                vector<double> V(n + 2, 0.0);
                double s = 0;
                for (int k = 0; k < EP; ++k) { upd(V, all[r][k]); s += rms(V, tv, n); }
                tot += s / EP;
            }
            return tot / RUNS;
        };
        fprintf(js, "\"rw19\":{\"nstep\":[");
        printf("E3 19 状态随机游走（左端 −1、右端 +1，γ=1，V0=0），前 10 回合平均 RMS，%d 次运行平均\n", RUNS);
        printf("   n 步 TD：各 n 的最优 α 与最小误差\n");
        for (size_t i = 0; i < ns.size(); ++i) {
            vector<pair<double, double>> p;
            double best = 1e9, ba = 0;
            for (double a : alphas) {
                double v = score([&](vector<double>& V, const Episode& e) { nstep(V, e, ns[i], a, n); });
                p.push_back({a, v});
                if (v < best) { best = v; ba = a; }
            }
            fprintf(js, "{\"n\":%d,\"best\":%s,\"best_a\":%g,\"pts\":%s}%s", ns[i], J(best).c_str(), ba, Jpts(p).c_str(), i + 1 < ns.size() ? "," : "");
            printf("   n=%-3d 最优 α=%.2f  RMS=%.4f\n", ns[i], ba, best);
        }
        fprintf(js, "],");
        for (int online = 0; online < 2; ++online) {
            fprintf(js, "\"%s\":[", online ? "lam_on" : "lam_off");
            printf("   %s TD(λ)（累积迹）：各 λ 的最优 α 与最小误差\n", online ? "在线" : "离线（= 离线 λ-回报）");
            for (size_t i = 0; i < lams.size(); ++i) {
                vector<pair<double, double>> p;
                double best = 1e9, ba = 0;
                for (double a : alphas) {
                    double v = score([&](vector<double>& V, const Episode& e) { td_lambda(V, e, lams[i], a, n, online); });
                    if (!isfinite(v)) v = 99.0;
                    p.push_back({a, v});
                    if (v < best) { best = v; ba = a; }
                }
                fprintf(js, "{\"lam\":%g,\"best\":%s,\"best_a\":%g,\"pts\":%s}%s", lams[i], J(best).c_str(), ba, Jpts(p).c_str(), i + 1 < lams.size() ? "," : "");
                printf("   λ=%-6g 最优 α=%.2f  RMS=%.4f\n", lams[i], ba, best);
            }
            fprintf(js, "],");
        }
        // run 0 的 n=4、α=0.4 每回合 RMS（Python 对拍用）
        vector<double> V(n + 2, 0.0), seed;
        for (int k = 0; k < EP; ++k) { nstep(V, all[0][k], 4, 0.4, n); seed.push_back(rms(V, tv, n)); }
        vector<double> V2(n + 2, 0.0), seed2;
        for (int k = 0; k < EP; ++k) { td_lambda(V2, all[0][k], 0.8, 0.4, n, false); seed2.push_back(rms(V2, tv, n)); }
        double len = 0;
        for (int r = 0; r < RUNS; ++r) for (auto& e : all[r]) len += e.rw.size();
        fprintf(js, "\"seed_n4\":%s,\"seed_lam08\":%s,\"avg_len\":%s},\n", Jv(seed, 0, EP - 1).c_str(), Jv(seed2, 0, EP - 1).c_str(), J(len / (RUNS * EP)).c_str());
        printf("   平均回合长度 = %.1f 步\n\n", len / (RUNS * EP));
    }

    // ============================================================ E4
    {
        const int n = 19, N = 2000;
        const double g = 0.9, lam = 0.8;
        Rng rng(777);
        vector<double> V(n + 2, 0.0);
        for (int i = 1; i <= n; ++i) V[i] = rng.u() * 2 - 1;   // 任意（不准确的）价值估计
        double e1 = 0, e2 = 0, e3 = 0;
        long long steps = 0;
        for (int k = 0; k < N; ++k) {
            Episode e = gen(n, rng, -1.0, 1.0);
            for (auto& r : e.rw) r += rng.u() - 0.5;            // 加上中间奖励噪声，让恒等式更一般
            int T = (int)e.rw.size();
            steps += T;
            auto Vs = [&](int t) { int s = e.st[t]; return (s == 0 || s == n + 1) ? 0.0 : V[s]; };
            vector<double> delta(T), G(T + 1, 0.0), GL(T + 1, 0.0);
            for (int t = 0; t < T; ++t) delta[t] = e.rw[t] + g * Vs(t + 1) - Vs(t);
            for (int t = T - 1; t >= 0; --t) G[t] = e.rw[t] + g * G[t + 1];
            // λ-回报：按定义（n 步回报的加权和，回合结束后的全部权重给 G_t）
            for (int t = 0; t < T; ++t) {
                double acc = 0, w = 1 - lam, Gn = 0, disc = 1;
                for (int k2 = 1; t + k2 <= T; ++k2) {
                    Gn += disc * e.rw[t + k2 - 1];
                    disc *= g;
                    if (t + k2 < T) { acc += w * (Gn + disc * Vs(t + k2)); w *= lam; }
                }
                acc += pow(lam, T - t - 1) * G[t];
                GL[t] = acc;
            }
            // ① G_t − V(S_t) = Σ γ^k δ_{t+k}；② G^λ_t − V(S_t) = Σ (γλ)^k δ_{t+k}
            for (int t = 0; t < T; ++t) {
                double s1 = 0, s2 = 0, d1 = 1, d2 = 1;
                for (int k2 = t; k2 < T; ++k2) { s1 += d1 * delta[k2]; s2 += d2 * delta[k2]; d1 *= g; d2 *= g * lam; }
                e1 = max(e1, fabs(G[t] - Vs(t) - s1));
                e2 = max(e2, fabs(GL[t] - Vs(t) - s2));
            }
            // ③ 离线：Σ_t δ_t z_t(s) = Σ_t (G^λ_t − V(S_t))·1[S_t = s]
            vector<double> fw(n + 2, 0.0), bw(n + 2, 0.0), z(n + 2, 0.0);
            for (int t = 0; t < T; ++t) {
                fw[e.st[t]] += GL[t] - Vs(t);
                for (int i = 1; i <= n; ++i) z[i] *= g * lam;
                z[e.st[t]] += 1;
                for (int i = 1; i <= n; ++i) bw[i] += delta[t] * z[i];
            }
            for (int i = 1; i <= n; ++i) e3 = max(e3, fabs(fw[i] - bw[i]));
        }
        fprintf(js, "\"ident\":{\"episodes\":%d,\"steps\":%lld,\"gamma\":%g,\"lam\":%g,\"td_sum\":%s,\"lam_ret\":%s,\"fwd_bwd\":%s}\n",
                N, steps, g, lam, J(e1).c_str(), J(e2).c_str(), J(e3).c_str());
        printf("E4 恒等式数值验证（19 状态、随机 V、带噪声中间奖励，γ=%.1f、λ=%.1f，%d 个回合 / %lld 步）\n", g, lam, N, steps);
        printf("   ① max|G_t − V(S_t) − Σγ^k δ_{t+k}|           = %.2e\n", e1);
        printf("   ② max|G^λ_t − V(S_t) − Σ(γλ)^k δ_{t+k}|      = %.2e\n", e2);
        printf("   ③ max|前向视角总增量 − 后向视角（资格迹）总增量| = %.2e\n", e3);
    }
    fprintf(js, "}\n");
    fclose(js);
    return 0;
}
