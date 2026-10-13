// 第 3 章实测：对线 MDP
//   E1 均匀策略：解析解 (I-γP)^-1 R  ⇄  迭代策略评估  ⇄  蒙特卡洛模拟（三方对拍）
//   E2 蒙特卡洛误差随回合数 N 的变化（30 次独立重复的 RMS，对照 σ/√N）
//   E3 价值迭代求 V*、Q*、π*
//   E4 穷举 8 个确定性策略，验证 π* 在每个状态上同时最优
// 输出：stdout（人读）+ ch03_output.json（build.py 读取并与 Python 对拍）
// 编译：cl /nologo /std:c++latest /EHsc /O2 /utf-8 ch03_mdp.cpp
#include <chrono>
#include <cmath>
#include <cstdio>
#include <random>
#include <string>
#include <utility>
#include <vector>
using namespace std;

struct MDP {
    int n;
    vector<vector<vector<pair<int, double>>>> P;  // P[s][a] = {(s', p)}
    vector<vector<double>> R;                       // R[s][a] = 期望即时奖励
    double g;
};

// 与 src/rlcore.py 的 LANE_P / LANE_R 完全相同
MDP lane(double g) {
    double P[3][2][3] = {{{0.9, 0.1, 0.0}, {0.3, 0.5, 0.2}},
                         {{0.4, 0.6, 0.0}, {0.2, 0.2, 0.6}},
                         {{1.0, 0.0, 0.0}, {0.8, 0.0, 0.2}}};
    double R[3][2] = {{1.2, 0.8}, {1.5, 2.5}, {-1.2, -0.8}};
    MDP m{3, {}, {}, g};
    m.P.assign(3, vector<vector<pair<int, double>>>(2));
    m.R.assign(3, vector<double>(2));
    for (int s = 0; s < 3; ++s)
        for (int a = 0; a < 2; ++a) {
            m.R[s][a] = R[s][a];
            for (int j = 0; j < 3; ++j)
                if (P[s][a][j] > 0) m.P[s][a].push_back({j, P[s][a][j]});
        }
    return m;
}
const char* SN[3] = {"对线", "团战", "回城"};
const char* AN[3][2] = {{"稳健补刀", "激进压线"}, {"后撤拉扯", "强行开团"}, {"快速上线", "补满状态"}};

using Pi = vector<vector<double>>;
Pi uniform_pi() { return Pi(3, vector<double>{0.5, 0.5}); }
Pi det_pi(const vector<int>& a) {
    Pi p(3, vector<double>(2, 0.0));
    for (int s = 0; s < 3; ++s) p[s][a[s]] = 1.0;
    return p;
}

// 高斯消元（列主元，消去所有其他行）——与 rlcore.solve 同一算法
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

vector<double> eval_exact(const MDP& m, const Pi& pi) {
    vector<vector<double>> A(m.n, vector<double>(m.n, 0.0));
    vector<double> b(m.n, 0.0);
    for (int s = 0; s < m.n; ++s) {
        A[s][s] = 1.0;
        for (int a = 0; a < 2; ++a) {
            b[s] += pi[s][a] * m.R[s][a];
            for (auto [s2, p] : m.P[s][a]) A[s][s2] -= m.g * pi[s][a] * p;
        }
    }
    return solve(A, b);
}

double qsa(const MDP& m, const vector<double>& V, int s, int a) {
    double acc = 0;
    for (auto [s2, p] : m.P[s][a]) acc += p * V[s2];
    return m.R[s][a] + m.g * acc;
}

int eval_iter(const MDP& m, const Pi& pi, double tol, vector<double>& V) {
    V.assign(m.n, 0.0);
    for (int k = 1;; ++k) {
        vector<double> nv(m.n);
        double delta = 0;
        for (int s = 0; s < m.n; ++s) {
            double v = 0;
            for (int a = 0; a < 2; ++a)
                if (pi[s][a] != 0) v += pi[s][a] * qsa(m, V, s, a);
            delta = max(delta, fabs(v - V[s]));
            nv[s] = v;
        }
        V = nv;
        if (delta < tol) return k;
    }
}

int value_iteration(const MDP& m, double tol, vector<double>& V) {
    V.assign(m.n, 0.0);
    for (int k = 1;; ++k) {
        vector<double> nv(m.n);
        double delta = 0;
        for (int s = 0; s < m.n; ++s) {
            double best = -INFINITY;
            for (int a = 0; a < 2; ++a) best = max(best, qsa(m, V, s, a));
            delta = max(delta, fabs(best - V[s]));
            nv[s] = best;
        }
        V = nv;
        if (delta < tol) return k;
    }
}

// 一条回合的折扣回报（策略按 pi 采样，奖励取期望即时奖励，视界截断 H）
double rollout(const MDP& m, const Pi& pi, int s, int H, mt19937_64& rng) {
    uniform_real_distribution<double> U(0.0, 1.0);
    double G = 0, disc = 1;
    for (int t = 0; t < H; ++t) {
        int a = U(rng) < pi[s][0] ? 0 : 1;
        G += disc * m.R[s][a];
        disc *= m.g;
        double u = U(rng), c = 0;
        int nx = m.P[s][a].back().first;
        for (auto [s2, p] : m.P[s][a]) {
            c += p;
            if (u < c) { nx = s2; break; }
        }
        s = nx;
    }
    return G;
}

string vec(const vector<double>& v) {
    string s = "[";
    char b[64];
    for (size_t i = 0; i < v.size(); ++i) {
        snprintf(b, sizeof b, "%s%.17g", i ? "," : "", v[i]);
        s += b;
    }
    return s + "]";
}

int main() {
    setvbuf(stdout, nullptr, _IONBF, 0);
    auto t0 = chrono::steady_clock::now();
    MDP m = lane(0.9);
    Pi u = uniform_pi();

    // ---------------- E1
    vector<double> Vu = eval_exact(m, u), Vit;
    int k_eval = eval_iter(m, u, 1e-10, Vit);
    double Rmax = 2.5;
    int H = (int)ceil(log(1e-6 * (1 - m.g) / Rmax) / log(m.g));
    printf("== E1 均匀随机策略的价值（γ=0.9）\n");
    printf("  解析解 (I-γP)^-1 R : %.6f %.6f %.6f\n", Vu[0], Vu[1], Vu[2]);
    printf("  迭代评估 tol=1e-10  : %.6f %.6f %.6f   （%d 次同步扫描）\n", Vit[0], Vit[1], Vit[2], k_eval);
    mt19937_64 rng(20261013);
    const int NB = 100000;
    vector<double> mean(3), se(3), err(3);
    double sigma0 = 0;
    for (int s = 0; s < 3; ++s) {
        double sum = 0, sq = 0;
        for (int i = 0; i < NB; ++i) {
            double G = rollout(m, u, s, H, rng);
            sum += G; sq += G * G;
        }
        mean[s] = sum / NB;
        double var = sq / NB - mean[s] * mean[s];
        se[s] = sqrt(var / NB);
        err[s] = mean[s] - Vu[s];
        if (s == 0) sigma0 = sqrt(var);
    }
    printf("  蒙特卡洛 N=%d, H=%d : %.4f±%.4f %.4f±%.4f %.4f±%.4f\n", NB, H, mean[0], se[0], mean[1], se[1], mean[2], se[2]);
    printf("  蒙特卡洛 - 解析解      : %+.4f %+.4f %+.4f   （单条回报标准差 σ(对线)=%.3f）\n", err[0], err[1], err[2], sigma0);

    // ---------------- E2
    printf("== E2 蒙特卡洛估计 V(对线) 的 RMS 误差（每个 N 独立重复 30 次）\n");
    int Ns[] = {10, 30, 100, 300, 1000, 3000, 10000};
    string rms_json = "[";
    for (int idx = 0; idx < 7; ++idx) {
        int N = Ns[idx];
        double se2 = 0;
        for (int rep = 0; rep < 30; ++rep) {
            double sum = 0;
            for (int i = 0; i < N; ++i) sum += rollout(m, u, 0, H, rng);
            double e = sum / N - Vu[0];
            se2 += e * e;
        }
        double rms = sqrt(se2 / 30);
        printf("  N=%-6d RMS=%.4f   σ/√N=%.4f\n", N, rms, sigma0 / sqrt((double)N));
        char b[96];
        snprintf(b, sizeof b, "%s[%d,%.17g]", idx ? "," : "", N, rms);
        rms_json += b;
    }
    rms_json += "]";

    // ---------------- E3
    vector<double> Vs;
    int k_vi = value_iteration(m, 1e-10, Vs);
    printf("== E3 价值迭代（tol=1e-10，%d 次扫描）\n", k_vi);
    vector<int> pistar(3);
    for (int s = 0; s < 3; ++s) {
        double q0 = qsa(m, Vs, s, 0), q1 = qsa(m, Vs, s, 1);
        pistar[s] = q1 > q0 + 1e-9 ? 1 : 0;
        printf("  %s: V*=%.6f  Q*(%s)=%.6f  Q*(%s)=%.6f  → π*=%s\n", SN[s], Vs[s], AN[s][0], q0, AN[s][1], q1, AN[s][pistar[s]]);
    }

    // ---------------- E4
    printf("== E4 穷举 8 个确定性策略（解析评估）\n");
    vector<double> B[3];
    int best = -1;
    for (int code = 0; code < 8; ++code) {
        vector<int> a = {(code >> 2) & 1, (code >> 1) & 1, code & 1};
        vector<double> V = eval_exact(m, det_pi(a));
        for (int s = 0; s < 3; ++s) B[s].push_back(V[s]);
        if (a == pistar) best = code;
        printf("  #%d %s·%s·%s : %8.4f %8.4f %8.4f%s\n", code, AN[0][a[0]], AN[1][a[1]], AN[2][a[2]], V[0], V[1], V[2], a == pistar ? "  ← π*" : "");
    }
    bool dom = true;
    for (int c = 0; c < 8; ++c)
        for (int s = 0; s < 3; ++s)
            if (B[s][best] < B[s][c] - 1e-12) dom = false;
    printf("  π* 在 3 个状态上同时不劣于其余 7 个策略：%s\n", dom ? "是" : "否");
    double ms = chrono::duration<double, milli>(chrono::steady_clock::now() - t0).count();
    printf("== 总耗时 %.1f ms\n", ms);

    FILE* f = fopen("ch03_output.json", "w");
    fprintf(f, "{\"Vu\":%s,\"Viter\":%s,\"k_eval\":%d,\"H\":%d,\n", vec(Vu).c_str(), vec(Vit).c_str(), k_eval, H);
    fprintf(f, " \"mc\":{\"N\":%d,\"mean\":%s,\"se\":%s,\"err\":%s,\"sigma\":%.17g,\"rms\":%s},\n", NB, vec(mean).c_str(), vec(se).c_str(), vec(err).c_str(), sigma0, rms_json.c_str());
    fprintf(f, " \"Vstar\":%s,\"k_vi\":%d,\"brute\":[%s,%s,%s],\"best\":%d,\"dominates\":%s,\"ms\":%.1f}\n", vec(Vs).c_str(), k_vi,
            vec(B[0]).c_str(), vec(B[1]).c_str(), vec(B[2]).c_str(), best, dom ? "true" : "false", ms);
    fclose(f);
    return 0;
}
