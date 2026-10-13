// Part III / IV demo 公共头文件：随机数、小型 MLP（手写反传 + Adam）、CartPole 动力学、JSON 输出、多线程跑多个种子
// 只依赖标准库；编译：cl /nologo /std:c++latest /EHsc /O2 /utf-8 chNN_xxx.cpp
#pragma once
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <fstream>
#include <functional>
#include <numeric>
#include <sstream>
#include <string>
#include <thread>
#include <utility>
#include <vector>

constexpr double PI = 3.14159265358979323846;

// ---------------------------------------------------------------- 随机数（xorshift64*，与第 5 章 / rlcore.py 相同）
struct Rng {
    uint64_t s;
    explicit Rng(uint64_t seed) : s(seed * 0x9E3779B97F4A7C15ULL + 1) {}
    double u() {
        s ^= s >> 12; s ^= s << 25; s ^= s >> 27;
        return (double)((s * 2685821657736338717ULL) >> 11) * (1.0 / 9007199254740992.0);
    }
    int below(int n) { int k = (int)(u() * n); return k < n ? k : n - 1; }
    double uni(double a, double b) { return a + (b - a) * u(); }
    double normal() {
        double u1 = u(); if (u1 < 1e-300) u1 = 1e-300;
        return std::sqrt(-2.0 * std::log(u1)) * std::cos(2 * PI * u());
    }
    int categorical(const std::vector<double>& p) {
        double r = u(), c = 0;
        for (size_t i = 0; i < p.size(); ++i) { c += p[i]; if (r < c) return (int)i; }
        return (int)p.size() - 1;
    }
};

// ---------------------------------------------------------------- MLP：隐藏层 tanh，输出层线性；梯度累加后统一 Adam
struct MLP {
    std::vector<int> sz;
    std::vector<std::vector<double>> W, b, gW, gb, mW, vW, mb, vb, a;
    int t = 0;
    MLP() = default;
    MLP(std::vector<int> s, Rng& rng, double out_scale = 1.0) : sz(std::move(s)) {
        int L = (int)sz.size() - 1;
        W.resize(L); b.resize(L); a.resize(L + 1);
        for (int l = 0; l < L; ++l) {
            int in = sz[l], out = sz[l + 1];
            double k = std::sqrt(6.0 / (in + out)) * (l == L - 1 ? out_scale : 1.0);
            W[l].resize((size_t)in * out); b[l].assign(out, 0.0);
            for (auto& w : W[l]) w = rng.uni(-k, k);
        }
        gW = W; gb = b; mW = W; vW = W; mb = b; vb = b;
        for (auto* v : {&gW, &gb, &mW, &vW, &mb, &vb}) for (auto& x : *v) std::fill(x.begin(), x.end(), 0.0);
    }
    const std::vector<double>& forward(const std::vector<double>& x) {
        int L = (int)W.size();
        a[0] = x;
        for (int l = 0; l < L; ++l) {
            int in = sz[l], out = sz[l + 1];
            std::vector<double>& o = a[l + 1];
            o.assign(out, 0.0);
            const double* w = W[l].data(); const double* xi = a[l].data();
            for (int i = 0; i < out; ++i) {
                double z = b[l][i];
                for (int j = 0; j < in; ++j) z += w[(size_t)i * in + j] * xi[j];
                o[i] = l < L - 1 ? std::tanh(z) : z;
            }
        }
        return a[L];
    }
    // gout = dLoss/d(输出)；使用最近一次 forward 的缓存，梯度累加到 gW / gb
    void backward(const std::vector<double>& gout) {
        int L = (int)W.size();
        std::vector<double> g = gout, gin;
        for (int l = L - 1; l >= 0; --l) {
            int in = sz[l], out = sz[l + 1];
            const double* x = a[l].data();
            for (int i = 0; i < out; ++i) {
                if (g[i] == 0.0) continue;
                gb[l][i] += g[i];
                double* gw = &gW[l][(size_t)i * in];
                for (int j = 0; j < in; ++j) gw[j] += g[i] * x[j];
            }
            if (l > 0) {
                gin.assign(in, 0.0);
                for (int i = 0; i < out; ++i) {
                    if (g[i] == 0.0) continue;
                    const double* w = &W[l][(size_t)i * in];
                    for (int j = 0; j < in; ++j) gin[j] += w[j] * g[i];
                }
                for (int j = 0; j < in; ++j) gin[j] *= 1.0 - x[j] * x[j];
                g.swap(gin);
            }
        }
    }
    void zero_grad() { for (auto& x : gW) std::fill(x.begin(), x.end(), 0.0); for (auto& x : gb) std::fill(x.begin(), x.end(), 0.0); }
    double grad_norm(double scale) const {
        double s = 0; for (auto& x : gW) for (double v : x) s += v * v * scale * scale;
        for (auto& x : gb) for (double v : x) s += v * v * scale * scale;
        return std::sqrt(s);
    }
    // scale：梯度先乘 scale（一般是 1/batch）；clip > 0 时按全局范数裁剪
    void adam(double lr, double scale = 1.0, double clip = 0.0, double b1 = 0.9, double b2 = 0.999, double eps = 1e-8) {
        if (clip > 0) { double n = grad_norm(scale); if (n > clip) scale *= clip / n; }
        ++t;
        double c1 = 1 - std::pow(b1, t), c2 = 1 - std::pow(b2, t);
        auto upd = [&](std::vector<double>& p, std::vector<double>& g, std::vector<double>& m, std::vector<double>& v) {
            for (size_t i = 0; i < p.size(); ++i) {
                double gi = g[i] * scale;
                m[i] = b1 * m[i] + (1 - b1) * gi; v[i] = b2 * v[i] + (1 - b2) * gi * gi;
                p[i] -= lr * (m[i] / c1) / (std::sqrt(v[i] / c2) + eps);
            }
        };
        for (size_t l = 0; l < W.size(); ++l) { upd(W[l], gW[l], mW[l], vW[l]); upd(b[l], gb[l], mb[l], vb[l]); }
        zero_grad();
    }
    void copy_params_from(const MLP& o) { W = o.W; b = o.b; }
    void soft_update_from(const MLP& o, double tau) {
        for (size_t l = 0; l < W.size(); ++l) {
            for (size_t i = 0; i < W[l].size(); ++i) W[l][i] += tau * (o.W[l][i] - W[l][i]);
            for (size_t i = 0; i < b[l].size(); ++i) b[l][i] += tau * (o.b[l][i] - b[l][i]);
        }
    }
};

inline std::vector<double> softmax(const std::vector<double>& z) {
    double m = *std::max_element(z.begin(), z.end()), s = 0;
    std::vector<double> p(z.size());
    for (size_t i = 0; i < z.size(); ++i) { p[i] = std::exp(z[i] - m); s += p[i]; }
    for (auto& x : p) x /= s;
    return p;
}

// ---------------------------------------------------------------- CartPole（与 Gymnasium CartPole-v1 相同的动力学与终止条件）
struct CartPole {
    double x = 0, xd = 0, th = 0, thd = 0; int t = 0; int max_steps = 500;
    std::vector<double> reset(Rng& r) {
        x = r.uni(-.05, .05); xd = r.uni(-.05, .05); th = r.uni(-.05, .05); thd = r.uni(-.05, .05); t = 0;
        return obs();
    }
    std::vector<double> obs() const { return {x, xd, th, thd}; }
    // 返回奖励；term = 杆倒 / 出界（真正终止），trunc = 到达 500 步（截断，应自举）
    double step(int act, bool& term, bool& trunc) {
        const double g = 9.8, mc = 1.0, mp = 0.1, tot = mc + mp, len = 0.5, pml = mp * len, F = 10.0, tau = 0.02;
        double force = act == 1 ? F : -F, c = std::cos(th), s = std::sin(th);
        double temp = (force + pml * thd * thd * s) / tot;
        double thacc = (g * s - c * temp) / (len * (4.0 / 3.0 - mp * c * c / tot));
        double xacc = temp - pml * thacc * c / tot;
        x += tau * xd; xd += tau * xacc; th += tau * thd; thd += tau * thacc;
        ++t;
        term = x < -2.4 || x > 2.4 || th < -0.20943951023931953 || th > 0.20943951023931953;
        trunc = !term && t >= max_steps;
        return 1.0;
    }
};

// ---------------------------------------------------------------- JSON（手写，够用即可）
inline std::string jn(double v, int prec = 10) {
    if (!std::isfinite(v)) return "null";
    char buf[48]; std::snprintf(buf, sizeof buf, "%.*g", prec, v); return buf;
}
inline std::string ja(const std::vector<double>& v, int prec = 10) {
    std::string s = "["; for (size_t i = 0; i < v.size(); ++i) { if (i) s += ","; s += jn(v[i], prec); } return s + "]";
}
inline std::string jai(const std::vector<int>& v) {
    std::string s = "["; for (size_t i = 0; i < v.size(); ++i) { if (i) s += ","; s += std::to_string(v[i]); } return s + "]";
}
inline std::string jpts(const std::vector<double>& x, const std::vector<double>& y, int prec = 6) {
    std::string s = "["; for (size_t i = 0; i < x.size(); ++i) { if (i) s += ","; s += "[" + jn(x[i], prec) + "," + jn(y[i], prec) + "]"; } return s + "]";
}
inline std::string js(const std::string& v) { return "\"" + v + "\""; }
struct JObj {
    std::vector<std::pair<std::string, std::string>> kv;
    JObj& add(const std::string& k, const std::string& raw) { kv.emplace_back(k, raw); return *this; }
    JObj& num(const std::string& k, double v, int prec = 10) { return add(k, jn(v, prec)); }
    std::string str() const {
        std::string s = "{";
        for (size_t i = 0; i < kv.size(); ++i) { if (i) s += ",\n"; s += "\"" + kv[i].first + "\":" + kv[i].second; }
        return s + "}";
    }
};
inline std::string jlist(const std::vector<std::string>& items) {
    std::string s = "["; for (size_t i = 0; i < items.size(); ++i) { if (i) s += ",\n"; s += items[i]; } return s + "]";
}
inline void write_file(const std::string& path, const std::string& s) { std::ofstream f(path, std::ios::binary); f << s; }

// 并行跑 n 个独立任务（各自的随机数与网络，结果互不干扰；结果与线程调度无关）
inline void parallel_for(int n, const std::function<void(int)>& fn) {
    std::vector<std::thread> th;
    for (int i = 0; i < n; ++i) th.emplace_back(fn, i);
    for (auto& t : th) t.join();
}

inline double mean(const std::vector<double>& v) { return v.empty() ? 0 : std::accumulate(v.begin(), v.end(), 0.0) / v.size(); }
inline double stdev(const std::vector<double>& v) {
    double m = mean(v), s = 0; for (double x : v) s += (x - m) * (x - m); return v.size() > 1 ? std::sqrt(s / v.size()) : 0;
}
struct Timer {
    std::chrono::steady_clock::time_point t0 = std::chrono::steady_clock::now();
    double sec() const { return std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count(); }
};
