// 第 13 章实测：RLHF、DPO 与 GRPO（表格化的“玩具大模型”：4 个提示 × 8 个候选回答，策略 = 每个提示一组 softmax logits）
//   E1 奖励模型：从参考策略采 2000 对回答，按 Bradley-Terry 模型 P(y1≻y2) = σ(r1 − r2) 生成偏好标签，极大似然拟合表格奖励 r̂
//   E2 KL 正则的闭式最优解 π*_β ∝ π_ref·exp(r/β)：画出“奖励–KL 前沿”；无限数据下 DPO 的最优解与 π*_β 逐项比较
//   E3 同一份 2000 对偏好数据上：RLHF-PPO（用 r̂ + 每样本 KL 惩罚）/ GRPO（用 r̂ + 组内标准化 + KL 损失）/ DPO（直接用偏好对），
//      5 个种子并行，记录真实期望奖励 E_π[r] 与 KL(π‖π_ref) 的轨迹
// 输出：stdout + ch13_output.json
// 编译：cl /nologo /std:c++latest /EHsc /O2 /utf-8 ch13_rlhf.cpp
#include "rlcommon.h"
using namespace std;

const int X = 4, Y = 8;
const double BETA = 0.5;
using Tab = vector<vector<double>>;
static double sigm(double z) { return 1 / (1 + exp(-z)); }
static Tab probs(const Tab& th) { Tab p(X); for (int x = 0; x < X; ++x) p[x] = softmax(th[x]); return p; }
static double expR(const Tab& p, const Tab& r) { double s = 0; for (int x = 0; x < X; ++x) for (int y = 0; y < Y; ++y) s += p[x][y] * r[x][y] / X; return s; }
static double klref(const Tab& p, const Tab& pr) { double s = 0; for (int x = 0; x < X; ++x) for (int y = 0; y < Y; ++y) s += p[x][y] * log(p[x][y] / pr[x][y]) / X; return s; }
static Tab closed(const Tab& pref, const Tab& r, double beta) {
    Tab th(X, vector<double>(Y)); for (int x = 0; x < X; ++x) for (int y = 0; y < Y; ++y) th[x][y] = log(pref[x][y]) + r[x][y] / beta; return probs(th);
}
struct Adam2 {   // 表格参数的 Adam（梯度上升 / 下降由调用方决定符号）
    Tab m, v; int t = 0;
    Adam2() : m(X, vector<double>(Y, 0)), v(X, vector<double>(Y, 0)) {}
    void step(Tab& th, const Tab& g, double lr) {   // th -= lr * adam(g)
        ++t; double c1 = 1 - pow(0.9, t), c2 = 1 - pow(0.999, t);
        for (int x = 0; x < X; ++x) for (int y = 0; y < Y; ++y) {
            m[x][y] = 0.9 * m[x][y] + 0.1 * g[x][y]; v[x][y] = 0.999 * v[x][y] + 0.001 * g[x][y] * g[x][y];
            th[x][y] -= lr * (m[x][y] / c1) / (sqrt(v[x][y] / c2) + 1e-8);
        }
    }
};
struct World { Tab r, thref, pref; };
static World make_world(int seed) {
    World w; Rng g(seed * 13 + 1);
    w.r.assign(X, vector<double>(Y)); w.thref.assign(X, vector<double>(Y));
    for (int x = 0; x < X; ++x) for (int y = 0; y < Y; ++y) { w.r[x][y] = g.normal(); w.thref[x][y] = 0.8 * g.normal(); }
    w.pref = probs(w.thref);
    return w;
}
struct Pair { int x, w, l; };
static vector<Pair> make_pairs(const World& W, int n, Rng& g) {
    vector<Pair> d;
    while ((int)d.size() < n) {
        int x = g.below(X), a = g.categorical(W.pref[x]), b = g.categorical(W.pref[x]);
        if (a == b) continue;
        if (g.u() < sigm(W.r[x][a] - W.r[x][b])) d.push_back({x, a, b}); else d.push_back({x, b, a});
    }
    return d;
}
static Tab fit_rm(const vector<Pair>& d) {
    Tab rh(X, vector<double>(Y, 0)); Adam2 opt;
    for (int it = 0; it < 800; ++it) {
        Tab g(X, vector<double>(Y, 0));
        for (auto& p : d) { double q = sigm(rh[p.x][p.w] - rh[p.x][p.l]); g[p.x][p.w] -= (1 - q) / d.size(); g[p.x][p.l] += (1 - q) / d.size(); }
        for (int x = 0; x < X; ++x) for (int y = 0; y < Y; ++y) g[x][y] += 1e-3 * rh[x][y];
        opt.step(rh, g, 0.05);
    }
    return rh;
}
static double centered_corr(const Tab& a, const Tab& b) {   // 每个提示内减均值后的相关系数（奖励只确定到“每个提示一个常数”）
    vector<double> u, v;
    for (int x = 0; x < X; ++x) { double ma = 0, mb = 0; for (int y = 0; y < Y; ++y) { ma += a[x][y] / Y; mb += b[x][y] / Y; } for (int y = 0; y < Y; ++y) { u.push_back(a[x][y] - ma); v.push_back(b[x][y] - mb); } }
    double su = 0, sv = 0, suv = 0; for (size_t i = 0; i < u.size(); ++i) { su += u[i] * u[i]; sv += v[i] * v[i]; suv += u[i] * v[i]; }
    return suv / sqrt(su * sv);
}

struct Traj { vector<double> kl, er; };
static Traj rlhf_ppo(const World& W, const Tab& rh, Rng& g, int iters) {
    Tab th = W.thref; Adam2 opt; vector<double> V(X, 0); Traj tr;
    const int B = 64; const double eps = 0.2;
    for (int it = 0; it <= iters; ++it) {
        auto p = probs(th);
        if (it % 10 == 0) { tr.kl.push_back(klref(p, W.pref)); tr.er.push_back(expR(p, W.r)); }
        if (it == iters) break;
        vector<int> xs(B), ys(B); vector<double> lpo(B), R(B), A(B);
        for (int b = 0; b < B; ++b) {
            xs[b] = g.below(X); ys[b] = g.categorical(p[xs[b]]); lpo[b] = log(p[xs[b]][ys[b]]);
            R[b] = rh[xs[b]][ys[b]] - BETA * (lpo[b] - log(W.pref[xs[b]][ys[b]]));   // InstructGPT：KL 惩罚写进奖励
            A[b] = R[b] - V[xs[b]];
        }
        for (int b = 0; b < B; ++b) V[xs[b]] += 0.1 * (R[b] - V[xs[b]]);
        double mu = mean(A), sd = stdev(A) + 1e-8; for (auto& a : A) a = (a - mu) / sd;
        for (int ep = 0; ep < 4; ++ep) {
            auto pc = probs(th); Tab gr(X, vector<double>(Y, 0));
            for (int b = 0; b < B; ++b) {
                int x = xs[b], y = ys[b]; double ratio = exp(log(pc[x][y]) - lpo[b]);
                bool act = A[b] >= 0 ? ratio < 1 + eps : ratio > 1 - eps;
                if (act) for (int k = 0; k < Y; ++k) gr[x][k] -= A[b] * ratio * ((k == y) - pc[x][k]) / B;
            }
            opt.step(th, gr, 0.05);
        }
    }
    return tr;
}
static Traj grpo(const World& W, const Tab& rh, Rng& g, int iters) {
    Tab th = W.thref; Adam2 opt; Traj tr;
    const int NP = 8, G = 8; const double eps = 0.2;
    for (int it = 0; it <= iters; ++it) {
        auto p = probs(th);
        if (it % 10 == 0) { tr.kl.push_back(klref(p, W.pref)); tr.er.push_back(expR(p, W.r)); }
        if (it == iters) break;
        vector<int> xs, ys; vector<double> lpo, A;
        for (int q = 0; q < NP; ++q) {
            int x = g.below(X); vector<double> rr(G); vector<int> yy(G);
            for (int i = 0; i < G; ++i) { yy[i] = g.categorical(p[x]); rr[i] = rh[x][yy[i]]; }
            double mu = mean(rr), sd = stdev(rr);
            for (int i = 0; i < G; ++i) { xs.push_back(x); ys.push_back(yy[i]); lpo.push_back(log(p[x][yy[i]])); A.push_back((rr[i] - mu) / (sd + 1e-8)); }
        }
        int N = (int)xs.size();
        for (int ep = 0; ep < 4; ++ep) {
            auto pc = probs(th); Tab gr(X, vector<double>(Y, 0));
            for (int b = 0; b < N; ++b) {
                int x = xs[b], y = ys[b]; double lp = log(pc[x][y]), ratio = exp(lp - lpo[b]);
                bool act = A[b] >= 0 ? ratio < 1 + eps : ratio > 1 - eps;
                double coef = act ? -A[b] * ratio : 0.0;
                coef += BETA * (1 - W.pref[x][y] / pc[x][y]);   // KL 损失 k3 = π_ref/π − log(π_ref/π) − 1 对 log π 的导数
                for (int k = 0; k < Y; ++k) gr[x][k] += coef * ((k == y) - pc[x][k]) / N;
            }
            opt.step(th, gr, 0.05);
        }
    }
    return tr;
}
static Traj dpo(const World& W, const vector<Pair>& d, int epochs, Tab* out = nullptr) {
    Tab th = W.thref; Adam2 opt; Traj tr;
    for (int ep = 0; ep <= epochs; ++ep) {
        auto p = probs(th);
        if (ep % 10 == 0) { tr.kl.push_back(klref(p, W.pref)); tr.er.push_back(expR(p, W.r)); }
        if (ep == epochs) break;
        Tab gr(X, vector<double>(Y, 0));
        for (auto& q : d) {
            double z = BETA * ((log(p[q.x][q.w]) - log(W.pref[q.x][q.w])) - (log(p[q.x][q.l]) - log(W.pref[q.x][q.l])));
            double c = (sigm(z) - 1) * BETA / d.size();   // 表格情形：∂z/∂θ = β(e_w − e_l)，softmax 的归一化项相互抵消
            gr[q.x][q.w] += c; gr[q.x][q.l] -= c;
        }
        opt.step(th, gr, 0.05);
    }
    if (out) *out = probs(th);
    return tr;
}
static Tab dpo_infinite(const World& W) {   // 期望 DPO 损失（对 π_ref×π_ref 的所有回答对、按真实 BT 概率加权）的最优解
    Tab th = W.thref; Adam2 opt;
    for (int it = 0; it < 20000; ++it) {
        auto p = probs(th); Tab gr(X, vector<double>(Y, 0));
        for (int x = 0; x < X; ++x) for (int a = 0; a < Y; ++a) for (int b = 0; b < Y; ++b) {
            if (a == b) continue;
            double w = W.pref[x][a] * W.pref[x][b] / X, pa = sigm(W.r[x][a] - W.r[x][b]);
            double z = BETA * ((log(p[x][a]) - log(W.pref[x][a])) - (log(p[x][b]) - log(W.pref[x][b])));
            double c = w * pa * (sigm(z) - 1) * BETA;    // 标签 a ≻ b 的那部分
            gr[x][a] += c; gr[x][b] -= c;
        }
        opt.step(th, gr, it < 10000 ? 0.05 : 0.005);
    }
    return probs(th);
}

int main() {
    // ---------------- E1 / E2（种子 1 的世界）
    World W = make_world(1);
    Rng g(100);
    auto data = make_pairs(W, 2000, g);
    Tab rh = fit_rm(data);
    double corr = centered_corr(rh, W.r);
    double acc = 0; for (auto& p : data) acc += rh[p.x][p.w] > rh[p.x][p.l]; acc /= data.size();
    double accTrue = 0; for (auto& p : data) accTrue += W.r[p.x][p.w] > W.r[p.x][p.l]; accTrue /= data.size();
    printf("[E1] 2000 对偏好：奖励模型训练集准确率 %.3f（真实奖励本身只能解释 %.3f——BT 标签有噪声），r̂ 与真实 r（每个提示内去中心）相关系数 %.4f\n", acc, accTrue, corr);
    vector<double> bk, fkl, fer;
    for (int i = 0; i <= 40; ++i) { double b = pow(10, -1.3 + 2.6 * i / 40); auto ps = closed(W.pref, W.r, b); bk.push_back(b); fkl.push_back(klref(ps, W.pref)); fer.push_back(expR(ps, W.r)); }
    auto pstar = closed(W.pref, W.r, BETA);
    auto pinf = dpo_infinite(W);
    double dmax = 0; for (int x = 0; x < X; ++x) for (int y = 0; y < Y; ++y) dmax = max(dmax, fabs(log(pinf[x][y]) - log(pstar[x][y])));
    double er_ref = expR(W.pref, W.r), er_star = expR(pstar, W.r), kl_star = klref(pstar, W.pref);
    double er_max = 0; for (int x = 0; x < X; ++x) er_max += *max_element(W.r[x].begin(), W.r[x].end()) / X;
    printf("[E2] β=%.1f：π_ref 的 E[r] %.3f；闭式 π* 的 E[r] %.3f、KL %.3f；贪心上限 E[r] %.3f；无限数据 DPO 最优解与 π* 的 |log π| 最大差 %.2e\n",
           BETA, er_ref, er_star, kl_star, er_max, dmax);
    // 提示 0 的三个分布（画柱状图）
    JObj e12; e12.num("acc", acc, 4).num("acc_true", accTrue, 4).num("corr", corr, 5).num("beta", BETA).num("er_ref", er_ref, 5).num("er_star", er_star, 5)
        .num("kl_star", kl_star, 5).num("er_max", er_max, 5).num("dpo_inf_err", dmax, 3).add("front", jpts(fkl, fer)).add("front_beta", ja(bk, 5))
        .add("r0", ja(W.r[0], 5)).add("pref0", ja(W.pref[0], 5)).add("pstar0", ja(pstar[0], 5)).add("pinf0", ja(pinf[0], 5));
    vector<string> lab; for (int y = 0; y < Y; ++y) lab.push_back(js("y" + to_string(y + 1)));
    e12.add("labels", jlist(lab));

    // ---------------- E3：三种方法 × 5 个种子
    const int SEEDS = 5, ITERS = 300;
    vector<Traj> res(3 * SEEDS); vector<double> fin_kl(3 * SEEDS), fin_er(3 * SEEDS), base_star(SEEDS), base_ref(SEEDS);
    parallel_for(3 * SEEDS, [&](int i) {
        int m = i / SEEDS, sd = i % SEEDS;
        World w = make_world(sd + 1); Rng gg(100 + sd);
        auto dd = make_pairs(w, 2000, gg); Tab rr = fit_rm(dd); Rng gs(500 + sd);
        if (m == 0) res[i] = rlhf_ppo(w, rr, gs, ITERS);
        else if (m == 1) res[i] = grpo(w, rr, gs, ITERS);
        else res[i] = dpo(w, dd, ITERS);
        fin_kl[i] = res[i].kl.back(); fin_er[i] = res[i].er.back();
        if (m == 0) { base_star[sd] = expR(closed(w.pref, w.r, BETA), w.r); base_ref[sd] = expR(w.pref, w.r); }
    });
    const char* names[3] = {"RLHF-PPO（奖励模型 + KL 惩罚）", "GRPO（组内标准化 + KL 损失）", "DPO（直接用偏好对）"};
    vector<string> runs;
    for (int m = 0; m < 3; ++m) {
        size_t n = res[m * SEEDS].kl.size(); vector<double> kl(n, 0), er(n, 0), gain(n, 0), xs(n);
        for (size_t i = 0; i < n; ++i) xs[i] = 10.0 * i;
        vector<double> fk, fe, fg;
        for (int sd = 0; sd < SEEDS; ++sd) {
            auto& t = res[m * SEEDS + sd];
            for (size_t i = 0; i < n; ++i) { kl[i] += t.kl[i] / SEEDS; er[i] += t.er[i] / SEEDS; gain[i] += (t.er[i] - base_ref[sd]) / (base_star[sd] - base_ref[sd]) / SEEDS; }
            fk.push_back(t.kl.back()); fe.push_back(t.er.back()); fg.push_back((t.er.back() - base_ref[sd]) / (base_star[sd] - base_ref[sd]));
        }
        printf("[E3] %-30s 第 %d 步：KL %.3f，真实 E[r] %.3f，达到闭式 π* 提升量的 %.0f%%（5 种子平均）\n", names[m], ITERS, mean(fk), mean(fe), 100 * mean(fg));
        runs.push_back(JObj().add("name", js(names[m])).add("traj", jpts(kl, er, 5)).add("gain", jpts(xs, gain, 5)).add("klc", jpts(xs, kl, 5))
            .num("kl", mean(fk), 4).num("er", mean(fe), 4).num("gain_f", 100 * mean(fg), 3).str());
    }
    // 5 个世界在同一 β 网格上的平均前沿（与 5 种子平均的轨迹对应）
    vector<double> akl(41, 0), aer(41, 0);
    for (int sd = 0; sd < SEEDS; ++sd) {
        World w = make_world(sd + 1);
        for (int i = 0; i <= 40; ++i) { double bb = pow(10, -1.3 + 2.6 * i / 40); auto ps = closed(w.pref, w.r, bb); akl[i] += klref(ps, w.pref) / SEEDS; aer[i] += expR(ps, w.r) / SEEDS; }
    }
    JObj root; root.add("e1", e12.str()).add("e3", JObj().add("runs", jlist(runs)).num("iters", ITERS).add("front_avg", jpts(akl, aer)).str());
    write_file("ch13_output.json", root.str());
    printf("写出 ch13_output.json\n");
}
