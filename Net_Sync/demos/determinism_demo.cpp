// determinism_demo.cpp —— 确定性实验：同一份源码，用不同浮点编译选项编译，比较逐帧 hash
// 编译三份：
//   cl /nologo /std:c++latest /EHsc /O2 /utf-8 /fp:precise            determinism_demo.cpp /Fe:det_precise.exe
//   cl /nologo /std:c++latest /EHsc /O2 /utf-8 /fp:fast /arch:AVX2    determinism_demo.cpp /Fe:det_fast.exe
//   cl /nologo /std:c++latest /EHsc /Od /utf-8 /fp:strict             determinism_demo.cpp /Fe:det_strict.exe
// 运行：det_xxx.exe <tag>   → 写 det_<tag>.json
// D1 浮点求和顺序   D2 浮点群体模拟逐帧 hash   D3 定点数（Q16.16）同一模拟   D4 1 ulp 扰动的蝴蝶效应
// D5 unordered_map 遍历顺序   D6 浮点累加时间的漂移   D7 定点 sqrt / sin 精度
#include <algorithm>
#include <cmath>
#include <cstring>
#include <cstdint>
#include <cstdio>
#include <map>
#include <string>
#include <unordered_map>
#include <vector>

static std::string hex64(uint64_t v) { char b[32]; snprintf(b, sizeof b, "\"%016llx\"", (unsigned long long)v); return b; }
static uint64_t fnv(uint64_t h, uint32_t v) { for (int k = 0; k < 4; k++) { h ^= (v >> (8 * k)) & 0xFF; h *= 1099511628211ULL; } return h; }
static uint32_t fbits(float f) { uint32_t u; memcpy(&u, &f, 4); return u; }
static uint32_t dbits_lo(double d) { uint64_t u; memcpy(&u, &d, 8); return (uint32_t)u; }

struct Rng {
    uint64_t s;
    explicit Rng(uint64_t seed) : s(seed * 0x9E3779B97F4A7C15ULL + 1) {}
    uint64_t next() { s ^= s >> 12; s ^= s << 25; s ^= s >> 27; return s * 2685821657736338717ULL; }
    uint32_t u32() { return (uint32_t)(next() >> 32); }
};

// ------------------------------------------------------------------ 浮点版群体模拟
const int N = 48, FRAMES = 600;
struct FU { float x, y, vx, vy, a, turn; };
static void init_float(std::vector<FU>& u) {
    Rng r(7);
    u.resize(N);
    for (int i = 0; i < N; i++) {
        u[i].x = 200.0f + (float)(r.u32() % 1600);
        u[i].y = 200.0f + (float)(r.u32() % 1600);
        u[i].vx = u[i].vy = 0;
        u[i].a = (float)(r.u32() % 6283) / 1000.0f;
        u[i].turn = ((float)(r.u32() % 200) - 100.0f) / 2000.0f;
    }
}
static void step_float(std::vector<FU>& u) {
    const float dt = 1.0f / 15.0f, speed = 300.0f, R = 120.0f;
    for (int i = 0; i < N; i++) {
        FU& p = u[i];
        p.a += p.turn;
        float dx = std::cos(p.a) * speed, dy = std::sin(p.a) * speed;
        for (int j = 0; j < N; j++) {
            if (j == i) continue;
            float ex = p.x - u[j].x, ey = p.y - u[j].y;
            float d2 = ex * ex + ey * ey;
            if (d2 < R * R && d2 > 1e-4f) {
                float d = std::sqrt(d2);
                float k = (R - d) / d * 6.0f;
                dx += ex * k; dy += ey * k;
            }
        }
        float len = std::sqrt(dx * dx + dy * dy);
        if (len > speed) { dx = dx / len * speed; dy = dy / len * speed; }
        p.vx = dx; p.vy = dy;
        p.x += p.vx * dt; p.y += p.vy * dt;
        if (p.x < 0) { p.x = -p.x; p.a = 3.14159265f - p.a; }
        if (p.x > 2000) { p.x = 4000 - p.x; p.a = 3.14159265f - p.a; }
        if (p.y < 0) { p.y = -p.y; p.a = -p.a; }
        if (p.y > 2000) { p.y = 4000 - p.y; p.a = -p.a; }
    }
}
static uint64_t hash_float(const std::vector<FU>& u) {
    uint64_t h = 1469598103934665603ULL;
    for (auto& p : u) { h = fnv(h, fbits(p.x)); h = fnv(h, fbits(p.y)); }
    return h;
}

// ------------------------------------------------------------------ Q16.16 定点数
typedef int32_t fx;
const fx ONE = 1 << 16;
static inline fx fmul(fx a, fx b) { return (fx)(((int64_t)a * b) >> 16); }
static inline fx fdiv(fx a, fx b) { return (fx)(((int64_t)a << 16) / b); }
static inline fx fint(int v) { return v * ONE; }
static uint32_t isqrt64(uint64_t v) {          // 整数开方（逐位法），结果向下取整
    uint64_t r = 0, bit = 1ULL << 62;
    while (bit > v) bit >>= 2;
    while (bit) { if (v >= r + bit) { v -= r + bit; r = (r >> 1) + bit; } else r >>= 1; bit >>= 2; }
    return (uint32_t)r;
}
static inline fx fsqrt(fx a) { return a <= 0 ? 0 : (fx)isqrt64((uint64_t)a << 16); }
const fx PI = 205887, TWO_PI = 411775, HALF_PI = 102944;
static fx fsin(fx x) {                         // 只用整数运算：区间规约 + 7 次泰勒多项式
    x %= TWO_PI;
    if (x > PI) x -= TWO_PI;
    if (x < -PI) x += TWO_PI;
    if (x > HALF_PI) x = PI - x;
    if (x < -HALF_PI) x = -PI - x;
    fx x2 = fmul(x, x), x3 = fmul(x2, x), x5 = fmul(x3, x2), x7 = fmul(x5, x2);
    return x - x3 / 6 + x5 / 120 - x7 / 5040;
}
static inline fx fcos(fx x) { return fsin(x + HALF_PI); }

struct XU { fx x, y, vx, vy, a, turn; };
static void init_fixed(std::vector<XU>& u) {
    Rng r(7);
    u.resize(N);
    for (int i = 0; i < N; i++) {
        u[i].x = fint(200 + (int)(r.u32() % 1600));
        u[i].y = fint(200 + (int)(r.u32() % 1600));
        u[i].vx = u[i].vy = 0;
        u[i].a = (fx)((int64_t)(r.u32() % 6283) * ONE / 1000);
        u[i].turn = (fx)((int64_t)((int)(r.u32() % 200) - 100) * ONE / 2000);
    }
}
static void step_fixed(std::vector<XU>& u) {
    const fx speed = fint(300), R = fint(120), DT = ONE / 15;
    for (int i = 0; i < N; i++) {
        XU& p = u[i];
        p.a += p.turn;
        fx dx = fmul(fcos(p.a), speed), dy = fmul(fsin(p.a), speed);
        for (int j = 0; j < N; j++) {
            if (j == i) continue;
            fx ex = p.x - u[j].x, ey = p.y - u[j].y;
            if (ex > R || ex < -R || ey > R || ey < -R) continue;
            int64_t d2 = ((int64_t)ex * ex + (int64_t)ey * ey) >> 16;
            if (d2 < ((int64_t)R * R >> 16) && d2 > 0) {
                fx d = fsqrt((fx)d2);
                if (d == 0) continue;
                fx k = fmul(fdiv(R - d, d), fint(6));
                dx += fmul(ex, k); dy += fmul(ey, k);
            }
        }
        int64_t l2 = ((int64_t)dx * dx + (int64_t)dy * dy) >> 16;
        fx len = (fx)isqrt64((uint64_t)l2 << 16);
        if (len > speed) { dx = fmul(fdiv(dx, len), speed); dy = fmul(fdiv(dy, len), speed); }
        p.vx = dx; p.vy = dy;
        p.x += fmul(p.vx, DT); p.y += fmul(p.vy, DT);
        if (p.x < 0) { p.x = -p.x; p.a = PI - p.a; }
        if (p.x > fint(2000)) { p.x = fint(4000) - p.x; p.a = PI - p.a; }
        if (p.y < 0) { p.y = -p.y; p.a = -p.a; }
        if (p.y > fint(2000)) { p.y = fint(4000) - p.y; p.a = -p.a; }
    }
}
static uint64_t hash_fixed(const std::vector<XU>& u) {
    uint64_t h = 1469598103934665603ULL;
    for (auto& p : u) { h = fnv(h, (uint32_t)p.x); h = fnv(h, (uint32_t)p.y); }
    return h;
}

int main(int argc, char** argv) {
    setvbuf(stdout, nullptr, _IONBF, 0);
    std::string tag = argc > 1 ? argv[1] : "precise";
    std::string J = "{\"tag\":\"" + tag + "\"";
    printf("== determinism_demo [%s] ==\n", tag.c_str());

    // D1 求和顺序
    {
        Rng r(1);
        std::vector<float> v(1000000);
        for (auto& x : v) x = (float)(r.u32() % 1000000) / 1000.0f * ((r.u32() & 7) == 0 ? 1000.0f : 1.0f);
        float fwd = 0, bwd = 0;
        for (size_t i = 0; i < v.size(); i++) fwd += v[i];
        for (size_t i = v.size(); i-- > 0;) bwd += v[i];
        std::vector<float> w = v;
        while (w.size() > 1) { std::vector<float> n2((w.size() + 1) / 2); for (size_t i = 0; i < n2.size(); i++) n2[i] = w[2 * i] + (2 * i + 1 < w.size() ? w[2 * i + 1] : 0.0f); w.swap(n2); }
        double exact = 0;
        for (float x : v) exact += x;
        printf("D1 正序 %.1f  倒序 %.1f  两两 %.1f  double 参考 %.1f\n", fwd, bwd, w[0], exact);
        char b[256];
        snprintf(b, sizeof b, ",\"D1\":{\"fwd\":%.1f,\"bwd\":%.1f,\"pair\":%.1f,\"exact\":%.1f}", fwd, bwd, w[0], exact);
        J += b;
    }
    // D2 浮点模拟逐帧 hash
    std::vector<uint64_t> fh;
    {
        std::vector<FU> u; init_float(u);
        for (int f = 0; f < FRAMES; f++) { step_float(u); fh.push_back(hash_float(u)); }
        printf("D2 浮点模拟 600 帧 hash = %016llx  单位0 = (%.6f, %.6f)\n", (unsigned long long)fh.back(), u[0].x, u[0].y);
        J += ",\"D2\":{\"final\":" + hex64(fh.back()) + ",\"x0\":" + std::to_string(u[0].x) + ",\"frames\":[";
        for (int f = 0; f < FRAMES; f++) J += (f ? "," : "") + hex64(fh[f]);
        J += "]}";
    }
    // D3 定点模拟
    {
        std::vector<XU> u; init_fixed(u);
        std::vector<uint64_t> xh;
        for (int f = 0; f < FRAMES; f++) { step_fixed(u); xh.push_back(hash_fixed(u)); }
        printf("D3 定点模拟 600 帧 hash = %016llx  单位0 = (%.4f, %.4f)\n", (unsigned long long)xh.back(), u[0].x / 65536.0, u[0].y / 65536.0);
        J += ",\"D3\":{\"final\":" + hex64(xh.back()) + ",\"f100\":" + hex64(xh[99]) + "}";
    }
    // D4 蝴蝶效应：单位 0 的 x 改动 1 ulp
    {
        std::vector<FU> a, b; init_float(a); init_float(b);
        b[0].x = std::nextafter(b[0].x, 1e9f);
        J += ",\"D4\":[";
        int f1 = -1, f100 = -1, first = -1;
        for (int f = 0; f < FRAMES; f++) {
            step_float(a); step_float(b);
            float m = 0;
            int diffUnits = 0;
            for (int i = 0; i < N; i++) { float d = std::fabs(a[i].x - b[i].x) + std::fabs(a[i].y - b[i].y); m = std::max(m, d); diffUnits += d > 0; }
            if (first < 0 && hash_float(a) != hash_float(b)) first = f + 1;
            if (f1 < 0 && m > 1.0f) f1 = f + 1;
            if (f100 < 0 && m > 100.0f) f100 = f + 1;
            char buf[64];
            snprintf(buf, sizeof buf, "%s[%d,%.6g,%d]", f ? "," : "", f + 1, std::max(m, 1e-9f), diffUnits);
            J += buf;
        }
        J += "],\"D4s\":{\"first\":" + std::to_string(first) + ",\"f1\":" + std::to_string(f1) + ",\"f100\":" + std::to_string(f100) + "}";
        printf("D4 1 ulp 扰动：hash 第 %d 帧即不同；位置差 >1 cm 在第 %d 帧，>1 m 在第 %d 帧\n", first, f1, f100);
    }
    // D5 unordered_map 遍历顺序
    {
        std::vector<int> ids = {1007, 2013, 3001, 4099, 5003, 6007, 7019, 8009, 9001, 10007, 11003, 12011};
        std::unordered_map<int, int> m1, m2;
        m2.reserve(1024);
        for (int id : ids) m1[id] = 1;
        for (int k = (int)ids.size() - 1; k >= 0; k--) m2[ids[k]] = 1;
        std::map<int, int> s1;
        for (int id : ids) s1[id] = 1;
        auto dump = [](auto& m) { std::string s = "["; bool f = true; for (auto& kv : m) { s += (f ? "" : ",") + std::to_string(kv.first); f = false; } return s + "]"; };
        J += ",\"D5\":{\"um1\":" + dump(m1) + ",\"um2\":" + dump(m2) + ",\"map\":" + dump(s1) + "}";
        printf("D5 unordered_map(默认桶) 顺序 %s\n   unordered_map(reserve 1024, 倒序插入) 顺序 %s\n   std::map 顺序 %s\n", dump(m1).c_str(), dump(m2).c_str(), dump(s1).c_str());
    }
    // D6 浮点累加时间
    {
        float tf = 0; double td = 0;
        const int frames = 15 * 60 * 20;     // 20 分钟 @15 帧/秒
        for (int i = 0; i < frames; i++) { tf += 1.0f / 15.0f; td += 1.0 / 15.0; }
        double exact = frames / 15.0;
        printf("D6 20 分钟：float 累加 %.4f s，double 累加 %.9f s，帧号×帧长 %.4f s，float 误差 %.1f ms\n", tf, td, exact, (tf - exact) * 1000);
        char b[200];
        snprintf(b, sizeof b, ",\"D6\":{\"tf\":%.4f,\"td\":%.9f,\"exact\":%.4f,\"errms\":%.2f}", tf, td, exact, (tf - exact) * 1000);
        J += b;
    }
    // D7 定点精度
    {
        double es = 0, ec = 0, eq = 0;
        for (int i = -3000; i <= 3000; i++) {
            double x = i / 1000.0;
            fx fxv = (fx)std::llround(x * 65536);
            es = std::max(es, std::fabs(fsin(fxv) / 65536.0 - std::sin(fxv / 65536.0)));
        }
        for (int i = 1; i <= 20000; i++) {
            double x = i / 10.0;
            fx fxv = (fx)std::llround(x * 65536);
            eq = std::max(eq, std::fabs(fsqrt(fxv) / 65536.0 - std::sqrt(fxv / 65536.0)));
        }
        printf("D7 Q16.16：sin 最大误差 %.2e，sqrt 最大误差 %.2e（1 LSB = %.2e）\n", es, eq, 1.0 / 65536);
        char b[160];
        snprintf(b, sizeof b, ",\"D7\":{\"sin\":%.3g,\"sqrt\":%.3g,\"lsb\":%.3g}", es, eq, 1.0 / 65536);
        J += b;
        (void)ec;
    }
    J += "}";
    std::string fn = "det_" + tag + ".json";
    FILE* f = fopen(fn.c_str(), "wb");
    fwrite(J.data(), 1, J.size(), f);
    fclose(f);
    printf("写入 %s\n", fn.c_str());
    return 0;
}
