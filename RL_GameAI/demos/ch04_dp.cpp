// 第 4 章实测：网格世界上的动态规划
//   E1 4×4 网格（Sutton 例 4.1）：均匀随机策略的精确价值
//   E2 地牢地图（无滑步 / 20% 滑步）：同步 VI、原地 VI 的扫描次数，策略迭代轮数（与 Python 对拍）
//   E3 γ 对扫描次数的影响：实测 vs 理论上界 ⌈log(tol/‖V1−V0‖)/log γ⌉+1
//   E4 规模化计时：随机迷宫 n×n，同步 VI / 原地 VI / 策略迭代（热启动的原地迭代评估）
// 输出：stdout + ch04_output.json
// 编译：cl /nologo /std:c++latest /EHsc /O2 /utf-8 ch04_dp.cpp
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <random>
#include <string>
#include <vector>
using namespace std;

struct Out { int s; double p; };
struct MDP {
    int n = 0;
    vector<vector<vector<Out>>> P;  // P[s][a]，终止状态没有动作
    vector<vector<double>> R;
    double g = 0.9;
};
const int DR[4] = {-1, 0, 1, 0}, DC[4] = {0, 1, 0, -1};  // ↑ → ↓ ←（与 rlcore.DIRS 同序）

struct Grid {
    vector<string> rows;
    int H, W;
    vector<int> idx;  // r*W+c → 状态号，墙为 -1
    vector<pair<int, int>> cells;
    MDP m;
    int start = 0;
    int move(int r, int c, int d) const {
        int r2 = r + DR[d], c2 = c + DC[d];
        if (r2 >= 0 && r2 < H && c2 >= 0 && c2 < W && rows[r2][c2] != '#') return idx[r2 * W + c2];
        return idx[r * W + c];
    }
    Grid(const vector<string>& rs, double gamma, double slip, double step, double goal, double trap) : rows(rs) {
        H = (int)rows.size(); W = (int)rows[0].size();
        idx.assign(H * W, -1);
        for (int r = 0; r < H; ++r)
            for (int c = 0; c < W; ++c)
                if (rows[r][c] != '#') { idx[r * W + c] = (int)cells.size(); cells.push_back({r, c}); }
        m.n = (int)cells.size(); m.g = gamma;
        m.P.resize(m.n); m.R.resize(m.n);
        for (int s = 0; s < m.n; ++s) {
            auto [r, c] = cells[s];
            char ch = rows[r][c];
            if (ch == 'S') start = s;
            if (ch == 'G' || ch == 'X' || ch == 'T') continue;
            m.P[s].resize(4); m.R[s].assign(4, 0.0);
            for (int a = 0; a < 4; ++a) {
                vector<pair<int, double>> outs;
                if (slip == 0) outs = {{a, 1.0 - slip}};
                else outs = {{a, 1.0 - slip}, {(a + 1) % 4, slip / 2}, {(a + 3) % 4, slip / 2}};
                double er = 0;
                for (auto [d, p] : outs) {
                    int j = move(r, c, d);
                    char cj = rows[cells[j].first][cells[j].second];
                    double bonus = cj == 'G' ? goal : (cj == 'X' ? trap : 0.0);
                    er += p * (step + bonus);
                    bool merged = false;
                    for (auto& e : m.P[s][a])
                        if (e.s == j) { e.p += p; merged = true; break; }
                    if (!merged) m.P[s][a].push_back({j, p});
                }
                m.R[s][a] = er;
            }
        }
    }
};

inline double qsa(const MDP& m, const vector<double>& V, int s, int a) {
    double acc = 0;
    for (const Out& o : m.P[s][a]) acc += o.p * V[o.s];
    return m.R[s][a] + m.g * acc;
}

// rev=true：原地更新时按状态号倒序扫描（随机迷宫的目标在右下角 → 从目标往外扫）
int value_iteration(const MDP& m, double tol, bool inplace, vector<double>& V, bool rev = false) {
    V.assign(m.n, 0.0);
    vector<double> nv(m.n);
    for (int k = 1;; ++k) {
        double delta = 0;
        for (int i = 0; i < m.n; ++i) {
            int s = rev ? m.n - 1 - i : i;
            if (m.P[s].empty()) { (inplace ? V : nv)[s] = 0; continue; }
            double best = -INFINITY;
            for (int a = 0; a < (int)m.P[s].size(); ++a) best = max(best, qsa(m, V, s, a));
            delta = max(delta, fabs(best - V[s]));
            (inplace ? V : nv)[s] = best;
        }
        if (!inplace) V.swap(nv);
        if (delta < tol) return k;
    }
}

vector<double> solve(vector<vector<double>> A, vector<double> b) {
    int n = (int)A.size();
    for (int i = 0; i < n; ++i) A[i].push_back(b[i]);
    for (int c = 0; c < n; ++c) {
        int p = c;
        for (int r = c; r < n; ++r)
            if (fabs(A[r][c]) > fabs(A[p][c])) p = r;
        swap(A[c], A[p]);
        for (int r = 0; r < n; ++r)
            if (r != c && A[r][c] != 0) {
                double f = A[r][c] / A[c][c];
                for (int k = 0; k <= n; ++k) A[r][k] -= f * A[c][k];
            }
    }
    vector<double> x(n);
    for (int i = 0; i < n; ++i) x[i] = A[i][n] / A[i][i];
    return x;
}

// 策略 pi[s][a] 的精确价值
vector<double> eval_exact(const MDP& m, const vector<vector<double>>& pi) {
    vector<vector<double>> A(m.n, vector<double>(m.n, 0.0));
    vector<double> b(m.n, 0.0);
    for (int s = 0; s < m.n; ++s) {
        A[s][s] = 1.0;
        for (int a = 0; a < (int)m.P[s].size(); ++a) {
            b[s] += pi[s][a] * m.R[s][a];
            for (const Out& o : m.P[s][a]) A[s][o.s] -= m.g * pi[s][a] * o.p;
        }
    }
    return solve(A, b);
}

// 原地迭代评估确定性策略（热启动）；返回扫描次数
int eval_det_inplace(const MDP& m, const vector<int>& act, double tol, vector<double>& V) {
    for (int k = 1;; ++k) {
        double delta = 0;
        for (int s = 0; s < m.n; ++s) {
            if (m.P[s].empty()) { V[s] = 0; continue; }
            double v = qsa(m, V, s, act[s]);
            delta = max(delta, fabs(v - V[s]));
            V[s] = v;
        }
        if (delta < tol) return k;
    }
}

struct PIRes { int rounds = 0; long long eval_sweeps = 0; vector<double> V; };
// 改进规则：只有严格更好（> 旧动作 + 1e-9）才换，与 rlcore.policy_iteration 一致
PIRes policy_iteration(const MDP& m, int a0, bool exact, double tol) {
    PIRes r;
    vector<int> act(m.n, a0);
    r.V.assign(m.n, 0.0);
    while (true) {
        ++r.rounds;
        if (exact) {
            vector<vector<double>> pi(m.n);
            for (int s = 0; s < m.n; ++s) {
                pi[s].assign(m.P[s].size(), 0.0);
                if (!m.P[s].empty()) pi[s][act[s]] = 1.0;
            }
            r.V = eval_exact(m, pi);
        } else {
            r.eval_sweeps += eval_det_inplace(m, act, tol, r.V);
        }
        bool stable = true;
        for (int s = 0; s < m.n; ++s) {
            if (m.P[s].empty()) continue;
            int b = 0;
            double qb = qsa(m, r.V, s, 0);
            for (int a = 1; a < 4; ++a) {
                double q = qsa(m, r.V, s, a);
                if (q > qb) { qb = q; b = a; }
            }
            if (qb > qsa(m, r.V, s, act[s]) + 1e-9) { act[s] = b; stable = false; }
        }
        if (stable) return r;
    }
}

string vec(const vector<double>& v) {
    string s = "[";
    char b[64];
    for (size_t i = 0; i < v.size(); ++i) { snprintf(b, sizeof b, "%s%.17g", i ? "," : "", v[i]); s += b; }
    return s + "]";
}
template <class F> double best_ms(F f, int rep = 3) {
    double b = 1e18;
    for (int i = 0; i < rep; ++i) {
        auto t = chrono::steady_clock::now();
        f();
        b = min(b, chrono::duration<double, milli>(chrono::steady_clock::now() - t).count());
    }
    return b;
}

int main() {
    setvbuf(stdout, nullptr, _IONBF, 0);
    FILE* js = fopen("ch04_output.json", "w");
    fprintf(js, "{");

    // ---------------- E1
    vector<string> sut = {"T...", "....", "....", "...T"};
    Grid gs(sut, 1.0, 0.0, -1.0, 10, -10);
    vector<vector<double>> uni(gs.m.n);
    for (int s = 0; s < gs.m.n; ++s) uni[s].assign(gs.m.P[s].size(), 0.25);
    vector<double> Vs = eval_exact(gs.m, uni);
    printf("== E1 4x4 网格，均匀随机策略，γ=1，每步 -1\n");
    for (int r = 0; r < 4; ++r) {
        printf("  ");
        for (int c = 0; c < 4; ++c) printf("%7.2f", Vs[gs.idx[r * 4 + c]]);
        printf("\n");
    }
    fprintf(js, "\"sutton_V\":%s,\n", vec(Vs).c_str());

    // ---------------- E2
    vector<string> dng = {".........", ".........", ".#XXXXX#.", "S.......G", ".#XXXXX#.", ".........", "........."};
    printf("== E2 地牢地图 7x9（γ=0.95，每步 -0.1，目标 +10，陷阱 -10），tol=1e-10\n");
    fprintf(js, "\"dungeon\":{");
    const char* tags[2] = {"s0", "s2"};
    double slips[2] = {0.0, 0.2};
    for (int t = 0; t < 2; ++t) {
        Grid g(dng, 0.95, slips[t], -0.1, 10, -10);
        vector<double> V1, V2;
        int k1 = value_iteration(g.m, 1e-10, false, V1);
        int k2 = value_iteration(g.m, 1e-10, true, V2);
        PIRes pe = policy_iteration(g.m, 1, true, 0);
        PIRes pit = policy_iteration(g.m, 1, false, 1e-10);
        printf("  滑步 %.0f%%：V*(S)=%.6f  同步 VI %d 次扫描，原地 VI %d 次；策略迭代 %d 轮（精确评估），迭代评估版 %d 轮 / 共 %lld 次评估扫描\n",
               slips[t] * 100, V1[g.start], k1, k2, pe.rounds, pit.rounds, pit.eval_sweeps);
        fprintf(js, "%s\"%s\":{\"Vstart\":%.17g,\"sweeps\":%d,\"sweeps_inplace\":%d,\"pi_rounds\":%d,\"pi_iter_rounds\":%d,\"pi_eval_sweeps\":%lld}",
                t ? "," : "", tags[t], V1[g.start], k1, k2, pe.rounds, pit.rounds, pit.eval_sweeps);
    }
    fprintf(js, "},\n");

    // ---------------- E3
    printf("== E3 γ 对扫描次数的影响（地牢，滑步 20%%，tol=1e-10）\n");
    double gl[] = {0.5, 0.7, 0.9, 0.95, 0.99, 0.999};
    fprintf(js, "\"gamma\":[");
    for (int i = 0; i < 6; ++i) {
        Grid g(dng, gl[i], 0.2, -0.1, 10, -10);
        double d1 = 0;
        for (int s = 0; s < g.m.n; ++s) {
            if (g.m.P[s].empty()) continue;
            double b = -INFINITY;
            for (int a = 0; a < 4; ++a) b = max(b, g.m.R[s][a]);
            d1 = max(d1, fabs(b));
        }
        vector<double> V;
        int k = value_iteration(g.m, 1e-10, false, V);
        int ki = value_iteration(g.m, 1e-10, true, V);
        PIRes pe = policy_iteration(g.m, 1, true, 0);
        int theory = (int)ceil(log(1e-10 / d1) / log(gl[i])) + 1;
        printf("  γ=%-6g 同步 VI %6d 次（理论上界 %6d），原地 VI %6d 次，策略迭代 %d 轮\n", gl[i], k, theory, ki, pe.rounds);
        fprintf(js, "%s{\"g\":%g,\"vi\":%d,\"vi_in\":%d,\"theory\":%d,\"pi\":%d}", i ? "," : "", gl[i], k, ki, theory, pe.rounds);
    }
    fprintf(js, "],\n");

    // ---------------- E4
    printf("== E4 随机迷宫 n×n（15%% 墙，5%% 陷阱，到不了目标的格子改成墙；滑步 20%%，每步 -0.1，tol=1e-8），取 3 次最快\n");
    fprintf(js, "\"scale\":[");
    int first = 1;
    for (double gamma : {0.9, 0.99}) {
        for (int n : {10, 20, 40}) {
            mt19937 rng(7 + n);
            uniform_real_distribution<double> U(0, 1);
            vector<string> rows(n, string(n, '.'));
            for (int r = 0; r < n; ++r)
                for (int c = 0; c < n; ++c) {
                    double u = U(rng);
                    rows[r][c] = u < 0.15 ? '#' : (u < 0.20 ? 'X' : '.');
                }
            rows[0][0] = 'S'; rows[n - 1][n - 1] = 'G';
            // 从目标出发 BFS（不穿过陷阱），到不了目标的空地改成墙：避免“封闭小房间”主导收敛速度
            vector<int> seen(n * n, 0), q = {(n - 1) * n + n - 1};
            seen[q[0]] = 1;
            for (size_t h = 0; h < q.size(); ++h) {
                int r = q[h] / n, c = q[h] % n;
                for (int d = 0; d < 4; ++d) {
                    int r2 = r + DR[d], c2 = c + DC[d];
                    if (r2 < 0 || r2 >= n || c2 < 0 || c2 >= n || seen[r2 * n + c2] || rows[r2][c2] == '#') continue;
                    seen[r2 * n + c2] = 1;
                    if (rows[r2][c2] != 'X') q.push_back(r2 * n + c2);
                }
            }
            for (int r = 0; r < n; ++r)
                for (int c = 0; c < n; ++c)
                    if (!seen[r * n + c]) rows[r][c] = '#';
            Grid g(rows, gamma, 0.2, -0.1, 10, -10);
            vector<double> Va, Vb, Vc;
            int ka = 0, kb = 0, kc = 0;
            PIRes pr;
            double ta = best_ms([&] { ka = value_iteration(g.m, 1e-8, false, Va); });
            double tb = best_ms([&] { kb = value_iteration(g.m, 1e-8, true, Vb); });
            double tc = best_ms([&] { kc = value_iteration(g.m, 1e-8, true, Vc, true); });
            double tp = best_ms([&] { pr = policy_iteration(g.m, 1, false, 1e-8); });
            // “躲起来”的格子：最优动作是撞墙不动（自转移概率 ≥ 0.8），价值 ≈ step/(1-γ)，误差每次扫描恰好乘 γ
            int hide = 0;
            for (int s = 0; s < g.m.n; ++s) {
                if (g.m.P[s].empty()) continue;
                int b = 0;
                for (int a = 1; a < 4; ++a) if (qsa(g.m, Va, s, a) > qsa(g.m, Va, s, b)) b = a;
                for (const Out& o : g.m.P[s][b]) if (o.s == s && o.p >= 0.8 - 1e-12) ++hide;
            }
            double md = 0;
            for (int s = 0; s < g.m.n; ++s) md = max({md, fabs(Va[s] - Vb[s]), fabs(Va[s] - Vc[s]), fabs(Va[s] - pr.V[s])});
            printf("  γ=%-4g n=%-3d 状态 %5d | 同步 VI %5d 次 %7.2f ms | 原地正序 %5d 次 %7.2f ms | 原地倒序 %5d 次 %7.2f ms | PI %2d 轮 / %6lld 次评估扫描 %7.2f ms | 最大差 %.1e | 撞墙躲避格 %d\n",
                   gamma, n, g.m.n, ka, ta, kb, tb, kc, tc, pr.rounds, pr.eval_sweeps, tp, md, hide);
            fprintf(js, "%s{\"g\":%g,\"n\":%d,\"S\":%d,\"vi\":%d,\"vi_ms\":%.3f,\"vin\":%d,\"vin_ms\":%.3f,\"vrev\":%d,\"vrev_ms\":%.3f,\"pi\":%d,\"pi_sweeps\":%lld,\"pi_ms\":%.3f,\"maxdiff\":%.3g,\"hide\":%d}",
                    first ? "" : ",", gamma, n, g.m.n, ka, ta, kb, tb, kc, tc, pr.rounds, pr.eval_sweeps, tp, md, hide);
            first = 0;
        }
    }
    fprintf(js, "]}\n");
    fclose(js);
    return 0;
}
