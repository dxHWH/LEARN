// netsync_demo.cpp —— 《网络同步全景：从 HOK 帧同步到 Unreal 状态同步》全部实测实验
// 编译：cl /nologo /std:c++latest /EHsc /O2 /utf-8 netsync_demo.cpp
// 运行：netsync_demo.exe            全部实验，写 figdata.json
//       netsync_demo.exe E3 E9      只跑指定实验（输出 figdata_part.json，不覆盖主文件）
// 所有随机数来自同一个 xorshift64*（种子固定），结果可复现。书里引用的数字全部来自 figdata.json。
#include <algorithm>
#include <climits>
#include <cmath>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <functional>
#include <map>
#include <queue>
#include <set>
#include <string>
#include <vector>

// ------------------------------------------------------------------ 工具
static std::string F(const char* fmt, ...) {
    va_list ap, ap2;
    va_start(ap, fmt);
    va_copy(ap2, ap);
    int n = vsnprintf(nullptr, 0, fmt, ap);
    va_end(ap);
    std::string s(n, '\0');
    vsnprintf(s.data(), n + 1, fmt, ap2);
    va_end(ap2);
    return s;
}
static std::string jn(double v) { return std::isfinite(v) ? F("%.6g", v) : std::string("null"); }
static std::string jarr(const std::vector<std::string>& v) {
    std::string s = "[";
    for (size_t i = 0; i < v.size(); i++) s += (i ? "," : "") + v[i];
    return s + "]";
}
static std::string jarrd(const std::vector<double>& v) {
    std::vector<std::string> t;
    for (double x : v) t.push_back(jn(x));
    return jarr(t);
}
struct JO {
    std::vector<std::pair<std::string, std::string>> kv;
    JO& raw(const std::string& k, const std::string& v) { kv.push_back({k, v}); return *this; }
    JO& num(const std::string& k, double v) { return raw(k, jn(v)); }
    JO& str(const std::string& k, const std::string& v) { return raw(k, "\"" + v + "\""); }
    std::string s() const {
        std::string o = "{";
        for (size_t i = 0; i < kv.size(); i++) o += (i ? "," : "") + ("\"" + kv[i].first + "\":") + kv[i].second;
        return o + "}";
    }
};
static std::string pts(const std::vector<std::pair<double, double>>& p) {
    std::vector<std::string> t;
    for (auto& q : p) t.push_back("[" + jn(q.first) + "," + jn(q.second) + "]");
    return jarr(t);
}

struct Rng {
    uint64_t s;
    explicit Rng(uint64_t seed) : s(seed * 0x9E3779B97F4A7C15ULL + 1) {}
    uint64_t next() { s ^= s >> 12; s ^= s << 25; s ^= s >> 27; return s * 2685821657736338717ULL; }
    double u() { return (next() >> 11) * (1.0 / 9007199254740992.0); }
    double expo(double mean) { return mean <= 0 ? 0.0 : -mean * std::log(1.0 - u()); }
    double gauss() { double a = u() + 1e-12, b = u(); return std::sqrt(-2 * std::log(a)) * std::cos(6.283185307179586 * b); }
};

static double mean(const std::vector<double>& v) { double s = 0; for (double x : v) s += x; return v.empty() ? 0 : s / v.size(); }
static double pct(std::vector<double> v, double p) {
    if (v.empty()) return 0;
    std::sort(v.begin(), v.end());
    size_t k = (size_t)std::min<double>(v.size() - 1, std::floor(p * (v.size() - 1) + 0.5));
    return v[k];
}

// ================================================================== E1 带宽模型：帧同步 vs 全量快照 vs 增量快照 vs 增量+AOI
static std::string E1() {
    printf("\n== E1 带宽：帧同步（只传输入） vs 状态同步（传状态），15 Hz，10 名玩家 ==\n");
    const double HZ = 15, UDP = 28, APP = 8;
    const int P = 10, K = 3;           // 帧同步：每包带最近 3 帧（冗余）
    const double cmdPerSec = 5;        // 每人每秒约 5 条命令（摇杆变化 + 技能）
    double cmdPerFrame = P * cmdPerSec / HZ;
    double fsPkt = UDP + APP + K * (4 + cmdPerFrame * 6);
    double fs = fsPkt * HZ / 1024;
    std::vector<std::string> rows;
    printf("  帧同步：每帧 %.2f 条命令，包 %.0f B → %.2f KB/s（与单位数无关）\n", cmdPerFrame, fsPkt, fs);
    printf("  %6s %10s %10s %10s %10s\n", "单位数", "帧同步", "全量快照", "增量快照", "增量+AOI");
    for (int N : {10, 25, 50, 100, 200, 400}) {
        auto pk = [&](double payload) { double n = std::ceil(payload / 1200.0); return payload + n * (UDP + APP); };
        double full = pk(N * 32.0) * HZ / 1024;
        Rng r(1000 + N);
        double sum = 0, sumA = 0;
        int ticks = 15 * 60;
        for (int t = 0; t < ticks; t++) {
            double pay = 0, payA = 0;
            for (int u = 0; u < N; u++) {
                bool mv = r.u() < 0.6, hp = r.u() < 0.1, ot = r.u() < 0.05, vis = r.u() < 0.35;
                double b = 0;
                if (mv || hp || ot) b = 2 + 1 + (mv ? 4 : 0) + (hp ? 2 : 0) + (ot ? 2 : 0);
                pay += b;
                if (vis) payA += b;
            }
            sum += pk(pay);
            sumA += pk(payA);
        }
        double delta = sum / ticks * HZ / 1024, aoi = sumA / ticks * HZ / 1024;
        printf("  %6d %9.2fK %9.2fK %9.2fK %9.2fK\n", N, fs, full, delta, aoi);
        rows.push_back(JO().num("n", N).num("fs", fs).num("full", full).num("delta", delta).num("aoi", aoi).s());
    }
    return JO().num("fs", fs).num("cmdPerFrame", cmdPerFrame).num("fsPkt", fsPkt).raw("rows", jarr(rows)).s();
}

// ================================================================== E2 固定逻辑步长 + 渲染插值
static std::string E2() {
    printf("\n== E2 逻辑 15 Hz、渲染约 60 fps（偶发 50 ms 长帧）：直接画逻辑状态 vs 插值 vs 外推 ==\n");
    const double T = 1000.0 / 15;
    auto truth = [](double t) { double s = t / 1000; return 300 * s + 40 * std::sin(3 * s); };   // 单位：厘米
    Rng r(2);
    double t = 0, acc = 0, prevX = truth(0), curX = truth(0), curT = 0;
    std::vector<std::pair<double, double>> sT, sRaw, sInt, sExt;
    std::vector<double> lagRaw, lagInt, lagExt, vRaw, vInt, vExt;
    double lastRaw = 0, lastInt = 0, lastExt = 0;
    int frames = 0;
    while (t < 6000) {
        double dt = 1000.0 / 60 + (r.u() < 0.04 ? 34 : 0) + r.gauss() * 1.2;
        t += dt;
        acc += dt;
        while (acc >= T) { prevX = curX; curT += T; curX = truth(curT); acc -= T; }
        double a = acc / T;
        double raw = curX, itp = prevX + (curX - prevX) * a, ext = curX + (curX - prevX) * a;
        double tx = truth(t), v = 300 + 120 * std::cos(3 * t / 1000);   // cm/s
        if (frames > 30) {
            lagRaw.push_back((tx - raw) / v * 1000); lagInt.push_back((tx - itp) / v * 1000); lagExt.push_back((tx - ext) / v * 1000);
            vRaw.push_back((raw - lastRaw) / dt * 1000 - v); vInt.push_back((itp - lastInt) / dt * 1000 - v); vExt.push_back((ext - lastExt) / dt * 1000 - v);
        }
        lastRaw = raw; lastInt = itp; lastExt = ext;
        if (t >= 1000 && t <= 1500) {
            sRaw.push_back({t, raw}); sInt.push_back({t, itp}); sExt.push_back({t, ext});
        }
        frames++;
    }
    for (double x = 1000; x <= 1500; x += 5) sT.push_back({x, truth(x)});
    auto sd = [](const std::vector<double>& v) { double m = mean(v), s = 0; for (double x : v) s += (x - m) * (x - m); return std::sqrt(s / v.size()); };
    printf("  平均滞后：直接 %.1f ms，插值 %.1f ms，外推 %.1f ms\n", mean(lagRaw), mean(lagInt), mean(lagExt));
    printf("  每帧速度抖动（标准差，cm/s）：直接 %.0f，插值 %.0f，外推 %.0f\n", sd(vRaw), sd(vInt), sd(vExt));
    return JO().num("lagRaw", mean(lagRaw)).num("lagInt", mean(lagInt)).num("lagExt", mean(lagExt))
        .num("jRaw", sd(vRaw)).num("jInt", sd(vInt)).num("jExt", sd(vExt))
        .raw("truth", pts(sT)).raw("raw", pts(sRaw)).raw("itp", pts(sInt)).raw("ext", pts(sExt)).s();
}

// ================================================================== E3 / E9 Lockstep 引擎：严格 / 定频广播不等人 / 回滚
struct Link { double base, jit, loss; };
struct Cfg {
    int P = 10; double T = 66; double dur = 60000; int d = 0; int W = 0; int R = 40; double q = 0.25; uint64_t seed = 1;
    std::vector<Link> L; int bad = -1; double o0 = 0, o1 = 0; int K = 3;
};
struct ClientRes { std::vector<double> exec; std::vector<std::pair<double, int>> rb; std::vector<double> lats; int resim = 0; int lost = 0; int reqs = 0; };

static bool hop(Rng& r, const Cfg& c, int p, double t, double& dly) {
    double u = r.u();
    dly = c.L[p].base + r.expo(c.L[p].jit);
    bool out = (p == c.bad && t >= c.o0 && t < c.o1);
    return !(out || u < c.L[p].loss);
}
static std::vector<std::vector<int>> gen_inputs(const Cfg& c, int n) {
    std::vector<std::vector<int>> v(c.P, std::vector<int>(n, 0));
    for (int p = 0; p < c.P; p++) {
        Rng r(c.seed * 131 + 17 + p);
        int cur = 0;
        for (int k = 0; k < n; k++) {
            if (k > 0 && r.u() < c.q) cur = (cur + 1 + (int)(r.next() % 7)) % 8;
            v[p][k] = cur;
        }
    }
    return v;
}
static double freeze_ms(const std::vector<double>& e, double T) {
    double s = 0;
    for (size_t i = 1; i < e.size(); i++) { double g = e[i] - e[i - 1]; if (g > 2 * T) s += g - T; }
    return s;
}

struct Pkt { double at; int to, from, lo; std::vector<int> v; };
struct PktCmp { bool operator()(const Pkt& a, const Pkt& b) const { return a.at > b.at; } };

// P2P：W = 0 即严格 Lockstep（必须收齐真实输入）；W > 0 即回滚（最多预测 W 帧）
static std::vector<ClientRes> run_p2p(const Cfg& c) {
    const int P = c.P, NF = (int)(c.dur / c.T), MAXF = NF + c.d + 8;
    auto inp = gen_inputs(c, MAXF + 8);
    struct St { int f = 0; double next = 0, nextSend = 0; std::vector<std::vector<int>> known, used; std::vector<int> conf; int mis = INT_MAX; std::vector<double> press; };
    std::vector<St> st(P);
    std::vector<ClientRes> res(P);
    Rng rnet(c.seed), rph(c.seed + 99);
    for (int i = 0; i < P; i++) {
        St& s = st[i];
        s.known.assign(P, std::vector<int>(MAXF, -1));
        s.used.assign(P, std::vector<int>(MAXF, -1));
        s.conf.assign(P, c.d - 1);
        for (int j = 0; j < P; j++) for (int n = 0; n < c.d; n++) s.known[j][n] = 0;
        s.press.assign(MAXF, -1);
        s.next = rph.u() * 5;           // 各端开局时刻相差几毫秒
        s.nextSend = s.next;
    }
    auto value = [&](St& s, int j, int n) { return s.known[j][n] != -1 ? s.known[j][n] : (s.conf[j] >= 0 ? s.known[j][s.conf[j]] : 0); };
    std::priority_queue<Pkt, std::vector<Pkt>, PktCmp> q;
    for (double t = 0; t <= c.dur + 3000; t += 1.0) {
        while (!q.empty() && q.top().at <= t) {
            Pkt p = q.top(); q.pop();
            St& s = st[p.to];
            int first = INT_MAX;
            for (size_t k = 0; k < p.v.size(); k++) {
                int n = p.lo + (int)k;
                if (n >= MAXF || p.v[k] < 0) continue;
                if (s.known[p.from][n] == -1) { s.known[p.from][n] = p.v[k]; first = std::min(first, n); }
            }
            while (s.conf[p.from] + 1 < MAXF && s.known[p.from][s.conf[p.from] + 1] != -1) s.conf[p.from]++;
            if (first != INT_MAX)
                for (int n = first; n < s.f; n++)
                    if (s.used[p.from][n] != value(s, p.from, n)) { s.mis = std::min(s.mis, n); break; }
        }
        for (int i = 0; i < P; i++) {
            St& s = st[i];
            if (s.mis < s.f) {                       // 回滚：回到分歧帧，用新输入重算到当前帧
                int depth = s.f - s.mis;
                res[i].rb.push_back({t, depth});
                res[i].resim += depth;
                for (int n = s.mis; n < s.f; n++) for (int j = 0; j < P; j++) s.used[j][n] = value(s, j, n);
                s.mis = INT_MAX;
            }
            int guard = 0;
            bool gen = false;
            while (t >= s.next && s.f < NF && guard++ < 64) {   // 逻辑时间锚定墙钟：卡住之后会快进追赶
                int n = s.f;
                int fn = n + c.d;                                 // 第一次尝试第 n 帧时采样本地输入，排到第 n+d 帧
                if (fn < MAXF && s.known[i][fn] == -1) { s.known[i][fn] = inp[i][n]; s.conf[i] = fn; s.press[fn] = t; gen = true; }
                bool ok = true;
                for (int j = 0; j < P && ok; j++) if (j != i && n - s.conf[j] > c.W) ok = false;
                if (!ok) break;
                for (int j = 0; j < P; j++) s.used[j][n] = value(s, j, n);
                res[i].exec.push_back(t);
                if (s.press[n] >= 0) res[i].lats.push_back(t - s.press[n]);
                s.f++;
                s.next += c.T;
            }
            if (gen || t >= s.nextSend) {                         // 有新输入立即发；卡住时每 T 毫秒重发一次
                s.nextSend = t + c.T;
                int hi = std::min(MAXF - 1, s.conf[i]), lo = std::max(0, hi - c.R + 1);
                std::vector<int> v;
                for (int n = lo; n <= hi; n++) v.push_back(s.known[i][n]);
                for (int j = 0; j < P; j++) {
                    if (j == i) continue;
                    double d1, d2;
                    bool a = hop(rnet, c, i, t, d1);
                    bool b = hop(rnet, c, j, t + d1, d2);
                    if (a && b) q.push({t + d1 + d2, j, i, lo, v});
                }
            }
        }
    }
    return res;
}

// 定频广播 + 不等人（HOK 模型）：服务器每 T 毫秒广播一帧，包内带最近 K 帧；客户端入窗即消费，空洞 / 超时请求补发
static std::vector<ClientRes> run_relay(const Cfg& c, std::vector<int>* lostOut = nullptr) {
    const int P = c.P, NF = (int)(c.dur / c.T);
    auto inp = gen_inputs(c, NF + 64);
    Rng r(c.seed + 7);
    std::vector<ClientRes> res(P);
    struct Cmd { int p; double press; int frame; };
    std::vector<Cmd> cmds;
    for (int p = 0; p < P; p++) {
        double phase = r.u() * c.T;
        int prev = inp[p][0];
        for (int m = 1; m < NF + 60; m++) {
            double s = phase + m * c.T;
            if (s > c.dur - 300) break;
            if (inp[p][m] == prev) continue;
            prev = inp[p][m];
            double best = 1e18;
            for (int k = 0; k < 3; k++) {                       // 短冗余：同一命令在 0 / 33 / 66 ms 各发一次
                double ts = s + 33.0 * k, dl;
                if (hop(r, c, p, ts, dl)) best = std::min(best, ts + dl);
            }
            if (best < 1e17) cmds.push_back({p, s, (int)std::ceil(best / c.T)});
            else res[p].lost++;
        }
    }
    for (int i = 0; i < P; i++) {
        std::vector<double> arr(NF + 2, 1e18);
        for (int k = 1; k <= NF; k++) {
            double t = k * c.T, dl;
            if (hop(r, c, i, t, dl)) for (int f = std::max(1, k - c.K + 1); f <= k; f++) arr[f] = std::min(arr[f], t + dl);
        }
        std::vector<std::pair<double, int>> lst;
        for (int f = 1; f <= NF; f++) if (arr[f] < 1e17) lst.push_back({arr[f], f});
        std::sort(lst.begin(), lst.end());
        struct Resp { double at; int lo, hi; };
        auto cmp = [](const Resp& a, const Resp& b) { return a.at > b.at; };
        std::priority_queue<Resp, std::vector<Resp>, decltype(cmp)> resp(cmp);
        std::vector<char> have(NF + 2, 0);
        std::vector<double> ex(NF + 2, -1);
        int beg = 1, maxf = 0;
        size_t ptr = 0;
        double lastProg = c.T + 80, lastReq = -1e9;
        for (double t = 0; t <= c.dur + 3000 && beg <= NF; t += 1.0) {
            while (ptr < lst.size() && lst[ptr].first <= t) { have[lst[ptr].second] = 1; maxf = std::max(maxf, lst[ptr].second); ptr++; }
            while (!resp.empty() && resp.top().at <= t) {
                Resp x = resp.top(); resp.pop();
                for (int f = x.lo; f <= x.hi && f <= NF; f++) { have[f] = 1; maxf = std::max(maxf, f); }
            }
            while (beg <= NF && have[beg]) { ex[beg] = t; beg++; lastProg = t; }
            if (((int)t) % 16 == 0 && beg <= NF && t > 2 * c.T) {
                bool hole = maxf > beg, tout = t - lastProg > 90;
                double iv = (t - lastProg) <= 1000 ? 120 : 1000;     // 两级频率：紧急 120 ms，持续卡顿后降到 1000 ms
                if ((hole || tout) && t - lastReq >= iv) {
                    lastReq = t;
                    res[i].reqs++;
                    double d1, d2;
                    if (hop(r, c, i, t, d1)) {
                        double ts = t + d1;
                        int cur = (int)std::floor(ts / c.T);
                        if (cur >= beg && hop(r, c, i, ts, d2)) resp.push({ts + d2, beg, std::min(cur, beg + 899)});
                    }
                }
            }
        }
        for (int f = 1; f <= NF; f++) if (ex[f] >= 0) res[i].exec.push_back(ex[f]);
        for (auto& cm : cmds) if (cm.p == i && cm.frame <= NF && ex[cm.frame] >= 0) res[i].lats.push_back(ex[cm.frame] - cm.press);
    }
    return res;
}

static Cfg moba_cfg() {
    Cfg c;
    c.P = 10; c.T = 66; c.dur = 60000; c.q = 0.25; c.seed = 3;
    for (int i = 0; i < 9; i++) c.L.push_back({18.0 + 3 * i, 6, 0.01});
    c.L.push_back({45, 25, 0.04});
    c.bad = 9; c.o0 = 20000; c.o1 = 22000;
    return c;
}

static std::string lane_json(const ClientRes& r, double T, double a, double b) {
    std::vector<std::string> ex, fz, rb;
    for (size_t k = 0; k < r.exec.size(); k++) {
        double t = r.exec[k];
        if (t >= a && t <= b) ex.push_back(jn(t));
        if (k > 0) { double g = t - r.exec[k - 1]; if (g > 2 * T && t >= a && r.exec[k - 1] <= b) fz.push_back("[" + jn(std::max(a, r.exec[k - 1] + T)) + "," + jn(std::min(b, t)) + "]"); }
    }
    for (auto& x : r.rb) if (x.first >= a && x.first <= b) rb.push_back("[" + jn(x.first) + "," + std::to_string(x.second) + "]");
    return JO().raw("ex", jarr(ex)).raw("fz", jarr(fz)).raw("rb", jarr(rb)).s();
}

static std::string E3() {
    printf("\n== E3 三条路线：10 人、15 帧/秒，玩家 9 网络差且在 20~22 s 完全断流 ==\n");
    Cfg base = moba_cfg();
    struct Route { const char* key; const char* name; int kind; int d; int W; };
    std::vector<Route> routes = {{"strict", "严格 Lockstep（P2P，输入延迟 2 帧）", 0, 2, 0},
                                 {"relay", "定频广播 + 不等人（HOK）", 1, 0, 0},
                                 {"rollback", "回滚（P2P，输入延迟 1 帧，最多预测 8 帧）", 0, 1, 8}};
    std::vector<std::string> rows, lanes;
    for (auto& rt : routes) {
        Cfg c = base;
        c.d = rt.d; c.W = rt.W;
        auto res = rt.kind == 0 ? run_p2p(c) : run_relay(c);
        std::vector<double> gf, gl, rbps, depth, rs;
        std::vector<std::string> per;
        int dmax = 0;
        for (int i = 0; i < c.P; i++) {
            double fz = freeze_ms(res[i].exec, c.T);
            per.push_back(jn(fz));
            if (i == c.bad) continue;
            gf.push_back(fz);
            for (double x : res[i].lats) gl.push_back(x);
            rbps.push_back(res[i].rb.size() / (c.dur / 1000));
            rs.push_back(res[i].resim / (c.dur / 1000));
            for (auto& x : res[i].rb) { depth.push_back(x.second); dmax = std::max(dmax, x.second); }
        }
        auto& B = res[c.bad];
        double bf = freeze_ms(B.exec, c.T);
        double bfIn = 0;
        for (size_t k = 1; k < B.exec.size(); k++) { double g = B.exec[k] - B.exec[k - 1]; if (g > 2 * c.T && B.exec[k] >= c.o0 && B.exec[k - 1] <= c.o1 + 3000) bfIn += g - c.T; }
        int reqs = 0;
        for (auto& x : res) reqs += x.reqs;
        printf("  [断流窗口内 %.0f ms / 窗口外 %.0f ms]\n", bfIn, bf - bfIn);
        printf("  %-40s 好玩家卡顿 %7.0f ms  差玩家卡顿 %7.0f ms  好玩家输入延迟 %5.0f/%5.0f ms(均值/p95)  差玩家 %5.0f ms  回滚 %.2f 次/s 均深 %.1f 最深 %d 重算 %.1f 帧/s  丢命令(差玩家) %d\n",
               rt.name, mean(gf), bf, mean(gl), pct(gl, 0.95), mean(B.lats), mean(rbps), mean(depth), dmax, mean(rs), B.lost);
        rows.push_back(JO().str("key", rt.key).str("name", rt.name).num("gfz", mean(gf)).num("gfzmax", *std::max_element(gf.begin(), gf.end())).num("bfz", bf)
            .num("glat", mean(gl)).num("glat95", pct(gl, 0.95)).num("blat", mean(B.lats)).num("rbps", mean(rbps)).num("depth", mean(depth))
            .num("dmax", dmax).num("resim", mean(rs)).num("blost", B.lost).num("bfzIn", bfIn).num("bfzOut", bf - bfIn).num("reqs", reqs).raw("per", jarr(per)).s());
        double a = 19000, b = 23500;
        lanes.push_back("\"" + std::string(rt.key) + "\":" + JO().raw("A", lane_json(res[0], c.T, a, b)).raw("B", lane_json(res[5], c.T, a, b)).raw("X", lane_json(B, c.T, a, b)).s());
    }
    std::string lj = "{";
    for (size_t i = 0; i < lanes.size(); i++) lj += (i ? "," : "") + lanes[i];
    lj += "}";
    return JO().num("T", base.T).num("o0", base.o0).num("o1", base.o1).raw("routes", jarr(rows)).raw("lanes", lj).s();
}

static std::string E9() {
    printf("\n== E9 格斗（2 人、60 帧/秒）：回滚 + 输入延迟 d 的权衡；对照纯延迟型（W=0） ==\n");
    std::vector<std::string> rows;
    printf("  %5s %3s | %8s %7s %6s %8s %9s | %10s\n", "RTT", "d", "回滚/s", "均深", "p95深", "重算帧/s", "卡顿ms/分", "延迟型卡顿");
    for (int rtt : {40, 80, 120, 160, 200}) {
        for (int d = 0; d <= 4; d++) {
            Cfg c;
            c.P = 2; c.T = 1000.0 / 60; c.dur = 60000; c.q = 0.2; c.R = 24; c.seed = 50 + rtt + d;
            c.L = {{rtt / 4.0, 1 + rtt * 0.04, 0.01}, {rtt / 4.0, 1 + rtt * 0.04, 0.01}};
            c.d = d; c.W = 8;
            auto res = run_p2p(c);
            std::vector<double> dep;
            double rb = 0, rs = 0, fz = 0;
            for (auto& r : res) { rb += r.rb.size(); rs += r.resim; fz += freeze_ms(r.exec, c.T); for (auto& x : r.rb) dep.push_back(x.second); }
            rb /= 2 * c.dur / 1000; rs /= 2 * c.dur / 1000; fz /= 2;
            Cfg c2 = c; c2.W = 0;
            auto r2 = run_p2p(c2);
            double fz2 = (freeze_ms(r2[0].exec, c.T) + freeze_ms(r2[1].exec, c.T)) / 2;
            printf("  %5d %3d | %8.2f %7.2f %6.0f %8.1f %9.0f | %10.0f\n", rtt, d, rb, mean(dep), pct(dep, 0.95), rs, fz, fz2);
            rows.push_back(JO().num("rtt", rtt).num("d", d).num("dms", d * c.T).num("rbps", rb).num("depth", mean(dep)).num("p95", pct(dep, 0.95))
                .num("resim", rs).num("fz", fz).num("fzDelay", fz2).s());
        }
    }
    return JO().raw("rows", jarr(rows)).s();
}

// ================================================================== E5 下行冗余：一包 k 帧能挡住多少丢包
static std::string E5() {
    printf("\n== E5 下行冗余：每个包携带最近 k 帧，帧仍然丢失（需要主动补发）的比例 ==\n");
    struct Model { const char* key; const char* name; double p; double burst; };
    std::vector<Model> ms = {{"b1", "随机丢包 1%", 0.01, 1}, {"b5", "随机丢包 5%", 0.05, 1}, {"b10", "随机丢包 10%", 0.10, 1}, {"g5", "突发丢包 5%（平均连丢 4 包）", 0.05, 4}};
    std::vector<std::string> rows;
    const int N = 400000;
    for (auto& m : ms) {
        Rng r(77);
        std::vector<char> lost(N);
        if (m.burst <= 1) for (int i = 0; i < N; i++) lost[i] = r.u() < m.p;
        else {
            double pbg = 1.0 / m.burst, pgb = m.p * pbg / (1 - m.p);
            bool bad = false;
            for (int i = 0; i < N; i++) { bad = bad ? (r.u() >= pbg) : (r.u() < pgb); lost[i] = bad; }
        }
        double realLoss = 0;
        for (int i = 0; i < N; i++) realLoss += lost[i];
        realLoss /= N;
        for (int k = 1; k <= 8; k++) {
            int hole = 0, cnt = 0;
            double delay = 0;
            int rec = 0;
            for (int n = 0; n + k < N; n++) {
                cnt++;
                int j = 0;
                while (j < k && lost[n + j]) j++;
                if (j == k) hole++;
                else { delay += j; rec++; }
            }
            double bytes = 28 + 8 + k * 24.0;
            rows.push_back(JO().str("m", m.key).str("name", m.name).num("k", k).num("hole", (double)hole / cnt).num("analytic", std::pow(realLoss, k))
                .num("delay", rec ? delay / rec * 66 : 0).num("bytes", bytes).num("loss", realLoss).s());
            if (k == 1 || k == 3 || k == 5 || k == 8)
                printf("  %-26s k=%d  需补发 %.2e（独立丢包公式 p^k = %.2e）  平均额外等待 %.1f ms  每包 %.0f B\n", m.name, k, (double)hole / cnt, std::pow(realLoss, k), rec ? delay / rec * 66 : 0, bytes);
        }
    }
    return JO().raw("rows", jarr(rows)).s();
}

// ================================================================== E7 上行：单发 / 短冗余 / 技能全量冗余 + 序号
static std::string E7() {
    printf("\n== E7 上行技能命令：单发 vs 短冗余（0/33/66 ms 三份） vs 全量冗余（每包带全部未确认技能，200 ms 重发，1 s 超时） ==\n");
    struct LM { const char* key; const char* name; double p; double burst; };
    std::vector<LM> lms = {{"b5", "随机丢包 5%", 0.05, 1}, {"g10", "突发丢包 10%（平均连丢 8 包≈264 ms）", 0.10, 8}};
    std::vector<std::string> rows;
    for (auto& lm : lms) {
        // 上行信道：每 33 ms 一个发送时隙（帧末统一 flush），时隙丢包按 GE 模型；同一时隙的包同生共死
        const double DUR = 600000, SLOT = 33;
        int NS = (int)(DUR / SLOT) + 200;
        Rng rc(9);
        std::vector<char> lost(NS);
        bool bad = false;
        double pbg = 1.0 / lm.burst, pgb = lm.p * pbg / (1 - lm.p);
        for (int i = 0; i < NS; i++) {
            if (lm.burst <= 1) lost[i] = rc.u() < lm.p;
            else { bad = bad ? (rc.u() >= pbg) : (rc.u() < pgb); lost[i] = bad; }
        }
        std::vector<double> lat(NS);
        for (int i = 0; i < NS; i++) lat[i] = 30 + rc.expo(20);
        Rng rp(10);
        std::vector<double> press;
        double t = 500;
        while (t < DUR - 3000) {
            press.push_back(t);
            if (rp.u() < 0.3) press.push_back(t + 80);      // 连招：80 ms 后接第二个技能
            t += 300 + rp.expo(900);
        }
        const char* nm[] = {"单发", "短冗余（3 份）", "技能全量冗余 + 序号"};
        for (int strat = 0; strat < 3; strat++) {
            int n = (int)press.size();
            std::vector<double> arrive(n, -1);
            std::vector<int> copies(n, 0);
            std::vector<int> firstSlot(n);
            for (int s = 0; s < n; s++) firstSlot[s] = (int)std::ceil(press[s] / SLOT);
            std::vector<int> queue;          // 全量冗余：未确认技能（按序号）
            int nextSkill = 0;
            double lastResend = -1e9;
            for (int sl = 0; sl < NS; sl++) {
                double now = sl * SLOT;
                std::vector<int> inPkt;
                bool newSkill = false;
                while (nextSkill < n && firstSlot[nextSkill] <= sl) { queue.push_back(nextSkill); nextSkill++; newSkill = true; }
                if (strat < 2) {
                    int reps = strat == 0 ? 1 : 3;
                    for (int s : queue) if (sl - firstSlot[s] < reps) inPkt.push_back(s);
                    queue.erase(std::remove_if(queue.begin(), queue.end(), [&](int s) { return sl - firstSlot[s] >= reps - 1; }), queue.end());
                } else {
                    // 确认：客户端在下行帧里看到自己的技能（到达服务器后约 60 ms）；超时：滞留超过 1 s 丢弃
                    queue.erase(std::remove_if(queue.begin(), queue.end(), [&](int s) { return (arrive[s] >= 0 && now >= arrive[s] + 60) || now - press[s] > 1000; }), queue.end());
                    if (!queue.empty() && (newSkill || now - lastResend >= 200)) { inPkt = queue; lastResend = now; }
                }
                for (int s : inPkt) {
                    copies[s]++;
                    if (!lost[sl]) { double a = now + lat[sl]; if (arrive[s] < 0 || a < arrive[s]) arrive[s] = a; }
                }
            }
            // 服务器生效顺序：单发 / 短冗余按到达顺序；全量冗余按序号（包内按序号排列，缺号时序号小的必然同包）
            std::vector<std::pair<double, int>> ord;
            int deliv = 0, inv = 0, cp = 0;
            std::vector<double> L;
            for (int s = 0; s < n; s++) {
                cp += copies[s];
                if (arrive[s] < 0) continue;
                deliv++; L.push_back(arrive[s] - press[s]);
                ord.push_back({arrive[s] + (strat == 2 ? s * 1e-6 : 0), s});
            }
            std::sort(ord.begin(), ord.end());
            int mx = -1;
            for (auto& x : ord) { if (x.second < mx) inv++; mx = std::max(mx, x.second); }
            printf("  [%s] %-18s 送达率 %.3f%%  平均延迟 %.0f ms  p99 %.0f ms  每技能平均发送 %.2f 份  顺序颠倒 %d 次（共 %d 个技能）\n",
                   lm.name, nm[strat], 100.0 * deliv / n, mean(L), pct(L, 0.99), (double)cp / n, inv, n);
            rows.push_back(JO().str("m", lm.key).str("mname", lm.name).num("s", strat).str("name", nm[strat]).num("deliv", 100.0 * deliv / n)
                .num("lat", mean(L)).num("p99", pct(L, 0.99)).num("copies", (double)cp / n).num("inv", inv).num("n", n).s());
        }
    }
    return JO().raw("rows", jarr(rows)).s();
}

// ================================================================== E8 ping 测量：主线程时刻 vs 网络线程到达时间戳
static std::string E8() {
    printf("\n== E8 ping：真实 RTT ≈ 50 ms；用主线程取包时刻计算 vs 用网络线程到达时间戳计算 ==\n");
    std::vector<std::string> rows;
    for (int fps : {30, 60, 120}) {
        for (int spike = 0; spike < 2; spike++) {
            Rng r(fps * 10 + spike);
            double t = 0;
            std::vector<double> frames;
            while (t < 120000) { double dt = 1000.0 / fps; if (spike && r.u() < 0.03) dt += 100; t += dt; frames.push_back(t); }
            std::vector<double> bm, bn, tr;
            for (size_t i = 0; i + 1 < frames.size(); i += (size_t)std::max(1, fps / 2)) {
                double send = frames[i], rtt = 50 + r.expo(5), arr = send + rtt;
                auto it = std::lower_bound(frames.begin(), frames.end(), arr);
                if (it == frames.end()) break;
                bm.push_back(*it - send); bn.push_back(arr - send); tr.push_back(rtt);
            }
            std::vector<double> err;
            for (size_t k = 0; k < bm.size(); k++) err.push_back(bm[k] - tr[k]);
            printf("  %3d fps%s：网络线程戳 %.1f ms；主线程算 %.1f ms（平均多算 %.1f ms，p95 多算 %.1f ms）\n", fps, spike ? " + 3% 长帧" : "         ", mean(bn), mean(bm), mean(err), pct(err, 0.95));
            rows.push_back(JO().num("fps", fps).num("spike", spike).num("net", mean(bn)).num("main", mean(bm)).num("bias", mean(err)).num("p95", pct(err, 0.95)).s());
        }
    }
    return JO().raw("rows", jarr(rows)).s();
}

// ================================================================== E10 快照插值：插值延迟 vs 抖动
static std::string E10() {
    printf("\n== E10 快照插值：服务器 20 Hz 快照，下行 40 ms + 指数抖动，丢包 2%%；客户端 60 fps 渲染 ==\n");
    auto X = [](double t) { double s = t / 1000; return 400 * s; };
    auto Y = [](double t) { double s = t / 1000; double ph = std::fmod(s, 1.4) / 1.4; double tri = ph < 0.5 ? ph * 4 - 1 : 3 - ph * 4; return 160 * tri + 40 * std::sin(5.0 * s); };
    std::vector<std::string> rows;
    std::string sample;
    for (double J : {2.0, 15.0, 40.0}) {
        Rng r((uint64_t)(J * 10) + 5);
        struct Snap { double ts, at, x, y; };
        std::vector<Snap> snaps;
        for (double ts = 0; ts < 60000; ts += 50) if (r.u() >= 0.02) snaps.push_back({ts, ts + 40 + r.expo(J), X(ts), Y(ts)});
        std::sort(snaps.begin(), snaps.end(), [](const Snap& a, const Snap& b) { return a.at < b.at; });
        for (double D : {0.0, 50.0, 100.0, 150.0, 200.0}) {
            std::vector<Snap> buf;
            size_t p = 0;
            int nExt = 0, nFrz = 0, n = 0;
            std::vector<double> err;
            std::vector<std::pair<double, double>> sy, ty;
            for (double t = 1000; t < 60000; t += 1000.0 / 60) {
                while (p < snaps.size() && snaps[p].at <= t) {
                    Snap s = snaps[p++];
                    auto it = std::lower_bound(buf.begin(), buf.end(), s.ts, [](const Snap& a, double v) { return a.ts < v; });
                    buf.insert(it, s);
                }
                double rt = t - 40 - D;     // 客户端用“估计的服务器时间 − 插值延迟”渲染
                double x, y;
                auto it = std::lower_bound(buf.begin(), buf.end(), rt, [](const Snap& a, double v) { return a.ts < v; });
                if (it != buf.end() && it != buf.begin()) {
                    const Snap& b = *it; const Snap& a = *(it - 1);
                    double k = (rt - a.ts) / (b.ts - a.ts);
                    x = a.x + (b.x - a.x) * k; y = a.y + (b.y - a.y) * k;
                } else if (buf.size() >= 2 && it == buf.end()) {
                    const Snap& b = buf.back(); const Snap& a = buf[buf.size() - 2];
                    double ahead = std::min(rt - b.ts, 250.0);
                    if (rt - b.ts > 250) nFrz++;
                    x = b.x + (b.x - a.x) / (b.ts - a.ts) * ahead; y = b.y + (b.y - a.y) / (b.ts - a.ts) * ahead;
                    nExt++;
                } else { n++; continue; }
                n++;
                err.push_back(std::hypot(x - X(rt), y - Y(rt)));
                if (J == 15.0 && t >= 5000 && t <= 6500) { sy.push_back({t, y}); }
            }
            printf("  抖动均值 %4.0f ms  插值延迟 %3.0f ms：外推帧 %5.1f%%  外推超 250 ms %4.1f%%  位置误差 均值 %5.1f / p99 %5.1f cm  画面落后 %3.0f ms\n",
                   J, D, 100.0 * nExt / n, 100.0 * nFrz / n, mean(err), pct(err, 0.99), 40 + J + D);
            rows.push_back(JO().num("J", J).num("D", D).num("ext", 100.0 * nExt / n).num("frz", 100.0 * nFrz / n).num("err", mean(err)).num("p99", pct(err, 0.99)).num("behind", 40 + J + D).s());
            if (J == 15.0 && (D == 0.0 || D == 100.0)) sample += (sample.empty() ? "" : ",") + ("\"D" + std::to_string((int)D) + "\":" + pts(sy));
        }
    }
    std::vector<std::pair<double, double>> ty;
    for (double t = 5000; t <= 6500; t += 10) ty.push_back({t, Y(t - 40)});
    sample += ",\"truth\":" + pts(ty);
    return JO().raw("rows", jarr(rows)).raw("sample", "{" + sample + "}").s();
}

// ================================================================== E11 客户端预测 + 服务器和解（CMC 的 SavedMoves 思想）
static std::string E11() {
    printf("\n== E11 预测与和解：60 Hz 输入、600 cm/s、RTT≈100 ms、上行丢包 5%%（每包带最近 3 个移动）；3.0~3.5 s 服务器判定被定身（客户端不知道） ==\n");
    const double DT = 1000.0 / 60, V = 600;
    const int N = 60 * 8;
    auto input = [](int k) { double t = k / 60.0; double ph = std::fmod(t, 3.2); return ph < 1.2 ? 1 : (ph < 1.6 ? 0 : (ph < 2.8 ? -1 : 0)); };
    auto rooted = [](double st) { return st >= 3000 && st < 3500; };
    Rng r(11);
    // 上行：移动 k 在 t=k*DT 发出，包里还带 k-1、k-2
    std::vector<double> upArr(N, 1e18);
    for (int k = 0; k < N; k++) {
        bool lost = r.u() < 0.05; double a = k * DT + 50 + r.expo(5);
        if (!lost) for (int j = k; j >= std::max(0, k - 2); j--) upArr[j] = std::min(upArr[j], a);
    }
    for (int k = N - 2; k >= 0; k--) if (upArr[k] > 1e17) upArr[k] = upArr[k + 1];   // 三份都丢：随下一个移动一起处理（CMC 用时间戳补足这段位移）
    // 服务器：按序号依次处理（等前一个到了才处理后一个）
    std::vector<double> sPos(N + 1, 0), sTime(N, 0);
    double pos = 0, last = 0;
    for (int k = 0; k < N; k++) {
        double tp = std::max(last, upArr[k]);
        last = tp; sTime[k] = tp;
        pos += rooted(tp) ? 0 : input(k) * V * DT / 1000;
        sPos[k + 1] = pos;
    }
    // 下行：服务器 30 Hz 发 (最后处理的移动序号, 位置)
    struct Ack { double at; int k; double pos; };
    std::vector<Ack> acks;
    for (double t = 0; t < N * DT + 500; t += 1000.0 / 30) {
        int k = -1;
        for (int j = 0; j < N; j++) if (sTime[j] <= t) k = j; else break;
        if (k < 0) continue;
        acks.push_back({t + 50 + r.expo(5), k, sPos[k + 1]});
    }
    std::sort(acks.begin(), acks.end(), [](const Ack& a, const Ack& b) { return a.at < b.at; });
    std::vector<std::string> rows;
    std::string series;
    const char* names[] = {"不预测（等服务器）", "预测 + 直接覆盖", "预测 + 和解（重放未确认移动）"};
    for (int sc = 0; sc < 3; sc++) {
        double cpos = 0, shown = 0;
        size_t ap = 0;
        int lastAck = -1, corr = 0;
        double corrSum = 0, corrMax = 0;
        std::vector<double> latency;
        std::vector<std::pair<double, double>> off;
        for (int k = 0; k < N; k++) {
            double t = k * DT;
            while (ap < acks.size() && acks[ap].at <= t) {
                const Ack& a = acks[ap++];
                if (a.k <= lastAck) continue;
                lastAck = a.k;
                double before = cpos;
                if (sc == 0) cpos = a.pos;
                else if (sc == 1) cpos = a.pos;
                else { cpos = a.pos; for (int j = a.k + 1; j < k; j++) cpos += input(j) * V * DT / 1000; }
                double c = std::fabs(cpos - before);
                if (sc > 0 && c > 1) { corr++; corrSum += c; corrMax = std::max(corrMax, c); }
            }
            if (sc > 0) cpos += input(k) * V * DT / 1000;
            shown = cpos;
            // “理想位置”：服务器对 0..k 的权威结果
            if (k % 2 == 0 && t <= 6000) off.push_back({t, shown - sPos[k + 1]});
        }
        // 输入到画面延迟：从第一次按右（k=0）到画面开始移动
        double lat = DT;
        if (sc == 0) { for (auto& a : acks) if (a.pos > 0.5) { lat = a.at; break; } }
        printf("  %-28s 输入→画面 %5.0f ms  纠正次数 %3d  平均纠正 %6.1f cm  最大纠正 %6.1f cm\n", names[sc], lat, corr, corr ? corrSum / corr : 0, corrMax);
        rows.push_back(JO().num("s", sc).str("name", names[sc]).num("lat", lat).num("corr", corr).num("avg", corr ? corrSum / corr : 0).num("max", corrMax).s());
        series += (series.empty() ? "" : ",") + ("\"s" + std::to_string(sc) + "\":" + pts(off));
    }
    return JO().raw("rows", jarr(rows)).raw("series", "{" + series + "}").s();
}

// ================================================================== E12 延迟补偿（服务器回溯）
static std::string E12() {
    printf("\n== E12 延迟补偿：目标 600 cm/s 横移随机折返，命中半径 34 cm，客户端插值延迟 100 ms ==\n");
    Rng r(12);
    // 目标轨迹：每 0.3~0.8 s 随机折返
    std::vector<std::pair<double, double>> seg;   // (切换时刻, 方向)
    double t = 0; int dir = 1;
    while (t < 400000) { seg.push_back({t, (double)dir}); t += 300 + r.u() * 500; dir = -dir; }
    auto X = [&](double tm) {
        double x = 0, prev = 0, d = 1;
        for (size_t i = 0; i < seg.size(); i++) {
            double t1 = seg[i].first;
            if (t1 > tm) break;
            x += d * 0.6 * (t1 - prev); prev = t1; d = seg[i].second;
        }
        return x + d * 0.6 * (tm - prev);
    };
    // 预计算 X 的查表（每 1 ms）
    std::vector<double> xs(400001);
    { double x = 0, d = 1; size_t si = 0; for (int ms = 0; ms <= 400000; ms++) { while (si < seg.size() && seg[si].first <= ms) { d = seg[si].second; si++; } xs[ms] = x; x += d * 0.6; } }
    auto XT = [&](double tm) { int a = std::clamp((int)tm, 0, 399999); double f = tm - a; return xs[a] + (xs[a + 1] - xs[a]) * f; };
    (void)X;
    std::vector<std::string> rows;
    for (int ping : {0, 50, 100, 150, 200, 300}) {
        int hitA = 0, hitB = 0, hitC = 0, n = 0;
        for (int s = 0; s < 4000; s++) {
            double tc = 2000 + s * 90;                                   // 客户端开火时刻
            double down = ping / 2.0 + r.expo(4), up = ping / 2.0 + r.expo(4);   // 本次快照下行 / 命令上行的真实单程
            double view = tc - down - 100;                                // 客户端此刻画的是服务器 (下行 + 插值延迟) 之前的状态
            double aim = XT(view) + r.gauss() * 6;                        // 瞄得很准（6 cm 手抖）
            double ts = tc + up;                                          // 服务器收到时刻
            bool a = std::fabs(XT(ts) - aim) <= 34;                       // 不补偿：用服务器当前位置判
            double est = ping + 8;                                        // 服务器估计的 RTT（含平均抖动）
            double rw = ts - (est + 100);                                 // 服务器回溯：倒回 RTT + 客户端插值延迟
            bool b = std::fabs(XT(rw) - aim) <= 34;
            double rw2 = ts - std::min(est + 100, 250.0);                 // 回溯上限 250 ms（防止高 ping 玩家“隔墙杀”）
            bool c = std::fabs(XT(rw2) - aim) <= 34;
            hitA += a; hitB += b; hitC += c; n++;
        }
        printf("  ping %3d ms：不补偿命中率 %5.1f%%   回溯补偿 %5.1f%%   回溯上限 250 ms %5.1f%%\n", ping, 100.0 * hitA / n, 100.0 * hitB / n, 100.0 * hitC / n);
        rows.push_back(JO().num("ping", ping).num("none", 100.0 * hitA / n).num("rewind", 100.0 * hitB / n).num("cap", 100.0 * hitC / n).s());
    }
    return JO().raw("rows", jarr(rows)).s();
}

// ================================================================== E13 UE 风格的优先级 + 带宽预算调度
static std::string E13() {
    printf("\n== E13 带宽饱和时的调度：UE 式“优先级 × 距上次发送时间” vs 静态优先级 vs 轮询（30 Hz，每 tick 预算 1200 B） ==\n");
    struct Cls { const char* name; int n; double prio, freq; int size; };
    std::vector<Cls> cls = {{"Pawn 英雄", 10, 3.0, 100, 60}, {"投射物", 20, 2.0, 30, 30}, {"可拾取物 / 机关", 100, 1.0, 10, 20}, {"PlayerState", 10, 1.0, 1, 40}};
    std::vector<std::string> rows;
    const char* sn[] = {"UE：优先级 × 等待时间", "静态优先级", "轮询"};
    for (int strat = 0; strat < 3; strat++) {
        struct A { int c; double nextDue = 0, last = -1, maxGap = 0, sumGap = 0; int nGap = 0; double bytes = 0; };
        std::vector<A> as;
        for (int c = 0; c < (int)cls.size(); c++) for (int k = 0; k < cls[c].n; k++) { A a; a.c = c; as.push_back(a); }
        Rng r(13);
        for (auto& a : as) a.nextDue = r.u() / cls[a.c].freq;
        size_t rr = 0;
        const double DT = 1.0 / 30;
        for (int tick = 0; tick < 30 * 60; tick++) {
            double t = tick * DT;
            std::vector<std::pair<double, int>> cand;
            for (int i = 0; i < (int)as.size(); i++) {
                if (as[i].nextDue > t) continue;
                double sc;
                if (strat == 0) sc = cls[as[i].c].prio * (as[i].last < 0 ? 10.0 : (t - as[i].last));
                else if (strat == 1) sc = cls[as[i].c].prio + 1e-6 * i;
                else sc = -(double)((i - (int)rr + (int)as.size()) % (int)as.size());
                cand.push_back({sc, i});
            }
            std::sort(cand.begin(), cand.end(), [](auto& a, auto& b) { return a.first > b.first; });
            int budget = 1200;
            for (auto& cd : cand) {
                A& a = as[cd.second];
                int sz = cls[a.c].size;
                if (budget < sz) continue;          // 饱和：本 tick 没发出去，下个 tick 继续参与竞争
                budget -= sz;
                if (a.last >= 0) { double g = t - a.last; a.maxGap = std::max(a.maxGap, g); a.sumGap += g; a.nGap++; }
                a.last = t; a.bytes += sz;
                a.nextDue = t + 1.0 / cls[a.c].freq;
            }
            rr = (rr + 37) % as.size();
        }
        double total = 0;
        for (auto& a : as) total += a.bytes;
        for (int c = 0; c < (int)cls.size(); c++) {
            double sg = 0, mg = 0, by = 0; int ng = 0, never = 0;
            for (auto& a : as) if (a.c == c) { sg += a.sumGap; ng += a.nGap; mg = std::max(mg, a.last < 0 ? 60.0 : a.maxGap); by += a.bytes; if (a.last < 0) never++; }
            double avg = ng ? sg / ng * 1000 : 60000;
            printf("  [%-20s] %-16s 期望间隔 %6.0f ms  实际平均 %7.0f ms  最长 %7.0f ms  带宽占比 %5.1f%%  从未发出 %d 个\n", sn[strat], cls[c].name, 1000 / cls[c].freq < 33.3 ? 33.3 : 1000 / cls[c].freq, avg, mg * 1000, 100 * by / total, never);
            rows.push_back(JO().num("s", strat).str("sname", sn[strat]).num("c", c).str("cname", cls[c].name).num("want", std::max(33.3, 1000 / cls[c].freq)).num("avg", avg).num("max", mg * 1000).num("share", 100 * by / total).num("never", never).s());
        }
    }
    return JO().raw("rows", jarr(rows)).s();
}

// ================================================================== E14 属性复制（状态）vs RPC（事件）
static std::string E14() {
    printf("\n== E14 0/10/20/30 ms 连续 4 次受击（100→75→50→25→10），300 ms 再受击（→0）；NetUpdateFrequency=10，下行丢包 20%%，单程 40 ms ==\n");
    const int TR = 20000;
    std::vector<double> hitT = {0, 10, 20, 30, 300};
    std::vector<int> hp = {75, 50, 25, 10, 0};
    Rng r(14);
    double onrep = 0, finalT = 0, unrel = 0, relLastLat = 0, relAllLat = 0;
    std::map<int, int> repDist;
    for (int tr = 0; tr < TR; tr++) {
        double phase = r.u() * 100;
        // ---- 属性：每 100 ms 一次网络更新，发送“当前值”；丢包后约 1 个 RTT 发现，下次更新补发最新值
        auto valueAt = [&](double t) { int v = 100; for (int i = 0; i < 5; i++) if (t >= hitT[i]) v = hp[i]; return v; };
        int client = 100, reps = 0; double done = -1;
        int lastSentOK = 100; bool pendingResend = false; double nakAt = 1e18;
        std::vector<std::pair<double, int>> arrivals;
        for (double u = phase; u < 1500; u += 100) {
            int cur = valueAt(u);
            bool need = (cur != lastSentOK) || (pendingResend && u >= nakAt);
            if (!need) continue;
            bool lost = r.u() < 0.2;
            double at = u + 40 + r.expo(5);
            if (!lost) { arrivals.push_back({at, cur}); lastSentOK = cur; pendingResend = false; }
            else { pendingResend = true; nakAt = u + 90; lastSentOK = -999; }
        }
        std::sort(arrivals.begin(), arrivals.end());
        for (auto& a : arrivals) if (a.second != client) { client = a.second; reps++; if (client == 0) done = a.first; }
        onrep += reps; repDist[reps]++; finalT += done;
        // ---- 不可靠 RPC：每次受击各发一次
        for (int i = 0; i < 5; i++) if (r.u() >= 0.2) unrel += 1;
        // ---- 可靠 RPC：丢了 100 ms 后重传；按序交付（前一个没到，后一个即使到了也要等）
        double prevDeliver = 0, lastLat = 0;
        for (int i = 0; i < 5; i++) {
            double s = hitT[i];
            while (r.u() < 0.2) s += 100;
            double at = std::max(s + 40 + r.expo(5), prevDeliver);
            prevDeliver = at;
            relAllLat += at - hitT[i];
            lastLat = at - hitT[i];
        }
        relLastLat += lastLat;
    }
    printf("  属性：平均 OnRep %.2f 次（5 次变化里大多数中间值被合并），最终值到达平均 %.0f ms\n", onrep / TR, finalT / TR);
    printf("  不可靠多播 RPC：平均收到 %.2f / 5 次受击事件\n", unrel / TR);
    printf("  可靠多播 RPC：5 次都收到，平均延迟 %.0f ms（按序交付带来队头阻塞）\n", relAllLat / TR / 5);
    std::vector<std::string> dist;
    for (auto& kv : repDist) dist.push_back("[" + std::to_string(kv.first) + "," + jn(100.0 * kv.second / TR) + "]");
    return JO().num("onrep", onrep / TR).num("finalT", finalT / TR).num("unrel", unrel / TR).num("relLat", relAllLat / TR / 5).num("relLast", relLastLat / TR).raw("repDist", jarr(dist)).s();
}

// ================================================================== main
int main(int argc, char** argv) {
    setvbuf(stdout, nullptr, _IONBF, 0);
    std::map<std::string, std::function<std::string()>> ex = {
        {"E1", E1}, {"E2", E2}, {"E3", E3}, {"E5", E5}, {"E7", E7}, {"E8", E8}, {"E9", E9}, {"E10", E10}, {"E11", E11}, {"E12", E12}, {"E13", E13}, {"E14", E14}};
    std::vector<std::string> want;
    for (int i = 1; i < argc; i++) want.push_back(argv[i]);
    std::string out = "{";
    bool first = true;
    for (auto& name : std::vector<std::string>{"E1", "E2", "E3", "E5", "E7", "E8", "E9", "E10", "E11", "E12", "E13", "E14"}) {
        if (!want.empty() && std::find(want.begin(), want.end(), name) == want.end()) continue;
        std::string j = ex[name]();
        out += (first ? "" : ",") + ("\"" + name + "\":" + j);
        first = false;
    }
    out += "}";
    const char* fn = want.empty() ? "figdata.json" : "figdata_part.json";
    FILE* f = fopen(fn, "wb");
    fwrite(out.data(), 1, out.size(), f);
    fclose(f);
    printf("\n写入 %s（%zu 字节）\n", fn, out.size());
    return 0;
}
