// soft_raster.cpp —《图形学渲染管线》配套实测程序
// 一个约 1000 行、只依赖 C++ 标准库的软件光栅化器，用来把书里的每个结论"跑出来"：
//   E1  MVP 逐级追踪            E2  近平面裁剪             E3  背面剔除比例
//   E4  填充规则 / 定点 vs 浮点  E5  透视校正插值            E6  深度精度与 Reversed-Z
//   E7  Overdraw/Early-Z/Prepass E8  2x2 Quad 效率            E9  Mipmap/各向异性/纹理缓存
//   E10 MSAA vs SSAA            E11 混合顺序与预乘 Alpha     E12 IMR vs TBR 带宽模型
//   E13 综合场景：Shadow Map + Blinn-Phong + 深度偏移
// 编译：cl /nologo /std:c++latest /EHsc /O2 /utf-8 soft_raster.cpp
// 运行：soft_raster.exe > run_output.txt   （图片写到 out/*.ppm，由 tools/ppm2png.py 转 PNG）
#define _USE_MATH_DEFINES
#define _CRT_SECURE_NO_WARNINGS
#include <cmath>
#include <cstdio>
#include <cstdint>
#include <cstring>
#include <cfloat>
#include <vector>
#include <string>
#include <algorithm>
#include <functional>
#include <random>
#include <chrono>
#include <direct.h>
#include <array>

using i64 = int64_t;
using u8 = uint8_t;

// ============================== 数学 ==============================
struct V2 { float x, y; };
struct V3 { float x, y, z; };
struct V4 { float x, y, z, w; };
static inline V3 operator+(V3 a, V3 b) { return {a.x + b.x, a.y + b.y, a.z + b.z}; }
static inline V3 operator-(V3 a, V3 b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
static inline V3 operator*(V3 a, float s) { return {a.x * s, a.y * s, a.z * s}; }
static inline V3 mulv(V3 a, V3 b) { return {a.x * b.x, a.y * b.y, a.z * b.z}; }
static inline float dot(V3 a, V3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
static inline V3 cross(V3 a, V3 b) { return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x}; }
static inline float len(V3 a) { return std::sqrt(dot(a, a)); }
static inline V3 nrm(V3 a) { return a * (1.f / len(a)); }
static inline float clamp01(float v) { return v < 0 ? 0 : (v > 1 ? 1 : v); }

struct M4 { float m[4][4]; };
static M4 ident() { M4 r{}; for (int i = 0; i < 4; i++) r.m[i][i] = 1; return r; }
static M4 operator*(const M4& a, const M4& b) {
    M4 r{};
    for (int i = 0; i < 4; i++) for (int j = 0; j < 4; j++) {
        float s = 0; for (int k = 0; k < 4; k++) s += a.m[i][k] * b.m[k][j]; r.m[i][j] = s;
    }
    return r;
}
static V4 operator*(const M4& a, V4 v) {
    V4 r;
    r.x = a.m[0][0] * v.x + a.m[0][1] * v.y + a.m[0][2] * v.z + a.m[0][3] * v.w;
    r.y = a.m[1][0] * v.x + a.m[1][1] * v.y + a.m[1][2] * v.z + a.m[1][3] * v.w;
    r.z = a.m[2][0] * v.x + a.m[2][1] * v.y + a.m[2][2] * v.z + a.m[2][3] * v.w;
    r.w = a.m[3][0] * v.x + a.m[3][1] * v.y + a.m[3][2] * v.z + a.m[3][3] * v.w;
    return r;
}
static V3 mulDir(const M4& a, V3 v) {
    return {a.m[0][0] * v.x + a.m[0][1] * v.y + a.m[0][2] * v.z,
            a.m[1][0] * v.x + a.m[1][1] * v.y + a.m[1][2] * v.z,
            a.m[2][0] * v.x + a.m[2][1] * v.y + a.m[2][2] * v.z};
}
static M4 translate(V3 t) { M4 r = ident(); r.m[0][3] = t.x; r.m[1][3] = t.y; r.m[2][3] = t.z; return r; }
static M4 rotY(float a) { M4 r = ident(); float c = cosf(a), s = sinf(a); r.m[0][0] = c; r.m[0][2] = s; r.m[2][0] = -s; r.m[2][2] = c; return r; }
static M4 scale(float s) { M4 r = ident(); r.m[0][0] = r.m[1][1] = r.m[2][2] = s; return r; }
// 右手坐标系，相机看向 -Z（OpenGL 习惯）
static M4 lookAt(V3 eye, V3 at, V3 up) {
    V3 f = nrm(at - eye), s = nrm(cross(f, up)), u = cross(s, f);
    M4 r = ident();
    r.m[0][0] = s.x;  r.m[0][1] = s.y;  r.m[0][2] = s.z;  r.m[0][3] = -dot(s, eye);
    r.m[1][0] = u.x;  r.m[1][1] = u.y;  r.m[1][2] = u.z;  r.m[1][3] = -dot(u, eye);
    r.m[2][0] = -f.x; r.m[2][1] = -f.y; r.m[2][2] = -f.z; r.m[2][3] = dot(f, eye);
    return r;
}
enum DepthMode { DM_D3D = 0, DM_REV = 1, DM_GL = 2, DM_REVINF = 3 };
static const char* kDM[] = {"D3D [0,1]", "Reversed-Z", "OpenGL [-1,1]", "Reversed 无限远"};
static M4 perspective(float fovyDeg, float aspect, float n, float f, int mode) {
    M4 r{};
    float t = 1.f / tanf(fovyDeg * (float)M_PI / 360.f);
    r.m[0][0] = t / aspect; r.m[1][1] = t; r.m[3][2] = -1;  // w_clip = -z_view
    switch (mode) {
    case DM_D3D:    r.m[2][2] = f / (n - f);       r.m[2][3] = n * f / (n - f); break;
    case DM_REV:    r.m[2][2] = n / (f - n);       r.m[2][3] = n * f / (f - n); break;
    case DM_GL:     r.m[2][2] = (f + n) / (n - f); r.m[2][3] = 2 * f * n / (n - f); break;
    case DM_REVINF: r.m[2][2] = 0;                 r.m[2][3] = n; break;
    }
    return r;
}
static M4 ortho(float l, float r_, float b, float t, float n, float f) {
    M4 r = ident();
    r.m[0][0] = 2 / (r_ - l); r.m[0][3] = -(r_ + l) / (r_ - l);
    r.m[1][1] = 2 / (t - b);  r.m[1][3] = -(t + b) / (t - b);
    r.m[2][2] = -1 / (f - n); r.m[2][3] = -n / (f - n);  // z_view=-n -> 0, -f -> 1
    return r;
}
static double nowMs() { using namespace std::chrono; return duration<double, std::milli>(steady_clock::now().time_since_epoch()).count(); }

// ============================== 图像 / 纹理 ==============================
struct Img {
    int w = 0, h = 0; std::vector<V3> p;
    Img() {}
    Img(int W, int H, V3 c = {0, 0, 0}) : w(W), h(H), p((size_t)W * H, c) {}
    V3& at(int x, int y) { return p[(size_t)y * w + x]; }
    const V3& at(int x, int y) const { return p[(size_t)y * w + x]; }
};
static u8 toU8(float v) { return (u8)(clamp01(v) * 255.f + 0.5f); }
static void savePPM(const Img& im, const char* name) {
    std::string path = std::string("out/") + name + ".ppm";
    FILE* f = fopen(path.c_str(), "wb"); if (!f) return;
    fprintf(f, "P6\n%d %d\n255\n", im.w, im.h);
    for (auto& c : im.p) { u8 b[3] = {toU8(c.x), toU8(c.y), toU8(c.z)}; fwrite(b, 1, 3, f); }
    fclose(f);
}
static void saveZoom(const Img& im, int x0, int y0, int w, int h, int s, const char* name) {
    Img z(w * s, h * s);
    for (int y = 0; y < h * s; y++) for (int x = 0; x < w * s; x++) z.at(x, y) = im.at(x0 + x / s, y0 + y / s);
    savePPM(z, name);
}
static double rmse(const Img& a, const Img& b, const std::vector<char>* mask = nullptr) {
    double s = 0; long long n = 0;
    for (size_t i = 0; i < a.p.size(); i++) {
        if (mask && !(*mask)[i]) continue;
        V3 d = a.p[i] - b.p[i]; s += (d.x * d.x + d.y * d.y + d.z * d.z) / 3.0; n++;
    }
    return n ? std::sqrt(s / n) : 0;
}

// 极简纹理缓存模拟：4x4 texel 为一条 64B 缓存行，256 行（16KB）直接映射
struct TexCache {
    static const int LINES = 256; uint64_t tag[LINES]; bool valid[LINES]; long long hit = 0, miss = 0; bool on = false;
    void reset() { memset(valid, 0, sizeof(valid)); hit = miss = 0; }
    void touch(int lvl, int x, int y) {
        if (!on) return;
        uint64_t id = ((uint64_t)lvl << 48) | ((uint64_t)(y >> 2) << 24) | (uint64_t)(x >> 2);
        int idx = (int)((id * 0x9E3779B97F4A7C15ull) >> 56);
        if (valid[idx] && tag[idx] == id) hit++; else { miss++; valid[idx] = true; tag[idx] = id; }
    }
};
static TexCache g_cache;
static long long g_texFetch = 0;

struct Tex {
    std::vector<Img> mips;
    explicit Tex(Img base) {
        mips.push_back(std::move(base));
        while (mips.back().w > 1 || mips.back().h > 1) {
            const Img& s = mips.back();
            int w = std::max(1, s.w / 2), h = std::max(1, s.h / 2);
            Img d(w, h);
            for (int y = 0; y < h; y++) for (int x = 0; x < w; x++) {
                V3 c = s.at(std::min(2 * x, s.w - 1), std::min(2 * y, s.h - 1)) + s.at(std::min(2 * x + 1, s.w - 1), std::min(2 * y, s.h - 1))
                     + s.at(std::min(2 * x, s.w - 1), std::min(2 * y + 1, s.h - 1)) + s.at(std::min(2 * x + 1, s.w - 1), std::min(2 * y + 1, s.h - 1));
                d.at(x, y) = c * 0.25f;
            }
            mips.push_back(std::move(d));
        }
    }
    int maxLod() const { return (int)mips.size() - 1; }
    V3 fetch(int l, int x, int y) const {
        const Img& m = mips[l];
        x &= (m.w - 1); y &= (m.h - 1);  // 2 的幂，Wrap 寻址
        g_cache.touch(l, x, y); g_texFetch++;
        return m.at(x, y);
    }
    V3 point(float u, float v, int l = 0) const {
        const Img& m = mips[l];
        return fetch(l, (int)floorf(u * m.w), (int)floorf(v * m.h));
    }
    V3 bilinear(float u, float v, int l = 0) const {
        const Img& m = mips[l];
        float fx = u * m.w - 0.5f, fy = v * m.h - 0.5f;
        int x0 = (int)floorf(fx), y0 = (int)floorf(fy); float tx = fx - x0, ty = fy - y0;
        V3 a = fetch(l, x0, y0), b = fetch(l, x0 + 1, y0), c = fetch(l, x0, y0 + 1), d = fetch(l, x0 + 1, y0 + 1);
        return (a * (1 - tx) + b * tx) * (1 - ty) + (c * (1 - tx) + d * tx) * ty;
    }
    V3 trilinear(float u, float v, float lod) const {
        if (lod <= 0) return bilinear(u, v, 0);
        if (lod >= maxLod()) return bilinear(u, v, maxLod());
        int l0 = (int)lod; float t = lod - l0;
        return bilinear(u, v, l0) * (1 - t) + bilinear(u, v, l0 + 1) * t;
    }
    float lodFor(float dudx, float dvdx, float dudy, float dvdy) const {
        float W = (float)mips[0].w, H = (float)mips[0].h;
        float lx = std::sqrt(dudx * W * dudx * W + dvdx * H * dvdx * H), ly = std::sqrt(dudy * W * dudy * W + dvdy * H * dvdy * H);
        return log2f(std::max(std::max(lx, ly), 1e-8f));
    }
    // 由屏幕空间导数 (du/dx, dv/dx, du/dy, dv/dy) 求 LOD；aniso>1 时沿长轴多次采样
    V3 sampleGrad(float u, float v, float dudx, float dvdx, float dudy, float dvdy, int aniso, float* lodOut = nullptr) const {
        float W = (float)mips[0].w, H = (float)mips[0].h;
        float lx = std::sqrt(dudx * W * dudx * W + dvdx * H * dvdx * H);
        float ly = std::sqrt(dudy * W * dudy * W + dvdy * H * dvdy * H);
        if (aniso <= 1) {
            float lod = log2f(std::max(std::max(lx, ly), 1e-8f));
            if (lodOut) *lodOut = lod;
            return trilinear(u, v, lod);
        }
        float major = std::max(lx, ly), minor = std::max(std::min(lx, ly), 1e-8f);
        int N = std::min(aniso, (int)ceilf(major / minor)); if (N < 1) N = 1;
        float lod = log2f(std::max(major / N, 1e-8f));
        if (lodOut) *lodOut = lod;
        float au = lx > ly ? dudx : dudy, av = lx > ly ? dvdx : dvdy;
        V3 acc{0, 0, 0};
        for (int i = 0; i < N; i++) { float t = (i + 0.5f) / N - 0.5f; acc = acc + trilinear(u + au * t, v + av * t, lod); }
        return acc * (1.f / N);
    }
};
static Img makeChecker(int size, int cell, V3 a, V3 b) {
    Img im(size, size);
    for (int y = 0; y < size; y++) for (int x = 0; x < size; x++) im.at(x, y) = (((x / cell) + (y / cell)) & 1) ? a : b;
    return im;
}

// ============================== 管线状态 ==============================
const int MAXA = 10;
struct VOut { V4 c; float a[MAXA]; };
struct Tri {  // 三角形 Setup 结果：屏幕坐标、深度、1/w、属性
    float sx[3], sy[3], z[3], iw[3], a[3][MAXA]; int na; bool persp;
    void interp(float px, float py, float* out) const {
        float e0 = (sx[2] - sx[1]) * (py - sy[1]) - (sy[2] - sy[1]) * (px - sx[1]);
        float e1 = (sx[0] - sx[2]) * (py - sy[2]) - (sy[0] - sy[2]) * (px - sx[2]);
        float e2 = (sx[1] - sx[0]) * (py - sy[0]) - (sy[1] - sy[0]) * (px - sx[0]);
        float s = e0 + e1 + e2; float b0 = e0 / s, b1 = e1 / s, b2 = e2 / s;
        if (persp) {  // 透视校正：对 attr/w 与 1/w 线性插值，再相除
            float w0 = b0 * iw[0], w1 = b1 * iw[1], w2 = b2 * iw[2]; float r = 1.f / (w0 + w1 + w2);
            b0 = w0 * r; b1 = w1 * r; b2 = w2 * r;
        }
        for (int i = 0; i < na; i++) out[i] = b0 * a[0][i] + b1 * a[1][i] + b2 * a[2][i];
    }
};
struct Frag { int x, y; float px, py; float a[MAXA]; const Tri* t; bool front; };
using FS = std::function<V4(const Frag&)>;
using VS = std::function<void(int, VOut&)>;

enum DFunc { DF_LESS, DF_GREATER, DF_LEQUAL, DF_GEQUAL, DF_EQUAL, DF_ALWAYS };
enum BlendMode { BL_NONE, BL_ALPHA, BL_PREMUL, BL_ADD };
struct State {
    bool cull = true, topLeft = true, persp = true, depthTest = true, depthWrite = true, colorWrite = true;
    bool earlyZ = true, ssaa = false, quant24 = false;
    int dfunc = DF_LESS, blend = BL_NONE, clip = DM_D3D;
};
struct Stats {
    long long trisIn = 0, culled = 0, degenerate = 0, clipped = 0, clipOut = 0, rasterTris = 0;
    long long frags = 0, fs = 0, lanes = 0, zfail = 0, zReads = 0, zWrites = 0, cWrites = 0;
};
struct Target {
    int W, H, S; std::vector<float> depth; std::vector<V4> color; std::vector<uint16_t> over;
    std::vector<int> quadStamp; int quadW, triCounter = 0; std::vector<int> offX, offY; Stats st;
    Target(int w, int h, int s = 1) : W(w), H(h), S(s), depth((size_t)w * h * s), color((size_t)w * h * s), over((size_t)w * h),
        quadStamp((size_t)((w + 1) / 2) * ((h + 1) / 2), -1), quadW((w + 1) / 2), offX(s), offY(s) {
        if (s == 1) { offX[0] = offY[0] = 128; }
        else if (s == 4) {  // D3D 标准 4x 旋转栅格（单位 1/16 像素）
            int px[4] = {-2, 6, -6, 2}, py[4] = {-6, -2, 2, 6};
            for (int i = 0; i < 4; i++) { offX[i] = 128 + px[i] * 16; offY[i] = 128 + py[i] * 16; }
        } else {  // n x n 规则栅格（用于参考图 / SSAA 真值）
            int n = (int)std::lround(std::sqrt((double)s));
            for (int j = 0; j < n; j++) for (int i = 0; i < n; i++) { offX[j * n + i] = (int)((i + 0.5) * 256 / n); offY[j * n + i] = (int)((j + 0.5) * 256 / n); }
        }
    }
    void clear(V4 c, float z) {
        std::fill(color.begin(), color.end(), c); std::fill(depth.begin(), depth.end(), z);
        std::fill(over.begin(), over.end(), 0); st = Stats{};
    }
    Img resolve() const {
        Img im(W, H);
        for (int i = 0; i < W * H; i++) {
            V3 a{0, 0, 0}; for (int s = 0; s < S; s++) { const V4& c = color[(size_t)i * S + s]; a = a + V3{c.x, c.y, c.z}; }
            im.p[i] = a * (1.f / S);
        }
        return im;
    }
};
static inline bool depthPass(int f, float z, float d) {
    switch (f) {
    case DF_LESS: return z < d; case DF_GREATER: return z > d; case DF_LEQUAL: return z <= d;
    case DF_GEQUAL: return z >= d; case DF_EQUAL: return z == d; default: return true;
    }
}
static inline float q24(float z) { z = clamp01(z); return (float)(std::floor((double)z * 16777215.0 + 0.5) / 16777215.0); }
static inline V4 blendOp(int m, V4 s, V4 d) {
    switch (m) {
    case BL_ALPHA:  return {s.x * s.w + d.x * (1 - s.w), s.y * s.w + d.y * (1 - s.w), s.z * s.w + d.z * (1 - s.w), s.w + d.w * (1 - s.w)};
    case BL_PREMUL: return {s.x + d.x * (1 - s.w), s.y + d.y * (1 - s.w), s.z + d.z * (1 - s.w), s.w + d.w * (1 - s.w)};
    case BL_ADD:    return {s.x + d.x, s.y + d.y, s.z + d.z, s.w + d.w};
    default: return s;
    }
}

// ============================== 光栅化（核心） ==============================
static void rasterTri(Target& T, const State& s, const VOut* v, int na, const FS& fs) {
    const int SUB = 8; const i64 ONE = (i64)1 << SUB;  // 8 位亚像素精度（定点）
    Tri t; t.na = na; t.persp = s.persp;
    i64 X[3], Y[3];
    for (int i = 0; i < 3; i++) {  // 透视除法 + 视口变换 + 吸附到定点网格
        float iw = 1.f / v[i].c.w;
        float nx = v[i].c.x * iw, ny = v[i].c.y * iw, nz = v[i].c.z * iw;
        if (s.clip == DM_GL) nz = nz * 0.5f + 0.5f;  // glDepthRange(0,1)
        double sx = ((double)nx * 0.5 + 0.5) * T.W, sy = (0.5 - 0.5 * (double)ny) * T.H;
        X[i] = (i64)std::llround(sx * ONE); Y[i] = (i64)std::llround(sy * ONE);
        t.sx[i] = (float)((double)X[i] / ONE); t.sy[i] = (float)((double)Y[i] / ONE);
        t.z[i] = nz; t.iw[i] = iw;
        for (int k = 0; k < na; k++) t.a[i][k] = v[i].a[k];
    }
    i64 area = (X[1] - X[0]) * (Y[2] - Y[0]) - (Y[1] - Y[0]) * (X[2] - X[0]);
    if (area == 0) { T.st.degenerate++; return; }
    bool front = area < 0;  // NDC 中逆时针 = 屏幕(y 向下)中有符号面积为负
    if (s.cull && !front) { T.st.culled++; return; }
    if (area < 0) {
        std::swap(X[1], X[2]); std::swap(Y[1], Y[2]); std::swap(t.sx[1], t.sx[2]); std::swap(t.sy[1], t.sy[2]);
        std::swap(t.z[1], t.z[2]); std::swap(t.iw[1], t.iw[2]);
        for (int k = 0; k < na; k++) std::swap(t.a[1][k], t.a[2][k]);
        area = -area;
    }
    T.st.rasterTris++;
    i64 minX = std::min({X[0], X[1], X[2]}), maxX = std::max({X[0], X[1], X[2]});
    i64 minY = std::min({Y[0], Y[1], Y[2]}), maxY = std::max({Y[0], Y[1], Y[2]});
    int x0 = (int)std::max<i64>(0, minX >> SUB), x1 = (int)std::min<i64>(T.W - 1, (maxX >> SUB) + 1);
    int y0 = (int)std::max<i64>(0, minY >> SUB), y1 = (int)std::min<i64>(T.H - 1, (maxY >> SUB) + 1);
    if (x0 > x1 || y0 > y1) return;
    // Top-Left 规则：非"上边/左边"上的样本点（边函数==0）不算覆盖
    auto isTL = [&](int a, int b) { i64 dx = X[b] - X[a], dy = Y[b] - Y[a]; return (dy == 0 && dx > 0) || dy < 0; };
    i64 b0 = (s.topLeft && !isTL(1, 2)) ? -1 : 0, b1 = (s.topLeft && !isTL(2, 0)) ? -1 : 0, b2 = (s.topLeft && !isTL(0, 1)) ? -1 : 0;
    auto E = [&](int a, int b, i64 px, i64 py) { return (X[b] - X[a]) * (py - Y[a]) - (Y[b] - Y[a]) * (px - X[a]); };
    int triId = ++T.triCounter;
    float invArea = 1.f / (float)area;
    static bool cov[256]; static float zs[256];
    for (int py = y0; py <= y1; py++) for (int px = x0; px <= x1; px++) {
        bool any = false;
        for (int si = 0; si < T.S; si++) {
            i64 SX = (i64)px * ONE + T.offX[si], SY = (i64)py * ONE + T.offY[si];
            i64 w0 = E(1, 2, SX, SY), w1 = E(2, 0, SX, SY), w2 = E(0, 1, SX, SY);
            cov[si] = (w0 + b0 >= 0) && (w1 + b1 >= 0) && (w2 + b2 >= 0);
            if (cov[si]) { any = true; zs[si] = ((float)w0 * t.z[0] + (float)w1 * t.z[1] + (float)w2 * t.z[2]) * invArea; if (s.quant24) zs[si] = q24(zs[si]); }
        }
        if (!any) continue;
        T.st.frags++;
        int q = (py >> 1) * T.quadW + (px >> 1);
        if (T.quadStamp[q] != triId) { T.quadStamp[q] = triId; T.st.lanes += 4; }  // GPU 以 2x2 quad 为单位跑 PS
        size_t base = ((size_t)py * T.W + px) * T.S;
        if (s.depthTest && s.earlyZ) {
            bool alive = false;
            for (int si = 0; si < T.S; si++) if (cov[si]) { T.st.zReads++; if (!depthPass(s.dfunc, zs[si], T.depth[base + si])) cov[si] = false; else alive = true; }
            if (!alive) { T.st.zfail++; continue; }
        }
        Frag f; f.x = px; f.y = py; f.t = &t; f.front = front;
        V4 shared{0, 0, 0, 1}; static V4 per[256];
        if (fs) {
            if (!s.ssaa) {  // 每像素着色一次（含 MSAA）
                f.px = px + 0.5f; f.py = py + 0.5f; t.interp(f.px, f.py, f.a); shared = fs(f); T.st.fs++; T.over[(size_t)py * T.W + px]++;
            } else {        // SSAA：每个样本单独着色
                for (int si = 0; si < T.S; si++) if (cov[si]) {
                    f.px = px + T.offX[si] / 256.f; f.py = py + T.offY[si] / 256.f; t.interp(f.px, f.py, f.a); per[si] = fs(f); T.st.fs++; T.over[(size_t)py * T.W + px]++;
                }
            }
        }
        if (s.depthTest && !s.earlyZ) {
            bool alive = false;
            for (int si = 0; si < T.S; si++) if (cov[si]) { T.st.zReads++; if (!depthPass(s.dfunc, zs[si], T.depth[base + si])) cov[si] = false; else alive = true; }
            if (!alive) { T.st.zfail++; continue; }
        }
        for (int si = 0; si < T.S; si++) if (cov[si]) {
            if (s.depthWrite) { T.depth[base + si] = zs[si]; T.st.zWrites++; }
            if (s.colorWrite && fs) { V4 c = s.ssaa ? per[si] : shared; T.color[base + si] = blendOp(s.blend, c, T.color[base + si]); T.st.cWrites++; }
        }
    }
}
static VOut lerpV(const VOut& a, const VOut& b, float t, int na) {
    VOut r; r.c = {a.c.x + (b.c.x - a.c.x) * t, a.c.y + (b.c.y - a.c.y) * t, a.c.z + (b.c.z - a.c.z) * t, a.c.w + (b.c.w - a.c.w) * t};
    for (int i = 0; i < na; i++) r.a[i] = a.a[i] + (b.a[i] - a.a[i]) * t;
    return r;
}
static float nearDist(int mode, const V4& c) {
    switch (mode) { case DM_D3D: return c.z; case DM_GL: return c.z + c.w; default: return c.w - c.z; }
}
// 齐次裁剪空间中对近平面做 Sutherland–Hodgman 裁剪（x/y 交给保护带 + 包围盒钳制）
static int clipNear(const State& s, const VOut* in, VOut* out, int na) {
    int n = 0;
    for (int i = 0; i < 3; i++) {
        const VOut& a = in[i]; const VOut& b = in[(i + 1) % 3];
        float da = nearDist(s.clip, a.c), db = nearDist(s.clip, b.c);
        if (da >= 0) out[n++] = a;
        if ((da >= 0) != (db >= 0)) out[n++] = lerpV(a, b, da / (da - db), na);
    }
    return n;
}
static void drawTris(Target& T, const State& s, int nv, const uint32_t* idx, int ni, int na, const VS& vs, const FS& fs) {
    std::vector<VOut> vo(nv);
    for (int i = 0; i < nv; i++) vs(i, vo[i]);  // 每个顶点只着色一次（理想的 Post-T&L 缓存）
    for (int i = 0; i + 2 < ni; i += 3) {
        T.st.trisIn++;
        VOut tri[3] = {vo[idx[i]], vo[idx[i + 1]], vo[idx[i + 2]]};
        float d0 = nearDist(s.clip, tri[0].c), d1 = nearDist(s.clip, tri[1].c), d2 = nearDist(s.clip, tri[2].c);
        if (d0 >= 0 && d1 >= 0 && d2 >= 0) { rasterTri(T, s, tri, na, fs); continue; }
        if (d0 < 0 && d1 < 0 && d2 < 0) { T.st.clipOut++; continue; }
        T.st.clipped++;
        VOut poly[4]; int n = clipNear(s, tri, poly, na);
        for (int k = 1; k + 1 < n; k++) { VOut t2[3] = {poly[0], poly[k], poly[k + 1]}; rasterTri(T, s, t2, na, fs); }
    }
}

// ============================== 网格 ==============================
struct Mesh { std::vector<V3> p, n; std::vector<V2> uv; std::vector<uint32_t> idx; };
static void fixWinding(Mesh& m) {  // 保证"从外面看逆时针"
    for (size_t i = 0; i + 2 < m.idx.size(); i += 3) {
        uint32_t a = m.idx[i], b = m.idx[i + 1], c = m.idx[i + 2];
        V3 fn = cross(m.p[b] - m.p[a], m.p[c] - m.p[a]);
        if (dot(fn, m.n[a] + m.n[b] + m.n[c]) < 0) std::swap(m.idx[i + 1], m.idx[i + 2]);
    }
}
static Mesh makePlane(float x0, float x1, float z0, float z1, float y, int nx, int nz, float uvScale) {
    Mesh m;
    for (int j = 0; j <= nz; j++) for (int i = 0; i <= nx; i++) {
        float x = x0 + (x1 - x0) * i / nx, z = z0 + (z1 - z0) * j / nz;
        m.p.push_back({x, y, z}); m.n.push_back({0, 1, 0}); m.uv.push_back({x * uvScale, z * uvScale});
    }
    for (int j = 0; j < nz; j++) for (int i = 0; i < nx; i++) {
        uint32_t a = j * (nx + 1) + i, b = a + 1, c = a + nx + 1, d = c + 1;
        m.idx.insert(m.idx.end(), {a, b, d, a, d, c});
    }
    fixWinding(m); return m;
}
static Mesh makeCube() {
    Mesh m; V3 N[6] = {{1, 0, 0}, {-1, 0, 0}, {0, 1, 0}, {0, -1, 0}, {0, 0, 1}, {0, 0, -1}};
    for (int f = 0; f < 6; f++) {
        V3 n = N[f], u = fabsf(n.y) > 0.5f ? V3{1, 0, 0} : V3{0, 1, 0}, v = cross(n, u);
        uint32_t b = (uint32_t)m.p.size();
        for (int k = 0; k < 4; k++) {
            float su = (k & 1) ? 0.5f : -0.5f, sv = (k & 2) ? 0.5f : -0.5f;
            m.p.push_back(n * 0.5f + u * su + v * sv); m.n.push_back(n); m.uv.push_back({su + 0.5f, sv + 0.5f});
        }
        m.idx.insert(m.idx.end(), {b, b + 1, b + 3, b, b + 3, b + 2});
    }
    fixWinding(m); return m;
}
static Mesh makeSphere(float r, int slices, int stacks) {
    Mesh m;
    for (int i = 0; i <= stacks; i++) for (int j = 0; j <= slices; j++) {
        float th = (float)M_PI * i / stacks, ph = 2 * (float)M_PI * j / slices;
        V3 n = {sinf(th) * cosf(ph), cosf(th), sinf(th) * sinf(ph)};
        m.p.push_back(n * r); m.n.push_back(n); m.uv.push_back({(float)j / slices, (float)i / stacks});
    }
    for (int i = 0; i < stacks; i++) for (int j = 0; j < slices; j++) {
        uint32_t a = i * (slices + 1) + j, b = a + 1, c = a + slices + 1, d = c + 1;
        if (i != 0) m.idx.insert(m.idx.end(), {a, c, b});
        if (i != stacks - 1) m.idx.insert(m.idx.end(), {b, c, d});
    }
    fixWinding(m); return m;
}
// 标准顶点着色器：输出 世界坐标(3) + 法线(3) + uv(2)
static VS stdVS(const Mesh& m, const M4& model, const M4& vp) {
    return [&m, model, vp](int i, VOut& o) {
        V3 p = m.p[i]; V4 w = model * V4{p.x, p.y, p.z, 1};
        o.c = vp * w; V3 n = nrm(mulDir(model, m.n[i]));
        o.a[0] = w.x; o.a[1] = w.y; o.a[2] = w.z; o.a[3] = n.x; o.a[4] = n.y; o.a[5] = n.z; o.a[6] = m.uv[i].x; o.a[7] = m.uv[i].y;
    };
}
static void drawMesh(Target& T, const State& s, const Mesh& m, const M4& model, const M4& vp, const FS& fs) {
    drawTris(T, s, (int)m.p.size(), m.idx.data(), (int)m.idx.size(), 8, stdVS(m, model, vp), fs);
}
static void line() { printf("--------------------------------------------------------------------------------\n"); }
static void title(const char* t) { printf("\n================================================================================\n%s\n================================================================================\n", t); }

// ============================== E1：MVP 逐级追踪 ==============================
static void E1() {
    title("E1  一个顶点的旅程：Model -> World -> View -> Clip -> NDC -> Screen");
    int W = 640, H = 360;
    M4 model = translate({0, 0, -5}) * rotY(30.f * (float)M_PI / 180.f);
    M4 view = lookAt({0, 1, 3}, {0, 0, -5}, {0, 1, 0});
    V4 lp = {0.5f, 0.5f, 0.5f, 1};
    V4 wp = model * lp, vp = view * wp;
    printf("局部坐标 (立方体一角)            : (%.3f, %.3f, %.3f, 1)\n", lp.x, lp.y, lp.z);
    printf("世界坐标 = Model * p             : (%.3f, %.3f, %.3f, 1)   [平移(0,0,-5)·绕Y转30°]\n", wp.x, wp.y, wp.z);
    printf("观察坐标 = View * world          : (%.3f, %.3f, %.3f, 1)   [相机(0,1,3)看向(0,0,-5)，-Z 为前方]\n", vp.x, vp.y, vp.z);
    for (int mode : {DM_D3D, DM_GL, DM_REV}) {
        M4 proj = perspective(60, (float)W / H, 0.1f, 100.f, mode);
        V4 c = proj * vp; float nx = c.x / c.w, ny = c.y / c.w, nz = c.z / c.w;
        float sx = (nx * 0.5f + 0.5f) * W, sy = (0.5f - 0.5f * ny) * H;
        float depth = mode == DM_GL ? nz * 0.5f + 0.5f : nz;
        printf("[%-14s] 裁剪坐标 (%.3f, %.3f, %.4f, w=%.3f) -> NDC (%.4f, %.4f, %.6f) -> 屏幕 (%.2f, %.2f) 深度缓冲值 %.6f\n",
               kDM[mode], c.x, c.y, c.z, c.w, nx, ny, nz, sx, sy, depth);
    }
    printf("观察：w_clip 恰好等于 -z_view = %.3f（相机到该点沿视线方向的距离），透视除法就是\"除以距离\"。\n", -vp.z);
}

// ============================== E2：近平面裁剪 ==============================
static void E2() {
    title("E2  一个顶点在相机背后：为什么必须在齐次空间裁剪（不能先做透视除法）");
    int W = 640, H = 360; M4 proj = perspective(60, (float)W / H, 0.1f, 100.f, DM_D3D);
    V3 vv[3] = {{-1, -0.5f, -2}, {1, -0.5f, -2}, {0.5f, -0.5f, 1}};
    VOut tri[3];
    for (int i = 0; i < 3; i++) {
        tri[i].c = proj * V4{vv[i].x, vv[i].y, vv[i].z, 1};
        tri[i].a[0] = vv[i].x; tri[i].a[1] = vv[i].y; tri[i].a[2] = vv[i].z;
        V4 c = tri[i].c; float sx = (c.x / c.w * 0.5f + 0.5f) * W, sy = (0.5f - 0.5f * c.y / c.w) * H;
        printf("顶点%c 观察空间(%5.2f,%5.2f,%5.2f)  clip=(%6.3f,%6.3f,%7.3f, w=%6.3f)  朴素除法->屏幕(%8.1f,%8.1f)%s\n",
               'A' + i, vv[i].x, vv[i].y, vv[i].z, c.x, c.y, c.z, c.w, sx, sy, c.w < 0 ? "  <-- w<0：被翻到屏幕另一侧！" : "");
    }
    State s; VOut poly[4]; int n = clipNear(s, tri, poly, 3);
    printf("近平面裁剪后得到 %d 个顶点（三角形变成四边形 -> 扇形拆成 %d 个三角形）：\n", n, n - 2);
    for (int i = 0; i < n; i++) {
        V4 c = poly[i].c; float sx = (c.x / c.w * 0.5f + 0.5f) * W, sy = (0.5f - 0.5f * c.y / c.w) * H;
        printf("  P%d 观察空间(%6.3f,%6.3f,%6.3f)  w=%.3f  屏幕(%7.1f,%7.1f)  深度 %.4f\n", i, poly[i].a[0], poly[i].a[1], poly[i].a[2], c.w, sx, sy, c.z / c.w);
    }
    Target T(W, H); T.clear({0, 0, 0, 1}, 1.f); State st; st.cull = false;
    uint32_t idx[3] = {0, 1, 2}; VOut src[3] = {tri[0], tri[1], tri[2]};
    drawTris(T, st, 3, idx, 3, 3, [&](int i, VOut& o) { o = src[i]; }, [](const Frag&) { return V4{1, 1, 1, 1}; });
    printf("实际光栅化：输入 1 个三角形，被裁剪 %lld 个，送入光栅化 %lld 个，覆盖 %lld 个像素（地面从脚下一直铺到远处）。\n", T.st.clipped, T.st.rasterTris, T.st.frags);
}

// ============================== E3：背面剔除 ==============================
static void E3() {
    title("E3  背面剔除到底剔掉多少？（球体 64x32，屏幕空间有符号面积判断）");
    Mesh sph = makeSphere(1.f, 64, 32);
    printf("球体三角形数：%zu\n", sph.idx.size() / 3);
    printf("%-10s %-10s %-10s %-10s %-10s %-14s\n", "相机距离", "输入", "剔除", "退化", "剔除率", "理论 1-(1-r/d)/2");
    for (float d : {1.5f, 3.f, 10.f, 100.f}) {
        float fov = 2.f * asinf(1.f / d) * 180.f / (float)M_PI * 1.15f;
        Target T(512, 512); T.clear({0, 0, 0, 1}, 1.f); State s; s.colorWrite = false;
        M4 vp = perspective(fov, 1.f, 0.05f, 1000.f, DM_D3D) * lookAt({0, 0.3f, d}, {0, 0, 0}, {0, 1, 0});
        drawMesh(T, s, sph, ident(), vp, nullptr);
        printf("%-10.1f %-10lld %-10lld %-10lld %9.1f%% %13.1f%%\n", d, T.st.trisIn, T.st.culled, T.st.degenerate,
               100.0 * T.st.culled / T.st.trisIn, 100.0 * (1 - (1 - 1.0 / d) / 2));
    }
    printf("结论：从外部看凸物体，能看到的部分不到一半；离得越近剔除率越高，远处趋近 50%%。\n");
}

// ============================== E4：填充规则 / 定点 vs 浮点 ==============================
static i64 rnd256(double v) { return (i64)std::llround(v * 256.0); }
static void coverTri(std::vector<int>& cov, int W, int H, V2 a, V2 b, V2 c, int rule, bool fixedPt, float off) {
    auto isTLf = [](double dx, double dy) { return (dy == 0 && dx > 0) || dy < 0; };
    if (fixedPt) {
        i64 X[3] = {rnd256(a.x + off), rnd256(b.x + off), rnd256(c.x + off)}, Y[3] = {rnd256(a.y + off), rnd256(b.y + off), rnd256(c.y + off)};
        i64 area = (X[1] - X[0]) * (Y[2] - Y[0]) - (Y[1] - Y[0]) * (X[2] - X[0]);
        if (area == 0) return; if (area < 0) { std::swap(X[1], X[2]); std::swap(Y[1], Y[2]); }
        auto E = [&](int i, int j, i64 px, i64 py) { return (X[j] - X[i]) * (py - Y[i]) - (Y[j] - Y[i]) * (px - X[i]); };
        auto bias = [&](int i, int j) -> i64 { if (rule == 0) return 0; if (rule == 1) return -1; return isTLf((double)(X[j] - X[i]), (double)(Y[j] - Y[i])) ? 0 : -1; };
        i64 bb0 = bias(1, 2), bb1 = bias(2, 0), bb2 = bias(0, 1);
        i64 O = rnd256(off);
        for (int py = 0; py < H; py++) for (int px = 0; px < W; px++) {
            i64 SX = (i64)px * 256 + 128 + O, SY = (i64)py * 256 + 128 + O;
            if (E(1, 2, SX, SY) + bb0 >= 0 && E(2, 0, SX, SY) + bb1 >= 0 && E(0, 1, SX, SY) + bb2 >= 0) cov[py * W + px]++;
        }
    } else {
        float X[3] = {a.x + off, b.x + off, c.x + off}, Y[3] = {a.y + off, b.y + off, c.y + off};
        float area = (X[1] - X[0]) * (Y[2] - Y[0]) - (Y[1] - Y[0]) * (X[2] - X[0]);
        if (area == 0) return; if (area < 0) { std::swap(X[1], X[2]); std::swap(Y[1], Y[2]); }
        auto E = [&](int i, int j, float px, float py) { return (X[j] - X[i]) * (py - Y[i]) - (Y[j] - Y[i]) * (px - X[i]); };
        auto ok = [&](float w, int i, int j) { if (w > 0) return true; if (w < 0) return false; return rule == 0 || (rule == 2 && isTLf(X[j] - X[i], Y[j] - Y[i])); };
        for (int py = 0; py < H; py++) for (int px = 0; px < W; px++) {
            float SX = px + 0.5f + off, SY = py + 0.5f + off;
            if (ok(E(1, 2, SX, SY), 1, 2) && ok(E(2, 0, SX, SY), 2, 0) && ok(E(0, 1, SX, SY), 0, 1)) cov[py * W + px]++;
        }
    }
}
static void E4() {
    title("E4  共享边上的像素归谁？—— 填充规则（Top-Left）与定点光栅化");
    const int W = 64, H = 64, N = 15;
    auto run = [&](bool jitter, int rule, bool fixedPt, float off, int& holes, int& dbl) {
        std::mt19937 rng(7); std::uniform_real_distribution<float> U(-1.5f, 1.5f);
        std::vector<V2> v((N + 1) * (N + 1));
        for (int j = 0; j <= N; j++) for (int i = 0; i <= N; i++) {
            V2 p = {2.5f + 4.f * i, 2.5f + 4.f * j};
            if (jitter && i > 0 && i < N && j > 0 && j < N) { p.x += U(rng); p.y += U(rng); }
            v[j * (N + 1) + i] = p;
        }
        std::vector<int> cov(W * H, 0);
        for (int j = 0; j < N; j++) for (int i = 0; i < N; i++) {
            V2 a = v[j * (N + 1) + i], b = v[j * (N + 1) + i + 1], c = v[(j + 1) * (N + 1) + i], d = v[(j + 1) * (N + 1) + i + 1];
            if ((i + j) & 1) { coverTri(cov, W, H, a, b, d, rule, fixedPt, off); coverTri(cov, W, H, a, d, c, rule, fixedPt, off); }
            else { coverTri(cov, W, H, a, b, c, rule, fixedPt, off); coverTri(cov, W, H, b, d, c, rule, fixedPt, off); }
        }
        holes = dbl = 0;
        for (int py = 3; py <= 61; py++) for (int px = 3; px <= 61; px++) { int k = cov[py * W + px]; if (k == 0) holes++; if (k > 1) dbl++; }
    };
    struct Case { const char* name; bool jit; int rule; bool fx; float off; };
    Case cs[] = {
        {"网格A(顶点在像素中心) 定点  规则 w>=0（全收）", false, 0, true, 0},
        {"网格A(顶点在像素中心) 定点  规则 w>0 （全不收）", false, 1, true, 0},
        {"网格A(顶点在像素中心) 定点  Top-Left", false, 2, true, 0},
        {"网格B(随机亚像素抖动) 定点  Top-Left", true, 2, true, 0},
        {"网格B(随机亚像素抖动) 定点  Top-Left 坐标+32768", true, 2, true, 32768},
    };
    printf("网格：15x15 个四边形 = 450 个共享边的三角形；统计内部 59x59=3481 个像素\n");
    printf("%-52s %8s %8s\n", "配置", "漏画", "重复画");
    for (auto& c : cs) { int h, d; run(c.jit, c.rule, c.fx, c.off, h, d); printf("%-52s %8d %8d\n", c.name, h, d); }
    line();
    printf("压力测试：1024x768 屏幕上 720 个细长三角形围成扇形（共享中心点和 720 条长边，边长约 600px，中心在亚像素位置）\n");
    {
        const int FW = 1024, FH = 768, NF = 720; std::mt19937 rng(11); std::uniform_real_distribution<float> U(0, 1);
        std::vector<float> ang(NF); for (int i = 0; i < NF; i++) ang[i] = (i + 0.2f + 0.6f * U(rng)) * 6.2831853f / NF;
        printf("%-44s %8s %8s %10s\n", "配置", "漏画", "重复画", "检查像素");
        for (int fx = 1; fx >= 0; fx--) for (float off : {0.f, 3000.f, 12000.f}) {
            V2 c = {511.37f, 383.71f}; std::vector<int> cov(FW * FH, 0);
            for (int i = 0; i < NF; i++) {
                float a0 = ang[i], a1 = ang[(i + 1) % NF];
                V2 p0 = {c.x + 600 * cosf(a0), c.y + 600 * sinf(a0)}, p1 = {c.x + 600 * cosf(a1), c.y + 600 * sinf(a1)};
                coverTri(cov, FW, FH, c, p0, p1, 2, fx == 1, off);
            }
            int h = 0, d = 0, n = 0;
            for (int py = 0; py < FH; py++) for (int px = 0; px < FW; px++) {
                float dx = px + 0.5f - c.x, dy = py + 0.5f - c.y; if (dx * dx + dy * dy > 560.f * 560.f) continue;
                n++; int k = cov[py * FW + px]; if (k == 0) h++; if (k > 1) d++;
            }
            char nm[96]; snprintf(nm, sizeof nm, "%s Top-Left，屏幕坐标整体平移 +%.0f", fx ? "定点(8位亚像素)" : "float 边函数", off);
            printf("%-44s %8d %8d %10d\n", nm, h, d, n);
        }
    }
    printf("结论：定点坐标 + Top-Left 规则在任何位置都不漏不重；float 边函数在共享边两侧的舍入不一致，长边/大坐标下出现漏画或重复（重复在半透明下就是\"亮线\"）。\n");
}

// ============================== E5：透视校正插值 ==============================
static void E5() {
    title("E5  透视校正插值：屏幕空间线性插值 uv 会怎样？（PS1 式纹理扭曲）");
    int W = 320, H = 180; Tex tex(makeChecker(64, 32, {0.92f, 0.92f, 0.92f}, {0.20f, 0.35f, 0.75f}));
    V3 eye = {0, 1.2f, 0}, at = {0, 0, -8};
    M4 view = lookAt(eye, at, {0, 1, 0}), proj = perspective(60, (float)W / H, 0.1f, 200, DM_D3D), vp = proj * view;
    auto analyticUV = [&](float px, float py, float& u, float& v) {
        float t = tanf(30.f * (float)M_PI / 180.f), nx = px / W * 2 - 1, ny = 1 - py / H * 2;
        V3 f = nrm(at - eye), s = nrm(cross(f, {0, 1, 0})), up = cross(s, f);
        V3 d = nrm(f + s * (nx * t * W / H) + up * (ny * t));
        float k = -eye.y / d.y; V3 p = eye + d * k; u = p.x * 0.4f; v = p.z * 0.4f;
    };
    struct R { const char* n; int seg; bool persp; };
    R rs[] = {{"仿射插值，地面 2 个三角形", 1, false}, {"仿射插值，地面细分 16x16=512 三角形", 16, false}, {"透视校正，地面 2 个三角形", 1, true}};
    Img ref;
    printf("%-38s %12s %12s %16s\n", "配置", "平均uv误差", "最大uv误差", "近处(<5m)平均误差");
    for (auto& r : rs) {
        Mesh pl = makePlane(-10, 10, -0.5f, -60, 0, r.seg, r.seg, 0.4f);
        Target T(W, H); T.clear({0.75f, 0.85f, 0.95f, 1}, 1.f); State s; s.persp = r.persp;
        double esum = 0, emax = 0, enear = 0; int n = 0, nn = 0;
        drawMesh(T, s, pl, ident(), vp, [&](const Frag& f) {
            float u, v; analyticUV(f.px, f.py, u, v);
            double e = std::hypot(f.a[6] - u, f.a[7] - v); esum += e; emax = std::max(emax, e); n++;
            if (-v / 0.4f < 5) { enear += e; nn++; }
            V3 c = tex.point(f.a[6], f.a[7]);
            return V4{c.x, c.y, c.z, 1};
        });
        printf("%-38s %12.4f %12.4f %16.4f\n", r.n, esum / n, emax, enear / nn);
        Img im = T.resolve();
        if (r.seg == 1) savePPM(im, r.persp ? "e5_persp_correct" : "e5_persp_affine");
        if (r.seg == 16) savePPM(im, "e5_persp_affine_tess");
    }
    // 单像素手算对照
    {
        Mesh pl = makePlane(-10, 10, -0.5f, -60, 0, 1, 1, 0.4f);
        for (bool p : {false, true}) {
            Target T(W, H); T.clear({0, 0, 0, 1}, 1.f); State s; s.persp = p;
            drawMesh(T, s, pl, ident(), vp, [&](const Frag& f) {
                if (f.x == 200 && f.y == 120) { float u, v; analyticUV(f.px, f.py, u, v);
                    printf("像素(200,120) %-6s uv=(%.4f, %.4f)   射线求交真值 uv=(%.4f, %.4f)\n", p ? "透视校正" : "仿射", f.a[6], f.a[7], u, v); }
                return V4{1, 1, 1, 1};
            });
        }
    }
    printf("（uv 单位 = 纹理重复次数；1 个棋盘格 = 0.5。误差 0.5 就是错一整格）\n");
}

// ============================== E6：深度精度 ==============================
static void E6() {
    title("E6  深度精度：同一段距离，标准 Z / 24 位定点 / Reversed-Z 能分辨多细？（near=0.1, far=10000）");
    const double n = 0.1, f = 10000;
    auto zStd = [&](double d) { return f * (d - n) / (d * (f - n)); };           // D3D [0,1]
    auto dStd = [&](double z) { return n * f / (f - z * (f - n)); };
    auto zRev = [&](double d) { return n * (f - d) / (d * (f - n)); };
    auto dRev = [&](double z) { return n * f / (z * (f - n) + n); };
    auto zGL = [&](double d) { return (f + n) / (f - n) - 2 * f * n / ((f - n) * d); };  // [-1,1]
    auto dGL = [&](double z) { return 2 * f * n / ((f + n) - z * (f - n)); };
    printf("表格：在距离 d 处，深度缓冲中相邻两个可表示值对应的世界距离差（越小越好）\n");
    printf("%-9s %14s %14s %14s %14s %14s\n", "距离 d", "标准Z float", "标准Z 24bit", "GL float", "Reversed float", "Rev 无限远");
    for (double d : {0.5, 1.0, 10.0, 100.0, 1000.0, 5000.0, 9000.0}) {
        float z1 = (float)zStd(d); float z1n = nextafterf(z1, 2.f); double a = dStd(z1n) - dStd(z1);
        double q = std::floor(zStd(d) * 16777215.0 + 0.5); double b = dStd((q + 1) / 16777215.0) - dStd(q / 16777215.0);
        float g = (float)zGL(d); float gn = nextafterf(g, 2.f); double c = dGL(gn) - dGL(g);
        float r = (float)zRev(d); float rn = nextafterf(r, -1.f); double e = dRev(rn) - dRev(r);
        float ri = (float)(n / d); float rin = nextafterf(ri, -1.f); double g2 = n / rin - n / ri;
        printf("%-9.1f %13.6gm %13.6gm %13.6gm %13.6gm %13.6gm\n", d, a, b, c, e, g2);
    }
    printf("标准Z 的非线性：z=0.5 对应 d=%.4fm，z=0.9 对应 d=%.4fm，z=0.99 对应 d=%.3fm —— 一半的深度值花在了前 %.2f 米\n",
           dStd(0.5), dStd(0.9), dStd(0.99), dStd(0.5));
    // ---- 渲染实测：5 组"墙面对"，每组红墙在前、绿墙在其后 0.25m（先画绿墙），统计绿色穿出比例
    line();
    printf("渲染实测：5 组倾斜 35° 的墙面对，分别位于 100/300/1000/3000/8000m，绿墙在红墙后方 0.25m 且先画；统计\"绿色穿出\"像素比例\n");
    const int W = 600, H = 200; const float dists[5] = {100, 300, 1000, 3000, 8000}, angs[5] = {-24, -12, 0, 12, 24};
    auto wall = [&](float D, float angDeg, float gap) {
        float a = angDeg * (float)M_PI / 180.f; V3 dir = {sinf(a), 0, -cosf(a)};
        V3 n = nrm(mulDir(rotY(35.f * (float)M_PI / 180.f), dir * -1.f)), up = {0, 1, 0}, right = nrm(cross(up, n));
        V3 c = dir * D - n * gap; float sh = gap > 0 ? 0.9f : 1.f, w = D * 0.13f * sh, h = D * 0.25f * sh; /* 后墙略小，轮廓藏在前墙后 */ Mesh m; const int G = 8;
        for (int j = 0; j <= G; j++) for (int i = 0; i <= G; i++) { m.p.push_back(c + right * ((i / (float)G - 0.5f) * w) + up * ((j / (float)G - 0.5f) * h)); m.n.push_back(n); m.uv.push_back({0, 0}); }
        for (int j = 0; j < G; j++) for (int i = 0; i < G; i++) { uint32_t q = j * (G + 1) + i; m.idx.insert(m.idx.end(), {q, q + 1, q + G + 2, q, q + G + 2, q + G + 1}); }
        fixWinding(m); return m;
    };
    struct C { const char* n; int mode; bool q; float nearP; };
    C cs[] = {{"标准Z float     near=0.1", DM_D3D, false, 0.1f}, {"标准Z 24bit定点 near=0.1", DM_D3D, true, 0.1f}, {"标准Z float     near=1.0", DM_D3D, false, 1.0f},
              {"OpenGL float    near=0.1", DM_GL, false, 0.1f}, {"Reversed float  near=0.1", DM_REV, false, 0.1f}, {"Reversed 无限远 near=0.1", DM_REVINF, false, 0.1f}};
    printf("%-28s %10s %10s %10s %10s %10s\n", "配置", "100m", "300m", "1000m", "3000m", "8000m");
    for (auto& c : cs) {
        bool rev = c.mode == DM_REV || c.mode == DM_REVINF;
        M4 vp = perspective(36, (float)W / H, c.nearP, 10000, c.mode);
        Target T(W, H); T.clear({0.85f, 0.9f, 0.97f, 1}, rev ? 0.f : 1.f);
        State s; s.clip = c.mode; s.quant24 = c.q; s.dfunc = rev ? DF_GREATER : DF_LESS;
        std::vector<int> owner(W * H, -1);
        for (int k = 0; k < 5; k++) {
            Mesh g = wall(dists[k], angs[k], 0.25f), r = wall(dists[k], angs[k], 0.f);
            drawMesh(T, s, g, ident(), vp, [&, k](const Frag& f) { owner[f.y * W + f.x] = k; return V4{0.1f, 0.75f, 0.2f, 1}; });
            drawMesh(T, s, r, ident(), vp, [&, k](const Frag& f) { owner[f.y * W + f.x] = k; return V4{0.85f, 0.15f, 0.15f, 1}; });
        }
        long long tot[5] = {}, bad[5] = {};
        for (int i = 0; i < W * H; i++) if (owner[i] >= 0) { tot[owner[i]]++; if (T.color[i].y > 0.5f && T.color[i].x < 0.5f) bad[owner[i]]++; }
        printf("%-28s", c.n); for (int b = 0; b < 5; b++) printf(" %9.1f%%", tot[b] ? 100.0 * bad[b] / tot[b] : 0.0); printf("\n");
        if (c.mode == DM_D3D && !c.q && c.nearP < 0.5f) savePPM(T.resolve(), "e6_zfight_std");
        if (c.mode == DM_REV) savePPM(T.resolve(), "e6_zfight_rev");
    }
}

// ============================== E7：Overdraw / Early-Z / Z-Prepass ==============================
struct QuadDesc { float x0, y0, x1, y1, z, seed; };
static Stats g_e7[5]; static double g_e7ms[5];
static void E7() {
    title("E7  Overdraw 与 Early-Z：同一批不透明方块，换个画法片元着色器少跑多少次？");
    const int W = 640, H = 360, NQ = 400;
    std::mt19937 rng(42); std::uniform_real_distribution<float> U(0, 1);
    std::vector<QuadDesc> qs(NQ);
    for (auto& q : qs) { float w = 40 + U(rng) * 160, h = 40 + U(rng) * 160, x = U(rng) * (W - w), y = U(rng) * (H - h);
        q = {x / W * 2 - 1, 1 - (y + h) / H * 2, (x + w) / W * 2 - 1, 1 - y / H * 2, 0.05f + U(rng) * 0.9f, U(rng)}; }
    auto drawAll = [&](Target& T, const State& s, const std::vector<QuadDesc>& list, const FS& fs) {
        std::vector<VOut> vs; std::vector<uint32_t> idx;
        for (auto& q : list) { uint32_t b = (uint32_t)vs.size();
            float xs[4] = {q.x0, q.x1, q.x1, q.x0}, ys[4] = {q.y0, q.y0, q.y1, q.y1};
            for (int k = 0; k < 4; k++) { VOut o; o.c = {xs[k], ys[k], q.z, 1}; o.a[0] = q.seed * 10; o.a[1] = q.z; vs.push_back(o); }
            idx.insert(idx.end(), {b, b + 1, b + 2, b, b + 2, b + 3}); }
        drawTris(T, s, (int)vs.size(), idx.data(), (int)idx.size(), 2, [&](int i, VOut& o) { o = vs[i]; }, fs);
    };
    FS heavy = [](const Frag& f) { float acc = f.a[0]; for (int i = 0; i < 48; i++) acc = acc * 0.97f + sinf(acc + (float)i) * 0.5f; return V4{0.3f + 0.1f * acc, f.a[1], 0.6f, 1}; };
    auto front2back = qs; std::sort(front2back.begin(), front2back.end(), [](auto& a, auto& b) { return a.z < b.z; });
    auto back2front = qs; std::sort(back2front.begin(), back2front.end(), [](auto& a, auto& b) { return a.z > b.z; });
    const char* names[5] = {"Late-Z（先着色后测试） 随机顺序", "Early-Z 随机顺序", "Early-Z 从前往后排序", "Early-Z 从后往前排序", "Z-Prepass + EQUAL 着色"};
    printf("%-34s %10s %10s %10s %10s %10s\n", "画法", "光栅片元", "PS执行", "平均overdraw", "深度剔除", "耗时ms");
    Img heat;
    for (int m = 0; m < 5; m++) {
        Target T(W, H); T.clear({0, 0, 0, 1}, 1.f); State s; s.cull = false; s.earlyZ = m != 0;
        double t0 = nowMs(); Stats acc{};
        if (m == 4) {
            State p = s; p.colorWrite = false; drawAll(T, p, qs, nullptr); Stats s1 = T.st;
            State q = s; q.depthWrite = false; q.dfunc = DF_EQUAL; T.st = Stats{}; drawAll(T, q, qs, heavy);
            acc = T.st; acc.frags += s1.frags; acc.zReads += s1.zReads; acc.zWrites += s1.zWrites;
        } else { drawAll(T, s, m == 2 ? front2back : (m == 3 ? back2front : qs), heavy); acc = T.st; }
        double ms = nowMs() - t0;
        long long covered = 0; for (int i = 0; i < W * H; i++) if (T.depth[i] < 1.f) covered++;
        printf("%-34s %10lld %10lld %10.2f %10lld %10.1f\n", names[m], acc.frags, acc.fs, (double)acc.fs / covered, acc.zfail, ms);
        g_e7[m] = acc; g_e7ms[m] = ms;
        if (m == 1 || m == 3) {
            Img im(W, H);
            for (int i = 0; i < W * H; i++) { float o = T.over[i] / 8.f; im.p[i] = T.over[i] == 0 ? V3{0.08f, 0.08f, 0.1f} : V3{clamp01(o * 2), clamp01(o * 2 - 0.6f), clamp01(0.4f - o)}; }
            savePPM(im, m == 1 ? "e7_overdraw_random" : "e7_overdraw_back2front");
        }
        if (m == 0) printf("  （屏幕覆盖像素 %lld / %d）\n", covered, W * H);
    }
}

// ============================== E8：2x2 Quad 效率 ==============================
static void E8() {
    title("E8  小三角形的代价：GPU 按 2x2 Quad 跑像素着色器，有效像素占多少？");
    const int W = 512, H = 512, NT = 3000;
    printf("%-26s %10s %10s %10s %12s %12s\n", "三角形（面积 px²）", "覆盖像素", "PS lanes", "Quad效率", "零覆盖三角形", "每三角形lanes");
    std::mt19937 rng(3); std::uniform_real_distribution<float> U(0, 1);
    struct C { const char* n; float area; float aspect; };
    C cs[] = {{"等边 0.5", 0.5f, 1}, {"等边 1", 1, 1}, {"等边 2", 2, 1}, {"等边 4", 4, 1}, {"等边 8", 8, 1}, {"等边 16", 16, 1}, {"等边 64", 64, 1},
              {"等边 256", 256, 1}, {"等边 1024", 1024, 1}, {"等边 4096", 4096, 1}, {"细长 64（底64x高2）", 64, 32}, {"细长 256（底256x高2）", 256, 128}};
    for (auto& c : cs) {
        Target T(W, H); T.clear({0, 0, 0, 1}, 1.f); State s; s.cull = false; s.depthTest = false; s.depthWrite = false;
        long long zeroTris = 0;
        for (int i = 0; i < NT; i++) {
            float side = std::sqrt(4 * c.area / std::sqrt(3.f));
            float bw = c.aspect == 1 ? side : std::sqrt(2 * c.area * c.aspect), bh = c.aspect == 1 ? side * std::sqrt(3.f) / 2 : 2 * c.area / bw;
            float cx = 40 + U(rng) * (W - 80), cy = 40 + U(rng) * (H - 80), ang = U(rng) * 6.2832f;
            V2 p[3] = {{-bw / 2, -bh / 3}, {bw / 2, -bh / 3}, {0, 2 * bh / 3}};
            VOut v[3];
            for (int k = 0; k < 3; k++) { float x = cx + p[k].x * cosf(ang) - p[k].y * sinf(ang), y = cy + p[k].x * sinf(ang) + p[k].y * cosf(ang);
                v[k].c = {x / W * 2 - 1, 1 - y / H * 2, 0.5f, 1}; }
            long long before = T.st.frags; rasterTri(T, s, v, 0, [](const Frag&) { return V4{1, 1, 1, 1}; });
            if (T.st.frags == before) zeroTris++;
        }
        printf("%-26s %10lld %10lld %9.1f%% %12lld %12.1f\n", c.n, T.st.frags, T.st.lanes, 100.0 * T.st.frags / T.st.lanes, zeroTris, (double)T.st.lanes / NT);
    }
}

// ============================== E9：Mipmap / 各向异性 / 纹理缓存 ==============================
static void E9() {
    title("E9  纹理缩小（minification）：点采样 / 双线性 / 三线性 Mipmap / 16x 各向异性");
    int W = 384, H = 216; Tex tex(makeChecker(256, 4, {0.95f, 0.95f, 0.95f}, {0.1f, 0.1f, 0.12f}));
    Mesh pl = makePlane(-60, 60, -0.2f, -300, 0, 4, 32, 0.25f);
    M4 vp = perspective(60, (float)W / H, 0.1f, 1000, DM_D3D) * lookAt({0, 1.0f, 0}, {0, 0.25f, -8}, {0, 1, 0});
    std::vector<float> dist(W * H, -1);
    // 参考：8x8 超采样 + 每样本 bilinear(LOD0) = 近似"像素内真实平均"
    Target R(W, H, 64); R.clear({0.7f, 0.8f, 0.95f, 1}, 1.f); State rs; rs.ssaa = true;
    drawMesh(R, rs, pl, ident(), vp, [&](const Frag& f) { dist[f.y * W + f.x] = -f.a[2]; V3 c = tex.bilinear(f.a[6], f.a[7], 0); return V4{c.x, c.y, c.z, 1}; });
    Img ref = R.resolve(); savePPM(ref, "e9_ref");
    std::vector<char> nearM(W * H), midM(W * H), any(W * H);
    for (int i = 0; i < W * H; i++) { any[i] = dist[i] > 0; nearM[i] = dist[i] > 0 && dist[i] < 3; midM[i] = dist[i] >= 3 && dist[i] < 12; }
    const char* names[5] = {"点采样 LOD0（无 mip）", "双线性 LOD0（无 mip）", "三线性 mipmap", "三线性 + 16x 各向异性", "三线性 + LOD Bias -2（过锐）"};
    printf("%-30s %9s %9s %9s %12s %10s %8s\n", "采样方式", "RMSE全图", "近<3m", "中3~12m", "每像素texel", "缓存命中", "平均LOD");
    for (int m = 0; m < 5; m++) {
        Target T(W, H); T.clear({0.7f, 0.8f, 0.95f, 1}, 1.f); State s;
        g_cache.reset(); g_cache.on = true; g_texFetch = 0; double lodSum = 0; long long lodN = 0;
        drawMesh(T, s, pl, ident(), vp, [&](const Frag& f) {
            float a1[MAXA], a2[MAXA]; f.t->interp(f.px + 1, f.py, a1); f.t->interp(f.px, f.py + 1, a2);
            float dudx = a1[6] - f.a[6], dvdx = a1[7] - f.a[7], dudy = a2[6] - f.a[6], dvdy = a2[7] - f.a[7];
            V3 c; float lod = 0;
            switch (m) {
            case 0: c = tex.point(f.a[6], f.a[7]); break;
            case 1: c = tex.bilinear(f.a[6], f.a[7], 0); break;
            case 2: c = tex.sampleGrad(f.a[6], f.a[7], dudx, dvdx, dudy, dvdy, 1, &lod); break;
            case 3: c = tex.sampleGrad(f.a[6], f.a[7], dudx, dvdx, dudy, dvdy, 16, &lod); break;
            default: lod = tex.lodFor(dudx, dvdx, dudy, dvdy); c = tex.trilinear(f.a[6], f.a[7], lod - 2); break;
            }
            if (m >= 2) { lodSum += std::max(lod, 0.f); lodN++; }
            return V4{c.x, c.y, c.z, 1};
        });
        g_cache.on = false;
        Img im = T.resolve();
        double hitRate = 100.0 * g_cache.hit / std::max(1ll, g_cache.hit + g_cache.miss);
        printf("%-30s %9.4f %9.4f %9.4f %12.1f %9.1f%% %8.2f\n", names[m], rmse(im, ref, &any), rmse(im, ref, &nearM), rmse(im, ref, &midM), (double)g_texFetch / T.st.fs, hitRate, lodN ? lodSum / lodN : 0.0);
        const char* fn[5] = {"e9_point", "e9_bilinear", "e9_trilinear", "e9_aniso16", "e9_lodbias"};
        savePPM(im, fn[m]);
    }
    printf("（纹理缓存模型 = 16KB 直接映射、4x4 texel 一行；平均 LOD 只统计 mip 方式，负值按 0 计）\n");
}

// ============================== E10：MSAA vs SSAA ==============================
static void E10() {
    title("E10 抗锯齿：无 AA / MSAA 4x / SSAA 4x，与 256 样本参考图比误差和着色次数");
    const int W = 128, H = 96;
    struct TV { float x, y; };
    std::vector<std::array<TV, 3>> tris = {
        {{{-0.85f, -0.7f}, {0.15f, -0.9f}, {-0.25f, 0.85f}}},
        {{{0.05f, 0.75f}, {0.92f, 0.62f}, {0.30f, -0.80f}}},
        {{{-0.95f, 0.20f}, {0.95f, 0.26f}, {0.95f, 0.30f}}},  // 细长条
    };
    std::vector<VOut> vs; std::vector<uint32_t> idx;
    for (auto& t : tris) for (int k = 0; k < 3; k++) { VOut o; o.c = {t[k].x, t[k].y, 0.5f, 1}; o.a[0] = t[k].x + t[k].y * 0.3f; idx.push_back((uint32_t)vs.size()); vs.push_back(o); }
    VS vsf = [&](int i, VOut& o) { o = vs[i]; };
    FS stripe = [](const Frag& f) { float c = 0.5f + 0.5f * sinf(f.a[0] * 268.f); return V4{0.95f * c + 0.05f, 0.8f * c + 0.1f, 0.3f + 0.4f * c, 1}; };
    FS flat = [](const Frag&) { return V4{1, 1, 1, 1}; };
    auto render = [&](int S, bool ss, const FS& fs, Stats* st) {
        Target T(W, H, S); T.clear({0.05f, 0.05f, 0.08f, 1}, 1.f); State s; s.cull = false; s.ssaa = ss; s.depthTest = false; s.depthWrite = false;
        drawTris(T, s, (int)vs.size(), idx.data(), (int)idx.size(), 1, vsf, fs); if (st) *st = T.st; return T.resolve();
    };
    Img cov = render(256, true, flat, nullptr), ref = render(256, true, stripe, nullptr);
    std::vector<char> edge(W * H), inner(W * H);
    for (int i = 0; i < W * H; i++) { float c = (cov.p[i].x - 0.05f) / 0.95f; edge[i] = c > 0.02f && c < 0.98f; inner[i] = c >= 0.98f; }
    struct C { const char* n; int S; bool ss; const char* fn; };
    C cs[] = {{"无 AA (1 样本)", 1, false, "e10_1x"}, {"MSAA 4x (每像素着色 1 次)", 4, false, "e10_msaa4"}, {"SSAA 4x (每样本着色)", 4, true, "e10_ssaa4"}};
    printf("%-30s %10s %10s %10s %10s %12s\n", "方式", "RMSE边缘", "RMSE内部", "PS执行", "深度/颜色样本", "边缘(纯色)RMSE");
    for (auto& c : cs) {
        Stats st; Img im = render(c.S, c.ss, stripe, &st);
        Img imf = render(c.S, c.ss, flat, nullptr);
        printf("%-30s %10.4f %10.4f %10lld %10lld %12.4f\n", c.n, rmse(im, ref, &edge), rmse(im, ref, &inner), st.fs, st.cWrites, rmse(imf, cov, &edge));
        saveZoom(im, 8, 14, 56, 40, 4, c.fn);
    }
    saveZoom(ref, 8, 14, 56, 40, 4, "e10_ref");
    long long ne = 0, ni = 0; for (int i = 0; i < W * H; i++) { ne += edge[i]; ni += inner[i]; }
    printf("（边缘像素 %lld 个，内部像素 %lld 个；内部是高频条纹着色，用来暴露\"着色走样\"）\n", ne, ni);
}

// ============================== E11：混合 ==============================
static void E11() {
    title("E11 透明混合：顺序相关性 与 预乘 Alpha");
    V4 L[3] = {{1, 0, 0, 0.5f}, {0, 1, 0, 0.5f}, {0, 0, 1, 0.5f}}; const char* nm = "RGB";
    int perm[6][3] = {{0, 1, 2}, {0, 2, 1}, {1, 0, 2}, {1, 2, 0}, {2, 0, 1}, {2, 1, 0}};
    printf("白色背景上依次叠加 3 层 alpha=0.5 的 红/绿/蓝（Over 混合 src*a + dst*(1-a)）：\n");
    for (auto& p : perm) {
        V4 d{1, 1, 1, 1}; for (int k = 0; k < 3; k++) d = blendOp(BL_ALPHA, L[p[k]], d);
        V4 a{1, 1, 1, 1}; for (int k = 0; k < 3; k++) a = blendOp(BL_ADD, V4{L[p[k]].x * 0.3f, L[p[k]].y * 0.3f, L[p[k]].z * 0.3f, 0}, a);
        printf("  顺序 %c->%c->%c : Over 结果 (%.3f, %.3f, %.3f)    Additive 结果 (%.2f, %.2f, %.2f)\n", nm[p[0]], nm[p[1]], nm[p[2]], d.x, d.y, d.z, a.x, a.y, a.z);
    }
    printf("结论：Over 混合不满足交换律 -> 半透明必须排序；Additive 满足交换律 -> 粒子特效常用加法混合免排序。\n");
    line();
    printf("双线性过滤的\"黑边\"：texel A=不透明白(1,1,1,a=1)，texel B=全透明但颜色为黑(0,0,0,a=0)，在两者正中采样，背景为绿(0,1,0)\n");
    V4 A{1, 1, 1, 1}, B{0, 0, 0, 0}, bg{0, 1, 0, 1};
    V4 mid{(A.x + B.x) / 2, (A.y + B.y) / 2, (A.z + B.z) / 2, (A.w + B.w) / 2};
    V4 straight = blendOp(BL_ALPHA, mid, bg);
    V4 Ap{A.x * A.w, A.y * A.w, A.z * A.w, A.w}, Bp{B.x * B.w, B.y * B.w, B.z * B.w, B.w};
    V4 midp{(Ap.x + Bp.x) / 2, (Ap.y + Bp.y) / 2, (Ap.z + Bp.z) / 2, (Ap.w + Bp.w) / 2};
    V4 premul = blendOp(BL_PREMUL, midp, bg);
    printf("  直通 Alpha：过滤后 (%.2f,%.2f,%.2f,a=%.2f) -> 混合结果 (%.3f, %.3f, %.3f)   <-- 偏暗，出现黑边\n", mid.x, mid.y, mid.z, mid.w, straight.x, straight.y, straight.z);
    printf("  预乘 Alpha：过滤后 (%.2f,%.2f,%.2f,a=%.2f) -> 混合结果 (%.3f, %.3f, %.3f)   <-- 正确：白与绿各半\n", midp.x, midp.y, midp.z, midp.w, premul.x, premul.y, premul.z);
}

// ============================== E12：IMR vs TBR 带宽模型 ==============================
static void E12() {
    title("E12 IMR vs TBR：用 E7 的统计估算一帧的显存（DRAM）流量（简化模型，忽略缓存与压缩）");
    const double W = 640, H = 360, px = W * H;
    printf("模型：IMR 每次深度测试读 4B、深度写 4B、颜色写 4B 都直接打到 DRAM；TBR 在片上 Tile 内存完成，帧末只写回一次颜色 4B/像素，深度不写回\n");
    printf("%-34s %14s %14s %10s\n", "画法（来自 E7）", "IMR 流量 KB", "TBR 流量 KB", "IMR/TBR");
    const char* names[5] = {"Late-Z 随机顺序", "Early-Z 随机顺序", "Early-Z 前->后", "Early-Z 后->前", "Z-Prepass"};
    for (int m = 0; m < 5; m++) {
        const Stats& s = g_e7[m];
        double imr = (s.zReads * 4.0 + s.zWrites * 4.0 + s.cWrites * 4.0) / 1024, tbr = px * 4 / 1024;
        printf("%-34s %14.0f %14.0f %9.1fx\n", names[m], imr, tbr, imr / tbr);
    }
    double imr4 = (g_e7[1].zReads * 4.0 + g_e7[1].zWrites * 4.0 + g_e7[1].cWrites * 4.0) * 4 / 1024 + px * 4 * 4 / 1024 + px * 4 / 1024;
    printf("%-34s %14.0f %14.0f %9.1fx\n", "MSAA4x Early-Z 随机（+Resolve）", imr4, px * 4 / 1024, imr4 / (px * 4 / 1024));
    printf("按 60fps 换算：IMR(Early-Z 随机) ≈ %.0f MB/s，TBR ≈ %.0f MB/s；移动端 DRAM 每 GB/s 约耗数百 mW，带宽就是功耗和发热。\n",
           (g_e7[1].zReads * 4.0 + g_e7[1].zWrites * 4.0 + g_e7[1].cWrites * 4.0) * 60 / 1048576, px * 4 * 60 / 1048576);
}

// ============================== E13：综合场景 ==============================
static bool raySphere(V3 o, V3 d, V3 c, float r) {
    V3 oc = o - c; float b = dot(oc, d), cc = dot(oc, oc) - r * r; if (cc < 0) return false;
    float disc = b * b - cc; if (disc < 0) return false; return -b - std::sqrt(disc) > 1e-4f;
}
static bool rayBox(V3 o, V3 d, V3 c, float ang, float half) {
    V3 lo = o - c; float ca = cosf(-ang), sa = sinf(-ang);
    V3 lo2 = {ca * lo.x + sa * lo.z, lo.y, -sa * lo.x + ca * lo.z}, ld = {ca * d.x + sa * d.z, d.y, -sa * d.x + ca * d.z};
    float t0 = -1e30f, t1 = 1e30f; float O[3] = {lo2.x, lo2.y, lo2.z}, D[3] = {ld.x, ld.y, ld.z};
    for (int i = 0; i < 3; i++) {
        if (fabsf(D[i]) < 1e-8f) { if (O[i] < -half || O[i] > half) return false; continue; }
        float a = (-half - O[i]) / D[i], b = (half - O[i]) / D[i]; if (a > b) std::swap(a, b);
        t0 = std::max(t0, a); t1 = std::min(t1, b);
    }
    return t1 >= t0 && t0 > 1e-4f;
}
static void E13() {
    title("E13 综合案例：两遍渲染（Shadow Map + Blinn-Phong），深度偏移对\"阴影痤疮 / 彼得潘\"的影响");
    const int W = 480, H = 270, SM = 1024;
    Mesh ground = makePlane(-6, 6, 6, -6, 0, 8, 8, 0.5f), cube = makeCube(), sphere = makeSphere(1.f, 48, 24);
    const float cubeAng = 30.f * (float)M_PI / 180.f, cubeHalf = 0.8f; const V3 cubeC = {-1.4f, 0.8f, 0.3f}, sphC = {1.4f, 1.0f, -0.6f};
    M4 mGround = ident(), mCube = translate(cubeC) * rotY(cubeAng) * scale(2 * cubeHalf), mSph = translate(sphC);
    V3 toL = nrm({-0.8f, 1.0f, -0.3f});
    M4 lightVP = ortho(-7, 7, -7, 7, 0.1f, 30) * lookAt(toL * 15.f, {0, 0, 0}, {0, 1, 0});
    Tex tex(makeChecker(64, 16, {0.85f, 0.82f, 0.75f}, {0.55f, 0.52f, 0.47f}));
    // ---------- Pass 1：从光源渲染深度 ----------
    Target SMT(SM, SM); SMT.clear({0, 0, 0, 1}, 1.f); State sp; sp.colorWrite = false; sp.cull = false;
    double t0 = nowMs();
    drawMesh(SMT, sp, ground, mGround, lightVP, nullptr); drawMesh(SMT, sp, cube, mCube, lightVP, nullptr); drawMesh(SMT, sp, sphere, mSph, lightVP, nullptr);
    double tShadow = nowMs() - t0;
    {
        Img v(SM / 2, SM / 2);
        for (int y = 0; y < SM / 2; y++) for (int x = 0; x < SM / 2; x++) { float d = SMT.depth[(size_t)(y * 2) * SM + x * 2]; float g = 1 - (d - 0.35f) / 0.4f; v.at(x, y) = {clamp01(g), clamp01(g), clamp01(g)}; }
        savePPM(v, "e13_shadowmap");
    }
    auto smLookup = [&](V3 P, V3 N, int mode, int pcf) -> float {
        V4 lp = lightVP * V4{P.x, P.y, P.z, 1};
        float sx = (lp.x * 0.5f + 0.5f) * SM, sy = (0.5f - 0.5f * lp.y) * SM, z = lp.z;
        float ndl = std::max(dot(N, toL), 0.f), tanT = std::sqrt(std::max(1 - ndl * ndl, 0.f)) / std::max(ndl, 0.05f);
        float bias = 0;
        switch (mode) { case 0: bias = 0; break; case 1: bias = 0.0005f; break; case 2: bias = 0.003f; break; case 3: bias = std::min(0.0002f + 0.0006f * tanT, 0.01f); break; case 4: bias = 0.02f; break; }
        int r = pcf ? 1 : 0; float lit = 0; int n = 0;
        for (int dy = -r; dy <= r; dy++) for (int dx = -r; dx <= r; dx++) {
            int ix = (int)floorf(sx) + dx, iy = (int)floorf(sy) + dy; n++;
            if (ix < 0 || iy < 0 || ix >= SM || iy >= SM) { lit += 1; continue; }
            lit += (z - bias <= SMT.depth[(size_t)iy * SM + ix]) ? 1.f : 0.f;
        }
        return lit / n;
    };
    auto truthLit = [&](V3 P, V3 N, int obj) {
        V3 o = P + N * 2e-3f;
        if (obj != 2 && raySphere(o, toL, sphC, 1.f)) return false;
        if (obj != 1 && rayBox(o, toL, cubeC, cubeAng, cubeHalf)) return false;
        return true;
    };
    V3 eye = {5.5f, 4.0f, 6.5f};
    M4 vp = perspective(45, (float)W / H, 0.1f, 100, DM_D3D) * lookAt(eye, {0, 0.6f, 0}, {0, 1, 0});
    const char* bn[5] = {"bias = 0", "常量 bias = 0.0005 (≈1.5cm)", "常量 bias = 0.003 (≈9cm)", "斜率缩放 bias（0.0002+0.0006·tanθ）", "常量 bias = 0.02 (≈60cm)"};
    printf("光源方向 toLight=(%.2f,%.2f,%.2f)，Shadow Map %dx%d，正交范围 14m x 14m（1 texel ≈ %.1fcm），深度范围 29.9m\n", toL.x, toL.y, toL.z, SM, SM, 1400.f / SM);
    printf("%-38s %14s %14s %12s\n", "深度偏移", "误判为阴影(痤疮+边缘)", "漏光(误判受光)", "受光面像素");
    double tMain = 0; Stats mainSt{};
    for (int mode = 0; mode < 6; mode++) {
        int bm = mode == 5 ? 3 : mode; bool pcf = mode == 5;
        Target T(W, H); T.clear({0.62f, 0.75f, 0.92f, 1}, 1.f); State s;
        long long acne = 0, leak = 0, litN = 0;
        auto shade = [&](int obj, V3 albedoC, bool textured) -> FS {
            return [&, obj, albedoC, textured](const Frag& f) {
                V3 P = {f.a[0], f.a[1], f.a[2]}, N = nrm({f.a[3], f.a[4], f.a[5]});
                V3 alb = albedoC;
                if (textured) { float a1[MAXA], a2[MAXA]; f.t->interp(f.px + 1, f.py, a1); f.t->interp(f.px, f.py + 1, a2);
                    alb = tex.sampleGrad(f.a[6], f.a[7], a1[6] - f.a[6], a1[7] - f.a[7], a2[6] - f.a[6], a2[7] - f.a[7], 1); }
                float ndl = dot(N, toL); float sh = ndl > 0 ? smLookup(P, N, bm, pcf) : 0.f;
                if (ndl > 0.1f && !pcf) { bool t = truthLit(P, N, obj); bool smLit = sh > 0.5f; litN++; if (t && !smLit) acne++; if (!t && smLit) leak++; }
                V3 V = nrm(eye - P), Hh = nrm(V + toL); float spec = powf(std::max(dot(N, Hh), 0.f), 48.f) * (ndl > 0 ? 1.f : 0.f);
                V3 c = mulv(alb, V3{0.16f, 0.18f, 0.22f}) + alb * (std::max(ndl, 0.f) * 0.95f * sh) + V3{1, 1, 1} * (0.35f * spec * sh);
                return V4{c.x, c.y, c.z, 1};
            };
        };
        double t1 = nowMs();
        drawMesh(T, s, cube, mCube, vp, shade(1, {0.80f, 0.35f, 0.25f}, false));   // 先画近处不透明物体
        drawMesh(T, s, sphere, mSph, vp, shade(2, {0.25f, 0.45f, 0.85f}, false));
        drawMesh(T, s, ground, mGround, vp, shade(0, {1, 1, 1}, true));             // 地面被遮挡部分会被 Early-Z 剔除
        if (mode == 5) { tMain = nowMs() - t1; mainSt = T.st; }
        Img im = T.resolve();
        // 伽马校正（线性空间着色 -> sRGB 近似）
        for (auto& c : im.p) c = {powf(clamp01(c.x), 1 / 2.2f), powf(clamp01(c.y), 1 / 2.2f), powf(clamp01(c.z), 1 / 2.2f)};
        if (mode < 5) printf("%-38s %13.2f%% %13.2f%% %12lld\n", bn[mode], 100.0 * acne / litN, 100.0 * leak / litN, litN);
        const char* fn[6] = {"e13_bias0", "e13_bias_small", "e13_bias_mid", "e13_bias_slope", "e13_bias_big", "e13_final"};
        savePPM(im, fn[mode]);
        if (mode == 5) {
            Img dv(W, H);
            for (int i = 0; i < W * H; i++) { float z = T.depth[i]; if (z >= 1) { dv.p[i] = {1, 1, 1}; continue; }
                float d = 0.1f * 100 / (100 - z * (100 - 0.1f)); float g = clamp01((d - 5) / 10); dv.p[i] = {g, g, g}; }
            savePPM(dv, "e13_depth_linear");
            Img dr(W, H); for (int i = 0; i < W * H; i++) { float z = T.depth[i]; dr.p[i] = {z, z, z}; } savePPM(dr, "e13_depth_raw");
        }
    }
    printf("最终图（斜率缩放 bias + 3x3 PCF）：Shadow Pass %.1f ms，Main Pass %.1f ms；主 Pass 输入三角形 %lld，剔除 %lld，光栅化 %lld，片元 %lld，PS 执行 %lld，深度剔除 %lld\n",
           tShadow, tMain, mainSt.trisIn, mainSt.culled, mainSt.rasterTris, mainSt.frags, mainSt.fs, mainSt.zfail);
}

int main() {
    _mkdir("out");
    printf("soft_raster —— 《图形学渲染管线》配套实测（Windows 11 / MSVC 14.44 /O2，单线程 CPU 软件光栅化）\n");
    double t = nowMs();
    E1(); E2(); E3(); E4(); E5(); E6(); E7(); E8(); E9(); E10(); E11(); E12(); E13();
    printf("\n全部实验完成，总耗时 %.1f s\n", (nowMs() - t) / 1000);
    return 0;
}
