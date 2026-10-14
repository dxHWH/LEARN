// feel_verify.cpp —— 第六部分（方法论篇）的量化实验：状态判定几何、宽限时间、跳跃曲线、单墙攀爬
// 数值取自 SampleScene.unity（玩家胶囊 size (0.770, 2.069) offset (0.043, -0.295)、GroundCheck (0, -1.221) 长 0.15、
// WallCheck (0.366, -0.006) 长 0.25、moveSpeed 7、jumpForce 20、gravityScale 5、wallSlideSpeed 2、wallJump (4, 18)），
// 关卡几何取自 Ground 瓦片地图的 CompositeCollider2D 路径（立柱 x∈[12,14]、y∈[6,13]，地面顶 y=6）。
// 模型规则与 rpg_verify.cpp 相同（R1~R4）；本文件的实验编号用 F 前缀，不改动 E1~E11 的数据。
//
// 编译：cl /nologo /std:c++latest /EHsc /O2 /utf-8 feel_verify.cpp
// 运行：feel_verify.exe            全部实验 → feeldata.json
//       feel_verify.exe F2 F5      只跑指定实验 → feeldata_partial.json
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <functional>
#include <set>
#include <string>
#include <utility>
#include <vector>

using KV = std::vector<std::pair<std::string, std::string>>;
static std::string jnum(double v, int p = 10) { if (!std::isfinite(v)) return "null"; char b[64]; snprintf(b, sizeof b, "%.*g", p, v); return b; }
static std::string jstr(const std::string& s) { return "\"" + s + "\""; }
static std::string jobj(const KV& kv) { std::string o = "{"; for (size_t i = 0; i < kv.size(); ++i) { if (i) o += ","; o += jstr(kv[i].first) + ":" + kv[i].second; } return o + "}"; }
static std::string jarr(const std::vector<std::string>& a) { std::string o = "["; for (size_t i = 0; i < a.size(); ++i) { if (i) o += ","; o += a[i]; } return o + "]"; }
static KV OUT;
static const double PI = 3.14159265358979323846;

// ---------------------------------------------------------------- 场景常量
static const double R = 0.7703179 / 2, OFFX = 0.04300517, OFFY = -0.29489255, HALFH = 2.0689855 / 2;
static const double BOTTOM = OFFY - HALFH;                 // 胶囊最低点（相对根节点）= -1.329
static const double TOP = OFFY + HALFH;                    // +0.740
static const double GC_Y = -1.221, GC_LEN = 0.15;          // GroundCheck
static const double WC_X = 0.366, WC_Y = -0.006, WC_LEN = 0.25;
static const double G = -9.81 * 5, H = 0.02;               // 重力 × gravityScale；物理步长

// ================================================================ F1 检测几何
static double solveMax(std::function<bool(double)> ok) {    // 0~89.9° 内最后一个 ok 为真的角度
    double best = 0; for (double a = 0; a < 89.9; a += 0.01) if (ok(a * PI / 180)) best = a; return best;
}
static void F1() {
    printf("\n==== F1 地面 / 墙检测的几何边界（只用场景数值计算）\n");
    double s0 = GC_Y - BOTTOM, m = GC_LEN - s0;             // 射线起点在脚底上方 s0，末端在脚底下方 m
    auto single = [&](double len) { double mm = len - s0; return solveMax([&](double t) { return R * (1 / cos(t) - 1) <= mm; }); };
    double d = 0.3;                                          // 三射线：x = 0, ±0.3（在胶囊半宽 0.385 以内）
    double tri = solveMax([&](double t) { return R * (1 / cos(t) - 1) - d * tan(t) <= m; });
    double a1 = single(GC_LEN), a2 = single(0.3);
    printf("  射线起点在脚底上方 %.3f m，伸到脚底下方 %.3f m\n", s0, m);
    printf("  单中心射线（源码，长 0.15）可识别的最大坡度 %.1f°；加长到 0.30 → %.1f°；三射线（±0.3）→ %.1f°\n", a1, a2, tri);
    double wallH = WC_Y - BOTTOM, reach = WC_X + WC_LEN - (OFFX + R);
    printf("  墙检测：只有一条腰部射线，距脚底 %.3f m；伸出碰撞体表面 %.3f m；墙面必须覆盖这一高度才算“贴墙”\n", wallH, reach);
    std::vector<std::string> slope;
    for (int deg : {0, 10, 20, 25, 26, 30, 35, 45}) {
        double t = deg * PI / 180, gap = R * (1 / cos(t) - 1);
        slope.push_back(jobj({{"deg", jnum(deg)}, {"gap", jnum(gap, 4)}, {"single", jstr(gap <= m ? "✔" : "✘")},
                              {"long", jstr(gap <= 0.3 - s0 ? "✔" : "✘")}, {"tri", jstr(gap - d * tan(t) <= m ? "✔" : "✘")}}));
    }
    OUT.push_back({"F1", jobj({{"s0", jnum(s0, 4)}, {"m", jnum(m, 4)}, {"a1", jnum(a1, 4)}, {"a2", jnum(a2, 4)}, {"tri", jnum(tri, 4)},
                               {"wallH", jnum(wallH, 4)}, {"reach", jnum(reach, 4)}, {"r", jnum(R, 4)}, {"slope", jarr(slope)}})});
}

// ================================================================ F2 / F3 宽限时间（Coyote Time / Jump Buffer）
// 帧时刻 t_k = φ + k·dt（φ 在 [0, dt) 内均匀取 400 个相位）；按键在时刻 δ 发生，在第一个 t_k ≥ δ 的帧被读到（R3）
static double phiAvg(std::function<bool(double)> succAtPhase) { const int N = 400; int ok = 0; for (int i = 0; i < N; ++i) ok += succAtPhase((i + 0.5) / N); return (double)ok / N; }
static double gaussMix(std::function<double(double)> f, double mu, double sd) {   // E[f(δ)]，δ ~ N(mu, sd)，数值积分
    double s = 0, w = 0; for (double x = mu - 5 * sd; x <= mu + 5 * sd; x += 0.0005) { double p = exp(-0.5 * (x - mu) * (x - mu) / (sd * sd)); s += p * f(x); w += p; } return s / w;
}
static void F2() {
    printf("\n==== F2 跑出平台边缘时按跳：Coyote Time（60 fps，离开地面的时刻记为 0）\n");
    const double dt = 1.0 / 60;
    auto succ = [&](double delta, double coyote) {            // 读到按键的那一帧时刻 < coyote（coyote=0 即“这一帧仍判定着地”）
        return phiAvg([&](double u) { double phi = u * dt; double k = ceil((delta - phi) / dt - 1e-12); double tr = phi + k * dt; return tr < coyote - 1e-12; });
    };
    std::vector<double> cs = {0, 0.05, 0.1, 0.15};
    std::vector<std::string> curve, pop;
    for (int ms = -60; ms <= 220; ms += 4) { std::vector<std::string> row = {jnum(ms)}; for (double c : cs) row.push_back(jnum(100 * succ(ms / 1000.0, c), 4)); curve.push_back(jarr(row)); }
    struct P { const char* n; double mu, sd; } ps[] = {{"准时型 N(+20 ms, 40 ms)", 0.02, 0.04}, {"偏晚型 N(+50 ms, 50 ms)", 0.05, 0.05}};
    for (auto& p : ps) {
        KV kv = {{"n", jstr(p.n)}};
        printf("  %s：", p.n);
        for (double c : cs) { double r = 100 * gaussMix([&](double x) { return succ(x, c); }, p.mu, p.sd); kv.push_back({"c" + std::to_string((int)lround(c * 1000)), jnum(r, 4)}); printf(" coyote %3.0f ms → %5.1f%%", c * 1000, r); }
        printf("\n"); pop.push_back(jobj(kv));
    }
    printf("  代价：coyote 期间以 7 m/s 跑出的距离 = 0.05 s → 0.35 m，0.1 s → 0.70 m，0.15 s → 1.05 m（看起来像“从空中起跳”）\n");
    OUT.push_back({"F2", jobj({{"curve", jarr(curve)}, {"pop", jarr(pop)}})});
}
static void F3() {
    printf("\n==== F3 落地前按跳：Jump Buffer（60 fps，着地后第一帧记为落地帧）\n");
    printf("     源码：落地帧 Fall.Update 才切到 Idle，本帧不再检查跳跃 → 必须在落地帧之后的帧读到按键\n");
    const double dt = 1.0 / 60;
    auto succ = [&](double delta, double buf) {               // 着地时刻 0；落地帧 k0 = 第一个 t_k ≥ 0 的帧；可起跳的最早帧 = k0+1
        return phiAvg([&](double u) {
            double phi = u * dt; double kr = ceil((delta - phi) / dt - 1e-12), tr = phi + kr * dt;
            double k0 = ceil((0 - phi) / dt - 1e-12), tok = phi + (k0 + 1) * dt;
            if (tr >= tok - 1e-12) return true;                // 按键在可起跳之后才被读到 → 直接起跳
            return tok - tr <= buf + 1e-12;                    // 否则看缓冲能不能撑到可起跳的那一帧
        });
    };
    std::vector<double> bs = {0, 0.08, 0.12, 0.2};
    std::vector<std::string> curve, pop;
    for (int ms = -260; ms <= 60; ms += 4) { std::vector<std::string> row = {jnum(ms)}; for (double b : bs) row.push_back(jnum(100 * succ(ms / 1000.0, b), 4)); curve.push_back(jarr(row)); }
    struct P { const char* n; double mu, sd; } ps[] = {{"预判型 N(−30 ms, 40 ms)", -0.03, 0.04}, {"抢跳型 N(−70 ms, 50 ms)", -0.07, 0.05}};
    for (auto& p : ps) {
        KV kv = {{"n", jstr(p.n)}};
        printf("  %s：", p.n);
        for (double b : bs) { double r = 100 * gaussMix([&](double x) { return succ(x, b); }, p.mu, p.sd); kv.push_back({"b" + std::to_string((int)lround(b * 1000)), jnum(r, 4)}); printf(" buffer %3.0f ms → %5.1f%%", b * 1000, r); }
        printf("\n"); pop.push_back(jobj(kv));
    }
    OUT.push_back({"F3", jobj({{"curve", jarr(curve)}, {"pop", jarr(pop)}})});
}

// ================================================================ F4 跳跃曲线（Box2D 半隐式欧拉，Δt = 0.02）
struct Arc { double apex, tApex, air; };
static Arc jump(double v0, double cutAt, double cutMul, double fallMul, double hangV, double hangMul) {
    double v = v0, y = 0, t = 0, apex = 0, tApex = 0; bool cut = false;
    while (true) {
        double g = G;
        if (v < 0) g *= fallMul;
        if (hangV > 0 && std::fabs(v) < hangV) g *= hangMul;
        v += g * H; t += H;
        if (!cut && cutAt >= 0 && t >= cutAt - 1e-9 && v > 0) { v *= cutMul; cut = true; }
        y += v * H;
        if (y > apex) { apex = y; tApex = t; }
        if (y <= 0 && t > 0.05) break;
    }
    return {apex, tApex, t};
}
static void F4() {
    printf("\n==== F4 跳跃曲线：源码（一次性 vy=20）vs 可变跳高 / 下落加重 / 顶点悬停\n");
    Arc base = jump(20, -1, 1, 1, 0, 1);
    printf("  源码：顶点 %.2f m（连续解 %.2f），到顶 %.3f s，落回原高度 %.3f s\n", base.apex, 400 / (2 * 9.81 * 5), base.tApex, base.air);
    std::vector<std::string> hold, var;
    for (double th : {0.04, 0.06, 0.08, 0.12, 0.16, 0.2, 0.25, 0.3, 0.35, 0.42}) {
        Arc a = jump(20, th, 0.5, 1, 0, 1);
        hold.push_back(jarr({jnum(th, 3), jnum(a.apex, 4)}));
    }
    struct V { const char* n; double cutAt, cutMul, fallMul, hangV, hangMul; } vs[] = {
        {"源码：按住与轻按相同", -1, 1, 1, 0, 1}, {"轻按（0.08 s 松键，速度 ×0.5）", 0.08, 0.5, 1, 0, 1},
        {"下落重力 ×1.6", -1, 1, 1.6, 0, 1}, {"下落 ×1.6 + 顶点悬停（|vy|<3 时重力 ×0.5）", -1, 1, 1.6, 3, 0.5},
        {"轻按 + 下落 ×1.6", 0.08, 0.5, 1.6, 0, 1}};
    for (auto& v : vs) {
        Arc a = jump(20, v.cutAt, v.cutMul, v.fallMul, v.hangV, v.hangMul);
        printf("  %-34s 顶点 %.2f m，到顶 %.3f s，总滞空 %.3f s\n", v.n, a.apex, a.tApex, a.air);
        var.push_back(jobj({{"n", jstr(v.n)}, {"apex", jnum(a.apex, 4)}, {"tApex", jnum(a.tApex, 4)}, {"air", jnum(a.air, 4)}}));
    }
    double gap = 3.0, dist = 7 * base.air;
    printf("  关卡第一道沟宽 %.0f m；满速跳跃水平距离 %.2f m → 最早可在沟前 %.2f m（%.0f ms）起跳\n", gap, dist, dist - gap, (dist - gap) / 7 * 1000);
    OUT.push_back({"F4", jobj({{"apex", jnum(base.apex, 4)}, {"apexC", jnum(400 / (2 * 9.81 * 5), 4)}, {"tApex", jnum(base.tApex, 4)}, {"air", jnum(base.air, 4)},
                               {"dist", jnum(dist, 4)}, {"early", jnum(dist - gap, 4)}, {"earlyMs", jnum((dist - gap) / 7 * 1000, 4)},
                               {"hold", jarr(hold)}, {"var", jarr(var)}})});
}

// ================================================================ F5 单墙攀爬：逐行复刻 Fall / WallSlide / WallJump，爬关卡里 7 m 高的立柱
struct ClimbCfg { double wjH; double react; bool footRay; };
struct ClimbRes { bool top; double t; int cycles; double gain, maxBottom; int deadZone; double steady, firstWS; };
static ClimbRes climb(ClimbCfg c) {
    enum S { IDLE, MOVE, JUMP, FALL, WSLIDE, WJUMP } s = IDLE;
    const double dt = 1.0 / 60, WALL = 12, PTOP = 13, FLOOR = 6;
    double x = WALL - (OFFX + R), y = FLOOR - BOTTOM, vx = 0, vy = 0, t = 0, ft = 0; int f = 1;
    auto right = [&]() { return x + R + OFFX * f; };
    auto left = [&]() { return x - R + OFFX * f; };
    auto setV = [&](double a, double b) { vx = a; vy = b; if (a > 0 && f < 0) f = 1; else if (a < 0 && f > 0) f = -1; };
    auto grounded = [&]() {                                    // GroundCheck 射线：地面顶 y=6（x∈[-8,21]）或立柱顶 y=13（x∈[12,14]）
        double sy = y + GC_Y; bool onTop = x >= WALL && x <= 14;
        double surf = onTop ? PTOP : FLOOR; return sy - surf >= -1e-9 && sy - surf <= GC_LEN;
    };
    auto wallRay = [&](double ry) { if (f < 0) return false; double sx = x + WC_X; return WALL - sx >= 0 && WALL - sx <= WC_LEN && ry > FLOOR && ry < PTOP; };
    auto wall = [&]() { return wallRay(y + WC_Y) || (c.footRay && wallRay(y + BOTTOM + 0.1)); };
    int cycles = 0, dead = 0, wsFrames = 0; double maxB = y + BOTTOM; bool pressed0 = false;
    std::vector<double> entries;                           // 每次进入墙滑时的脚底高度
    auto summary = [&](bool top) {
        double gs = 0; int gn = 0;
        if (entries.size() >= 2) { gs = entries[1] - entries[0]; gn = 1; }   // 第一次蹬墙的净升高（离墙顶还远，不受顶端死区影响）
        return ClimbRes{top, t, cycles, gn ? gs / gn : NAN, maxB, dead, entries.empty() ? NAN : entries.back(), entries.empty() ? NAN : entries[0]};
    };
    for (int frame = 0; frame < 60 * 20; ++frame) {
        t += dt;
        while (ft + H <= t + 1e-12) {                           // R1：先跑物理步
            ft += H; vy += G * H;
            double ox = x; x += vx * H;
            if (right() > WALL && left() < 14 && y + BOTTOM < PTOP - 1e-9 && ox + R + OFFX * f <= WALL + 1e-9) { x = WALL - (R + OFFX * f); vx = 0; }
            double ob = y + BOTTOM; y += vy * H;
            bool overP = right() > WALL && left() < 14;
            if (overP && ob >= PTOP - 1e-9 && y + BOTTOM < PTOP) { y = PTOP - BOTTOM; vy = 0; }
            if (y + BOTTOM < FLOOR) { y = FLOOR - BOTTOM; vy = 0; }
            maxB = std::max(maxB, y + BOTTOM);
        }
        if (x >= WALL - 0.05 && y + BOTTOM >= PTOP - 1e-6 && x + R > WALL + 0.3) return summary(true);
        bool jump = false;                                     // 玩家：一直按住 →；首帧起跳；每次进入墙滑后经过 react 秒按一次跳
        if (!pressed0) { jump = true; pressed0 = true; }
        if (s == WSLIDE) { ++wsFrames; if (wsFrames - 1 >= (int)lround(c.react / dt)) jump = true; } else wsFrames = 0;
        // Update（逐行复刻源码的转移顺序）
        auto enterWS = [&]() { s = WSLIDE; entries.push_back(y + BOTTOM); };
#ifdef DBG
        static int ls = -1; if ((int)s != ls || frame % 6 == 0) { printf("    t=%.3f s=%d x=%.3f bottom=%.3f vx=%.2f vy=%.2f f=%d wall=%d jump=%d\n", t, (int)s, x, y + BOTTOM, vx, vy, f, (int)wall(), (int)jump); ls = (int)s; }
#endif
#ifdef DBG
        static int ls = -1; if ((int)s != ls || frame % 6 == 0) { printf("    t=%.3f s=%d x=%.3f bottom=%.3f vx=%.2f vy=%.2f f=%d wall=%d jump=%d
", t, (int)s, x, y + BOTTOM, vx, vy, f, (int)wall(), (int)jump); ls = (int)s; }
#endif
        switch (s) {
        case IDLE: case MOVE:
            if (jump && grounded()) { s = JUMP; setV(vx, 20); break; }
            if (!grounded()) { s = FALL; break; }
            if (s == IDLE) { s = MOVE; } else setV(7, vy);
            break;
        case JUMP: if (vy < 0) s = FALL; break;
        case FALL:
            if (grounded()) { setV(1, vy); s = IDLE; }
            setV(7, vy);
            if (wall()) { setV(1, vy); enterWS(); }
            else if (!grounded() && f > 0 && right() >= WALL - 1e-6 && y + BOTTOM < PTOP && y + WC_Y > PTOP) ++dead;   // 贴着墙却检测不到
            break;
        case WSLIDE:
            if (jump) { s = WJUMP; ++cycles; setV(-f * c.wjH, 18); break; }
            vx = 0; vy = -2;
            if (grounded()) s = IDLE;
            break;
        case WJUMP: if (vy < 0) s = FALL; break;
        }
    }
    return summary(false);
}
static void F5() {
    printf("\n==== F5 单墙攀爬：玩家一直按住 →，在墙滑中按跳（关卡立柱高 7 m，跳跃顶点只有 ~4.1 m）\n");
    std::vector<std::string> rows;
    struct C { const char* n; ClimbCfg c; } cs[] = {
        {"源码：反应 0 帧（进入墙滑当帧按跳）", {4, 0, false}}, {"源码：反应 1 帧", {4, 1 / 60.0, false}}, {"源码：反应 2 帧", {4, 2 / 60.0, false}},
        {"源码：反应 3 帧（50 ms）", {4, 3 / 60.0, false}}, {"源码：反应 6 帧（100 ms）", {4, 6 / 60.0, false}}, {"源码：反应 12 帧（200 ms）", {4, 12 / 60.0, false}},
        {"推力 5、反应 100 ms", {5, 0.1, false}}, {"推力 6、反应 100 ms", {6, 0.1, false}}, {"推力 8、反应 100 ms", {8, 0.1, false}},
        {"加脚部墙射线、反应 100 ms", {4, 0.1, true}}, {"加脚部墙射线、反应 200 ms", {4, 0.2, true}}};
    for (auto& k : cs) {
        ClimbRes r = climb(k.c);
        printf("  %-30s %s，用时 %5.2f s，蹬墙 %2d 次，首次蹬墙净升高 %+.2f m，最后一次墙滑脚底 %.2f，“贴墙却测不到” %d 帧\n", k.n,
               r.top ? "爬上立柱" : "20 s 内爬不上", r.top ? r.t : NAN, r.cycles, r.gain, r.steady, r.deadZone);
        rows.push_back(jobj({{"n", jstr(k.n)}, {"wjH", jnum(k.c.wjH)}, {"react", jnum(k.c.react)}, {"top", r.top ? "true" : "false"}, {"t", jnum(r.top ? r.t : -1, 4)},
                             {"cycles", jnum(r.cycles)}, {"gain", jnum(r.gain, 4)}, {"maxB", jnum(r.maxBottom, 4)}, {"dead", jnum(r.deadZone)}, {"steady", jnum(r.steady, 4)}, {"first", jnum(r.firstWS, 4)}}));
    }
    OUT.push_back({"F5", jobj({{"rows", jarr(rows)}})});
}

int main(int argc, char** argv) {
    setvbuf(stdout, nullptr, _IONBF, 0);
    std::set<std::string> want; for (int i = 1; i < argc; ++i) want.insert(argv[i]);
    auto on = [&](const char* k) { return want.empty() || want.count(k); };
    printf("RPGDemo 方法论篇实测（feel_verify.cpp）\n");
    if (on("F1")) F1(); if (on("F2")) F2(); if (on("F3")) F3(); if (on("F4")) F4(); if (on("F5")) F5();
    const char* path = want.empty() ? "feeldata.json" : "feeldata_partial.json";
    FILE* fp = fopen(path, "wb"); std::string js = jobj(OUT); fwrite(js.data(), 1, js.size(), fp); fclose(fp);
    printf("\n写出 %s（%zu 字节）\n", path, js.size());
}
