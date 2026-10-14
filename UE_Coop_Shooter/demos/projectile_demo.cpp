// projectile_demo.cpp —— 第 5B 章“不用射线、硬用实体子弹”配套实验 E8~E10
//
// 与 coop_net_demo.cpp 同一套网络模型（服务器网络 Tick 30 Hz、单程延迟 = RTT/2 + 0~20 ms 均匀抖动、
// 模拟代理 = 最新快照 + 速度 × 收到后经过的时间、目标横移 480 cm/s 随机折返、胶囊半径 30 cm），
// 专门比较“实体子弹”的几种联机做法：
//   E8 视觉时序：本机看到自己子弹的延迟、不隐藏真弹时的“双子弹”间距、交接跳变、
//                远端首次看到子弹的位置与滞后、被瞄准的人能提前看到子弹多久（PvP 公平性）
//   E9 命中一致性：当前做法（服务器复制子弹，客户端副本也碰撞）/ 本机假弹 + 服务器快进 /
//                  假弹 + 快进 + 目标回溯 / 假弹 + 客户端判定 + 服务器容差，
//                  “本机看到的命中”和“服务器判定”的一致率
//   E10 带宽估算：复制移动 / 只发首包 / 首包且不发给拥有者 / 数据化弹道（与 E7 同一字节口径）
//
// 编译：cl /nologo /std:c++latest /EHsc /O2 /utf-8 projectile_demo.cpp
// 运行：projectile_demo.exe            全部实验，写 figdata_proj.json
//       projectile_demo.exe E9         只跑指定实验（不写 json）
#include <algorithm>
#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <functional>
#include <numeric>
#include <random>
#include <string>
#include <vector>

static std::string gJson;
static void J(const char* fmt, ...) {
    va_list ap; va_start(ap, fmt);
    va_list ap2; va_copy(ap2, ap);
    int n = vsnprintf(nullptr, 0, fmt, ap); va_end(ap);
    std::string buf(n + 1, '\0'); vsnprintf(buf.data(), n + 1, fmt, ap2); va_end(ap2);
    buf.resize(n); gJson += buf;
}
static double mean(const std::vector<double>& v) { double s = 0; for (double x : v) s += x; return v.empty() ? 0 : s / v.size(); }

struct Link {
    double rtt, jitter; std::mt19937 rng;
    Link(double rtt_ms, double jit_ms, unsigned seed) : rtt(rtt_ms / 1000), jitter(jit_ms / 1000), rng(seed) {}
    double u01() { return std::uniform_real_distribution<double>(0, 1)(rng); }
    double oneway() { return rtt / 2 + jitter * u01(); }
};
static const double NET_DT = 1.0 / 30.0;
static double nextTick(double t) { return std::ceil(t / NET_DT - 1e-9) * NET_DT; }
// 服务器 / 客户端对单程延迟的估计：测得的平均 ping 的一半（= RTT/2 + 平均抖动），不可能知道每个包的实际抖动
static double oneWayEst(double rtt_ms) { return (rtt_ms / 2 + (rtt_ms > 0 ? 10 : 0)) / 1000.0; }

// ------------------------------------------------------------------ 目标与客户端视图（与 E3 相同的模型）
static const double TX = 2000;     // 目标所在距离（cm）：20 m
static const double TR = 30;       // 胶囊半径（cm）
static const double TV = 480;      // 横移速度（cm/s）：原 Demo 冲刺僵尸
static const double TT = 62;       // 模拟总时长（s）

struct World {
    std::vector<double> flipT, flipP;   // 第 k 次折返的时刻与位置
    struct Snap { double ts, ta, p, vel; };
    std::vector<Snap> snaps;            // 服务器每个网络 Tick 发出的目标快照（ta = 到达客户端的时刻）
    std::vector<int> best;              // 1 ms 网格：此刻客户端已收到的、服务器时间最新的快照
    World(unsigned seed, double rtt_ms) {
        std::mt19937 rng(seed);
        auto U = [&](double a, double b) { return std::uniform_real_distribution<double>(a, b)(rng); };
        double t = 0, p = 0, dir = 1; flipT.push_back(0); flipP.push_back(0);
        while (t < TT + 3) { double nt = t + U(0.25, 0.9); p += dir * TV * (nt - t); dir = -dir; t = nt; flipT.push_back(t); flipP.push_back(p); }
        Link down(rtt_ms, rtt_ms > 0 ? 20 : 0, seed + 99);
        for (double ts = 0; ts < TT + 1; ts += NET_DT) snaps.push_back({ts, ts + down.oneway(), P(ts), V(ts)});
        std::vector<int> ord(snaps.size()); std::iota(ord.begin(), ord.end(), 0);
        std::sort(ord.begin(), ord.end(), [&](int a, int b) { return snaps[a].ta < snaps[b].ta; });
        int n = (int)(TT * 1000) + 10; best.assign(n, -1);
        size_t k = 0; int cur = -1;
        for (int i = 0; i < n; ++i) {
            double tg = i / 1000.0;
            while (k < ord.size() && snaps[ord[k]].ta <= tg) { if (cur < 0 || snaps[ord[k]].ts > snaps[cur].ts) cur = ord[k]; ++k; }
            best[i] = cur;
        }
    }
    double P(double t) const {          // 服务器上的真实位置
        size_t k = std::upper_bound(flipT.begin(), flipT.end(), t) - flipT.begin() - 1;
        double dir = (k % 2 == 0) ? 1 : -1;
        return flipP[k] + dir * TV * (t - flipT[k]);
    }
    double V(double t) const { size_t k = std::upper_bound(flipT.begin(), flipT.end(), t) - flipT.begin() - 1; return (k % 2 == 0) ? TV : -TV; }
    const Snap* snapAt(double t) const { int i = (int)std::floor(t * 1000); if (i < 0 || i >= (int)best.size() || best[i] < 0) return nullptr; return &snaps[best[i]]; }
    double view(double t) const { const Snap* s = snapAt(t); return s ? s->p + s->vel * (t - s->ta) : 0; }     // 客户端看到的位置
    double viewT(double t) const { const Snap* s = snapAt(t); return s ? s->ts + (t - s->ta) : t; }            // 客户端“看到的是服务器哪个时刻”
    double hist(double t) const {       // 服务器每个网络 Tick 记一帧历史，回溯时线性插值
        double k = std::floor(t / NET_DT); double a = P(k * NET_DT), b = P((k + 1) * NET_DT);
        return a + (b - a) * (t / NET_DT - k);
    }
};

// 子弹：从原点沿 dir 匀速直线飞（俯视 2D，x 朝目标，y 横向）；elapsed = 从“开火”算起的飞行时间
// 只在目标所在的 x 附近扫掠（目标是 x = TX 处的竖直胶囊）；targetY(e) 给出“这一段检查时目标在哪”
struct Shot { double dx, dy, vp; };
static bool sweep(const Shot& s, double eFrom, double eTo, const std::function<double(double)>& targetY, double* eHit = nullptr) {
    double ex = s.dx * s.vp;
    double lo = std::max(eFrom, (TX - TR - 60) / ex), hi = std::min(eTo, (TX + TR + 60) / ex);
    const double h = 0.0005;
    for (double e = lo; e < hi; e += h) {
        double e2 = std::min(e + h, hi);
        double ty = targetY(0.5 * (e + e2));
        double ax = ex * e, ay = s.dy * s.vp * e, bx = ex * e2, by = s.dy * s.vp * e2;
        double vx = bx - ax, vy = by - ay, wx = TX - ax, wy = ty - ay;
        double L = vx * vx + vy * vy, u = L > 0 ? std::clamp((wx * vx + wy * vy) / L, 0.0, 1.0) : 0;
        double qx = ax + u * vx - TX, qy = ay + u * vy - ty;
        if (qx * qx + qy * qy <= TR * TR) { if (eHit) *eHit = e + u * (e2 - e); return true; }
    }
    return false;
}
static Shot aim(double vp, double Pc, double Vc) {   // 按本机看到的位置和速度提前量瞄准
    double e = TX / vp;
    for (int i = 0; i < 8; ++i) { double y = Pc + Vc * e; e = std::sqrt(TX * TX + y * y) / vp; }
    double y = Pc + Vc * e, L = std::sqrt(TX * TX + y * y);
    return {TX / L, y / L, vp};
}

// ------------------------------------------------------------------ E9：命中一致性
static void E9() {
    printf("\n=== E9 实体子弹的命中一致性（客户端玩家开火；目标 20 m、横移 480 cm/s 随机折返；按本机看到的位置和速度提前量瞄准）===\n");
    printf("S0 当前做法：服务器收到请求才生成复制子弹（不快进）；客户端副本从复制到达时刻起飞，也和本机看到的目标碰撞\n");
    printf("S2 本机假弹 + 服务器快进“测得的单程延迟”（RTT/2 + 平均抖动）；S3 再加目标回溯（按射手看到的时刻，上限 200 ms）；S4 假弹命中后上报，服务器容差 = 半径 + 200 cm\n");
    printf("%7s %5s | %7s | %7s %7s %6s | %7s %6s %6s | %7s %6s | %7s %6s\n", "弹速", "RTT", "本机%", "S0副本%", "S0服务%", "S0不一", "S2服务%", "S2不一", "S2幽灵", "S3服务%", "S3不一", "S4服务%", "S4不一");
    J("\"E9\":[");
    bool first = true;
    const double MAXREWIND = 0.200, LEEWAY = 200;
    for (double vp : {20000.0, 4000.0})
        for (double rtt : {0.0, 50.0, 100.0, 150.0, 200.0}) {
            World w(777, rtt);
            Link up(rtt, rtt > 0 ? 20 : 0, 199), down(rtt, rtt > 0 ? 20 : 0, 299);
            int n = 0, hL = 0, h0c = 0, h0s = 0, m0 = 0, h2 = 0, m2 = 0, g2 = 0, h3 = 0, m3 = 0, h4 = 0, m4 = 0, g0 = 0;
            for (double t0 = 1.0; t0 < TT - 3; t0 += 0.05) {
                if (!w.snapAt(t0)) continue;
                double Pc = w.view(t0), vT0 = w.viewT(t0);
                const auto* sn = w.snapAt(t0);
                Shot s = aim(vp, Pc, sn->vel);
                double flight = 2.0 * TX / vp;
                // 本机假弹：从 t0 起飞，和本机看到的目标碰撞
                double eL = 0;
                bool L = sweep(s, 0, flight, [&](double e) { return w.view(t0 + e); }, &eL);
                // 当前做法
                double ts = t0 + up.oneway();
                double tick = nextTick(ts), tarr = tick + down.oneway(), e0 = tick - ts;
                bool S0s = sweep(s, 0, flight, [&](double e) { return w.P(ts + e); });
                bool S0c = sweep(s, e0, flight, [&](double e) { return w.view(tarr + (e - e0)); });
                // 本机假弹 + 服务器快进 c = 测得的平均单程（RTT/2 + 平均抖动 10 ms）
                double c = oneWayEst(rtt);
                auto srvTime = [&](double e) { return e <= c ? ts : ts + (e - c); };
                bool S2 = sweep(s, 0, flight, [&](double e) { return w.P(srvTime(e)); });
                bool S3 = sweep(s, 0, flight, [&](double e) { return w.hist(std::max(vT0 + e, srvTime(e) - MAXREWIND)); });
                bool S4 = false;
                if (L) { double th = t0 + eL; double arrive = th + up.oneway(); S4 = std::fabs(w.view(th) - w.P(arrive)) <= TR + LEEWAY; }
                n++; hL += L; h0c += S0c; h0s += S0s; m0 += (S0c != S0s); g0 += (S0c && !S0s);
                h2 += S2; m2 += (L != S2); g2 += (L && !S2); h3 += S3; m3 += (L != S3); h4 += S4; m4 += (L != S4);
            }
            auto P = [&](int k) { return 100.0 * k / n; };
            printf("%7.0f %5.0f | %6.1f%% | %6.1f%% %6.1f%% %5.1f%% | %6.1f%% %5.1f%% %5.1f%% | %6.1f%% %5.1f%% | %6.1f%% %5.1f%%\n",
                   vp / 100, rtt, P(hL), P(h0c), P(h0s), P(m0), P(h2), P(m2), P(g2), P(h3), P(m3), P(h4), P(m4));
            J("%s{\"vp\":%.0f,\"rtt\":%.0f,\"n\":%d,\"local\":%.1f,\"c0\":%.1f,\"s0\":%.1f,\"m0\":%.1f,\"g0\":%.1f,\"s2\":%.1f,\"m2\":%.1f,\"g2\":%.1f,\"s3\":%.1f,\"m3\":%.1f,\"s4\":%.1f,\"m4\":%.1f}",
              first ? "" : ",", vp / 100, rtt, n, P(hL), P(h0c), P(h0s), P(m0), P(g0), P(h2), P(m2), P(g2), P(h3), P(m3), P(h4), P(m4));
            first = false;
        }
    J("],\n");
}

// ------------------------------------------------------------------ E8：视觉时序
static void E8() {
    printf("\n=== E8 实体子弹的视觉时序（每格 1000 发平均；“远端” = 另一台客户端，与射手同样的 RTT；被瞄准者距射手 20 m）===\n");
    printf("%7s %5s | %8s %8s %8s %8s | %8s %8s %8s %8s | %8s %8s %8s\n", "弹速", "RTT", "本机延迟", "双弹距0", "双弹距2", "交接跳cm",
           "远端出现0", "远端滞后", "前推出现", "前推误差", "反应0ms", "反应快进", "反应限50");
    J("\"E8\":[");
    bool first = true;
    for (double vp : {20000.0, 4000.0})
        for (double rtt : {0.0, 50.0, 100.0, 150.0, 200.0}) {
            Link up(rtt, rtt > 0 ? 20 : 0, 11), down(rtt, rtt > 0 ? 20 : 0, 22), obs(rtt, rtt > 0 ? 20 : 0, 33);
            std::mt19937 rng(44); std::uniform_real_distribution<double> U(0, 1);
            std::vector<double> own, gap0, gap2, jump, app0, lag, app2f, err2f, re0, re2, re2c;
            double c = oneWayEst(rtt), f = oneWayEst(rtt), cap = std::min(c, 0.050), flightT = TX / vp;
            for (int k = 0; k < 1000; ++k) {
                double t0 = 1 + U(rng);                       // 开火时刻在网络 Tick 之间均匀分布
                double ts = t0 + up.oneway(), tick = nextTick(ts), e0 = tick - ts;
                double tarr = tick + down.oneway();           // 子弹 Actor 首包到达射手
                double tobs = tick + obs.oneway();            // 首包到达远端
                own.push_back((tarr - t0) * 1000);
                gap0.push_back(vp * ((tarr - t0) - e0) / 100);           // 有假弹但真弹没隐藏、服务器不快进：两颗子弹相距
                gap2.push_back(vp * ((tarr - t0) - (e0 + c)) / 100);     // 服务器快进 RTT/2 之后仍相距
                jump.push_back(vp * std::fabs((tarr - t0) - (e0 + c + f)));    // 射手把到达的真弹再按单程估计前推后与假弹的差（cm）
                app0.push_back(vp * e0 / 100);                            // 远端首次看到子弹时它离枪口多远（不快进）
                lag.push_back(vp * (tobs - tick) / 100);                  // 远端显示的子弹落后服务器多少
                app2f.push_back(vp * (e0 + c + f) / 100);                 // 服务器快进 + 远端也按单程前推：出现位置
                err2f.push_back(vp * std::fabs((tobs - tick) - f));       // 前推后与服务器位置的误差（cm）
                re0.push_back((flightT - e0) * 1000);                     // 被瞄准者从看到子弹到“被打中”的时间（ms）
                re2.push_back((flightT - e0 - c) * 1000);
                re2c.push_back((flightT - e0 - cap) * 1000);
            }
            printf("%7.0f %5.0f | %8.1f %8.2f %8.2f %8.1f | %8.2f %8.2f %8.2f %8.1f | %8.0f %8.0f %8.0f\n", vp / 100, rtt,
                   mean(own), mean(gap0), mean(gap2), mean(jump), mean(app0), mean(lag), mean(app2f), mean(err2f), mean(re0), mean(re2), mean(re2c));
            J("%s{\"vp\":%.0f,\"rtt\":%.0f,\"own\":%.1f,\"gap0\":%.2f,\"gap2\":%.2f,\"jump\":%.1f,\"app0\":%.2f,\"lag\":%.2f,\"app2f\":%.2f,\"err2f\":%.1f,\"re0\":%.0f,\"re2\":%.0f,\"re2c\":%.0f}",
              first ? "" : ",", vp / 100, rtt, mean(own), mean(gap0), mean(gap2), mean(jump), mean(app0), mean(lag), mean(app2f), mean(err2f), mean(re0), mean(re2), mean(re2c));
            first = false;
        }
    J("],\n");
}

// ------------------------------------------------------------------ E10：带宽估算
static void E10() {
    printf("\n=== E10 实体子弹每秒网络消息量估算（2 名玩家同时开火；Listen Server：主机 + 1 个远端客户端；字节口径与 E7 相同）===\n");
    printf("RPC 头 ~8B、带参数的开火请求 ~16B、Actor 首包 ~60B、移动更新 ~20B（30 Hz）、关闭 ~4B、命中 / 爆炸状态 ~16B、开火计数 ~12B\n");
    struct Wp { const char* name; double rate, life; };
    J("\"E10\":[");
    bool first = true;
    for (Wp wp : {Wp{"步枪（8.3 发/秒，子弹存活 0.25 s）", 1 / 0.12, 0.25}, Wp{"榴弹 / 火箭（1 发/秒，存活 1 s）", 1.0, 1.0}}) {
        double r = wp.rate;
        double b0 = r * 8 + r * 2 * 8 + r * 2 * (60 + 4) + r * 2 * wp.life * 30 * 20;  // 当前做法：逐发 RPC + 开火组播 + 复制 Actor（含移动更新）
        double b1 = r * 16 + r * 2 * (60 + 16 + 4);                                    // 只发首包 + 爆炸状态（不复制移动）
        double b2 = r * 16 + r * 1 * (60 + 16 + 4);                                    // 再加：真弹不发给拥有者（拥有者只看假弹）
        double b3 = r * 16 + std::min(r, 30.0) * 2 * 12;                               // 数据化弹道：只同步开火计数 / 种子（与 E7 推荐方案同口径）
        printf("%-34s | 当前 %6.0f | 只发首包 %6.0f | 且不发拥有者 %6.0f | 数据化 %6.0f B/s\n", wp.name, b0, b1, b2, b3);
        J("%s{\"w\":\"%s\",\"b0\":%.0f,\"b1\":%.0f,\"b2\":%.0f,\"b3\":%.0f}", first ? "" : ",", first ? "rifle" : "rocket", b0, b1, b2, b3);
        first = false;
    }
    J("]\n");
}

int main(int argc, char** argv) {
    setvbuf(stdout, nullptr, _IONBF, 0);
    std::vector<std::string> want; for (int i = 1; i < argc; ++i) want.push_back(argv[i]);
    auto on = [&](const char* e) { return want.empty() || std::find(want.begin(), want.end(), e) != want.end(); };
    printf("projectile_demo —— 实体子弹联机做法对比（MSVC %d）\n", _MSC_VER);
    gJson = "{\n";
    if (on("E8")) E8();
    if (on("E9")) E9();
    if (on("E10")) E10(); else gJson += "\"_\":0\n";
    gJson += "}\n";
    if (want.empty()) {
        FILE* fp = fopen("figdata_proj.json", "wb");
        if (fp) { fwrite(gJson.data(), 1, gJson.size(), fp); fclose(fp); }
    }
    return 0;
}
