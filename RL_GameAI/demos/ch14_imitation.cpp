// 第 14 章实测：模仿学习与离线 RL
//   E1 复合误差：每步犯错概率 ε，BC 一旦离开专家分布就回不来（每步代价 1 直到回合结束），DAgger 能纠正（只在犯错那一步付代价）；
//      蒙特卡洛 vs 解析式，T = 10 … 200
//   E2 迷宫 GridWorld（带 10% 滑步）：BC vs DAgger，横轴 = 专家标注的状态数，纵轴 = 成功率 / 学生在自己分布上与专家的不一致率（20 个种子）
//   E3 离线 RL 的外推误差：行为策略只覆盖部分动作；朴素离线 Q 迭代 / 只在数据支持的动作里取 max（BCQ 思路）/
//      对未见动作做保守估计（CQL 思路）/ 直接 BC，比较成功率
// 输出：stdout + ch14_output.json
// 编译：cl /nologo /std:c++latest /EHsc /O2 /utf-8 ch14_imitation.cpp
#include "rlcommon.h"
#include <array>
#include <tuple>
using namespace std;

// ================================================================ E1
static JObj e1() {
    const double eps = 0.02; const int R = 200000;
    vector<double> Ts, bc_mc, bc_th, dg_mc, dg_th;
    Rng r(14);
    for (int T = 10; T <= 200; T += 10) {
        double sb = 0, sd = 0;
        for (int k = 0; k < R; ++k) {
            int first = -1, errs = 0;
            for (int t = 1; t <= T; ++t) if (r.u() < eps) { ++errs; if (first < 0) first = t; }
            if (first > 0) sb += T - first + 1;   // BC：第一次犯错后一直偏离
            sd += errs;                            // DAgger：犯错一次付一次代价
        }
        double th = 0; for (int t = 1; t <= T; ++t) th += eps * pow(1 - eps, t - 1) * (T - t + 1);
        Ts.push_back(T); bc_mc.push_back(sb / R); bc_th.push_back(th); dg_mc.push_back(sd / R); dg_th.push_back(eps * T);
    }
    printf("[E1] ε=%.2f：T=50 时 BC 期望代价 %.2f（解析 %.2f），DAgger %.2f；T=200 时 BC %.2f（解析 %.2f），DAgger %.2f；εT²/2 = %.1f\n",
           eps, bc_mc[4], bc_th[4], dg_mc[4], bc_mc.back(), bc_th.back(), dg_mc.back(), eps * 200 * 200 / 2);
    return JObj().num("eps", eps).add("bc_mc", jpts(Ts, bc_mc)).add("bc_th", jpts(Ts, bc_th)).add("dg_mc", jpts(Ts, dg_mc)).add("dg_th", jpts(Ts, dg_th))
        .num("bc50", bc_mc[4], 4).num("dg50", dg_mc[4], 4).num("bc200", bc_mc.back(), 4).num("dg200", dg_mc.back(), 4).num("half", eps * 200 * 200 / 2, 4);
}

// ================================================================ 迷宫
static const char* MAZE[] = {
    "##############",
    "#...#.......G#",
    "#.#.#.#####..#",
    "#.#...#...#..#",
    "#.#####.#.#.##",
    "#.....#.#.#..#",
    "###.#.#.#.##.#",
    "#...#...#....#",
    "#.#######.##.#",
    "#.#.....#..#.#",
    "#...###...##.#",
    "#S#.....#....#",
    "##############"};
const int H = 13, Wd = 14, NA = 4;
const int DX[4] = {0, 1, 0, -1}, DY[4] = {-1, 0, 1, 0};
struct Maze {
    vector<int> free_; int start, goal; int n;
    Maze() {
        n = H * Wd; free_.assign(n, 0);
        for (int y = 0; y < H; ++y) for (int x = 0; x < Wd; ++x) {
            char c = MAZE[y][x]; int s = y * Wd + x; free_[s] = c != '#';
            if (c == 'S') start = s; if (c == 'G') goal = s;
        }
    }
    int move(int s, int a) const { int x = s % Wd + DX[a], y = s / Wd + DY[a]; int t = y * Wd + x; return free_[t] ? t : s; }
    int step(int s, int a, Rng& r, double slip) const { if (r.u() < slip) a = r.below(NA); return move(s, a); }
};
static vector<int> expert_policy(const Maze& M, double slip, vector<double>* Vout = nullptr) {   // 价值迭代（考虑滑步）
    vector<double> V(M.n, 0); const double g = 0.99;
    for (int it = 0; it < 2000; ++it) {
        vector<double> V2 = V;
        for (int s = 0; s < M.n; ++s) {
            if (!M.free_[s] || s == M.goal) continue;
            double best = -1e9;
            for (int a = 0; a < NA; ++a) {
                double q = 0;
                for (int b = 0; b < NA; ++b) { double p = (b == a ? 1 - slip : 0) + slip / NA; int t = M.move(s, b); q += p * (-1 + g * (t == M.goal ? 0 : V[t])); }
                best = max(best, q);
            }
            V2[s] = best;
        }
        V = V2;
    }
    vector<int> pi(M.n, 0);
    for (int s = 0; s < M.n; ++s) {
        double best = -1e9;
        for (int a = 0; a < NA; ++a) {
            double q = 0;
            for (int b = 0; b < NA; ++b) { double p = (b == a ? 1 - slip : 0) + slip / NA; int t = M.move(s, b); q += p * (-1 + g * (t == M.goal ? 0 : V[t])); }
            if (q > best + 1e-12) { best = q; pi[s] = a; }
        }
    }
    if (Vout) *Vout = V;
    return pi;
}
struct Learner {   // 表格分类器：每个状态的标签计数取多数；没见过的状态随机选
    vector<array<int, 4>> cnt;
    explicit Learner(int n) : cnt(n, {0, 0, 0, 0}) {}
    void add(int s, int a) { cnt[s][a]++; }
    int act(int s, Rng& r) const {
        int best = -1, bc = 0;
        for (int a = 0; a < NA; ++a) if (cnt[s][a] > bc) { bc = cnt[s][a]; best = a; }
        return best < 0 ? r.below(NA) : best;
    }
};
struct EvalRes { double succ, steps, disagree; };
static EvalRes evaluate(const Maze& M, const Learner& L, const vector<int>& ex, Rng& r, double slip, int episodes = 400, int maxT = 120) {
    double succ = 0, steps = 0, dis = 0, tot = 0;
    for (int e = 0; e < episodes; ++e) {
        int s = M.start, t = 0;
        for (; t < maxT && s != M.goal; ++t) { int a = L.act(s, r); dis += a != ex[s]; tot += 1; s = M.step(s, a, r, slip); }
        succ += s == M.goal; steps += t;
    }
    return {succ / episodes, steps / episodes, dis / tot};
}

static JObj e2() {
    Maze M; const double slip = 0.2; auto ex = expert_policy(M, slip);
    const int SEEDS = 20; const int budgets[] = {25, 50, 100, 200, 400, 800};
    const int NB = 6, TL = 45;   // 时间上限 45 步（专家平均约 30 步）
    vector<double> bc_t(NB, 0), dn_t(NB, 0), dg_t(NB, 0), bc_s(NB, 0), dn_s(NB, 0), dg_s(NB, 0), bc_d(NB, 0), dn_d(NB, 0), dg_d(NB, 0), bc_train(NB, 0), bc_cov(NB, 0), dg_cov(NB, 0);
    auto coverage = [&](const Learner& L) { int c = 0; for (int s = 0; s < M.n; ++s) if (L.cnt[s][0] + L.cnt[s][1] + L.cnt[s][2] + L.cnt[s][3]) ++c; return (double)c; };
    for (int sd = 0; sd < SEEDS; ++sd) {
        for (int bi = 0; bi < NB; ++bi) {
            int budget = budgets[bi];
            // BC（干净示范）：专家在无噪声条件下示范，测试时环境有 20% 滑步
            for (int noisy = 0; noisy < 2; ++noisy) {
                Rng r(1000 * sd + bi + 500 * noisy); Learner L(M.n); int labels = 0; vector<pair<int,int>> data;
                while (labels < budget) { int s = M.start; for (int t = 0; t < 120 && s != M.goal && labels < budget; ++t) { L.add(s, ex[s]); data.push_back({s, ex[s]}); ++labels; s = M.step(s, ex[s], r, noisy ? slip : 0.0); } }
                Rng re(77 + sd); auto ev = evaluate(M, L, ex, re, slip, 400, TL);
                if (!noisy) { bc_t[bi] += ev.steps / SEEDS; double tr = 0; for (auto& q : data) tr += L.act(q.first, r) != q.second; bc_train[bi] += tr / data.size() / SEEDS; bc_s[bi] += ev.succ / SEEDS; bc_d[bi] += ev.disagree / SEEDS; bc_cov[bi] += coverage(L) / SEEDS; }
                else { dn_t[bi] += ev.steps / SEEDS; dn_s[bi] += ev.succ / SEEDS; dn_d[bi] += ev.disagree / SEEDS; }
            }
            // DAgger：先用最多 25 个干净示范标注起步，之后每轮按学生策略在有噪声的环境里跑，请专家标注学生访问到的状态
            { Rng r(2000 * sd + bi); Learner L(M.n); int labels = 0;
              { int s = M.start; for (int t = 0; t < 120 && s != M.goal && labels < min(25, budget); ++t) { L.add(s, ex[s]); ++labels; s = M.step(s, ex[s], r, 0.0); } }
              while (labels < budget) {
                  int s = M.start;
                  for (int t = 0; t < 120 && s != M.goal && labels < budget; ++t) { L.add(s, ex[s]); ++labels; s = M.step(s, L.act(s, r), r, slip); }
              }
              Rng re(77 + sd); auto ev = evaluate(M, L, ex, re, slip, 400, TL); dg_t[bi] += ev.steps / SEEDS; dg_s[bi] += ev.succ / SEEDS; dg_d[bi] += ev.disagree / SEEDS; dg_cov[bi] += coverage(L) / SEEDS; }
        }
    }
    double exs = 0, ext = 0; { Rng re(77); for (int e = 0; e < 4000; ++e) { int s = M.start, k = 0; for (; k < TL && s != M.goal; ++k) s = M.step(s, ex[s], re, slip); exs += s == M.goal; ext += k; } exs /= 4000; ext /= 4000; }
    printf("[E2] 专家在同样条件下：成功率 %.3f，平均步数 %.1f\n", exs, ext);
    vector<double> bx(budgets, budgets + NB);
    int nfree = (int)count(M.free_.begin(), M.free_.end(), 1);
    printf("[E2] 迷宫 %d 个空格，测试时滑步 %.0f%%，20 个种子：\n", nfree, slip * 100);
    for (int i = 0; i < NB; ++i) printf("     标注 %4d：BC(干净示范) 成功率 %.2f 步数 %.1f（训练集错误 %.3f，覆盖 %.0f 格，自己分布上不一致 %.3f） | BC(噪声示范) %.2f 步数 %.1f | DAgger %.2f 步数 %.1f（覆盖 %.0f 格，不一致 %.3f）\n",
                                       budgets[i], bc_s[i], bc_t[i], bc_train[i], bc_cov[i], bc_d[i], dn_s[i], dn_t[i], dg_s[i], dg_t[i], dg_cov[i], dg_d[i]);
    vector<string> rows;
    for (int i = 0; i < NB; ++i) rows.push_back(JObj().num("n", budgets[i]).num("bc", bc_s[i], 4).num("bctr", bc_train[i], 4).num("bccov", bc_cov[i], 4).num("bcd", bc_d[i], 4)
        .num("dn", dn_s[i], 4).num("dg", dg_s[i], 4).num("bct", bc_t[i], 4).num("dnt", dn_t[i], 4).num("dgt", dg_t[i], 4).num("dgcov", dg_cov[i], 4).num("dgd", dg_d[i], 4).str());
    return JObj().add("bct", jpts(bx, bc_t)).add("dnt", jpts(bx, dn_t)).add("dgt", jpts(bx, dg_t)).num("tl", TL).add("bc", jpts(bx, bc_s)).add("dn", jpts(bx, dn_s)).add("dg", jpts(bx, dg_s)).add("bcd", jpts(bx, bc_d)).add("dgd", jpts(bx, dg_d)).add("rows", jlist(rows))
        .num("free", nfree).num("slip", slip).num("exs", exs, 4).num("ext", ext, 4);
}

// ================================================================ E3 离线 RL
static JObj e3() {
    Maze M; const double slip = 0.1, g = 0.99; vector<double> Vex; auto ex = expert_policy(M, slip, &Vex);
    // 行为策略：80% 专家动作，20% 一个固定的“次选”动作（每个状态预先随机指定），其余两个动作从不出现
    Rng r(31); vector<int> alt(M.n);
    for (int s = 0; s < M.n; ++s) { do alt[s] = r.below(NA); while (alt[s] == ex[s]); }
    struct T4 { int s, a, s2; };
    vector<T4> D;
    for (int e = 0; e < 300; ++e) { int s = M.start; for (int t = 0; t < 150 && s != M.goal; ++t) { int a = r.u() < 0.8 ? ex[s] : alt[s]; int s2 = M.step(s, a, r, slip); D.push_back({s, a, s2}); s = s2; } }
    vector<array<int, 4>> seen(M.n, {0, 0, 0, 0}); for (auto& d : D) seen[d.s][d.a]++;
    int covered = 0, visited = 0; for (int s = 0; s < M.n; ++s) { int k = 0; for (int a = 0; a < NA; ++a) k += seen[s][a] > 0; if (k) { ++visited; covered += k; } }
    // 经验转移模型（表格的确定性等价，第 5 章）
    auto run = [&](int mode) {   // 0 朴素（未见动作 Q 保持初值 0）；1 只在数据支持的动作里取 max；2 未见动作取下界 −1/(1−γ)
        vector<array<double, 4>> Q(M.n, {0, 0, 0, 0});
        vector<array<vector<int>, 4>> nxt(M.n); for (auto& d : D) nxt[d.s][d.a].push_back(d.s2);
        const double low = -1.0 / (1 - g);
        if (mode == 2) for (int s = 0; s < M.n; ++s) for (int a = 0; a < NA; ++a) if (!seen[s][a]) Q[s][a] = low;
        auto vmax = [&](int s) { if (s == M.goal) return 0.0; double b = -1e18; for (int a = 0; a < NA; ++a) if (mode != 1 || seen[s][a]) b = max(b, Q[s][a]); return b < -1e17 ? low : b; };
        for (int it = 0; it < 3000; ++it)
            for (int s = 0; s < M.n; ++s) for (int a = 0; a < NA; ++a) {
                if (nxt[s][a].empty()) continue;
                double tgt = 0; for (int s2 : nxt[s][a]) tgt += -1 + g * vmax(s2); Q[s][a] = tgt / nxt[s][a].size();
            }
        vector<int> pi(M.n, 0);
        for (int s = 0; s < M.n; ++s) { double b = -1e18; for (int a = 0; a < NA; ++a) if ((mode != 1 || seen[s][a]) && Q[s][a] > b) { b = Q[s][a]; pi[s] = a; } }
        int ood = 0, vs = 0; for (int s = 0; s < M.n; ++s) if (M.free_[s] && s != M.goal && (seen[s][0] + seen[s][1] + seen[s][2] + seen[s][3])) { ++vs; ood += !seen[s][pi[s]]; }
        Rng re(99); double succ = 0, steps = 0;
        for (int e = 0; e < 2000; ++e) { int s = M.start, t = 0; for (; t < 150 && s != M.goal; ++t) s = M.step(s, pi[s], re, slip); succ += s == M.goal; steps += t; }
        return make_tuple(succ / 2000, steps / 2000, (double)ood / vs);
    };
    // BC 基线（多数投票，几乎就是专家动作）
    Learner L(M.n); for (auto& d : D) L.add(d.s, d.a);
    Rng re(99); double bsucc = 0, bsteps = 0;
    for (int e = 0; e < 2000; ++e) { int s = M.start, t = 0; for (; t < 150 && s != M.goal; ++t) s = M.step(s, L.act(s, re), re, slip); bsucc += s == M.goal; bsteps += t; }
    double esucc = 0, esteps = 0; Rng rx(99);
    for (int e = 0; e < 2000; ++e) { int s = M.start, t = 0; for (; t < 150 && s != M.goal; ++t) s = M.step(s, ex[s], rx, slip); esucc += s == M.goal; esteps += t; }
    const char* names[3] = {"朴素离线 Q 迭代", "只在数据支持的动作中取 max（BCQ 思路）", "未见动作取价值下界（CQL 思路）"};
    vector<string> rows;
    printf("[E3] 离线数据 %zu 条转移，访问过 %d 个状态，平均每个状态覆盖 %.2f / 4 个动作\n", D.size(), visited, (double)covered / visited);
    rows.push_back(JObj().add("name", js("专家（参考上限）")).num("succ", esucc / 2000, 4).num("steps", esteps / 2000, 4).num("ood", 0).str());
    for (int m = 0; m < 3; ++m) {
        auto [su, st, ood] = run(m);
        printf("     %-36s 成功率 %.3f，平均步数 %.1f，贪心动作落在数据外的状态比例 %.2f\n", names[m], su, st, ood);
        rows.push_back(JObj().add("name", js(names[m])).num("succ", su, 4).num("steps", st, 4).num("ood", ood, 4).str());
    }
    printf("     %-36s 成功率 %.3f，平均步数 %.1f\n", "行为克隆（多数投票）", bsucc / 2000, bsteps / 2000);
    rows.push_back(JObj().add("name", js("行为克隆（多数投票）")).num("succ", bsucc / 2000, 4).num("steps", bsteps / 2000, 4).num("ood", 0).str());
    printf("     专家：成功率 %.3f，平均步数 %.1f\n", esucc / 2000, esteps / 2000);
    return JObj().add("rows", jlist(rows)).num("n", (double)D.size()).num("visited", visited).num("cover", (double)covered / visited, 4);
}

int main() {
    JObj root;
    root.add("e1", e1().str());
    root.add("e2", e2().str());
    root.add("e3", e3().str());
    write_file("ch14_output.json", root.str());
    printf("写出 ch14_output.json\n");
}
