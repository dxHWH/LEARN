// 第 20 章实测：引擎侧推理
//   E1 小 MLP 推理的每 NPC 耗时：逐个标量 / 合批标量（权重行复用）/ 逐个 AVX2 / 合批 AVX2（4×8 微内核），
//      两种网络规模（小：64-128-128-16，约 0.1 MB；大：256-1024-1024-64，约 5.3 MB，超出 L2），批大小 1…256
//   E2 数值：int8 权重量化（逐行对称）、int8 权重 + 激活量化，以及“部署时忘了观测归一化”对输出与动作的影响
//   E3 帧预算：200 个 NPC，按距离分 LOD（10 Hz / 2 Hz / 不推理），60 FPS；比较“同一帧集中决策”与“错峰分桶”的每帧推理耗时
// 网络为随机初始化（He 初始化，ReLU），观测为合成数据：本 demo 只测性能与数值误差，不涉及策略好坏。
// 输出：stdout + ch20_output.json      编译：cl /nologo /std:c++latest /EHsc /O2 /utf-8 ch20_infer_bench.cpp
#include "rlcommon.h"
#include <immintrin.h>
#include <intrin.h>
#include <map>
using namespace std;

struct Net {
    vector<int> sz;                       // 层宽
    vector<vector<float>> W, WT, b;       // W：out×in 行主序；WT：in×outP（outP 补齐到 8 的倍数）；b：补齐
    vector<int> outP;
    Net(vector<int> s, Rng& r) : sz(std::move(s)) {
        int L = (int)sz.size() - 1;
        W.resize(L); WT.resize(L); b.resize(L); outP.resize(L);
        for (int l = 0; l < L; ++l) {
            int in = sz[l], out = sz[l + 1], op = (out + 7) / 8 * 8; outP[l] = op;
            W[l].resize((size_t)in * out); WT[l].assign((size_t)in * op, 0.f); b[l].assign(op, 0.f);
            double sd = sqrt(2.0 / in);
            for (int i = 0; i < out; ++i) for (int k = 0; k < in; ++k) { float w = (float)(r.normal() * sd); W[l][(size_t)i * in + k] = w; WT[l][(size_t)k * op + i] = w; }
            for (int i = 0; i < out; ++i) b[l][i] = (float)(r.normal() * 0.01);
        }
    }
    size_t bytes() const { size_t s = 0; for (size_t l = 0; l < W.size(); ++l) s += (W[l].size() + sz[l + 1]) * 4; return s; }
    double flops() const { double f = 0; for (size_t l = 0; l + 1 < sz.size(); ++l) f += 2.0 * sz[l] * sz[l + 1]; return f; }
};

// ---------------- 书中代码块（开始）
// ---------------- 1) 逐个、标量：每个 NPC 单独跑一遍，权重每次都从内存 / 缓存重新读
static void fwd_single_scalar(const Net& n, const float* X, float* Y, int B, vector<float>& h0, vector<float>& h1) {
    int L = (int)n.W.size(), in0 = n.sz[0], outL = n.sz[L];
    for (int bi = 0; bi < B; ++bi) {
        h0.assign(X + (size_t)bi * in0, X + (size_t)(bi + 1) * in0);
        for (int l = 0; l < L; ++l) {
            int in = n.sz[l], out = n.sz[l + 1]; h1.resize(out);
            const float* w = n.W[l].data();
            for (int i = 0; i < out; ++i) {
                float s = n.b[l][i]; const float* wr = w + (size_t)i * in;
                for (int k = 0; k < in; ++k) s += wr[k] * h0[k];
                h1[i] = l < L - 1 ? max(0.f, s) : s;
            }
            h0.swap(h1);
        }
        memcpy(Y + (size_t)bi * outL, h0.data(), outL * 4);
    }
}
// ---------------- 2) 合批、标量：逐层计算，同一行权重对 B 个 NPC 复用
static void fwd_batch_scalar(const Net& n, const float* X, float* Y, int B, vector<float>& A0, vector<float>& A1) {
    int L = (int)n.W.size();
    A0.assign(X, X + (size_t)B * n.sz[0]);
    for (int l = 0; l < L; ++l) {
        int in = n.sz[l], out = n.sz[l + 1]; A1.resize((size_t)B * out);
        const float* w = n.W[l].data();
        for (int i = 0; i < out; ++i) {
            const float* wr = w + (size_t)i * in;
            for (int bi = 0; bi < B; ++bi) {
                float s = n.b[l][i]; const float* x = A0.data() + (size_t)bi * in;
                for (int k = 0; k < in; ++k) s += wr[k] * x[k];
                A1[(size_t)bi * out + i] = l < L - 1 ? max(0.f, s) : s;
            }
        }
        A0.swap(A1);
    }
    memcpy(Y, A0.data(), (size_t)B * n.sz[L] * 4);
}
// ---------------- 3/4) AVX2 + FMA：权重按 in×out 存储，沿输出维 8 路向量化；合批时一次处理 4 个 NPC（4×8 微内核）
static void layer_avx2(const float* WT, const float* bias, int in, int out, int op, const float* X, int ldx, float* Yo, int ldy, int B, bool relu) {
    const __m256 zero = _mm256_setzero_ps();
    for (int j = 0; j < op; j += 8) {
        __m256 bj = _mm256_loadu_ps(bias + j);
        int bi = 0;
        for (; bi + 4 <= B; bi += 4) {
            __m256 a0 = bj, a1 = bj, a2 = bj, a3 = bj;
            const float* x0 = X + (size_t)bi * ldx; const float* x1 = x0 + ldx; const float* x2 = x1 + ldx; const float* x3 = x2 + ldx;
            for (int k = 0; k < in; ++k) {
                __m256 w = _mm256_loadu_ps(WT + (size_t)k * op + j);      // 这一行权重被 4 个 NPC 共用
                a0 = _mm256_fmadd_ps(_mm256_set1_ps(x0[k]), w, a0);
                a1 = _mm256_fmadd_ps(_mm256_set1_ps(x1[k]), w, a1);
                a2 = _mm256_fmadd_ps(_mm256_set1_ps(x2[k]), w, a2);
                a3 = _mm256_fmadd_ps(_mm256_set1_ps(x3[k]), w, a3);
            }
            if (relu) { a0 = _mm256_max_ps(a0, zero); a1 = _mm256_max_ps(a1, zero); a2 = _mm256_max_ps(a2, zero); a3 = _mm256_max_ps(a3, zero); }
            _mm256_storeu_ps(Yo + (size_t)bi * ldy + j, a0); _mm256_storeu_ps(Yo + (size_t)(bi + 1) * ldy + j, a1);
            _mm256_storeu_ps(Yo + (size_t)(bi + 2) * ldy + j, a2); _mm256_storeu_ps(Yo + (size_t)(bi + 3) * ldy + j, a3);
        }
        for (; bi < B; ++bi) {
            __m256 a0 = bj; const float* x0 = X + (size_t)bi * ldx;
            for (int k = 0; k < in; ++k) a0 = _mm256_fmadd_ps(_mm256_set1_ps(x0[k]), _mm256_loadu_ps(WT + (size_t)k * op + j), a0);
            if (relu) a0 = _mm256_max_ps(a0, zero);
            _mm256_storeu_ps(Yo + (size_t)bi * ldy + j, a0);
        }
    }
    (void)out;
}
// ---------------- 书中代码块（结束）
static void fwd_avx2(const Net& n, const float* X, float* Y, int B, vector<float>& A0, vector<float>& A1, bool batched) {
    int L = (int)n.W.size(), in0 = n.sz[0], outL = n.sz[L];
    if (!batched) {   // 逐个调用（B 次 batch = 1）
        for (int bi = 0; bi < B; ++bi) {
            A0.assign(X + (size_t)bi * in0, X + (size_t)(bi + 1) * in0); int ld = in0;
            for (int l = 0; l < L; ++l) { A1.resize(n.outP[l]); layer_avx2(n.WT[l].data(), n.b[l].data(), n.sz[l], n.sz[l + 1], n.outP[l], A0.data(), ld, A1.data(), n.outP[l], 1, l < L - 1); ld = n.outP[l]; A0.swap(A1); }
            memcpy(Y + (size_t)bi * outL, A0.data(), outL * 4);
        }
        return;
    }
    A0.assign(X, X + (size_t)B * in0); int ld = in0;
    for (int l = 0; l < L; ++l) { A1.resize((size_t)B * n.outP[l]); layer_avx2(n.WT[l].data(), n.b[l].data(), n.sz[l], n.sz[l + 1], n.outP[l], A0.data(), ld, A1.data(), n.outP[l], B, l < L - 1); ld = n.outP[l]; A0.swap(A1); }
    for (int bi = 0; bi < B; ++bi) memcpy(Y + (size_t)bi * outL, A0.data() + (size_t)bi * ld, outL * 4);
}
static bool has_avx2_fma() {
    int r[4]; __cpuid(r, 7); bool avx2 = (r[1] & (1 << 5)) != 0; __cpuid(r, 1); bool fma = (r[2] & (1 << 12)) != 0; return avx2 && fma;
}
// 计时：重复直到累计 ≥ 30 ms，取 5 轮中最快的一轮，返回每次调用的秒数
template <class F> static double bench(F f) {
    double best = 1e9;
    for (int rep = 0; rep < 5; ++rep) {
        int iters = 0; Timer t;
        do { f(); ++iters; } while (t.sec() < 0.03);
        best = min(best, t.sec() / iters);
    }
    return best;
}

static JObj e1(double& small_ns64) {
    bool ok = has_avx2_fma();
    printf("[E1] CPU 支持 AVX2 + FMA：%s\n", ok ? "是" : "否（AVX2 两列跳过）");
    vector<int> Bs = {1, 4, 16, 64, 256};
    vector<pair<string, vector<int>>> nets = {{"small", {64, 128, 128, 16}}, {"large", {256, 1024, 1024, 64}}};
    JObj res;
    for (auto& [name, sz] : nets) {
        Rng r(20); Net n(sz, r);
        int in0 = sz.front(), outL = sz.back();
        vector<float> X((size_t)256 * in0), Y1((size_t)256 * outL), Y2 = Y1, Y3 = Y1, Y4 = Y1, a, bb;
        for (auto& v : X) v = (float)r.normal();
        // 正确性：四种实现输出一致
        fwd_single_scalar(n, X.data(), Y1.data(), 64, a, bb); fwd_batch_scalar(n, X.data(), Y2.data(), 64, a, bb);
        double md = 0;
        if (ok) { fwd_avx2(n, X.data(), Y3.data(), 64, a, bb, false); fwd_avx2(n, X.data(), Y4.data(), 64, a, bb, true); }
        for (size_t i = 0; i < (size_t)64 * outL; ++i) { md = max(md, (double)fabs(Y1[i] - Y2[i])); if (ok) { md = max(md, (double)fabs(Y1[i] - Y3[i])); md = max(md, (double)fabs(Y1[i] - Y4[i])); } }
        vector<string> rows; vector<double> xs; vector<vector<double>> ys(4);
        for (int B : Bs) {
            double t1 = bench([&] { fwd_single_scalar(n, X.data(), Y1.data(), B, a, bb); });
            double t2 = bench([&] { fwd_batch_scalar(n, X.data(), Y2.data(), B, a, bb); });
            double t3 = ok ? bench([&] { fwd_avx2(n, X.data(), Y3.data(), B, a, bb, false); }) : 0;
            double t4 = ok ? bench([&] { fwd_avx2(n, X.data(), Y4.data(), B, a, bb, true); }) : 0;
            double ns[4] = {t1 / B * 1e9, t2 / B * 1e9, t3 / B * 1e9, t4 / B * 1e9};
            xs.push_back(B); for (int k = 0; k < 4; ++k) ys[k].push_back(ns[k]);
            double gf = n.flops() / (ns[3] > 0 ? ns[3] : ns[1]);   // GFLOP/s = flops / ns
            printf("[E1] %-5s 批大小 %3d：每 NPC 逐个标量 %8.0f ns | 合批标量 %8.0f ns | 逐个 AVX2 %7.0f ns | 合批 AVX2 %7.0f ns（%.1f GFLOP/s）\n", name.c_str(), B, ns[0], ns[1], ns[2], ns[3], gf);
            rows.push_back(JObj().num("B", B).num("s1", ns[0], 5).num("s2", ns[1], 5).num("s3", ns[2], 5).num("s4", ns[3], 5).num("gf", gf, 4)
                .num("sp", ns[0] / (ns[3] > 0 ? ns[3] : ns[1]), 4).str());
            if (name == "small" && B == 64) small_ns64 = ns[3] > 0 ? ns[3] : ns[1];
        }
        printf("[E1] %-5s 网络 %s：参数 %.2f MB，每次前向 %.2f MFLOP；四种实现输出最大差 %.2e\n", name.c_str(),
               [&] { string s; for (size_t i = 0; i < sz.size(); ++i) { if (i) s += "-"; s += to_string(sz[i]); } return s; }().c_str(), n.bytes() / 1048576.0, n.flops() / 1e6, md);
        res.add(name, JObj().add("rows", jlist(rows)).add("p1", jpts(xs, ys[0], 5)).add("p2", jpts(xs, ys[1], 5)).add("p3", jpts(xs, ys[2], 5)).add("p4", jpts(xs, ys[3], 5))
                  .num("mb", n.bytes() / 1048576.0, 4).num("mflop", n.flops() / 1e6, 4).num("maxdiff", md, 3).str());
    }
    return res.num("avx2", ok);
}

// ---------------- E2 量化与归一化
static void fwd_ref(const vector<vector<float>>& W, const vector<vector<float>>& b, const vector<int>& sz, const float* x, float* y, bool quant_act) {
    vector<float> h(x, x + sz[0]), h2; int L = (int)W.size();
    for (int l = 0; l < L; ++l) {
        if (quant_act) {   // 动态逐张量对称量化：scale = max|h| / 127
            float m = 1e-12f; for (float v : h) m = max(m, fabs(v)); float s = m / 127.f;
            for (auto& v : h) v = roundf(v / s) * s;
        }
        int in = sz[l], out = sz[l + 1]; h2.assign(out, 0.f);
        for (int i = 0; i < out; ++i) { float s = b[l][i]; for (int k = 0; k < in; ++k) s += W[l][(size_t)i * in + k] * h[k]; h2[i] = l < L - 1 ? max(0.f, s) : s; }
        h.swap(h2);
    }
    memcpy(y, h.data(), sz[L] * 4);
}
static JObj e2() {
    Rng r(21); vector<int> sz = {64, 128, 128, 16}; Net n(sz, r);
    vector<vector<float>> Wq = n.W;
    for (size_t l = 0; l < Wq.size(); ++l) {   // 逐行（逐输出通道）对称 int8：w ≈ q·s，q ∈ [−127, 127]
        int in = sz[l], out = sz[l + 1];
        for (int i = 0; i < out; ++i) { float m = 1e-12f; for (int k = 0; k < in; ++k) m = max(m, fabs(Wq[l][(size_t)i * in + k])); float s = m / 127.f;
            for (int k = 0; k < in; ++k) { float& w = Wq[l][(size_t)i * in + k]; w = roundf(w / s) * s; } }
    }
    // “训练时”的观测统计量：每个特征的均值 / 标准差差别很大（坐标以厘米计、血量 0~5000、冷却 0~1 …）
    vector<double> mu(64), sd(64);
    for (int k = 0; k < 64; ++k) { mu[k] = r.uni(-500, 500) * (k % 4 == 0); sd[k] = pow(10.0, r.uni(-1, 3)); }
    const int M = 20000; int agree_w = 0, agree_wa = 0, agree_raw = 0;
    double maxd_w = 0, maxd_wa = 0, kl_w = 0, kl_wa = 0, kl_raw = 0, sum_abs = 0;
    vector<float> x(64), xr(64), y0(16), y1(16), y2(16), y3(16);
    auto amax = [](const vector<float>& v) { return (int)(max_element(v.begin(), v.end()) - v.begin()); };
    auto kl = [](const vector<float>& p, const vector<float>& q) {
        vector<double> a(p.begin(), p.end()), c(q.begin(), q.end()); auto sa = softmax(a), sc = softmax(c); double s = 0;
        for (size_t i = 0; i < sa.size(); ++i) s += sa[i] * log(max(sa[i], 1e-300) / max(sc[i], 1e-300)); return s; };
    for (int m = 0; m < M; ++m) {
        for (int k = 0; k < 64; ++k) { double raw = mu[k] + sd[k] * r.normal(); xr[k] = (float)raw; x[k] = (float)((raw - mu[k]) / sd[k]); }
        fwd_ref(n.W, n.b, sz, x.data(), y0.data(), false);
        fwd_ref(Wq, n.b, sz, x.data(), y1.data(), false);
        fwd_ref(Wq, n.b, sz, x.data(), y2.data(), true);
        fwd_ref(n.W, n.b, sz, xr.data(), y3.data(), false);   // 部署端忘了归一化
        for (int i = 0; i < 16; ++i) { maxd_w = max(maxd_w, (double)fabs(y1[i] - y0[i])); maxd_wa = max(maxd_wa, (double)fabs(y2[i] - y0[i])); sum_abs += fabs(y0[i]) / (16.0 * M); }
        agree_w += amax(y0) == amax(y1); agree_wa += amax(y0) == amax(y2); agree_raw += amax(y0) == amax(y3);
        kl_w += kl(y0, y1) / M; kl_wa += kl(y0, y2) / M; kl_raw += kl(y0, y3) / M;
    }
    printf("[E2] logits 平均幅值 %.3f；int8 权重：最大误差 %.4f，argmax 一致率 %.2f%%，平均 KL %.2e | int8 权重 + 激活：最大误差 %.4f，一致率 %.2f%%，KL %.2e\n",
           sum_abs, maxd_w, 100.0 * agree_w / M, kl_w, maxd_wa, 100.0 * agree_wa / M, kl_wa);
    printf("[E2] 部署时忘了观测归一化：argmax 一致率 %.2f%%（16 个动作随机猜为 6.25%%），平均 KL %.2f\n", 100.0 * agree_raw / M, kl_raw);
    return JObj().num("mag", sum_abs, 4).num("maxd_w", maxd_w, 4).num("agree_w", 100.0 * agree_w / M, 5).num("kl_w", kl_w, 3)
        .num("maxd_wa", maxd_wa, 4).num("agree_wa", 100.0 * agree_wa / M, 5).num("kl_wa", kl_wa, 3).num("agree_raw", 100.0 * agree_raw / M, 4).num("kl_raw", kl_raw, 4).num("M", M);
}

// ---------------- E3 帧预算与错峰
static JObj e3() {
    bool ok = has_avx2_fma();
    Rng r(22); vector<int> sz = {256, 1024, 1024, 64}; Net n(sz, r);   // 用“大”网络（例如带实体编码的策略）才看得出帧尖峰
    const int NPC = 200, FPS = 60, FR = 60; const double budget_ms = 2.0;
    // 距离分层：40 个近处（10 Hz → 每 6 帧），80 个中距（2 Hz → 每 30 帧），80 个远处（不推理，行为树兜底）
    vector<int> period(NPC, 0); for (int i = 0; i < NPC; ++i) period[i] = i < 40 ? 6 : (i < 120 ? 30 : 0);
    vector<float> X((size_t)NPC * 256), Y((size_t)NPC * 64), a, bb; for (auto& v : X) v = (float)r.normal();
    map<int, double> memo;
    auto cost = [&](int B) { if (B == 0) return 0.0; if (memo.count(B)) return memo[B]; return memo[B] = bench([&] { if (ok) fwd_avx2(n, X.data(), Y.data(), B, a, bb, true); else fwd_batch_scalar(n, X.data(), Y.data(), B, a, bb); }); };
    vector<double> fx, f_all, f_stag; double peak_all = 0, peak_stag = 0, mean_all = 0, mean_stag = 0; int maxB_all = 0, maxB_stag = 0;
    for (int f = 0; f < FR; ++f) {
        int ba = 0, bs = 0;
        for (int i = 0; i < NPC; ++i) if (period[i]) { if (f % period[i] == 0) ++ba; if ((f + i) % period[i] == 0) ++bs; }
        double ca = cost(ba) * 1e3, cs = cost(bs) * 1e3;
        fx.push_back(f); f_all.push_back(ca); f_stag.push_back(cs);
        peak_all = max(peak_all, ca); peak_stag = max(peak_stag, cs); mean_all += ca / FR; mean_stag += cs / FR; maxB_all = max(maxB_all, ba); maxB_stag = max(maxB_stag, bs);
    }
    double frame_ms = 1000.0 / FPS;
    int over_all = 0, over_stag = 0; for (int f = 0; f < FR; ++f) { over_all += f_all[f] > budget_ms; over_stag += f_stag[f] > budget_ms; }
    printf("[E3] 200 个 NPC（40 个 10 Hz、80 个 2 Hz、80 个不推理），60 FPS，网络 256-1024-1024-64：集中决策 峰值 %.2f ms（一帧最多 %d 个）/ 平均 %.2f ms / 超出 %.0f ms 预算的帧 %d 个；错峰分桶 峰值 %.2f ms（最多 %d 个）/ 平均 %.2f ms / 超预算 %d 帧；一帧 %.2f ms\n",
           peak_all, maxB_all, mean_all, budget_ms, over_all, peak_stag, maxB_stag, mean_stag, over_stag, frame_ms);
    double c1 = cost(1) * 1e6, c64 = cost(64) * 1e6 / 64;
    printf("[E3] 参考（大网络）：单个 NPC 单独推理 %.1f μs；64 个合批时每个 %.1f μs\n", c1, c64);
    return JObj().add("all", jpts(fx, f_all, 4)).add("stag", jpts(fx, f_stag, 4)).num("peak_all", peak_all, 4).num("peak_stag", peak_stag, 4)
        .num("mean_all", mean_all, 4).num("mean_stag", mean_stag, 4).num("maxB_all", maxB_all).num("maxB_stag", maxB_stag).num("frame_ms", frame_ms, 4).num("c1", c1, 4).num("c64", c64, 4).num("budget", budget_ms).num("over_all", over_all).num("over_stag", over_stag);
}

int main() {
    double small64 = 0;
    JObj root;
    root.add("e1", e1(small64).str());
    root.add("e2", e2().str());
    root.add("e3", e3().str());
    write_file("ch20_output.json", root.str());
    printf("写出 ch20_output.json（计时数字每次运行会有波动）\n");
}
