// 第 8 章实测：价值函数近似与 DQN 家族
//   E0 手写 MLP 反传的数值梯度检查（中心差分）
//   E1 最大化偏差：(a) max 估计量 vs 双估计量的偏差随动作数变化；(b) Sutton 例 6.7 上 Q-learning vs Double Q-learning
//   E2 致命三角：Baird 反例上 off-policy 半梯度 TD 发散；去掉“off-policy”或“函数近似”任一角后收敛
//   E3 PER 的 sum-tree：8 叶子结构图数据、10⁶ 次采样频率 vs 理论概率、IS 权重、与线性扫描的计时对比
//   E4 CartPole：DQN / 无目标网络 / Double DQN（各 3 个种子，并行），回报曲线 + Q 估计 vs 实际折扣回报
// 输出：stdout + ch08_output.json
// 编译：cl /nologo /std:c++latest /EHsc /O2 /utf-8 ch08_dqn.cpp
#include "rlcommon.h"
using namespace std;

// ================================================================ E0
static JObj e0() {
    Rng rng(7);
    MLP net({4, 8, 8, 3}, rng);
    vector<double> x = {0.3, -1.2, 0.8, 0.05}, y = {0.5, -0.2, 1.0};
    auto loss = [&]() { auto& o = net.forward(x); double L = 0; for (int i = 0; i < 3; ++i) L += 0.5 * (o[i] - y[i]) * (o[i] - y[i]); return L; };
    loss();
    vector<double> g(3); { auto& o = net.a.back(); for (int i = 0; i < 3; ++i) g[i] = o[i] - y[i]; }
    net.zero_grad(); net.forward(x); net.backward(g);
    double max_rel = 0; int checked = 0; const double h = 1e-5;
    for (size_t l = 0; l < net.W.size(); ++l)
        for (size_t i = 0; i < net.W[l].size(); ++i) {
            double old = net.W[l][i];
            net.W[l][i] = old + h; double lp = loss();
            net.W[l][i] = old - h; double lm = loss();
            net.W[l][i] = old;
            double num = (lp - lm) / (2 * h), ana = net.gW[l][i];
            double rel = fabs(num - ana) / max(1e-12, fabs(num) + fabs(ana));
            max_rel = max(max_rel, rel); ++checked;
        }
    printf("[E0] MLP 4-8-8-3 数值梯度检查：%d 个权重，最大相对误差 %.2e\n", checked, max_rel);
    return JObj().num("checked", checked).num("max_rel", max_rel, 3);
}

// ================================================================ E1
static JObj e1() {
    // (a) 偏差随动作数：每个动作真值都是 0，各有 10 个 N(0,1) 样本
    vector<int> ks = {1, 2, 4, 8, 16, 32, 64};
    vector<double> single, dbl;
    Rng rng(11);
    const int trials = 20000, n = 10;
    for (int k : ks) {
        double s1 = 0, s2 = 0;
        for (int t = 0; t < trials; ++t) {
            vector<double> mA(k), mB(k), mAll(k);
            for (int a = 0; a < k; ++a) {
                double sa = 0, sb = 0;
                for (int i = 0; i < n / 2; ++i) sa += rng.normal();
                for (int i = 0; i < n / 2; ++i) sb += rng.normal();
                mA[a] = sa / (n / 2); mB[a] = sb / (n / 2); mAll[a] = (sa + sb) / n;
            }
            s1 += *max_element(mAll.begin(), mAll.end());
            s2 += mB[max_element(mA.begin(), mA.end()) - mA.begin()];
        }
        single.push_back(s1 / trials); dbl.push_back(s2 / trials);
    }
    printf("[E1a] 真值全为 0，每个动作 10 个样本：\n");
    for (size_t i = 0; i < ks.size(); ++i) printf("      k=%2d  max 估计 %+.4f   双估计 %+.4f\n", ks[i], single[i], dbl[i]);

    // (b) Sutton 例 6.7：A 向右终止(0)，向左到 B(0)；B 有 10 个动作，奖励 N(-0.1, 1) 后终止
    const int runs = 10000, eps_n = 300, nb = 10;
    const double alpha = 0.1, eps = 0.1;
    vector<double> leftQ(eps_n, 0), leftD(eps_n, 0);
    double qaL_Q = 0, qaL_D = 0;
    auto argmax_rand = [](const vector<double>& q, Rng& r) {
        double m = *max_element(q.begin(), q.end()); vector<int> c;
        for (int i = 0; i < (int)q.size(); ++i) if (q[i] == m) c.push_back(i);
        return c[r.below((int)c.size())];
    };
    for (int run = 0; run < runs; ++run) {
        for (int alg = 0; alg < 2; ++alg) {
            Rng r(1000 + run);
            vector<double> QA1(2, 0), QA2(2, 0), QB1(nb, 0), QB2(nb, 0);   // A: 0 = left, 1 = right
            for (int ep = 0; ep < eps_n; ++ep) {
                vector<double> qa(2); for (int i = 0; i < 2; ++i) qa[i] = alg ? QA1[i] + QA2[i] : QA1[i];
                int a = r.u() < eps ? r.below(2) : argmax_rand(qa, r);
                if (a == 0) (alg ? leftD : leftQ)[ep] += 1;
                if (a == 1) {   // 直接终止，奖励 0
                    if (!alg) QA1[1] += alpha * (0 - QA1[1]);
                    else if (r.u() < 0.5) QA1[1] += alpha * (0 - QA1[1]); else QA2[1] += alpha * (0 - QA2[1]);
                    continue;
                }
                // A --left--> B，奖励 0，然后在 B 选动作
                if (!alg) {
                    QA1[0] += alpha * (0 + *max_element(QB1.begin(), QB1.end()) - QA1[0]);
                } else if (r.u() < 0.5) {
                    int bstar = argmax_rand(QB1, r); QA1[0] += alpha * (0 + QB2[bstar] - QA1[0]);
                } else {
                    int bstar = argmax_rand(QB2, r); QA2[0] += alpha * (0 + QB1[bstar] - QA2[0]);
                }
                vector<double> qb(nb); for (int i = 0; i < nb; ++i) qb[i] = alg ? QB1[i] + QB2[i] : QB1[i];
                int b = r.u() < eps ? r.below(nb) : argmax_rand(qb, r);
                double rew = -0.1 + r.normal();
                if (!alg) QB1[b] += alpha * (rew - QB1[b]);
                else if (r.u() < 0.5) QB1[b] += alpha * (rew - QB1[b]); else QB2[b] += alpha * (rew - QB2[b]);
            }
            if (!alg) qaL_Q += QA1[0]; else qaL_D += 0.5 * (QA1[0] + QA2[0]);
        }
    }
    vector<double> xs, yq, yd;
    for (int ep = 0; ep < eps_n; ++ep) { xs.push_back(ep + 1); yq.push_back(100.0 * leftQ[ep] / runs); yd.push_back(100.0 * leftD[ep] / runs); }
    auto avg_last = [&](const vector<double>& y) { double s = 0; for (int i = eps_n - 50; i < eps_n; ++i) s += y[i]; return s / 50; };
    double peakQ = *max_element(yq.begin(), yq.end());
    int peakQ_ep = (int)(max_element(yq.begin(), yq.end()) - yq.begin()) + 1;
    printf("[E1b] 例 6.7（%d 次运行）：Q-learning 向左比例峰值 %.1f%%（第 %d 回合），最后 50 回合 %.1f%%；Double Q 最后 50 回合 %.1f%%（最优 = 5%%）\n",
           runs, peakQ, peakQ_ep, avg_last(yq), avg_last(yd));
    printf("      第 300 回合 Q(A,left)：Q-learning %.4f，Double Q %.4f（真值 -0.1）\n", qaL_Q / runs, qaL_D / runs);
    JObj bias; bias.add("k", jai(ks)).add("single", ja(single, 6)).add("double", ja(dbl, 6));
    vector<double> kx(ks.begin(), ks.end());
    bias.add("single_pts", jpts(kx, single)).add("double_pts", jpts(kx, dbl));
    JObj mdp; mdp.add("q", jpts(xs, yq, 5)).add("dq", jpts(xs, yd, 5)).num("peak_q", peakQ, 5).num("peak_q_ep", peakQ_ep)
        .num("last_q", avg_last(yq), 5).num("last_dq", avg_last(yd), 5).num("qaL_q", qaL_Q / runs, 5).num("qaL_dq", qaL_D / runs, 5).num("runs", runs);
    return JObj().add("bias", bias.str()).add("mdp", mdp.str());
}

// ================================================================ E2 Baird 反例
static JObj e2() {
    const int S = 7, steps = 3000; const double gamma = 0.99, alpha = 0.01;
    auto run = [&](int mode, uint64_t seed) {   // 0：线性特征 + off-policy；1：线性特征 + on-policy；2：表格 + off-policy
        int D = mode == 2 ? 7 : 8;
        vector<vector<double>> phi(S, vector<double>(D, 0.0));
        if (mode == 2) for (int s = 0; s < S; ++s) phi[s][s] = 1;
        else { for (int s = 0; s < 6; ++s) { phi[s][s] = 2; phi[s][7] = 1; } phi[6][6] = 1; phi[6][7] = 2; }
        vector<double> w(D);
        if (mode == 2) { for (int s = 0; s < 6; ++s) w[s] = 3; w[6] = 12; }       // 与线性版初值对应的价值 Φw₀
        else { for (int i = 0; i < 8; ++i) w[i] = 1; w[6] = 10; }
        Rng r(seed);
        int s = r.below(S);
        vector<double> xs, ys;
        auto rmsV = [&]() { double t = 0; for (int i = 0; i < S; ++i) { double v = 0; for (int j = 0; j < D; ++j) v += phi[i][j] * w[j]; t += v * v; } return sqrt(t / S); };
        for (int k = 0; k <= steps; ++k) {
            if (k % 30 == 0) { xs.push_back(k); ys.push_back(rmsV()); }
            bool solid = r.u() < 1.0 / 7;
            int s2 = solid ? 6 : r.below(6);
            double rho = mode == 1 ? 1.0 : (solid ? 7.0 : 0.0);
            double v = 0, v2 = 0;
            for (int j = 0; j < D; ++j) { v += phi[s][j] * w[j]; v2 += phi[s2][j] * w[j]; }
            double delta = 0 + gamma * v2 - v;
            for (int j = 0; j < D; ++j) w[j] += alpha * rho * delta * phi[s][j];
            s = s2;
        }
        return make_pair(xs, ys);
    };
    auto [x0, y0] = run(0, 5); auto [x1, y1] = run(1, 5); auto [x2, y2] = run(2, 5);
    printf("[E2] Baird 反例 3000 步后价值 RMS：线性+off-policy %.3e，线性+on-policy %.3e，表格+off-policy %.3e（初始 %.3f）\n",
           y0.back(), y1.back(), y2.back(), y0.front());
    return JObj().add("off", jpts(x0, y0)).add("on", jpts(x1, y1)).add("tab", jpts(x2, y2))
        .num("off_end", y0.back(), 4).num("on_end", y1.back(), 4).num("tab_end", y2.back(), 4).num("init", y0.front(), 5);
}

// ================================================================ E3 sum-tree
struct SumTree {
    int n; vector<double> t;   // t[1] 为根；叶子 t[n..2n-1]
    explicit SumTree(int cap) : n(1) { while (n < cap) n <<= 1; t.assign(2 * n, 0.0); }
    void set(int i, double p) { int k = i + n; double d = p - t[k]; for (; k >= 1; k >>= 1) t[k] += d; }
    double total() const { return t[1]; }
    int find(double u, vector<int>* path = nullptr) const {   // u ∈ [0, total)
        int k = 1;
        while (k < n) {
            if (path) path->push_back(k);
            if (u < t[2 * k]) k = 2 * k; else { u -= t[2 * k]; k = 2 * k + 1; }
        }
        if (path) path->push_back(k);
        return k - n;
    }
};
static JObj e3() {
    // 结构图：8 个叶子
    vector<double> pr = {3, 10, 12, 4, 1, 2, 8, 2};
    SumTree st(8); for (int i = 0; i < 8; ++i) st.set(i, pr[i]);
    double u = 24.0; vector<int> path; int leaf = st.find(u, &path);
    printf("[E3] 8 叶子 sum-tree 根 = %.0f；u = %.0f 落在叶子 %d（路径", st.total(), u, leaf);
    for (int k : path) printf(" %d", k); printf("）\n");
    // 统计检验：N=1000，p_i = (|δ_i|+1e-3)^α，采样 10⁶ 次
    const int N = 1000, M = 1000000; const double alpha = 0.6, beta = 0.4;
    Rng r(3);
    vector<double> p(N); SumTree big(N);
    for (int i = 0; i < N; ++i) { double d = fabs(r.normal()); p[i] = pow(d + 1e-3, alpha); big.set(i, p[i]); }
    vector<int> cnt(N, 0);
    for (int m = 0; m < M; ++m) cnt[big.find(r.u() * big.total())]++;
    double tv = 0, maxerr = 0, wmax = 0, wmin = 1e300;
    for (int i = 0; i < N; ++i) {
        double P = p[i] / big.total(), f = (double)cnt[i] / M;
        tv += 0.5 * fabs(P - f); maxerr = max(maxerr, fabs(P - f));
        double w = pow(N * P, -beta); wmax = max(wmax, w); wmin = min(wmin, w);
    }
    // 关键性质：E_P[w·g] 在 β=1 时等于均匀采样下的平均
    double uni = 0, isw = 0; for (int i = 0; i < N; ++i) uni += p[i] / N;   // 用 p 当作“梯度”g 的代表
    for (int i = 0; i < N; ++i) { double P = p[i] / big.total(); isw += P * pow(N * P, -1.0) * p[i]; }
    printf("[E3] N=%d α=%.1f：采样 %d 次，总变差距离 %.4f，单项最大偏差 %.2e；IS 权重(β=%.1f) 归一化前范围 [%.3f, %.3f]，β=1 时加权期望 %.6f = 均匀期望 %.6f\n",
           N, alpha, M, tv, maxerr, beta, wmin, wmax, isw, uni);
    // 计时：N = 2^20，一次 batch 采样 64 个，重复 2000 次
    const int NB = 1 << 20; SumTree huge(NB); vector<double> pp(NB);
    for (int i = 0; i < NB; ++i) { pp[i] = 0.1 + r.u(); huge.set(i, pp[i]); }
    Timer t1; long long sink = 0;
    for (int rep = 0; rep < 2000; ++rep) for (int b = 0; b < 64; ++b) sink += huge.find(r.u() * huge.total());
    double ts = t1.sec();
    Timer t2;
    for (int rep = 0; rep < 20; ++rep) for (int b = 0; b < 64; ++b) {
        double uu = r.u() * huge.total(), c = 0; int k = 0;
        for (; k < NB - 1; ++k) { c += pp[k]; if (uu < c) break; }
        sink += k;
    }
    double tl = t2.sec() * 100;   // 换算到 2000 次
    printf("[E3] N=2^20，2000 个 batch×64：sum-tree %.3f s，线性扫描 %.1f s（按 20 次外推），约 %.0f 倍  (sink %lld)\n", ts, tl, tl / ts, sink % 7);
    JObj fig; fig.add("tree", ja(vector<double>(st.t.begin() + 1, st.t.end()), 6)).num("u", u).num("leaf", leaf).add("path", jai(path));
    return JObj().add("fig", fig.str()).num("N", N).num("M", M).num("tv", tv, 4).num("maxerr", maxerr, 3).num("wmin", wmin, 4).num("wmax", wmax, 4)
        .num("isw", isw, 8).num("uni", uni, 8).num("t_tree", ts, 3).num("t_lin", tl, 3).num("speedup", tl / ts, 3);
}

// ================================================================ E4 CartPole DQN
struct Trans { vector<double> s, s2; int a; double r; bool term; };
struct DqnCurve { vector<double> step, ret, q0, g0; double sec = 0; };

static DqnCurve dqn_run(int variant, int seed) {   // 0 DQN，1 无目标网络，2 Double DQN
    const int total = 80000, start = 1000, batch = 64, target_every = 500, eval_every = 4000, cap = 10000;
    const double gamma = 0.99, lr = 5e-4;
    Rng rng(seed * 101 + 17), erng(seed * 7 + 3);
    MLP q({4, 64, 64, 2}, rng), tq = q;
    vector<Trans> buf; buf.reserve(cap); int pos = 0;
    CartPole env; auto s = env.reset(rng);
    DqnCurve c; Timer tm;
    for (int k = 1; k <= total; ++k) {
        double eps = max(0.05, 1.0 - 0.95 * k / 16000.0);
        int a;
        if (rng.u() < eps) a = rng.below(2);
        else { auto& o = q.forward(s); a = o[1] > o[0] ? 1 : 0; }
        bool term, trunc; double rw = env.step(a, term, trunc);
        auto s2 = env.obs();
        Trans tr{s, s2, a, rw, term};
        if ((int)buf.size() < cap) buf.push_back(tr); else buf[pos] = tr;
        pos = (pos + 1) % cap;
        s = (term || trunc) ? env.reset(rng) : s2;
        if (k >= start) {
            for (int b = 0; b < batch; ++b) {
                const Trans& x = buf[rng.below((int)buf.size())];
                double y = x.r;
                if (!x.term) {
                    MLP& tgt = variant == 1 ? q : tq;
                    if (variant == 2) {
                        auto& oo = q.forward(x.s2); int as = oo[1] > oo[0] ? 1 : 0;
                        y += gamma * tgt.forward(x.s2)[as];
                    } else {
                        auto& ot = tgt.forward(x.s2); y += gamma * max(ot[0], ot[1]);
                    }
                }
                auto& o = q.forward(x.s);
                double d = o[x.a] - y; d = max(-1.0, min(1.0, d));   // Huber 损失的梯度
                vector<double> g(2, 0.0); g[x.a] = d;
                q.backward(g);
            }
            q.adam(lr, 1.0 / batch, 10.0);
            if (variant != 1 && k % target_every == 0) tq.copy_params_from(q);
        }
        if (k % eval_every == 0) {
            double R = 0, Q0 = 0, G0 = 0; const int ne = 5;
            for (int e = 0; e < ne; ++e) {
                CartPole ev; ev.max_steps = 1000; auto o = ev.reset(erng);
                auto& qo = q.forward(o); Q0 += max(qo[0], qo[1]);
                double g = 0, disc = 1; int len = 0; bool te = false, tu = false;
                while (!te && !tu) {
                    auto& z = q.forward(o); int aa = z[1] > z[0] ? 1 : 0;
                    g += disc * ev.step(aa, te, tu); disc *= gamma; ++len; o = ev.obs();
                }
                R += min(len, 500); G0 += g;
            }
            c.step.push_back(k); c.ret.push_back(R / ne); c.q0.push_back(Q0 / ne); c.g0.push_back(G0 / ne);
        }
    }
    c.sec = tm.sec();
    return c;
}

static JObj e4() {
    const int V = 3, SEEDS = 5;
    vector<DqnCurve> res(V * SEEDS);
    Timer tm;
    parallel_for(V * SEEDS, [&](int i) { res[i] = dqn_run(i / SEEDS, i % SEEDS + 1); });
    const char* names[V] = {"DQN", "无目标网络", "Double DQN"};
    vector<string> out;
    for (int v = 0; v < V; ++v) {
        auto& c0 = res[v * SEEDS];
        size_t n = c0.step.size();
        vector<double> ret(n, 0), q0(n, 0), g0(n, 0), over(n, 0);
        for (int sd = 0; sd < SEEDS; ++sd) for (size_t i = 0; i < n; ++i) {
            auto& c = res[v * SEEDS + sd];
            ret[i] += c.ret[i] / SEEDS; q0[i] += c.q0[i] / SEEDS; g0[i] += c.g0[i] / SEEDS; over[i] += (c.q0[i] - c.g0[i]) / SEEDS;
        }
        double last = 0, lastover = 0; int m = 4;
        for (size_t i = n - m; i < n; ++i) { last += ret[i] / m; lastover += over[i] / m; }
        double maxover = *max_element(over.begin(), over.end());
        vector<double> seedlast;
        for (int sd = 0; sd < SEEDS; ++sd) { auto& c = res[v * SEEDS + sd]; double t = 0; for (size_t i = n - m; i < n; ++i) t += c.ret[i] / m; seedlast.push_back(t); }
        printf("[E4] %-10s 最后 4 次评估平均回报 %.1f（各种子 %.0f / %.0f / %.0f / %.0f / %.0f），Q(s0)−G(s0) 最后 %.2f、最大 %.2f\n",
               names[v], last, seedlast[0], seedlast[1], seedlast[2], seedlast[3], seedlast[4], lastover, maxover);
        JObj o; o.add("name", js(names[v])).add("ret", jpts(c0.step, ret, 5)).add("q0", jpts(c0.step, q0, 5)).add("g0", jpts(c0.step, g0, 5))
            .add("over", jpts(c0.step, over, 5)).num("last", last, 4).num("lastover", lastover, 4).num("maxover", maxover, 4).add("seedlast", ja(seedlast, 4));
        out.push_back(o.str());
    }
    printf("[E4] 15 个训练任务并行，总耗时 %.1f s（单个约 %.1f s）\n", tm.sec(), res[0].sec);
    return JObj().add("runs", jlist(out)).num("sec", tm.sec(), 3);
}

int main() {
    JObj root;
    root.add("e0", e0().str());
    root.add("e1", e1().str());
    root.add("e2", e2().str());
    root.add("e3", e3().str());
    root.add("e4", e4().str());
    write_file("ch08_output.json", root.str());
    printf("写出 ch08_output.json\n");
}
