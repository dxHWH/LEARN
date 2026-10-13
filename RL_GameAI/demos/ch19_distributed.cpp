// 第 19 章实测：分布式训练系统
//   E1 V-trace 的不动点（表格，5 状态 × 3 动作，γ = 0.9）：用行为策略 μ 的一条长轨迹做 n 步 V-trace 评估，
//      比较 不修正 / ρ̄ = c̄ = 1 / ρ̄ = 0.5 / ρ̄ = ∞ 且 c̄ = 1 / 全重要性采样 五种设置收敛到哪个价值，并与理论不动点 V_{π_ρ̄} 对照
//   E2 actor-learner 吞吐与策略滞后：N 个 actor 线程（每步模拟约 10 μs 的游戏逻辑 + 本地 MLP 推理）+ 1 个 learner 线程，
//      rollout 经有界队列送给 learner；测量环境步 / 秒、learner 更新 / 秒，以及“样本生成时的参数版本”落后多少次更新
//   E3 策略滞后对学习的影响：CartPole 上 A2C，行为策略取 k 次更新之前的参数（k = 0 / 8 / 32 / 64），
//      不修正（n 步回报）vs V-trace（ρ̄ = c̄ = 1）修正，各 5 个种子
// 输出：stdout + ch19_output.json
// 编译：cl /nologo /std:c++latest /EHsc /O2 /utf-8 ch19_distributed.cpp
#include "rlcommon.h"
#include <atomic>
#include <condition_variable>
#include <deque>
#include <mutex>
using namespace std;

// ================================================================ E1 V-trace 不动点
const int S = 5, A = 3;
const double G1 = 0.9;
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
static double P1[S][A][S], R1[S][A];
static vector<double> value_of(const vector<vector<double>>& pi) {
    vector<vector<double>> M(S, vector<double>(S)); vector<double> b(S, 0);
    for (int s = 0; s < S; ++s) { for (int t = 0; t < S; ++t) M[s][t] = s == t; for (int a = 0; a < A; ++a) { b[s] += pi[s][a] * R1[s][a]; for (int t = 0; t < S; ++t) M[s][t] -= G1 * pi[s][a] * P1[s][a][t]; } }
    return solve(M, b);
}
static JObj e1() {
    Rng r(19);
    for (int s = 0; s < S; ++s) for (int a = 0; a < A; ++a) { double z = 0; for (int t = 0; t < S; ++t) { P1[s][a][t] = pow(r.u(), 2.0); z += P1[s][a][t]; } for (int t = 0; t < S; ++t) P1[s][a][t] /= z; R1[s][a] = r.uni(-1, 1); }
    vector<vector<double>> mu(S), pi(S);
    for (int s = 0; s < S; ++s) { vector<double> z1(A), z2(A); for (int a = 0; a < A; ++a) { z1[a] = r.normal() * 0.6; z2[a] = r.normal() * 1.5; } mu[s] = softmax(z1); pi[s] = softmax(z2); }
    auto Vmu = value_of(mu), Vpi = value_of(pi);
    auto pi_rho = [&](double rb) { vector<vector<double>> p(S, vector<double>(A)); for (int s = 0; s < S; ++s) { double z = 0; for (int a = 0; a < A; ++a) { p[s][a] = min(rb * mu[s][a], pi[s][a]); z += p[s][a]; } for (int a = 0; a < A; ++a) p[s][a] /= z; } return p; };
    struct M { const char* name; double rb, cb; int target; };   // target：0 = V_μ，1 = V_π，2 = V_{π_ρ̄}
    vector<M> ms = {{"不修正（ρ = c = 1）", -1, -1, 0}, {"V-trace ρ̄ = c̄ = 1", 1, 1, 2}, {"V-trace ρ̄ = c̄ = 0.5", 0.5, 0.5, 2}, {"V-trace ρ̄ = ∞、c̄ = 1", 1e18, 1, 1}, {"全重要性采样 ρ̄ = c̄ = ∞", 1e18, 1e18, 1}};
    const long long L = 2000000; const int NST = 10;
    // 生成一条行为轨迹（所有方法共用）
    vector<int> xs(L + NST + 1), as(L + NST); vector<double> rs(L + NST);
    { Rng g(7); int s = 0; for (long long t = 0; t < L + NST; ++t) { int a = g.categorical(mu[s]); xs[t] = s; as[t] = a; rs[t] = R1[s][a]; s = g.categorical(vector<double>(P1[s][a], P1[s][a] + S)); } xs[L + NST] = s; }
    vector<string> out; vector<double> ratio_max(S * A);
    double wmax = 0; for (int s = 0; s < S; ++s) for (int a = 0; a < A; ++a) wmax = max(wmax, pi[s][a] / mu[s][a]);
    for (auto& m : ms) {
        vector<double> V(S, 0), N(S, 0), Vavg(S, 0); vector<double> cx, ce; long long navg = 0;
        for (long long t = 0; t < L; ++t) {
            // n 步 V-trace 目标：v_s − V(x_s) = Σ_k γ^k (Π_{i<k} c_i) ρ_k δ_k（窗口 NST 步）
            double vs = 0, coef = 1;
            for (int k = 0; k < NST; ++k) {
                long long i = t + k; int s = xs[i], a = as[i];
                double w = pi[s][a] / mu[s][a];
                double rho = m.rb < 0 ? 1.0 : min(m.rb, w), c = m.cb < 0 ? 1.0 : min(m.cb, w);
                double delta = rs[i] + G1 * V[xs[i + 1]] - V[s];
                vs += coef * rho * delta; coef *= G1 * c;
            }
            int s0 = xs[t]; N[s0] += 1; double alpha = 1.0 / pow(N[s0], 0.6);
            V[s0] += alpha * vs;
            if (t >= L / 2) { for (int s = 0; s < S; ++s) Vavg[s] += V[s]; ++navg; }   // 后一半迭代取平均（Polyak 平均），压掉步长带来的噪声
            if ((t + 1) % 20000 == 0) { double e = 0; for (int s = 0; s < S; ++s) e = max(e, fabs(V[s] - Vpi[s])); cx.push_back((double)t + 1); ce.push_back(e); }
        }
        for (int s = 0; s < S; ++s) V[s] = Vavg[s] / navg;
        auto Vt = m.target == 0 ? Vmu : (m.target == 1 ? Vpi : value_of(pi_rho(m.rb)));
        double d_t = 0, d_pi = 0, d_mu = 0; for (int s = 0; s < S; ++s) { d_t = max(d_t, fabs(V[s] - Vt[s])); d_pi = max(d_pi, fabs(V[s] - Vpi[s])); d_mu = max(d_mu, fabs(V[s] - Vmu[s])); }
        double d_rho = 0; if (m.rb > 0 && m.rb < 1e17) { auto Vr = value_of(pi_rho(m.rb)); for (int s = 0; s < S; ++s) d_rho = max(d_rho, fabs(V[s] - Vr[s])); }
        printf("[E1] %-24s 与理论不动点最大差 %.4f；与 V_π 差 %.4f，与 V_μ 差 %.4f\n", m.name, d_t, d_pi, d_mu);
        out.push_back(JObj().add("name", js(m.name)).num("d_t", d_t, 4).num("d_pi", d_pi, 4).num("d_mu", d_mu, 4).add("V", ja(V, 6)).add("curve", jpts(cx, ce, 4)).str());
    }
    auto V1 = value_of(pi_rho(1.0)), V05 = value_of(pi_rho(0.5));
    double gap_mu_pi = 0, gap1 = 0; for (int s = 0; s < S; ++s) { gap_mu_pi = max(gap_mu_pi, fabs(Vmu[s] - Vpi[s])); gap1 = max(gap1, fabs(V1[s] - Vpi[s])); }
    printf("[E1] 理论值：max|V_μ − V_π| = %.4f，max|V_{π_1} − V_π| = %.4f；最大比值 π/μ = %.2f\n", gap_mu_pi, gap1, wmax);
    return JObj().add("runs", jlist(out)).add("Vpi", ja(Vpi, 6)).add("Vmu", ja(Vmu, 6)).add("V1", ja(V1, 6)).add("V05", ja(V05, 6))
        .num("gap", gap_mu_pi, 4).num("gap1", gap1, 4).num("wmax", wmax, 4).num("L", (double)L).num("n", NST);
}

// ================================================================ E2 actor-learner 吞吐与滞后
static double fake_game_logic(int iters, double x) {   // 模拟一步游戏逻辑的 CPU 开销
    for (int i = 0; i < iters; ++i) x = x * 1.0000001 + sin(x) * 1e-7;
    return x;
}
static atomic<long long> g_sink{0};   // 防止编译器把“没人用的游戏逻辑结果”整段优化掉
struct Rollout { int version; int steps; vector<double> obs; };
struct BQueue {
    mutex m; condition_variable cv_put, cv_get; deque<Rollout> q; size_t cap;
    explicit BQueue(size_t c) : cap(c) {}
    bool put(Rollout&& r, atomic<bool>& stop) {
        unique_lock<mutex> lk(m);
        cv_put.wait(lk, [&] { return q.size() < cap || stop.load(); });
        if (stop.load()) return false;
        q.push_back(std::move(r)); cv_get.notify_one(); return true;
    }
    bool get(Rollout& r, atomic<bool>& stop) {
        unique_lock<mutex> lk(m);
        cv_get.wait(lk, [&] { return !q.empty() || stop.load(); });
        if (q.empty()) return false;
        r = std::move(q.front()); q.pop_front(); cv_put.notify_one(); return true;
    }
};
struct TPRes { double steps_per_s, upd_per_s, lag_mean, lag_p95; int lag_max; double learner_busy; };
static TPRes run_tp(int nact, int qcap, int logic_iters, double secs) {
    const int T = 64, BATCH = 4;   // 每段 rollout 64 步；learner 每次更新用 4 段
    Rng r0(5); MLP net({32, 64, 64, 8}, r0);
    mutex pm; MLP shared = net; atomic<int> version{0};
    atomic<bool> stop{false}; atomic<long long> steps{0};
    BQueue q(qcap);
    vector<int> lags; mutex lm; atomic<int> updates{0}; double busy = 0;
    vector<thread> th;
    for (int k = 0; k < nact; ++k) th.emplace_back([&, k] {
        Rng r(100 + k); MLP local = net; double x = 0.1 * k;
        vector<double> obs(32);
        while (!stop.load()) {
            int ver; { lock_guard<mutex> lk(pm); local.copy_params_from(shared); ver = version.load(); }   // 每段 rollout 开始时拉一次参数
            Rollout ro; ro.version = ver; ro.steps = T; ro.obs.reserve(T * 32);
            for (int t = 0; t < T && !stop.load(); ++t) {
                for (auto& v : obs) v = r.normal();
                auto& o = local.forward(obs);                      // 本地推理（batch = 1）
                x = fake_game_logic(logic_iters, x + o[0] * 1e-9); // 游戏逻辑
                ro.obs.insert(ro.obs.end(), obs.begin(), obs.end());
            }
            steps += T;
            if (!q.put(std::move(ro), stop)) break;
        }
        g_sink += (long long)(x * 1000);
    });
    thread learner([&] {
        Rng r(9); MLP L = net; Timer tb; double work = 0;
        while (!stop.load()) {
            vector<Rollout> batch;
            bool ok = true;
            for (int b = 0; b < BATCH && ok; ++b) { Rollout ro; if (!q.get(ro, stop)) ok = false; else batch.push_back(std::move(ro)); }
            if (!ok) break;
            Timer tw;
            int cur = version.load();
            { lock_guard<mutex> lk(lm); for (auto& ro : batch) lags.push_back(cur - ro.version); }
            for (auto& ro : batch) for (int t = 0; t < ro.steps; t += 4) {   // 真实的前向 + 反向开销（每 4 步取 1 个样本，learner 处理速度约为单个 actor 的若干倍）
                vector<double> o(ro.obs.begin() + t * 32, ro.obs.begin() + (t + 1) * 32);
                auto& y = L.forward(o); vector<double> g(8); for (int i = 0; i < 8; ++i) g[i] = y[i] * 1e-3; L.backward(g);
            }
            L.adam(1e-4, 4.0 / (BATCH * T));
            { lock_guard<mutex> lk(pm); shared.copy_params_from(L); version.fetch_add(1); }
            updates++; work += tw.sec();
        }
        busy = work;
    });
    Timer tm;
    this_thread::sleep_for(chrono::milliseconds((int)(secs * 1000)));
    double el = tm.sec();
    long long st = steps.load(); int up = updates.load();
    stop = true; q.cv_put.notify_all(); q.cv_get.notify_all();
    for (auto& t : th) t.join(); learner.join();
    TPRes res{};
    res.steps_per_s = st / el; res.upd_per_s = up / el; res.learner_busy = busy / el;
    if (!lags.empty()) {
        sort(lags.begin(), lags.end()); double s = 0; for (int v : lags) s += v;
        res.lag_mean = s / lags.size(); res.lag_p95 = lags[(size_t)(lags.size() * 0.95)]; res.lag_max = lags.back();
    }
    return res;
}
static JObj e2() {
    // 标定：每步游戏逻辑约 10 μs
    int iters = 200; { Timer t; double x = 0.3; for (int k = 0; k < 2000; ++k) x = fake_game_logic(iters, x); double us = t.sec() / 2000 * 1e6; iters = max(1, (int)(iters * 10.0 / us)); if (x == 12345) printf("."); }
    double us_check; { Timer t; double x = 0.3; for (int k = 0; k < 2000; ++k) x = fake_game_logic(iters, x); us_check = t.sec() / 2000 * 1e6; if (x == 12345) printf("."); }
    vector<int> ns = {1, 2, 4, 8, 16, 24};
    vector<string> rows; vector<double> xs, sps, lagm, ups;
    for (int n : ns) {
        auto r = run_tp(n, 2 * n + 4, iters, 1.5);
        printf("[E2] %2d 个 actor（队列容量 %2d）：%7.0f 环境步/秒，learner %5.1f 次更新/秒（忙碌 %3.0f%%），策略滞后 平均 %.2f / p95 %.0f / 最大 %d 次更新\n",
               n, 2 * n + 4, r.steps_per_s, r.upd_per_s, 100 * r.learner_busy, r.lag_mean, r.lag_p95, r.lag_max);
        rows.push_back(JObj().num("n", n).num("q", 2 * n + 4).num("sps", r.steps_per_s, 6).num("ups", r.upd_per_s, 4).num("busy", 100 * r.learner_busy, 3)
            .num("lag", r.lag_mean, 4).num("p95", r.lag_p95).num("lmax", r.lag_max).str());
        xs.push_back(n); sps.push_back(r.steps_per_s); lagm.push_back(r.lag_mean); ups.push_back(r.upd_per_s);
    }
    // 同样 16 个 actor，队列容量从 4 加到 64：滞后随排队变长
    vector<string> qrows; vector<double> qx, qlag;
    for (int qc : {4, 8, 16, 32, 64}) {
        auto r = run_tp(16, qc, iters, 1.5);
        printf("[E2] 16 个 actor、队列容量 %2d：%7.0f 环境步/秒，策略滞后平均 %.2f、最大 %d\n", qc, r.steps_per_s, r.lag_mean, r.lag_max);
        qrows.push_back(JObj().num("q", qc).num("sps", r.steps_per_s, 6).num("lag", r.lag_mean, 4).num("lmax", r.lag_max).str());
        qx.push_back(qc); qlag.push_back(r.lag_mean);
    }
    printf("[E2] 标定：每步游戏逻辑 %.1f μs（%d 次迭代）；本机硬件线程数 %u\n", us_check, iters, thread::hardware_concurrency());
    return JObj().add("rows", jlist(rows)).add("sps", jpts(xs, sps, 6)).add("lag", jpts(xs, lagm, 4)).add("ups", jpts(xs, ups, 4))
        .add("qrows", jlist(qrows)).add("qlag", jpts(qx, qlag, 4)).num("us", us_check, 3).num("hw", thread::hardware_concurrency());
}

// ================================================================ E3 策略滞后 × 修正
struct LagCurve { vector<double> step, ret; double final = 0; };
static LagCurve a2c_lag(int lag, bool vtrace, int seed, double lr) {
    const int NE = 8, NS = 16, total = 300000; const double gamma = 0.99, ent = 0.01;
    Rng rng(seed * 977 + 5);
    MLP pi({4, 64, 64, 2}, rng, 0.01), vf({4, 64, 64, 1}, rng);
    deque<MLP> hist; hist.push_back(pi);   // hist.front() 是 lag 次更新之前的参数
    vector<CartPole> env(NE); vector<vector<double>> obs(NE);
    for (int e = 0; e < NE; ++e) obs[e] = env[e].reset(rng);
    LagCurve c; double acc = 0; int accn = 0, steps = 0, next_log = 10000;
    while (steps < total) {
        MLP& beh = hist.front();
        int N = NE * NS;
        vector<vector<double>> Sx(N), S2(N); vector<int> Ax(N), term(N), trunc(N); vector<double> R(N), LPmu(N);
        for (int t = 0; t < NS; ++t) for (int e = 0; e < NE; ++e) {
            int i = e * NS + t;
            Sx[i] = obs[e];
            auto p = softmax(beh.forward(obs[e]));
            int a = rng.u() < p[1] ? 1 : 0; Ax[i] = a; LPmu[i] = log(p[a]);
            bool te, tu; R[i] = env[e].step(a, te, tu); term[i] = te; trunc[i] = tu; S2[i] = env[e].obs();
            if (te || tu) { acc += env[e].t; ++accn; obs[e] = env[e].reset(rng); } else obs[e] = S2[i];
            ++steps;
        }
        for (int e = 0; e < NE; ++e) {
            vector<double> V(NS), Vn(NS), rho(NS), cc(NS), vs(NS);
            for (int t = 0; t < NS; ++t) {
                int i = e * NS + t;
                V[t] = vf.forward(Sx[i])[0]; Vn[t] = term[i] ? 0.0 : vf.forward(S2[i])[0];
                double w = exp(log(softmax(pi.forward(Sx[i]))[Ax[i]]) - LPmu[i]);
                rho[t] = vtrace ? min(1.0, w) : 1.0; cc[t] = vtrace ? min(1.0, w) : 1.0;
            }
            // v_s = V(x_s) + ρ_s δ_s + γ c_s (v_{s+1} − V(x_{s+1}))，在终止 / 截断处切断
            double nxt = 0;
            for (int t = NS - 1; t >= 0; --t) {
                int i = e * NS + t; bool end = term[i] || trunc[i] || t == NS - 1;
                double delta = R[i] + gamma * Vn[t] - V[t];
                double corr = end ? 0.0 : gamma * cc[t] * nxt;
                nxt = rho[t] * delta + corr;   // = v_t − V(x_t)
                vs[t] = V[t] + nxt;
            }
            for (int t = 0; t < NS; ++t) {
                int i = e * NS + t; bool end = term[i] || trunc[i] || t == NS - 1;
                double vnext = end ? Vn[t] : vs[t + 1];
                double adv = rho[t] * (R[i] + gamma * (term[i] ? 0.0 : vnext) - V[t]);
                auto p = softmax(pi.forward(Sx[i]));
                double H = 0; for (double x : p) H -= x * log(max(x, 1e-12));
                vector<double> g(2);
                for (int k = 0; k < 2; ++k) g[k] = (p[k] - (k == Ax[i])) * adv + ent * p[k] * (log(max(p[k], 1e-12)) + H);
                pi.backward(g);
                double vv = vf.forward(Sx[i])[0]; vf.backward({vv - vs[t]});
            }
        }
        pi.adam(lr, 1.0 / (NE * NS), 0.5); vf.adam(lr, 1.0 / (NE * NS), 0.5);
        hist.push_back(pi); while ((int)hist.size() > lag + 1) hist.pop_front();
        while (steps >= next_log) { c.step.push_back(next_log); c.ret.push_back(accn ? acc / accn : (c.ret.empty() ? 0 : c.ret.back())); acc = 0; accn = 0; next_log += 10000; }
    }
    double f = 0; for (size_t i = c.ret.size() - 5; i < c.ret.size(); ++i) f += c.ret[i] / 5; c.final = f;
    return c;
}
static JObj e3() {
    vector<int> lags = {0, 4, 16, 64};
    const int L = (int)lags.size(), SEEDS = 5;
    vector<LagCurve> res(L * 2 * SEEDS), res2(L * 2 * SEEDS);
    Timer tm;
    parallel_for(L * 2 * SEEDS, [&](int i) { int l = i / (2 * SEEDS), v = (i / SEEDS) % 2, sd = i % SEEDS; res[i] = a2c_lag(lags[l], v == 1, sd + 1, 1e-3); });
    parallel_for(L * 2 * SEEDS, [&](int i) { int l = i / (2 * SEEDS), v = (i / SEEDS) % 2, sd = i % SEEDS; res2[i] = a2c_lag(lags[l], v == 1, sd + 1, 5e-4); });
    vector<string> small;
    for (int l = 0; l < L; ++l) { double m[2]; for (int v = 0; v < 2; ++v) { m[v] = 0; for (int sd = 0; sd < SEEDS; ++sd) m[v] += res2[(l * 2 + v) * SEEDS + sd].final / SEEDS; }
        printf("[E3] 学习率 5e-4：滞后 %2d 不修正 %.1f，V-trace %.1f\n", lags[l], m[0], m[1]);
        small.push_back(JObj().num("lag", lags[l]).num("raw", m[0], 4).num("vt", m[1], 4).str()); }
    vector<string> out;
    for (int l = 0; l < L; ++l) for (int v = 0; v < 2; ++v) {
        int base = (l * 2 + v) * SEEDS; size_t n = res[base].step.size(); vector<double> mu(n, 0), fin;
        for (int sd = 0; sd < SEEDS; ++sd) { auto& c = res[base + sd]; fin.push_back(c.final); for (size_t k = 0; k < n; ++k) mu[k] += c.ret[k] / SEEDS; }
        printf("[E3] 滞后 %2d 次更新，%-12s 最后 5 万步平均回合长度 %.1f（种子 %.0f/%.0f/%.0f/%.0f/%.0f）\n", lags[l], v ? "V-trace 修正" : "不修正", mean(fin), fin[0], fin[1], fin[2], fin[3], fin[4]);
        out.push_back(JObj().num("lag", lags[l]).num("vt", v).add("name", js(string("滞后 ") + to_string(lags[l]) + (v ? "，V-trace" : "，不修正"))).add("mean", jpts(res[base].step, mu, 5))
            .num("final", mean(fin), 4).add("fin", ja(fin, 4)).str());
    }
    printf("[E3] 80 个训练（2 组 × 40 个并行），耗时 %.1f s\n", tm.sec());
    return JObj().add("runs", jlist(out)).add("lr2", jlist(small));
}

int main() {
    JObj root;
    root.add("e1", e1().str());
    root.add("e2", e2().str());
    root.add("e3", e3().str());
    write_file("ch19_output.json", root.str());
    printf("写出 ch19_output.json\n");
}
