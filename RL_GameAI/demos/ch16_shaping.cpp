// 第 16 章实测：奖励塑形
//   E1 9×9 开阔房间，目标在右上角（奖励 +1，终止），γ=0.95。三种奖励下用价值迭代求最优策略：
//      原始稀疏奖励 / 势函数塑形 F = γΦ(s′) − Φ(s)（Φ = −0.2·曼哈顿距离）/ 朴素塑形（每靠近一步 +0.2，远离不扣分）；
//      比较最优策略是否一致、贪心策略能否到达目标，并数值验证 Q′* = Q* − Φ
//   E2 21×21 房间上的 Q-learning（ε-greedy，20 个种子）：原始 / 势函数塑形 / 朴素塑形，每回合到达目标的比例与步数
// 输出：stdout + ch16_output.json
// 编译：cl /nologo /std:c++latest /EHsc /O2 /utf-8 ch16_shaping.cpp
#include "rlcommon.h"
#include <array>
using namespace std;

const int DX[4] = {0, 1, 0, -1}, DY[4] = {-1, 0, 1, 0};
struct Room {
    int N, goal;
    explicit Room(int n) : N(n), goal(n - 1) {}   // 目标在第 0 行最后一列（右上角）
    int move(int s, int a) const { int x = s % N + DX[a], y = s / N + DY[a]; if (x < 0 || y < 0 || x >= N || y >= N) return s; return y * N + x; }
    int dist(int s) const { return abs(s % N - (N - 1)) + abs(s / N - 0); }
};
// mode 0 原始；1 势函数塑形；2 朴素塑形
static double reward(const Room& R, int s, int s2, int mode, double g) {
    double r = s2 == R.goal ? 1.0 : 0.0;
    if (mode == 1) { double phi = -0.2 * R.dist(s), phi2 = s2 == R.goal ? 0.0 : -0.2 * R.dist(s2); r += g * phi2 - phi; }   // 终止状态的势取 0
    if (mode == 2 && R.dist(s2) < R.dist(s)) r += 0.2;
    return r;
}
static vector<array<double, 4>> vi(const Room& R, int mode, double g) {
    int n = R.N * R.N; vector<double> V(n, 0); vector<array<double, 4>> Q(n);
    for (int it = 0; it < 5000; ++it) {
        double diff = 0;
        for (int s = 0; s < n; ++s) {
            if (s == R.goal) continue;
            double b = -1e9;
            for (int a = 0; a < 4; ++a) { int s2 = R.move(s, a); Q[s][a] = reward(R, s, s2, mode, g) + (s2 == R.goal ? 0 : g * V[s2]); b = max(b, Q[s][a]); }
            diff = max(diff, fabs(b - V[s])); V[s] = b;
        }
        if (diff < 1e-13) break;
    }
    return Q;
}
static int greedy(const array<double, 4>& q) { int b = 0; for (int a = 1; a < 4; ++a) if (q[a] > q[b] + 1e-12) b = a; return b; }

static JObj e1() {
    Room R(9); const double g = 0.95; int n = 81;
    auto Q0 = vi(R, 0, g), Q1 = vi(R, 1, g), Q2 = vi(R, 2, g);
    // 最优动作集合是否一致（考虑并列）
    auto optset = [&](const array<double, 4>& q) { double m = *max_element(q.begin(), q.end()); int mask = 0; for (int a = 0; a < 4; ++a) if (q[a] > m - 1e-9) mask |= 1 << a; return mask; };
    int same1 = 0, same2 = 0, cnt = 0; double qdiff = 0;
    for (int s = 0; s < n; ++s) {
        if (s == R.goal) continue; ++cnt;
        same1 += optset(Q0[s]) == optset(Q1[s]); same2 += optset(Q0[s]) == optset(Q2[s]);
        double phi = -0.2 * R.dist(s);
        for (int a = 0; a < 4; ++a) qdiff = max(qdiff, fabs(Q1[s][a] - (Q0[s][a] - phi)));
    }
    auto run = [&](const vector<array<double, 4>>& Q, vector<double>* path) { int s = (R.N - 1) * R.N, t = 0; if (path) path->push_back(s); for (; t < 200 && s != R.goal; ++t) { s = R.move(s, greedy(Q[s])); if (path && t < 40) path->push_back(s); } return s == R.goal ? t : -1; };
    vector<double> p0, p2; int t0 = run(Q0, &p0), t1 = run(Q1, nullptr), t2 = run(Q2, &p2);
    // 朴素塑形下，贴近目标的格子上“来回走”的价值 vs “直接进门”的价值
    int adj = R.goal + R.N;   // 目标正下方
    double loop = g * 0.2 / (1 - g * g), enter = 1.0 + 0.2;   // 先退一步（0）再回来（+0.2），无限循环
    printf("[E1] 9×9 房间：势函数塑形的最优动作集合与原始一致 %d/%d 个状态，max|Q′−(Q−Φ)| = %.2e；朴素塑形一致 %d/%d\n", same1, cnt, qdiff, same2, cnt);
    printf("     从左下角出发的贪心策略：原始 %d 步到达，势函数塑形 %d 步到达，朴素塑形 %s\n", t0, t1, t2 < 0 ? "200 步内到不了（在目标附近来回刷分）" : "到达");
    printf("     朴素塑形下目标正下方的格子：退一步再回来的循环价值 γ·0.2/(1−γ²) = %.3f > 直接进门 1 + 0.2 = %.3f；VI 给出 Q(进门) = %.3f，max Q = %.3f\n",
           loop, enter, Q2[adj][0], *max_element(Q2[adj].begin(), Q2[adj].end()));
    // 价值 / 策略网格（画图用）
    vector<int> pol0(n), pol2(n); vector<double> v0(n), v2(n);
    for (int s = 0; s < n; ++s) { pol0[s] = s == R.goal ? -1 : greedy(Q0[s]); pol2[s] = s == R.goal ? -1 : greedy(Q2[s]); v0[s] = s == R.goal ? 0 : *max_element(Q0[s].begin(), Q0[s].end()); v2[s] = s == R.goal ? 0 : *max_element(Q2[s].begin(), Q2[s].end()); }
    return JObj().num("same1", same1).num("same2", same2).num("cnt", cnt).num("qdiff", qdiff, 3).num("t0", t0).num("t1", t1).num("t2", t2)
        .num("loop", loop, 5).num("enter", enter, 5).num("q_enter", Q2[adj][0], 5).num("q_max", *max_element(Q2[adj].begin(), Q2[adj].end()), 5)
        .add("pol0", jai(pol0)).add("pol2", jai(pol2)).add("v0", ja(v0, 5)).add("v2", ja(v2, 5)).add("path0", ja(p0, 4)).add("path2", ja(p2, 4)).num("N", R.N);
}

static JObj e2() {
    vector<double> shaped; double eqdiff = 0;
    Room R(21); const double g = 0.99, alpha = 0.2, eps = 0.1; const int EP = 1000, SEEDS = 20, TMAX = 1000;
    vector<string> out; const char* names[4] = {"原始稀疏奖励", "势函数塑形", "朴素塑形（靠近 +0.2）", "原始奖励 + 初值 Q₀ = Φ"};
    for (int mode = 0; mode < 4; ++mode) {
        vector<double> steps(EP, 0), succ(EP, 0);
        for (int sd = 0; sd < SEEDS; ++sd) {
            Rng r(100 * sd + 7); int n = R.N * R.N; vector<array<double, 4>> Q(n, {0, 0, 0, 0});
            if (mode == 3) for (int s = 0; s < n; ++s) for (int a = 0; a < 4; ++a) Q[s][a] = -0.2 * R.dist(s);   // Wiewiora (2003)：等价于势函数塑形
            for (int ep = 0; ep < EP; ++ep) {
                int s = (R.N - 1) * R.N, t = 0;
                for (; t < TMAX && s != R.goal; ++t) {
                    int a = r.u() < eps ? r.below(4) : [&] { double m = *max_element(Q[s].begin(), Q[s].end()); int c[4], k = 0; for (int b = 0; b < 4; ++b) if (Q[s][b] > m - 1e-9) c[k++] = b; return c[r.below(k)]; }();   // 平局用容差判断，避免两种等价参数化因舍入误差走上不同轨迹
                    int s2 = R.move(s, a); double rw = reward(R, s, s2, mode == 3 ? 0 : mode, g);
                    double tgt = rw + (s2 == R.goal ? 0 : g * *max_element(Q[s2].begin(), Q[s2].end()));
                    Q[s][a] += alpha * (tgt - Q[s][a]); s = s2;
                }
                steps[ep] += (double)t / SEEDS; succ[ep] += (s == R.goal) / (double)SEEDS;
            }
        }
        vector<double> xs, ys, ss; double first = 0, last = 0, lsucc = 0;
        for (int ep = 0; ep < EP; ep += 10) { double m = 0, q = 0; for (int k = ep; k < ep + 10; ++k) { m += steps[k] / 10; q += succ[k] / 10; } xs.push_back(ep + 10); ys.push_back(m); ss.push_back(q); }
        for (int k = 0; k < 20; ++k) first += steps[k] / 20;
        for (int k = EP - 100; k < EP; ++k) { last += steps[k] / 100; lsucc += succ[k] / 100; }
        printf("[E2] %-20s 前 20 回合平均 %.0f 步，最后 100 回合平均 %.1f 步，到达率 %.2f（最短路 %d 步）\n", names[mode], first, last, lsucc, 2 * (R.N - 1));
        if (mode == 1) shaped = steps; if (mode == 3) { double md = 0; for (int k = 0; k < EP; ++k) md = max(md, fabs(steps[k] - shaped[k])); eqdiff = md; printf("[E2] 势函数塑形 vs 初值 Φ：%d 个回合的平均步数最大差 %.3g\n", EP, md); }
        out.push_back(JObj().add("name", js(names[mode])).add("steps", jpts(xs, ys, 5)).add("succ", jpts(xs, ss, 4)).num("first", first, 4).num("last", last, 4).num("lsucc", lsucc, 4).str());
    }
    return JObj().add("runs", jlist(out)).num("eqdiff", eqdiff, 3).num("shortest", 2 * (R.N - 1)).num("N", R.N);
}

int main() {
    JObj root;
    root.add("e1", e1().str());
    root.add("e2", e2().str());
    write_file("ch16_output.json", root.str());
    printf("写出 ch16_output.json\n");
}
