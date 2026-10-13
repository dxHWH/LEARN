// anim_demo.cpp —《UE 动画系统：从骨骼到动画蓝图》配套实测
// 不依赖 UE：用最小的数学库复现 UE 动画管线里的关键算法，拿到书中引用的全部数字。
// 约定与 UE 一致：FTransform 复合 A*B = 先 A 后 B；组件空间 = 局部 * 父组件空间；
//                骨骼数组按拓扑序存放（父骨骼下标 < 子骨骼下标），与 FReferenceSkeleton 相同。
// 编译：cl /nologo /std:c++latest /EHsc /O2 /utf-8 anim_demo.cpp
#include <algorithm>
#include <array>
#include <atomic>
#include <barrier>
#include <chrono>
#include <cmath>
#include <complex>
#include <cstdio>
#include <cstring>
#include <functional>
#include <memory>
#include <numeric>
#include <random>
#include <string>
#include <thread>
#include <vector>

using namespace std;
using Clock = chrono::steady_clock;
static const double PI = 3.14159265358979323846;
static double D2R(double d) { return d * PI / 180.0; }
static double R2D(double r) { return r * 180.0 / PI; }
static volatile double g_sink = 0;  // 防止被优化掉

// ============================================================ 最小数学库
struct V3 { float x = 0, y = 0, z = 0; };
static inline V3 operator+(V3 a, V3 b) { return {a.x + b.x, a.y + b.y, a.z + b.z}; }
static inline V3 operator-(V3 a, V3 b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
static inline V3 operator*(V3 a, float s) { return {a.x * s, a.y * s, a.z * s}; }
static inline V3 mulc(V3 a, V3 b) { return {a.x * b.x, a.y * b.y, a.z * b.z}; }
static inline float dot(V3 a, V3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
static inline V3 cross(V3 a, V3 b) { return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x}; }
static inline float len(V3 a) { return sqrtf(dot(a, a)); }
static inline V3 norm(V3 a) { float l = len(a); return l > 1e-12f ? a * (1.f / l) : V3{1, 0, 0}; }
static inline V3 lerp(V3 a, V3 b, float t) { return a + (b - a) * t; }

struct Q { float x = 0, y = 0, z = 0, w = 1; };
static inline Q qmul(Q a, Q b) {  // a*b：先 b 后 a
    return {a.w * b.x + a.x * b.w + a.y * b.z - a.z * b.y, a.w * b.y - a.x * b.z + a.y * b.w + a.z * b.x,
            a.w * b.z + a.x * b.y - a.y * b.x + a.z * b.w, a.w * b.w - a.x * b.x - a.y * b.y - a.z * b.z};
}
static inline Q conj(Q q) { return {-q.x, -q.y, -q.z, q.w}; }
static inline Q neg(Q q) { return {-q.x, -q.y, -q.z, -q.w}; }
static inline float qdot(Q a, Q b) { return a.x * b.x + a.y * b.y + a.z * b.z + a.w * b.w; }
static inline Q qnorm(Q q) { float l = sqrtf(qdot(q, q)); return {q.x / l, q.y / l, q.z / l, q.w / l}; }
static inline Q qaxis(V3 axis, double deg) {
    axis = norm(axis); float h = (float)D2R(deg) * 0.5f, s = sinf(h);
    return {axis.x * s, axis.y * s, axis.z * s, cosf(h)};
}
static inline V3 rot(Q q, V3 v) {
    V3 u{q.x, q.y, q.z}; float s = q.w;
    return u * (2.f * dot(u, v)) + v * (s * s - dot(u, u)) + cross(u, v) * (2.f * s);
}
static inline Q qlerpRaw(Q a, Q b, float t) { return {a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t, a.z + (b.z - a.z) * t, a.w + (b.w - a.w) * t}; }
static inline Q nlerp(Q a, Q b, float t) { if (qdot(a, b) < 0) b = neg(b); return qnorm(qlerpRaw(a, b, t)); }
static inline Q slerp(Q a, Q b, float t) {
    float c = qdot(a, b);
    if (c < 0) { b = neg(b); c = -c; }
    if (c > 0.9995f) return qnorm(qlerpRaw(a, b, t));
    float th = acosf(c), s = sinf(th);
    float wa = sinf((1 - t) * th) / s, wb = sinf(t * th) / s;
    return {a.x * wa + b.x * wb, a.y * wa + b.y * wb, a.z * wa + b.z * wb, a.w * wa + b.w * wb};
}
static inline double qangle(Q a, Q b) {  // 两个旋转之间的夹角（度）
    double c = fabs((double)qdot(a, b)); if (c > 1) c = 1; return R2D(2 * acos(c));
}
static inline double yawDeg(Q q) { V3 f = rot(q, {1, 0, 0}); return R2D(atan2((double)f.y, (double)f.x)); }

struct Xf { Q r; V3 t; V3 s{1, 1, 1}; };
// UE: A*B —— 先应用 A，再应用 B。子骨骼组件空间 = 子局部 * 父组件空间
static inline Xf compose(const Xf& A, const Xf& B) {
    Xf o; o.r = qmul(B.r, A.r); o.s = mulc(A.s, B.s); o.t = rot(B.r, mulc(B.s, A.t)) + B.t; return o;
}
static inline V3 xfPoint(const Xf& X, V3 p) { return rot(X.r, mulc(X.s, p)) + X.t; }

static mt19937 rng(20261012);
static float urand(float a, float b) { return uniform_real_distribution<float>(a, b)(rng); }
static V3 randDir() { for (;;) { V3 v{urand(-1, 1), urand(-1, 1), urand(-1, 1)}; float l = len(v); if (l > 0.1f && l <= 1) return v * (1 / l); } }

template <class F> static double bestMs(int reps, F&& f) {
    double best = 1e30;
    for (int r = 0; r < reps; ++r) {
        auto t0 = Clock::now(); f(); auto t1 = Clock::now();
        best = min(best, chrono::duration<double, milli>(t1 - t0).count());
    }
    return best;
}

// ============================================================ 骨架
struct Skeleton { vector<int> parent; vector<string> name; int n() const { return (int)parent.size(); } };
// 仿 UE4 SK_Mannequin 的 68 根骨骼：前 23 根是主干（LOD 时保留），后面是扭转骨、手指、IK 骨
static const int CORE_BONES = 23;
static Skeleton mannequin() {
    Skeleton s;
    auto add = [&](const string& nm, const string& parent) {
        int p = -1;
        if (!parent.empty()) { p = (int)(find(s.name.begin(), s.name.end(), parent) - s.name.begin()); if (p >= (int)s.name.size()) { printf("bad parent %s\n", parent.c_str()); exit(1); } }
        s.name.push_back(nm); s.parent.push_back(p);
    };
    add("root", ""); add("pelvis", "root"); add("spine_01", "pelvis"); add("spine_02", "spine_01"); add("spine_03", "spine_02");
    add("neck_01", "spine_03"); add("head", "neck_01");
    for (string sd : {"l", "r"}) { add("clavicle_" + sd, "spine_03"); add("upperarm_" + sd, "clavicle_" + sd); add("lowerarm_" + sd, "upperarm_" + sd); add("hand_" + sd, "lowerarm_" + sd); }
    for (string sd : {"l", "r"}) { add("thigh_" + sd, "pelvis"); add("calf_" + sd, "thigh_" + sd); add("foot_" + sd, "calf_" + sd); add("ball_" + sd, "foot_" + sd); }
    for (string sd : {"l", "r"}) { add("lowerarm_twist_01_" + sd, "lowerarm_" + sd); add("upperarm_twist_01_" + sd, "upperarm_" + sd);
                                   add("calf_twist_01_" + sd, "calf_" + sd); add("thigh_twist_01_" + sd, "thigh_" + sd); }
    for (string sd : {"l", "r"}) for (string f : {"index", "middle", "pinky", "ring", "thumb"}) {
        add(f + "_01_" + sd, "hand_" + sd); add(f + "_02_" + sd, f + "_01_" + sd); add(f + "_03_" + sd, f + "_02_" + sd); }
    add("ik_foot_root", "root"); add("ik_foot_l", "ik_foot_root"); add("ik_foot_r", "ik_foot_root");
    add("ik_hand_root", "root"); add("ik_hand_gun", "ik_hand_root"); add("ik_hand_l", "ik_hand_gun"); add("ik_hand_r", "ik_hand_gun");
    return s;
}
static Skeleton makeSkeleton(int) { return mannequin(); }
static vector<Xf> randomPose(int n, float maxDeg = 30) {
    vector<Xf> p(n);
    for (int i = 0; i < n; ++i) { p[i].r = qaxis(randDir(), urand(-maxDeg, maxDeg)); p[i].t = randDir() * urand(5, 15); }
    p[0].t = {0, 0, 0};
    return p;
}
static void fkLinear(const vector<int>& par, const Xf* local, Xf* comp, int n) {
    comp[0] = local[0];
    for (int i = 1; i < n; ++i) comp[i] = compose(local[i], comp[par[i]]);
}

// ============================================================ E1 局部空间 -> 组件空间（FK）
struct Node { Xf local, comp; vector<Node*> kids; };
static void dfs(Node* nd, const Xf& pc) { nd->comp = compose(nd->local, pc); for (Node* k : nd->kids) dfs(k, nd->comp); }

static void E1() {
    printf("\n==================== E1 局部空间 → 组件空间（FK）的三种写法 ====================\n");
    Skeleton sk = mannequin();
    const int NB = sk.n(), NC = 2000;
    vector<int> depth(NB, 0); int maxD = 0; double avgD = 0;
    for (int i = 1; i < NB; ++i) { depth[i] = depth[sk.parent[i]] + 1; maxD = max(maxD, depth[i]); }
    for (int d : depth) avgD += d; avgD /= NB;
    printf("骨架：仿 UE4 Mannequin 的 %d 根骨骼，父下标 < 子下标（拓扑序），平均深度 %.1f，最大深度 %d；%d 个角色\n", NB, avgD, maxD, NC);
    vector<vector<Xf>> local(NC), comp(NC, vector<Xf>(NB)), comp2(NC, vector<Xf>(NB));
    for (int c = 0; c < NC; ++c) local[c] = randomPose(NB);

    double tLin = bestMs(15, [&] { for (int c = 0; c < NC; ++c) fkLinear(sk.parent, local[c].data(), comp[c].data(), NB); });
    double tNaive = bestMs(5, [&] {
        for (int c = 0; c < NC; ++c)
            for (int i = 0; i < NB; ++i) {
                Xf X = local[c][i];
                for (int p = sk.parent[i]; p >= 0; p = sk.parent[p]) X = compose(X, local[c][p]);
                comp2[c][i] = X;
            }
    });
    double err = 0;
    for (int c = 0; c < NC; ++c) for (int i = 0; i < NB; ++i) err = max(err, (double)len(comp[c][i].t - comp2[c][i].t));

    // 指针树：节点分散在堆上，递归 DFS
    vector<vector<Node*>> nodes(NC, vector<Node*>(NB));
    vector<unique_ptr<Node>> pool; vector<unique_ptr<char[]>> junk;
    vector<pair<int, int>> order;
    for (int c = 0; c < NC; ++c) for (int i = 0; i < NB; ++i) order.push_back({c, i});
    shuffle(order.begin(), order.end(), rng);
    for (auto [c, i] : order) { pool.emplace_back(new Node); nodes[c][i] = pool.back().get(); nodes[c][i]->local = local[c][i]; junk.emplace_back(new char[(int)urand(16, 200)]); }
    for (int c = 0; c < NC; ++c) for (int i = 1; i < NB; ++i) nodes[c][sk.parent[i]]->kids.push_back(nodes[c][i]);
    double tPtr = bestMs(15, [&] { for (int c = 0; c < NC; ++c) dfs(nodes[c][0], Xf{}); });
    double err2 = 0;
    for (int c = 0; c < NC; ++c) for (int i = 0; i < NB; ++i) err2 = max(err2, (double)len(comp[c][i].t - nodes[c][i]->comp.t));

    printf("  写法                                    总耗时(ms)   每角色(us)   相对\n");
    printf("  A 线性数组：comp[i]=local[i]*comp[par]  %9.3f   %9.2f    1.00x\n", tLin, tLin * 1000 / NC);
    printf("  B 指针树递归 DFS（节点散落在堆上）     %9.3f   %9.2f    %.2fx\n", tPtr, tPtr * 1000 / NC, tPtr / tLin);
    printf("  C 每根骨骼向上回溯到根（不复用父结果） %9.3f   %9.2f    %.2fx\n", tNaive, tNaive * 1000 / NC, tNaive / tLin);
    printf("  结果一致性：B 与 A 最大位置差 %.2e，C 与 A 最大位置差 %.2e（float 舍入）\n", err2, err);
    printf("  复合次数：A/B = %d 次/角色，C = %d 次/角色（Σ深度）\n", NB - 1, (int)(avgD * NB));
}

// ============================================================ E2 旋转混合：欧拉角 / 四元数 lerp / nlerp / slerp
static void E2() {
    printf("\n==================== E2 旋转混合：为什么一定要四元数 + 最短路径 ====================\n");
    // 1) 170° 与 -170° 的中点
    Q a = qaxis({0, 0, 1}, 170), b = qaxis({0, 0, 1}, -170);
    double euler = (170 + -170) * 0.5;
    Q naive = qnorm(qlerpRaw(a, b, 0.5f));
    Q nl = nlerp(a, b, 0.5f), sl = slerp(a, b, 0.5f);
    printf("Yaw 170° 与 Yaw -170° 各占 50%%（正确答案 ±180°，两者只差 20°）：\n");
    printf("  欧拉角直接插值                 → %7.2f°\n", euler);
    printf("  四元数 lerp，不检查 dot 符号    → %7.2f°   (dot=%.3f < 0，走了 340° 的远路)\n", yawDeg(naive), qdot(a, b));
    printf("  nlerp（dot<0 时取反）           → %7.2f°\n", yawDeg(nl));
    printf("  slerp                          → %7.2f°\n", yawDeg(sl));

    // 2) nlerp 相对 slerp 的角度误差
    printf("nlerp 与 slerp 的最大角度偏差（在 t∈[0,1] 上取最大）：\n  两姿势夹角:");
    double thetas[] = {30, 60, 90, 120, 150, 179};
    for (double th : thetas) printf(" %6.0f°", th);
    printf("\n  最大偏差  :");
    for (double th : thetas) {
        Q p = qaxis({0, 0, 1}, 0), q = qaxis({0, 0, 1}, th);
        double mx = 0;
        for (int k = 0; k <= 1000; ++k) { float t = k / 1000.f; mx = max(mx, qangle(nlerp(p, q, t), slerp(p, q, t))); }
        printf(" %6.2f°", mx);
    }
    printf("\n");
    // 3) 速度
    const int N = 20000000;
    vector<Q> qs(1024); for (auto& q : qs) q = qaxis(randDir(), urand(-170, 170));
    double tN = bestMs(3, [&] { Q acc{}; for (int i = 0; i < N; ++i) { Q r = nlerp(qs[i & 1023], qs[(i * 7 + 3) & 1023], (i & 255) / 255.f); acc.x += r.x; } g_sink += acc.x; });
    double tS = bestMs(3, [&] { Q acc{}; for (int i = 0; i < N; ++i) { Q r = slerp(qs[i & 1023], qs[(i * 7 + 3) & 1023], (i & 255) / 255.f); acc.x += r.x; } g_sink += acc.x; });
    printf("耗时（%d 次）：nlerp %.1f ms（%.2f ns/次），slerp %.1f ms（%.2f ns/次），slerp/nlerp = %.2fx\n", N, tN, tN * 1e6 / N, tS, tS * 1e6 / N, tS / tN);

    // 4) 多路混合：加权求和再归一化（UE BlendPosesTogether 的做法）与“链式 slerp”
    Q A = qaxis({0, 0, 1}, 0), B = qaxis({0, 0, 1}, 80), C = qaxis({0, 1, 0}, 80);
    auto wsum = [](vector<pair<Q, float>> v) {
        Q acc{0, 0, 0, 0}; Q ref = v[0].first;
        for (auto [q, w] : v) { if (qdot(q, ref) < 0) q = neg(q); acc.x += q.x * w; acc.y += q.y * w; acc.z += q.z * w; acc.w += q.w * w; }
        return qnorm(acc);
    };
    Q s1 = wsum({{A, 1 / 3.f}, {B, 1 / 3.f}, {C, 1 / 3.f}}), s2 = wsum({{C, 1 / 3.f}, {A, 1 / 3.f}, {B, 1 / 3.f}}), s3 = wsum({{B, 1 / 3.f}, {C, 1 / 3.f}, {A, 1 / 3.f}});
    Q c1 = slerp(slerp(A, B, 0.5f), C, 1 / 3.f), c2 = slerp(slerp(C, A, 0.5f), B, 1 / 3.f), c3 = slerp(slerp(B, C, 0.5f), A, 1 / 3.f);
    printf("三路等权混合（Yaw0 / Yaw80 / Pitch80）：\n");
    printf("  加权求和+归一化：三种顺序结果两两最大夹角 %.4f°\n", max({qangle(s1, s2), qangle(s2, s3), qangle(s1, s3)}));
    printf("  链式 slerp     ：三种顺序结果两两最大夹角 %.4f°（与加权求和最大相差 %.3f°）\n",
           max({qangle(c1, c2), qangle(c2, c3), qangle(c1, c3)}), max({qangle(c1, s1), qangle(c2, s1), qangle(c3, s1)}));
}

// ============================================================ E3 蒙皮：LBS 与 DQS
struct DQ { Q r, d; };
static DQ toDQ(const Xf& X) {  // 忽略缩放
    Q t{X.t.x, X.t.y, X.t.z, 0}; Q d = qmul(t, X.r); d.x *= .5f; d.y *= .5f; d.z *= .5f; d.w *= .5f; return {X.r, d};
}
static V3 dqPoint(DQ q, V3 p) {
    float l = sqrtf(qdot(q.r, q.r));
    Q r{q.r.x / l, q.r.y / l, q.r.z / l, q.r.w / l}, d{q.d.x / l, q.d.y / l, q.d.z / l, q.d.w / l};
    Q t = qmul(d, conj(r)); V3 tr{t.x * 2, t.y * 2, t.z * 2};
    return rot(r, p) + tr;
}
static void E3() {
    printf("\n==================== E3 蒙皮：线性混合蒙皮 LBS vs 双四元数 DQS ====================\n");
    // 圆柱沿 x 轴，关节在 x=1，关节处一圈顶点对两根骨骼各 0.5 权重
    V3 J{1, 0, 0};
    auto ring = [&](int k) { float a = (float)(2 * PI * k / 32); return V3{1, cosf(a), sinf(a)}; };
    auto measure = [&](Xf M1, double& lbsR, double& dqsR, double& lbsMin) {
        DQ d0 = toDQ(Xf{}), d1 = toDQ(M1);
        if (qdot(d0.r, d1.r) < 0) { d1.r = neg(d1.r); d1.d = neg(d1.d); }
        vector<V3> L, D;
        for (int k = 0; k < 32; ++k) {
            V3 p = ring(k);
            L.push_back(p * 0.5f + xfPoint(M1, p) * 0.5f);
            DQ b{{(d0.r.x + d1.r.x) * .5f, (d0.r.y + d1.r.y) * .5f, (d0.r.z + d1.r.z) * .5f, (d0.r.w + d1.r.w) * .5f},
                 {(d0.d.x + d1.d.x) * .5f, (d0.d.y + d1.d.y) * .5f, (d0.d.z + d1.d.z) * .5f, (d0.d.w + d1.d.w) * .5f}};
            D.push_back(dqPoint(b, p));
        }
        auto stat = [](vector<V3>& v, double& avg, double* mn) {
            V3 c{}; for (auto& p : v) c = c + p; c = c * (1.f / v.size());
            avg = 0; double m = 1e9; for (auto& p : v) { double r = len(p - c); avg += r; m = min(m, r); }
            avg /= v.size(); if (mn) *mn = m;
        };
        stat(L, lbsR, &lbsMin); stat(D, dqsR, nullptr);
    };
    printf("关节处截面（原半径 1.0）在骨骼扭转 / 弯曲后的平均半径：\n");
    printf("  角度   扭转-LBS  扭转-DQS  |  弯曲-LBS(平均/最小)  弯曲-DQS\n");
    for (double deg : {0.0, 45.0, 90.0, 135.0, 170.0, 180.0}) {
        Xf tw; tw.r = qaxis({1, 0, 0}, deg); tw.t = J - rot(tw.r, J);
        Xf bd; bd.r = qaxis({0, 0, 1}, deg); bd.t = J - rot(bd.r, J);
        double l1, d1, m1, l2, d2, m2;
        measure(tw, l1, d1, m1); measure(bd, l2, d2, m2);
        printf("  %4.0f°   %7.3f   %7.3f  |  %7.3f / %6.3f        %7.3f\n", deg, l1, d1, l2, m2, d2);
    }
    // 性能：30000 顶点 × 4 影响
    const int NV = 30000, NB = 68;
    vector<Xf> bones = randomPose(NB, 60);
    struct M34 { float m[12]; };
    vector<M34> mats(NB); vector<DQ> dqs(NB);
    for (int i = 0; i < NB; ++i) {
        V3 ex = rot(bones[i].r, {1, 0, 0}), ey = rot(bones[i].r, {0, 1, 0}), ez = rot(bones[i].r, {0, 0, 1});
        mats[i] = {{ex.x, ey.x, ez.x, bones[i].t.x, ex.y, ey.y, ez.y, bones[i].t.y, ex.z, ey.z, ez.z, bones[i].t.z}};
        dqs[i] = toDQ(bones[i]);
    }
    struct Vtx { V3 p; int b[4]; float w[4]; };
    vector<Vtx> vs(NV); vector<V3> out(NV);
    for (auto& v : vs) {
        v.p = randDir() * urand(1, 50); float s = 0;
        for (int k = 0; k < 4; ++k) { v.b[k] = (int)urand(0, NB - 0.01f); v.w[k] = urand(0.05f, 1); s += v.w[k]; }
        for (float& w : v.w) w /= s;
    }
    double tL = bestMs(30, [&] {
        for (int i = 0; i < NV; ++i) {
            const Vtx& v = vs[i]; float m[12] = {};
            for (int k = 0; k < 4; ++k) { const float* s = mats[v.b[k]].m; for (int j = 0; j < 12; ++j) m[j] += s[j] * v.w[k]; }
            out[i] = {m[0] * v.p.x + m[1] * v.p.y + m[2] * v.p.z + m[3], m[4] * v.p.x + m[5] * v.p.y + m[6] * v.p.z + m[7], m[8] * v.p.x + m[9] * v.p.y + m[10] * v.p.z + m[11]};
        }
        g_sink += out[NV / 2].x;
    });
    double tD = bestMs(30, [&] {
        for (int i = 0; i < NV; ++i) {
            const Vtx& v = vs[i]; DQ acc{{0, 0, 0, 0}, {0, 0, 0, 0}}; Q ref = dqs[v.b[0]].r;
            for (int k = 0; k < 4; ++k) {
                DQ q = dqs[v.b[k]]; float w = qdot(q.r, ref) < 0 ? -v.w[k] : v.w[k];
                acc.r.x += q.r.x * w; acc.r.y += q.r.y * w; acc.r.z += q.r.z * w; acc.r.w += q.r.w * w;
                acc.d.x += q.d.x * w; acc.d.y += q.d.y * w; acc.d.z += q.d.z * w; acc.d.w += q.d.w * w;
            }
            out[i] = dqPoint(acc, v.p);
        }
        g_sink += out[NV / 2].x;
    });
    printf("CPU 蒙皮 %d 顶点 × 4 骨骼影响：LBS %.3f ms，DQS %.3f ms，DQS/LBS = %.2fx\n", NV, tL, tD, tD / tL);
}

// ============================================================ E4 分层骨骼混合：Blend Depth 与 Mesh Space Rotation Blend
// 返回每根骨骼的混合权重；规则与 FAnimationRuntime::CreateMaskWeights 一致：
//  depth=0 → 该骨骼及其子孙全部 1；depth=N>0 → 第 k 层（从 0 开始）权重 min(1,(k+1)/N)；depth<0 → 该分支排除（权重 0）
static vector<float> maskWeights(const vector<int>& par, int filterBone, int blendDepth) {
    int n = (int)par.size(); vector<float> w(n, 0);
    for (int i = 0; i < n; ++i) {
        int d = 0, p = i; while (p >= 0 && p != filterBone) { p = par[p]; ++d; }
        if (p != filterBone) continue;
        if (blendDepth < 0) w[i] = 0;
        else if (blendDepth == 0) w[i] = 1;
        else w[i] = min(1.f, (d + 1) / (float)blendDepth);
    }
    return w;
}
static void layeredBlend(const vector<int>& par, const vector<Xf>& base, const vector<Xf>& blend, const vector<float>& w,
                         bool meshSpace, vector<Xf>& out) {
    int n = (int)par.size(); out.resize(n);
    if (!meshSpace) {
        for (int i = 0; i < n; ++i) { out[i].r = nlerp(base[i].r, blend[i].r, w[i]); out[i].t = lerp(base[i].t, blend[i].t, w[i]); out[i].s = base[i].s; }
        return;
    }
    // 组件空间旋转混合：两份姿势都转到组件空间，旋转在组件空间里插值，再转回局部
    static thread_local vector<Xf> cb, cB; static thread_local vector<Q> outComp;
    cb.resize(n); cB.resize(n); outComp.resize(n);
    fkLinear(par, base.data(), cb.data(), n); fkLinear(par, blend.data(), cB.data(), n);
    for (int i = 0; i < n; ++i) {
        outComp[i] = nlerp(cb[i].r, cB[i].r, w[i]);
        Q parentComp = par[i] < 0 ? Q{} : outComp[par[i]];
        out[i].r = qnorm(qmul(conj(parentComp), outComp[i]));
        out[i].t = lerp(base[i].t, blend[i].t, w[i]); out[i].s = base[i].s;
    }
}
static void E4() {
    printf("\n==================== E4 分层骨骼混合（Layered Blend per Bone） ====================\n");
    // 0 root, 1 pelvis, 2 spine_01, 3 spine_02, 4 spine_03, 5 neck, 6 head, 7 thigh_l, 8 calf_l, 9 clavicle_r, 10 upperarm_r
    vector<int> par = {-1, 0, 1, 2, 3, 4, 5, 1, 7, 4, 9};
    const char* nm[] = {"root", "pelvis", "spine_01", "spine_02", "spine_03", "neck", "head", "thigh_l", "calf_l", "clavicle_r", "upperarm_r"};
    int n = (int)par.size();
    printf("Branch Filter = spine_01 时，不同 Blend Depth 的每骨骼权重：\n  %-11s", "骨骼");
    int depths[] = {0, 1, 2, 3, 4};
    for (int d : depths) printf("  depth=%d", d);
    printf("\n");
    vector<vector<float>> W; for (int d : depths) W.push_back(maskWeights(par, 2, d));
    for (int i = 0; i < n; ++i) { printf("  %-11s", nm[i]); for (auto& w : W) printf("  %7.3f", w[i]); printf("\n"); }
    // 叠加排除：spine_01 depth 0 + clavicle_r depth -1
    auto w0 = maskWeights(par, 2, 0); auto ex = maskWeights(par, 9, 0);  // 排除项：clavicle_r 分支
    for (int i = 0; i < n; ++i) if (ex[i] > 0) w0[i] = 0;
    printf("  再加一条 clavicle_r / depth=-1 的排除项后：clavicle_r=%.0f upperarm_r=%.0f spine_03=%.0f head=%.0f\n", w0[9], w0[10], w0[4], w0[6]);

    // 姿势：下半身跑步时骨盆朝右扭 40°，脊柱逐节回扭 -10°；上半身瞄准姿势（制作时骨盆与脊柱都朝前）
    auto yawPose = [&](vector<double> yaws) {
        vector<Xf> p(n); for (int i = 0; i < n; ++i) { p[i].r = qaxis({0, 0, 1}, yaws[i]); p[i].t = i ? V3{10, 0, 0} : V3{}; } return p;
    };
    //                  root pelvis s1   s2   s3  neck head thigh calf clav uarm
    vector<Xf> base = yawPose({0, 40, -10, -10, -10, 0, 0, -40, 0, 0, 0});
    vector<Xf> aim = yawPose({0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 30});
    auto headYaw = [&](const vector<Xf>& loc) { vector<Xf> c(n); fkLinear(par, loc.data(), c.data(), n); return yawDeg(c[6].r); };
    auto compYaw = [&](const vector<Xf>& loc, int b) { vector<Xf> c(n); fkLinear(par, loc.data(), c.data(), n); return yawDeg(c[b].r); };
    printf("下半身：骨盆 Yaw +40°、脊柱三节各 -10°（头朝 %.0f°）；上半身瞄准姿势：头朝 %.0f°、右臂 %.0f°\n", headYaw(base), headYaw(aim), compYaw(aim, 10));
    printf("  模式                          头部朝向   右上臂朝向   spine_01/02/03 组件朝向\n");
    for (int d : {0, 3}) for (bool ms : {false, true}) {
        vector<Xf> out; layeredBlend(par, base, aim, maskWeights(par, 2, d), ms, out);
        printf("  %-12s depth=%d          %7.2f°    %7.2f°     %6.2f / %6.2f / %6.2f\n", ms ? "Mesh Space" : "Local Space", d,
               headYaw(out), compYaw(out, 10), compYaw(out, 2), compYaw(out, 3), compYaw(out, 4));
    }
    // 性能：68 骨骼，spine_01 以上的上半身在分支内
    Skeleton sk = mannequin();
    vector<Xf> b1 = randomPose(sk.n()), b2 = randomPose(sk.n()), out;
    vector<float> ww = maskWeights(sk.parent, 2, 0);  // spine_01 以上整个上半身
    const int N = 200000;
    double tLoc = bestMs(5, [&] { for (int k = 0; k < N; ++k) { layeredBlend(sk.parent, b1, b2, ww, false, out); } g_sink += out[3].r.x; });
    double tMesh = bestMs(5, [&] { for (int k = 0; k < N; ++k) { layeredBlend(sk.parent, b1, b2, ww, true, out); } g_sink += out[3].r.x; });
    printf("耗时（68 骨骼 Mannequin，Branch Filter=spine_01，%d 次）：局部空间 %.3f us/次，Mesh Space 旋转混合 %.3f us/次，%.2fx\n", N, tLoc * 1000 / N, tMesh * 1000 / N, tMesh / tLoc);
}

// ============================================================ E5 混合空间：三角剖分 + 重心坐标；输入平滑 vs 权重速度
struct BSample { const char* name; float x, y; };
struct Tri { int a, b, c; };
static vector<Tri> delaunay(const vector<BSample>& s) {
    vector<Tri> t; int n = (int)s.size();
    for (int i = 0; i < n; ++i) for (int j = i + 1; j < n; ++j) for (int k = j + 1; k < n; ++k) {
        double ax = s[i].x, ay = s[i].y, bx = s[j].x, by = s[j].y, cx = s[k].x, cy = s[k].y;
        double d = 2 * (ax * (by - cy) + bx * (cy - ay) + cx * (ay - by));
        if (fabs(d) < 1e-9) continue;
        double ux = ((ax * ax + ay * ay) * (by - cy) + (bx * bx + by * by) * (cy - ay) + (cx * cx + cy * cy) * (ay - by)) / d;
        double uy = ((ax * ax + ay * ay) * (cx - bx) + (bx * bx + by * by) * (ax - cx) + (cx * cx + cy * cy) * (bx - ax)) / d;
        double r2 = (ax - ux) * (ax - ux) + (ay - uy) * (ay - uy);
        bool ok = true;
        for (int m = 0; m < n && ok; ++m) if (m != i && m != j && m != k) {
            double dd = (s[m].x - ux) * (s[m].x - ux) + (s[m].y - uy) * (s[m].y - uy);
            if (dd < r2 * (1 - 1e-9)) ok = false;
        }
        if (ok) t.push_back({i, j, k});
    }
    return t;
}
static bool bary(const vector<BSample>& s, Tri t, double px, double py, double w[3]) {
    double ax = s[t.a].x, ay = s[t.a].y, bx = s[t.b].x, by = s[t.b].y, cx = s[t.c].x, cy = s[t.c].y;
    double det = (by - cy) * (ax - cx) + (cx - bx) * (ay - cy);
    w[0] = ((by - cy) * (px - cx) + (cx - bx) * (py - cy)) / det;
    w[1] = ((cy - ay) * (px - cx) + (ax - cx) * (py - cy)) / det;
    w[2] = 1 - w[0] - w[1];
    return w[0] >= -1e-9 && w[1] >= -1e-9 && w[2] >= -1e-9;
}
static vector<double> bsWeights(const vector<BSample>& s, const vector<Tri>& tris, double px, double py, int* triIdx = nullptr, double* cx = nullptr, double* cy = nullptr) {
    vector<double> w(s.size(), 0);
    for (int i = 0; i < (int)tris.size(); ++i) {
        double b[3];
        if (bary(s, tris[i], px, py, b)) { w[tris[i].a] = b[0]; w[tris[i].b] = b[1]; w[tris[i].c] = b[2]; if (triIdx) *triIdx = i; if (cx) { *cx = px; *cy = py; } return w; }
    }
    // 在三角网之外：投影到最近的边界边（只出现在一个三角形里的边）
    double best = 1e30; int ba = 0, bb = 0; double bt = 0;
    for (auto& t : tris) {
        int e[3][2] = {{t.a, t.b}, {t.b, t.c}, {t.c, t.a}};
        for (auto& ed : e) {
            int cnt = 0; for (auto& u : tris) { int v[3] = {u.a, u.b, u.c}; int h = 0; for (int q : v) h += (q == ed[0]) + (q == ed[1]); cnt += h == 2; }
            if (cnt != 1) continue;
            double ax = s[ed[0]].x, ay = s[ed[0]].y, bx = s[ed[1]].x, by = s[ed[1]].y;
            double tt = ((px - ax) * (bx - ax) + (py - ay) * (by - ay)) / ((bx - ax) * (bx - ax) + (by - ay) * (by - ay));
            tt = clamp(tt, 0.0, 1.0);
            double qx = ax + (bx - ax) * tt, qy = ay + (by - ay) * tt, d = (qx - px) * (qx - px) + (qy - py) * (qy - py);
            if (d < best) { best = d; ba = ed[0]; bb = ed[1]; bt = tt; if (cx) { *cx = qx; *cy = qy; } }
        }
    }
    w[ba] = 1 - bt; w[bb] = bt; if (triIdx) *triIdx = -1;
    return w;
}
static void E5() {
    printf("\n==================== E5 混合空间（Blend Space） ====================\n");
    // 速度空间（Vx 右正 / Vy 前正，cm/s）的 9 个采样
    vector<BSample> s = {{"Idle", 0, 0},     {"Walk_F", 0, 160},   {"Walk_B", 0, -140}, {"Walk_L", -150, 0}, {"Walk_R", 150, 0},
                         {"Jog_F", 0, 420},  {"Jog_B", 0, -340},   {"Jog_L", -370, 0},  {"Jog_R", 380, 0}};
    auto tris = delaunay(s);
    double area = 0;
    for (auto& t : tris) area += fabs((s[t.b].x - s[t.a].x) * (s[t.c].y - s[t.a].y) - (s[t.c].x - s[t.a].x) * (s[t.b].y - s[t.a].y)) / 2;
    double hull = (420.0 + 340.0) * (370.0 + 380.0) / 2;  // 外圈菱形的面积
    printf("9 个采样的 Delaunay 三角剖分：%zu 个三角形，面积和 %.0f，外圈凸包面积 %.0f（相等 → 无重叠无空洞）\n", tris.size(), area, hull);
    for (auto& t : tris) printf("  △ %s - %s - %s\n", s[t.a].name, s[t.b].name, s[t.c].name);
    struct QP { double x, y; const char* note; };
    QP qs[] = {{0, 0, "原地"}, {0, 290, "前进，介于走与慢跑之间"}, {180, 180, "右前斜向"}, {-100, -60, "左后慢走"}, {500, 500, "超出范围"}};
    printf("查询点的权重（只列 >0 的采样）：\n");
    for (auto& q : qs) {
        int ti; double cx, cy; auto w = bsWeights(s, tris, q.x, q.y, &ti, &cx, &cy);
        double rx = 0, ry = 0, sum = 0;
        printf("  (%5.0f,%5.0f) %-22s →", q.x, q.y, q.note);
        for (size_t i = 0; i < s.size(); ++i) if (w[i] > 1e-6) { printf(" %s %.3f", s[i].name, w[i]); rx += w[i] * s[i].x; ry += w[i] * s[i].y; sum += w[i]; }
        printf("  | Σw=%.3f 重建=(%.0f,%.0f)%s\n", sum, rx, ry, ti < 0 ? " ← 投影到边界" : "");
    }
    // 输入平滑 vs 采样权重速度：1D 方向混合空间 Left(-90) / Fwd(0) / Right(90)
    printf("1D 方向混合空间 Left(-90°)/Fwd(0°)/Right(90°)，输入从 -90° 突变到 +90°，0.3 s 内完成：\n");
    auto w1d = [](double dir, double w[3]) {
        w[0] = w[1] = w[2] = 0; if (dir <= 0) { double t = (dir + 90) / 90; w[0] = 1 - t; w[1] = t; } else { double t = dir / 90; w[1] = 1 - t; w[2] = t; }
    };
    double dt = 1 / 60.0, maxFwdA = 0, maxFwdB = 0; int framesA = 0, framesB = 0;
    double cur = -90, wb[3] = {1, 0, 0};
    for (int f = 1; f <= 60; ++f) {
        cur = min(90.0, cur + 180 / 0.3 * dt);  // Linear 输入平滑：0.3 s 走完 180°
        double w[3]; w1d(cur, w); maxFwdA = max(maxFwdA, w[1]); if (w[1] > 0.5) ++framesA;
        double tgt[3] = {0, 0, 1}, spd = 1 / 0.3;  // Weight Speed：每秒最多变化 1/0.3
        double sum = 0; for (int k = 0; k < 3; ++k) { wb[k] += clamp(tgt[k] - wb[k], -spd * dt, spd * dt); sum += wb[k]; }
        for (double& x : wb) x /= sum; maxFwdB = max(maxFwdB, wb[1]); if (wb[1] > 0.5) ++framesB;
    }
    printf("  输入平滑（Smoothing Time=0.3, Linear）：途经 Fwd，Fwd 最大权重 %.2f，Fwd 占主导(>0.5) %d 帧\n", maxFwdA, framesA);
    printf("  采样权重速度（Weight Speed=3.33/s）  ：Left 直接淡到 Right，Fwd 最大权重 %.2f，占主导 %d 帧\n", maxFwdB, framesB);
}

// ============================================================ E6 同步组：不同步 / 归一化时间同步 / 同步标记
static void E6() {
    printf("\n==================== E6 同步组（Sync Group）与同步标记（Sync Marker） ====================\n");
    double walkLen = 1.2, runLen = 0.7, walkL = 0.00, runL = 0.10;  // 左脚落地标记位置（归一化）；右脚在 +0.5
    auto foot = [](double phase, double pL) { double p = fmod(phase - pL + 2.0, 1.0); return p < 0.5 ? 0 : 1; };  // 0 左脚支撑 1 右脚
    printf("Walk 周期 %.1f s（L 标记 %.2f、R 标记 %.2f），Run 周期 %.1f s（L %.2f、R %.2f），各 50%% 权重，模拟 20 s @60fps\n",
           walkLen, walkL, walkL + .5, runLen, runL, runL + .5);
    const double dt = 1 / 60.0; const int F = 1200;
    double groupLen = 0.5 * walkLen + 0.5 * runLen;
    for (int mode = 0; mode < 3; ++mode) {
        int bad = 0; double maxPh = 0;
        for (int f = 0; f < F; ++f) {
            double t = f * dt, pw, pr;
            if (mode == 0) { pw = fmod(t / walkLen, 1); pr = fmod(t / runLen, 1); }
            else {
                pw = fmod(t / groupLen, 1);  // 混合空间语义：采样同步推进，整体周期取采样周期的加权平均
                if (mode == 1) pr = pw;      // 归一化时间同步：follower 取相同的归一化位置
                else { double rel = fmod(pw - walkL + 1, 1); pr = fmod(runL + rel, 1); }  // 标记同步：相对“上一个标记”对齐
            }
            if (foot(pw, walkL) != foot(pr, runL)) ++bad;
            double d = fabs(pw - pr); maxPh = max(maxPh, min(d, 1 - d));
        }
        const char* nm[] = {"不同步（各播各的）", "同步组：归一化时间", "同步组：同步标记"};
        printf("  %-22s 两条动画支撑脚不一致的帧 %5.1f%%，最大相位差 %.2f\n", nm[mode], 100.0 * bad / F, maxPh);
    }
    printf("  组周期 = 0.5×%.1f + 0.5×%.1f = %.2f s；归一化同步下左脚落地事件相差 %.0f ms\n", walkLen, runLen, groupLen, (runL - walkL) * groupLen * 1000);
}

// ============================================================ E7 过渡：硬切 / 交叉淡化 / 惯性化
static double inertialOffset(double x0, double v0, double t1, double t) {
    // David Bollo, “Inertialization: High-Performance Animation Transitions in Gears of War”, GDC 2018
    double sgn = x0 < 0 ? -1 : 1; x0 *= sgn; v0 *= sgn;
    if (x0 < 1e-9) return 0;
    v0 = min(v0, 0.0);
    if (v0 < 0) t1 = min(t1, -5 * x0 / v0);
    if (t >= t1) return 0;
    double a0 = max(0.0, (-8 * v0 * t1 - 20 * x0) / (t1 * t1));
    double A = -(a0 * t1 * t1 + 6 * v0 * t1 + 12 * x0) / (2 * pow(t1, 5));
    double B = (3 * a0 * t1 * t1 + 16 * v0 * t1 + 30 * x0) / (2 * pow(t1, 4));
    double C = -(3 * a0 * t1 * t1 + 12 * v0 * t1 + 20 * x0) / (2 * pow(t1, 3));
    return sgn * (A * pow(t, 5) + B * pow(t, 4) + C * pow(t, 3) + a0 / 2 * t * t + v0 * t + x0);
}
static void E7() {
    printf("\n==================== E7 状态过渡：硬切 / 交叉淡化 / 惯性化 ====================\n");
    auto a = [](double t) { return 50 * sin(2 * PI * t / 1.0); };           // 源：手臂前后摆（度）
    auto b = [](double t) { return 10 + 20 * sin(2 * PI * t / 0.8 + 1); };  // 目标：另一套摆臂
    const double dt = 1 / 120.0, t0 = 0.40, T = 0.2;  // t0 时源动画正以最大速度回摆
    printf("单根骨骼角度信号：源 50·sin(2πt)，目标 10+20·sin(2πt/0.8+1)；t0=%.2f s 切换，过渡 %.1f s，采样 120 Hz\n", t0, T);
    printf("  方法                 最大单帧跳变   最大速度突变     过渡期内姿势求值次数(60fps)\n");
    const char* nm[] = {"硬切", "线性交叉淡化", "SmoothStep 淡化", "惯性化（五次多项式）"};
    double x0 = a(t0) - b(t0), v0 = ((a(t0) - a(t0 - dt)) - (b(t0) - b(t0 - dt))) / dt;
    for (int m = 0; m < 4; ++m) {
        vector<double> xs;
        for (int f = -10; f <= 80; ++f) {
            double t = t0 + f * dt, x;
            if (f < 0) x = a(t);
            else {
                double u = (t - t0) / T, al = clamp(u, 0.0, 1.0);
                if (m == 0) x = b(t);
                else if (m == 1) x = (1 - al) * a(t) + al * b(t);
                else if (m == 2) { double s = al * al * (3 - 2 * al); x = (1 - s) * a(t) + s * b(t); }
                else x = b(t) + inertialOffset(x0, v0, T, t - t0);
            }
            xs.push_back(x);
        }
        double maxJump = 0, maxDv = 0;
        for (size_t i = 1; i < xs.size(); ++i) maxJump = max(maxJump, fabs(xs[i] - xs[i - 1]));
        for (size_t i = 2; i < xs.size(); ++i) maxDv = max(maxDv, fabs((xs[i] - xs[i - 1]) - (xs[i - 1] - xs[i - 2])) / dt);
        int evals = m == 0 ? 12 : (m == 3 ? 12 : 24);
        printf("  %-20s %8.2f°      %9.1f°/s          %d\n", nm[m], maxJump, maxDv, evals);
    }
    double natural = 0; for (int f = -10; f <= 0; ++f) { double t = t0 + f * dt; natural = max(natural, fabs(a(t) - a(t - dt))); }
    printf("  参考：源动画自身每帧最大变化 %.2f°；切换瞬间位置差 x0=%.1f°，速度差 v0=%.1f°/s\n", natural, x0, v0);
}

// ============================================================ E8 IK：两骨骼解析解 与 FABRIK
static void E8() {
    printf("\n==================== E8 IK：Two Bone IK（解析）与 FABRIK（迭代） ====================\n");
    const float L1 = 30, L2 = 28; V3 A{0, 0, 0}, pole{0, -1, 0};
    auto twoBone = [&](V3 T, V3& elbow, V3& end) {
        V3 d = T - A; float dist = len(d); V3 dir = d * (1 / dist);
        float dc = clamp(dist, fabsf(L1 - L2) + 1e-3f, L1 + L2 - 1e-4f);
        float cosA = (L1 * L1 + dc * dc - L2 * L2) / (2 * L1 * dc), sinA = sqrtf(max(0.f, 1 - cosA * cosA));
        V3 n = norm(cross(dir, pole)); V3 bend = norm(cross(n, dir));
        elbow = A + dir * (L1 * cosA) + bend * (L1 * sinA);
        end = A + dir * dc;
    };
    int reach = 0, unreach = 0; double maxErr = 0, maxLen = 0, maxGap = 0, poleBad = 0;
    vector<V3> tg(1000000);
    for (auto& t : tg) t = randDir() * urand(5, 70);
    for (int i = 0; i < 20000; ++i) {
        V3 e, f; twoBone(tg[i], e, f);
        maxLen = max({maxLen, (double)fabsf(len(e - A) - L1), (double)fabsf(len(f - e) - L2)});
        float dist = len(tg[i]);
        if (dist <= L1 + L2 && dist >= fabsf(L1 - L2)) { ++reach; maxErr = max(maxErr, (double)len(f - tg[i])); }
        else { ++unreach; maxGap = max(maxGap, fabs((double)len(f - tg[i]) - fabs(dist - (L1 + L2)))); }
        V3 dir = norm(tg[i]), pp = pole - dir * dot(pole, dir), ep = e - dir * dot(e, dir);
        if (len(pp) > 1e-3f && dot(ep, pp) < -1e-3f) ++poleBad;  // 肘部必须在 pole 那一侧
    }
    double tIK = bestMs(5, [&] { V3 e, f, acc{}; for (auto& t : tg) { twoBone(t, e, f); acc = acc + e; } g_sink += acc.x; });
    printf("Two Bone IK（上臂 %.0f / 前臂 %.0f，pole=-Y）2 万个随机目标：\n", L1, L2);
    printf("  可达 %d 个：末端误差最大 %.4f；不可达 %d 个：手臂伸直指向目标；骨长误差最大 %.4f；肘部偏离 pole 侧 %d 次\n", reach, maxErr, unreach, maxLen, (int)poleBad);
    printf("  耗时 %.1f ns/次（100 万次 %.2f ms）\n", tIK * 1e6 / tg.size(), tIK);

    // FABRIK：5 节链，每节 10
    const int NJ = 6; float seg = 10;
    auto fabrik = [&](V3 T, vector<V3>& p, float tol, int maxIt) {
        V3 root = p[0]; int it = 0;
        for (; it < maxIt; ++it) {
            if (len(p[NJ - 1] - T) < tol) break;
            p[NJ - 1] = T;
            for (int i = NJ - 2; i >= 0; --i) p[i] = p[i + 1] + norm(p[i] - p[i + 1]) * seg;
            p[0] = root;
            for (int i = 1; i < NJ; ++i) p[i] = p[i - 1] + norm(p[i] - p[i - 1]) * seg;
        }
        return it;
    };
    for (float tol : {1.0f, 0.1f, 0.01f}) {
        double sumIt = 0; int maxIt = 0, fail = 0; const int N = 20000;
        auto t0 = Clock::now();
        for (int k = 0; k < N; ++k) {
            vector<V3> p(NJ); for (int i = 0; i < NJ; ++i) p[i] = {seg * i, 0.001f * i * i, 0};
            V3 T = randDir() * urand(5, 45);
            int it = fabrik(T, p, tol, 50); sumIt += it; maxIt = max(maxIt, it); if (it == 50) ++fail;
        }
        double us = chrono::duration<double, micro>(Clock::now() - t0).count() / N;
        printf("FABRIK 5 节链（每节 10，可达目标）容差 %.2f：平均 %.1f 次迭代，最多 %d 次，50 次未收敛 %d 个，%.2f us/次\n", tol, sumIt / N, maxIt, fail, us);
    }
}

// ============================================================ E9 动画压缩：去常量轨 → 量化 → 关键帧精简；误差沿层级累积
static void E9() {
    printf("\n==================== E9 动画压缩与误差累积 ====================\n");
    Skeleton sk = mannequin();
    const int NB = sk.n(), FR = 121;  // 4 s @30fps
    vector<int> depth(NB, 0); for (int i = 1; i < NB; ++i) depth[i] = depth[sk.parent[i]] + 1;
    vector<vector<Xf>> clip(FR, vector<Xf>(NB));
    vector<int> kind(NB);  // 0 静止 1 摆动
    vector<V3> axis(NB), bone(NB); vector<float> A1(NB), A2(NB), ph(NB), ph2(NB);
    for (int b = 0; b < NB; ++b) {
        kind[b] = (b < 2 || urand(0, 1) > 0.3f) ? 1 : 0;
        axis[b] = randDir(); A1[b] = urand(5, 40); A2[b] = urand(0, 10); ph[b] = urand(0, 6.28f); ph2[b] = urand(0, 6.28f);
        bone[b] = randDir() * urand(5, 15);
    }
    for (int f = 0; f < FR; ++f) for (int b = 0; b < NB; ++b) {
        float t = f / 30.f; Xf& x = clip[f][b];
        Q rest = qaxis(axis[(b * 7 + 1) % NB], 20);
        x.r = kind[b] ? qmul(qaxis(axis[b], A1[b] * sinf(2 * (float)PI * t + ph[b]) + A2[b] * sinf(4 * (float)PI * t + ph2[b])), rest) : rest;
        x.t = b == 0 ? V3{300 * t, 0, 0} : b == 1 ? V3{0, 0, 95 + 3 * sinf(4 * (float)PI * t)} : bone[b];
    }
    size_t raw = (size_t)NB * FR * 40;
    int animRot = 0, animTr = 0; for (int b = 0; b < NB; ++b) { animRot += kind[b]; }
    animTr = 2;
    size_t sA = (size_t)animRot * FR * 16 + (size_t)(NB - animRot) * 16 + (size_t)animTr * FR * 12 + (size_t)(NB - animTr) * 12 + (size_t)NB * 12;
    printf("%d 骨骼 × %d 帧（4 s @30fps），其中旋转有动画的骨骼 %d 根，平移有动画的 2 根，缩放全常量\n", NB, FR, animRot);
    printf("  阶段                                   大小(B)    压缩比   局部旋转最大误差\n");
    printf("  0 原始（每帧每骨 Q16+T12+S12 字节）    %8zu    1.00x    0\n", raw);
    printf("  1 去掉常量轨道（只存 1 个 key）         %8zu    %.2fx    0\n", sA, (double)raw / sA);
    // 量化：旋转保证 w>=0 后丢弃 w，xyz 各 16 位
    auto quant = [](Q q) { if (q.w < 0) q = neg(q); auto qq = [](float v) { return roundf((v * 0.5f + 0.5f) * 65535.f) / 65535.f * 2 - 1; };
        Q o{qq(q.x), qq(q.y), qq(q.z), 0}; o.w = sqrtf(max(0.f, 1 - o.x * o.x - o.y * o.y - o.z * o.z)); return o; };
    vector<vector<Xf>> qc = clip; double qErr = 0;
    for (int f = 0; f < FR; ++f) for (int b = 0; b < NB; ++b) { qc[f][b].r = quant(clip[f][b].r); qErr = max(qErr, qangle(qc[f][b].r, clip[f][b].r)); }
    size_t sB = (size_t)animRot * FR * 6 + (size_t)(NB - animRot) * 16 + (size_t)animTr * FR * 6 + animTr * 24 + (size_t)(NB - animTr) * 12 + (size_t)NB * 12;
    printf("  2 旋转丢 w + xyz 各 16 位，平移按范围 16 位 %7zu    %.2fx    %.4f°\n", sB, (double)raw / sB, qErr);
    // 关键帧精简：nlerp 插值误差 ≤ thr 时删掉中间帧
    for (double thr : {0.05, 0.2, 0.5}) {
        vector<vector<Xf>> rc = qc; size_t keys = 0; double lErr = 0;
        for (int b = 0; b < NB; ++b) {
            if (!kind[b]) continue;
            vector<int> kept = {0}; int a = 0;
            while (a < FR - 1) {
                int bb = a + 1;
                while (bb + 1 < FR) {
                    int c = bb + 1; bool ok = true;
                    for (int k = a + 1; k < c && ok; ++k) ok = qangle(nlerp(qc[a][b].r, qc[c][b].r, (float)(k - a) / (c - a)), qc[k][b].r) <= thr;
                    if (!ok) break; bb = c;
                }
                kept.push_back(bb); a = bb;
            }
            keys += kept.size();
            for (size_t k = 0; k + 1 < kept.size(); ++k)
                for (int f = kept[k]; f <= kept[k + 1]; ++f) {
                    rc[f][b].r = nlerp(qc[kept[k]][b].r, qc[kept[k + 1]][b].r, (float)(f - kept[k]) / (kept[k + 1] - kept[k]));
                    lErr = max(lErr, qangle(rc[f][b].r, clip[f][b].r));
                }
        }
        size_t sC = keys * 7 + (size_t)(NB - animRot) * 16 + (size_t)animTr * FR * 6 + animTr * 24 + (size_t)(NB - animTr) * 12 + (size_t)NB * 12;
        // 组件空间末端误差：局部误差沿父子链累积
        double cErr = 0, cErrDeep = 0, cRot = 0; int deepest = (int)(max_element(depth.begin(), depth.end()) - depth.begin());
        vector<Xf> c0(NB), c1(NB);
        for (int f = 0; f < FR; ++f) {
            fkLinear(sk.parent, clip[f].data(), c0.data(), NB); fkLinear(sk.parent, rc[f].data(), c1.data(), NB);
            for (int b = 0; b < NB; ++b) { cErr = max(cErr, (double)len(c0[b].t - c1[b].t)); cRot = max(cRot, qangle(c0[b].r, c1[b].r)); }
            cErrDeep = max(cErrDeep, (double)len(c0[deepest].t - c1[deepest].t));
        }
        printf("  3 关键帧精简 阈值 %.2f°：保留 %4.1f%% key  %7zu    %.2fx    %.4f°  → 组件空间旋转误差 %.3f°，位置误差 %.3f cm（最深骨骼 %s 深度 %d：%.3f cm）\n",
               thr, 100.0 * keys / (animRot * FR), sC, (double)raw / sC, lErr, cRot, cErr, sk.name[deepest].c_str(), depth[deepest], cErrDeep);
    }
}

// ============================================================ E10 角色动画整体开销：线程 / LOD / URO
struct Clip { int frames; vector<Xf> keys; };  // keys[f*NB+b]
static void samplePose(const Clip& c, int nb, int NBfull, float t, Xf* out) {
    float ft = fmodf(t * 30.f, (float)(c.frames - 1)); int f0 = (int)ft; float a = ft - f0;
    const Xf* k0 = &c.keys[(size_t)f0 * NBfull]; const Xf* k1 = &c.keys[(size_t)(f0 + 1) * NBfull];
    for (int b = 0; b < nb; ++b) { out[b].r = nlerp(k0[b].r, k1[b].r, a); out[b].t = lerp(k0[b].t, k1[b].t, a); out[b].s = k0[b].s; }
}
struct Char { float t, w; int bones; vector<Xf> p0, p1, comp; vector<array<float, 12>> skin; };
static void evalChar(const Skeleton& sk, const vector<Xf>& invBind, const Clip& A, const Clip& B, Char& ch) {
    int nb = ch.bones, NB = sk.n();
    samplePose(A, nb, NB, ch.t, ch.p0.data());
    samplePose(B, nb, NB, ch.t * 1.1f, ch.p1.data());
    for (int b = 0; b < nb; ++b) { ch.p0[b].r = nlerp(ch.p0[b].r, ch.p1[b].r, ch.w); ch.p0[b].t = lerp(ch.p0[b].t, ch.p1[b].t, ch.w); }
    fkLinear(sk.parent, ch.p0.data(), ch.comp.data(), nb);
    for (int b = 0; b < nb; ++b) {
        Xf m = compose(invBind[b], ch.comp[b]);
        V3 ex = rot(m.r, {1, 0, 0}), ey = rot(m.r, {0, 1, 0}), ez = rot(m.r, {0, 0, 1});
        ch.skin[b] = {ex.x, ey.x, ez.x, m.t.x, ex.y, ey.y, ez.y, m.t.y, ex.z, ey.z, ez.z, m.t.z};
    }
}
static void E10() {
    printf("\n==================== E10 角色动画开销：并行求值 / LOD 减骨 / URO 降频 ====================\n");
    Skeleton sk = mannequin();
    const int NB = sk.n(), NCH = 300;
    Clip A{121, {}}, B{91, {}};
    for (Clip* c : {&A, &B}) for (int f = 0; f < c->frames; ++f) { auto p = randomPose(NB); c->keys.insert(c->keys.end(), p.begin(), p.end()); }
    vector<Xf> bind(NB), invBind(NB); auto rp = randomPose(NB, 10); fkLinear(sk.parent, rp.data(), bind.data(), NB);
    for (int b = 0; b < NB; ++b) { invBind[b].r = conj(bind[b].r); invBind[b].t = rot(invBind[b].r, bind[b].t) * -1.f; }
    vector<Char> chars(NCH);
    for (auto& c : chars) { c.t = urand(0, 3); c.w = urand(0, 1); c.bones = NB; c.p0.resize(NB); c.p1.resize(NB); c.comp.resize(NB); c.skin.resize(NB); }
    const int FRAMES = 240;
    auto frame1 = [&](int f, const vector<int>& rate, const vector<int>& bones) {
        for (int i = 0; i < NCH; ++i) { if (f % rate[i]) continue; chars[i].bones = bones[i]; chars[i].t += 1 / 60.f; evalChar(sk, invBind, A, B, chars[i]); }
    };
    vector<int> rate1(NCH, 1), bonesFull(NCH, NB);
    double tFull = bestMs(3, [&] { for (int f = 0; f < FRAMES; ++f) frame1(f, rate1, bonesFull); }) / FRAMES;
    printf("%d 个角色，每个 %d 骨骼：采样 2 条动画 → 混合 → FK → 蒙皮矩阵\n", NCH, NB);
    printf("  单线程，全部满帧满骨骼              %.3f ms/帧（%.2f us/角色）\n", tFull, tFull * 1000 / NCH);
    // 并行：持久线程 + barrier（相当于 UE 把每个 SkeletalMeshComponent 的求值派发到 TaskGraph）
    unsigned hw = max(2u, thread::hardware_concurrency());
    for (unsigned T : {2u, 4u, 8u, hw}) {
        if (T > hw) continue;
        atomic<int> next{0}; atomic<bool> quit{false};
        barrier start((ptrdiff_t)T + 1), done((ptrdiff_t)T + 1);
        vector<thread> th;
        for (unsigned k = 0; k < T; ++k) th.emplace_back([&] {
            for (;;) {
                start.arrive_and_wait(); if (quit) break;
                for (int i; (i = next.fetch_add(4)) < NCH;) for (int j = i; j < min(NCH, i + 4); ++j) { chars[j].t += 1 / 60.f; evalChar(sk, invBind, A, B, chars[j]); }
                done.arrive_and_wait();
            }
        });
        double t = bestMs(3, [&] { for (int f = 0; f < FRAMES; ++f) { next = 0; start.arrive_and_wait(); done.arrive_and_wait(); } }) / FRAMES;
        quit = true; start.arrive_and_wait(); for (auto& x : th) x.join();
        printf("  %2u 个工作线程并行求值                %.3f ms/帧（加速 %.2fx）\n", T, t, tFull / t);
        if (T == hw) break;
    }
    // LOD + URO：近 20% 满帧满骨；中 30% 隔 1 帧更新；远 50% 每 4 帧更新且只算前 23 根主干骨骼
    vector<int> rate(NCH), bones(NCH);
    for (int i = 0; i < NCH; ++i) { double r = (double)i / NCH; rate[i] = r < 0.2 ? 1 : r < 0.5 ? 2 : 4; bones[i] = r < 0.5 ? NB : CORE_BONES; }
    double tOpt = bestMs(3, [&] { for (int f = 0; f < FRAMES; ++f) frame1(f, rate, bones); }) / FRAMES;
    double theory = 0.2 + 0.3 / 2 + 0.5 / 4 * (double)CORE_BONES / NB;
    printf("  单线程，LOD 减骨 + URO 降频           %.3f ms/帧（%.1f%% 的工作量；理论 %.1f%%）\n", tOpt, 100 * tOpt / tFull, 100 * theory);
    printf("  （硬件线程数 %u；绝对值与机器有关，请看相对关系）\n", hw);
}

// ============================================================ E11 四元数基础（白话篇 B3 的全部数字）
struct M3 { float m[9]; };  // 行主序，列向量约定：v' = M·v
static M3 m3mul(const M3& A, const M3& B) { M3 C; for (int i = 0; i < 3; ++i) for (int j = 0; j < 3; ++j) { float s = 0; for (int k = 0; k < 3; ++k) s += A.m[i * 3 + k] * B.m[k * 3 + j]; C.m[i * 3 + j] = s; } return C; }
static V3 m3v(const M3& A, V3 v) { return {A.m[0] * v.x + A.m[1] * v.y + A.m[2] * v.z, A.m[3] * v.x + A.m[4] * v.y + A.m[5] * v.z, A.m[6] * v.x + A.m[7] * v.y + A.m[8] * v.z}; }
static M3 q2m(Q q) {
    float x = q.x, y = q.y, z = q.z, w = q.w;
    return {{1 - 2 * (y * y + z * z), 2 * (x * y - z * w), 2 * (x * z + y * w), 2 * (x * y + z * w), 1 - 2 * (x * x + z * z), 2 * (y * z - x * w),
             2 * (x * z - y * w), 2 * (y * z + x * w), 1 - 2 * (x * x + y * y)}};
}
struct D3 { double m[9]; };
static D3 dmul(const D3& A, const D3& B) { D3 C; for (int i = 0; i < 3; ++i) for (int j = 0; j < 3; ++j) { double s = 0; for (int k = 0; k < 3; ++k) s += A.m[i * 3 + k] * B.m[k * 3 + j]; C.m[i * 3 + j] = s; } return C; }
static D3 daxis(int ax, double deg) {
    double c = cos(D2R(deg)), s = sin(D2R(deg));
    if (ax == 0) return {{1, 0, 0, 0, c, -s, 0, s, c}};
    if (ax == 1) return {{c, 0, s, 0, 1, 0, -s, 0, c}};
    return {{c, -s, 0, s, c, 0, 0, 0, 1}};
}
// 本 demo 的欧拉角约定：先绕 X 转 roll，再绕 Y 转 pitch，最后绕 Z 转 yaw（都绕世界轴）= Rz(yaw)·Ry(pitch)·Rx(roll)
static D3 deuler(double yaw, double pitch, double roll) { return dmul(daxis(2, yaw), dmul(daxis(1, pitch), daxis(0, roll))); }
static double wrap180d(double a) { a = fmod(a + 180.0, 360.0); if (a < 0) a += 360.0; return a - 180.0; }
static double dangle(const D3& A, const D3& B) {  // 两个旋转矩阵之间的夹角（度）
    double tr = 0; for (int i = 0; i < 3; ++i) for (int k = 0; k < 3; ++k) tr += A.m[k * 3 + i] * B.m[k * 3 + i];
    double c = max(-1.0, min(1.0, (tr - 1) / 2)); return R2D(acos(c));
}

static void E11() {
    printf("\n==================== E11 四元数基础：从复数旋转到 q·v·q⁻¹ ====================\n");
    // (a) 二维：复数相乘 = 角度相加
    complex<double> a = polar(1.0, D2R(30)), b = polar(1.0, D2R(45)), c = a * b;
    printf("(a) 二维：(cos30°+i·sin30°)×(cos45°+i·sin45°) = %.4f + %.4fi，角度 %.1f°，长度 %.4f（转 30° 再转 45° = 转 75°）\n", c.real(), c.imag(), R2D(arg(c)), abs(c));
    // (b) 轴角 → 四元数
    printf("(b) 绕单位轴 n 转 θ：q = (w, x, y, z) = (cos θ/2, n·sin θ/2)\n");
    struct Item { const char* nm; V3 ax; double deg; } items[] = {
        {"不转（单位四元数）", {0, 0, 1}, 0}, {"绕 Z 轴 90°", {0, 0, 1}, 90}, {"绕 Z 轴 180°", {0, 0, 1}, 180}, {"绕 X 轴 90°", {1, 0, 0}, 90},
        {"绕 (1,1,1) 轴 120°", {1, 1, 1}, 120}, {"绕 Z 轴 360°", {0, 0, 1}, 360}};
    for (auto& it : items) { Q q = qaxis(it.ax, it.deg); printf("    %-20s w=%7.4f x=%7.4f y=%7.4f z=%7.4f  |q|=%.4f\n", it.nm, q.w, q.x, q.y, q.z, sqrtf(qdot(q, q))); }
    {
        Q q = qaxis({0, 0, 1}, 90); V3 v = rot(q, {1, 0, 0});
        printf("    例：q(绕Z 90°) 作用于 (1,0,0) → (%.4f, %.4f, %.4f)，即 X 轴被转到 Y 轴\n", v.x, v.y, v.z);
        Q g = qaxis(norm({1, 1, 1}), 120); double ang = R2D(2 * acos(g.w)); V3 n = norm({g.x, g.y, g.z});
        printf("    反推：θ = 2·acos(w) = %.2f°，n = (x,y,z)/sin(θ/2) = (%.4f, %.4f, %.4f)\n", ang, n.x, n.y, n.z);
    }
    // (c) 三种方式旋转向量结果一致
    double e1 = 0, e2 = 0;
    for (int k = 0; k < 100000; ++k) {
        V3 n = randDir(), v = randDir() * urand(1, 100); double th = urand(-180, 180);
        Q q = qaxis(n, th);
        Q pv{v.x, v.y, v.z, 0}; Q r = qmul(qmul(q, pv), conj(q));                     // 夹心：q·v·q⁻¹
        float ct = cosf((float)D2R(th)), st = sinf((float)D2R(th));
        V3 rod = v * ct + cross(n, v) * st + n * (dot(n, v) * (1 - ct));              // 罗德里格斯公式
        V3 fast = rot(q, v);                                                            // 展开后的快速写法
        e1 = max(e1, (double)len(V3{r.x, r.y, r.z} - rod)); e2 = max(e2, (double)len(fast - rod));
    }
    printf("(c) 10 万个随机向量：q·v·q⁻¹ 与罗德里格斯公式最大差 %.2e，展开公式与罗德里格斯最大差 %.2e（长度 1~100，float 舍入）\n", e1, e2);
    // (d) 半角与 q / -q
    {
        Q q1 = qaxis({0, 0, 1}, 60), q2 = qaxis({0, 0, 1}, 420); V3 v{3, 4, 5}; V3 r1 = rot(q1, v), r2 = rot(q2, v), r3 = rot(neg(q1), v);
        printf("(d) 绕 Z 60°：q=(%.4f,0,0,%.4f)；绕 Z 420°（多转一圈）：q=(%.4f,0,0,%.4f) = −q\n", q1.w, q1.z, q2.w, q2.z);
        printf("    两者作用于 (3,4,5)：(%.4f,%.4f,%.4f) vs (%.4f,%.4f,%.4f)；−q 也是 (%.4f,%.4f,%.4f) → 同一个旋转\n", r1.x, r1.y, r1.z, r2.x, r2.y, r2.z, r3.x, r3.y, r3.z);
    }
    // (e) 乘法不交换
    {
        Q qx = qaxis({1, 0, 0}, 90), qy = qaxis({0, 1, 0}, 90);
        Q xy = qmul(qy, qx), yx = qmul(qx, qy);  // qmul(a,b)：先 b 后 a（与 UE 的 FQuat 相同）
        V3 f1 = rot(xy, {1, 0, 0}), f2 = rot(yx, {1, 0, 0}), u1 = rot(xy, {0, 0, 1}), u2 = rot(yx, {0, 0, 1});
        printf("(e) 先绕 X 90° 再绕 Y 90°：前方(1,0,0)→(%.0f,%.0f,%.0f)，上方(0,0,1)→(%.0f,%.0f,%.0f)\n", f1.x, f1.y, f1.z, u1.x, u1.y, u1.z);
        printf("    先绕 Y 90° 再绕 X 90°：前方(1,0,0)→(%.0f,%.0f,%.0f)，上方(0,0,1)→(%.0f,%.0f,%.0f)；两种顺序的结果相差 %.1f°\n", f2.x, f2.y, f2.z, u2.x, u2.y, u2.z, qangle(xy, yx));
    }
    // (f) 共轭 = 逆；“差值”
    {
        double e = 0; for (int k = 0; k < 10000; ++k) { Q q = qaxis(randDir(), urand(-180, 180)); Q i = qmul(q, conj(q)); e = max(e, (double)max({fabsf(i.x), fabsf(i.y), fabsf(i.z), fabsf(i.w - 1)})); }
        Q A = qaxis({0, 0, 1}, 30), B = qaxis({0, 0, 1}, 75); Q dlt = qmul(B, conj(A));
        printf("(f) q·q* 与单位四元数最大差 %.2e（共轭就是逆）；Yaw75° · (Yaw30°)⁻¹ = 绕 Z 转 %.1f°（“两个旋转的差”）\n", e, R2D(2 * acos(min(1.f, dlt.w))));
    }
    // (g) 二维手臂 FK 的手算例子
    {
        vector<int> par = {-1, 0, 1}; vector<Xf> loc(3), cp(3);
        loc[0].r = qaxis({0, 0, 1}, 30); loc[1].t = {30, 0, 0}; loc[1].r = qaxis({0, 0, 1}, 45); loc[2].t = {28, 0, 0};
        fkLinear(par, loc.data(), cp.data(), 3);
        printf("(g) 肩转 30°、上臂长 30、肘再转 45°、前臂长 28：肘在 (%.2f, %.2f)，手在 (%.2f, %.2f)，手的朝向 %.0f°\n", cp[1].t.x, cp[1].t.y, cp[2].t.x, cp[2].t.y, yawDeg(cp[2].r));
    }
    // (h) 性能：组合旋转 vs 旋转向量
    const int N = 10000000; vector<Q> qs(1024); vector<M3> ms(1024); vector<V3> vs(1024);
    for (int i = 0; i < 1024; ++i) { qs[i] = qaxis(randDir(), urand(-180, 180)); ms[i] = q2m(qs[i]); vs[i] = randDir(); }
    double tqq = bestMs(3, [&] { Q acc{}; for (int i = 0; i < N; ++i) { Q r = qmul(qs[i & 1023], qs[(i * 7 + 1) & 1023]); acc.x += r.x; } g_sink += acc.x; });
    double tmm = bestMs(3, [&] { float acc = 0; for (int i = 0; i < N; ++i) { M3 r = m3mul(ms[i & 1023], ms[(i * 7 + 1) & 1023]); acc += r.m[0] + r.m[4] + r.m[8]; } g_sink += acc; });
    double tqv = bestMs(3, [&] { float acc = 0; for (int i = 0; i < N; ++i) { V3 r = rot(qs[i & 1023], vs[(i * 7 + 1) & 1023]); acc += r.x; } g_sink += acc; });
    double tmv = bestMs(3, [&] { float acc = 0; for (int i = 0; i < N; ++i) { V3 r = m3v(ms[i & 1023], vs[(i * 7 + 1) & 1023]); acc += r.x; } g_sink += acc; });
    double tqm = bestMs(3, [&] { float acc = 0; for (int i = 0; i < N; ++i) { M3 r = q2m(qs[i & 1023]); acc += r.m[0] + r.m[4] + r.m[8]; } g_sink += acc; });
    printf("(h) 耗时（%d 次，ns/次）：四元数×四元数 %.2f，矩阵×矩阵 %.2f；四元数转向量 %.2f，矩阵转向量 %.2f；四元数→矩阵 %.2f\n",
           N, tqq * 1e6 / N, tmm * 1e6 / N, tqv * 1e6 / N, tmv * 1e6 / N, tqm * 1e6 / N);
    printf("    存储：四元数 4 个 float（16 B），3×3 旋转矩阵 9 个 float（36 B）\n");
}

// ============================================================ E12 欧拉角的问题：万向节锁、多解、插值乱转、矩阵漂移
static void E12() {
    printf("\n==================== E12 欧拉角与矩阵的问题 ====================\n");
    printf("约定：(yaw, pitch, roll) = 先绕 X 转 roll，再绕 Y 转 pitch，最后绕 Z 转 yaw\n");
    // (a) 万向节锁
    for (double p : {0.0, 45.0, 90.0}) {
        D3 base = deuler(30, p, 0);
        printf("(a) pitch=%2.0f°：(30,%2.0f,0) 与 (0,%2.0f,30) 相差 %6.2f°，与 (0,%2.0f,-30) 相差 %6.2f°，与 (60,%2.0f,30) 相差 %6.2f°\n",
               p, p, p, dangle(base, deuler(0, p, 30)), p, dangle(base, deuler(0, p, -30)), p, dangle(base, deuler(60, p, 30)));
    }
    // pitch=90 时 yaw、roll 各自的转轴（世界空间）
    {
        D3 R = deuler(0, 90, 0); double rx = R.m[0 * 3 + 0], ry = R.m[1 * 3 + 0], rz = R.m[2 * 3 + 0];  // 局部 X 轴（roll 轴）在世界里的方向
        printf("    pitch=90° 时：yaw 绕世界 Z (0,0,1)，roll 绕的局部 X 轴此时指向世界 (%.0f,%.0f,%.0f) —— 两根轴重合，少了一个自由度\n", rx, ry, rz);
    }
    // (b) 一个朝向，多组欧拉角
    printf("(b) (0,0,0) 与 (180,180,180) 相差 %.4f°；(90,0,0) 与 (450,0,0) 相差 %.4f°；(90,0,0) 与 (-270,0,0) 相差 %.4f°\n",
           dangle(deuler(0, 0, 0), deuler(180, 180, 180)), dangle(deuler(90, 0, 0), deuler(450, 0, 0)), dangle(deuler(90, 0, 0), deuler(-270, 0, 0)));
    // (c) 越过“头顶”时，从矩阵反算的欧拉角会突然跳变
    {
        auto toEuler = [](const D3& R, double& y, double& p, double& r) {  // R = Rz(y)·Ry(p)·Rx(r)
            p = R2D(asin(max(-1.0, min(1.0, -R.m[6])))); y = R2D(atan2(R.m[3], R.m[0])); r = R2D(atan2(R.m[7], R.m[8]));
        };
        double py = 0, pp = 0, pr = 0, maxJump = 0, stepAng = 0, jy0 = 0, jr0 = 0, jy1 = 0, jr1 = 0, jp = 0; D3 prevM{};
        const int N = 1000;
        for (int k = 0; k <= N; ++k) {
            double pitch = 80 + 20.0 * k / N;  // 绕 pitch 轴从 80° 匀速转到 100°（翻过头顶）
            D3 M = dmul(daxis(2, 5), daxis(1, pitch)); double y, pt, r; toEuler(M, y, pt, r);
            if (k) {
                double j = max(fabs(wrap180d(y - py)), fabs(wrap180d(r - pr)));
                if (j > maxJump) { maxJump = j; jy0 = py; jr0 = pr; jy1 = y; jr1 = r; jp = pt; stepAng = dangle(prevM, M); }
            }
            py = y; pp = pt; pr = r; prevM = M;
        }
        D3 M0 = deuler(jy0, jp, jr0), M1 = deuler(jy1, jp, jr1), mid = deuler((jy0 + jy1) / 2, jp, (jr0 + jr1) / 2);
        printf("(c) 朝向绕 pitch 轴从 80° 匀速转到 100°（每步 %.2f°），每步都反算成欧拉角：\n", 20.0 / N);
        printf("    越过 90° 的那一步，朝向只变了 %.3f°，欧拉角却从 (yaw %.0f, roll %.0f) 跳到 (yaw %.0f, roll %.0f)（pitch≈%.2f°）\n", stepAng, jy0, jr0, jy1, jr1, jp);
        printf("    如果在这两帧之间对欧拉角做插值，中点的朝向离两端都有 %.1f°（真实中点离两端各 %.3f°）\n", min(dangle(mid, M0), dangle(mid, M1)), stepAng / 2);
    }
    // (d) 漂移：小旋转累乘 100 万次，不做修正
    {
        Q dq = qaxis(norm({0.3f, -0.5f, 0.8f}), 0.37); M3 dm = q2m(dq);
        Q q{}; M3 M = {{1, 0, 0, 0, 1, 0, 0, 0, 1}}; Q qn{};
        const int STEPS = 1000000;
        for (int i = 0; i < STEPS; ++i) { q = qmul(dq, q); M = m3mul(dm, M); qn = qnorm(qmul(dq, qn)); }
        double orth = 0; for (int i = 0; i < 3; ++i) for (int j = 0; j < 3; ++j) { double s = 0; for (int k = 0; k < 3; ++k) s += (double)M.m[k * 3 + i] * M.m[k * 3 + j]; orth = max(orth, fabs(s - (i == j))); }
        V3 u{1, 0, 0}; double lm = len(m3v(M, u)), lq = len(rot(q, u)), lqn = len(rot(qn, u));
        printf("(d) 绕同一轴每次转 0.37°，连乘 %d 次（float，中途不修正）：\n", STEPS);
        printf("    矩阵：|MᵀM − I| 最大 %.2e，单位向量变换后长度 %.6f（模型被拉伸 / 压扁 %.4f%%）\n", orth, lm, 100 * fabs(lm - 1));
        printf("    四元数：|q| = %.6f，单位向量变换后长度 %.6f；每步归一化的四元数：%.6f\n", sqrtf(qdot(q, q)), lq, lqn);
    }
}

int main(int argc, char** argv) {
    setvbuf(stdout, nullptr, _IONBF, 0);
    printf("anim_demo — UE 动画系统电子书配套实测（MSVC /O2 x64，float，单位：角度°、长度 cm）\n");
    // 不带参数：全部运行；带参数（如 anim_demo.exe E11 E12）：只跑指定实验
    void (*all[])() = {E1, E2, E3, E4, E5, E6, E7, E8, E9, E10, E11, E12};
    for (int i = 0; i < 12; ++i) {
        bool run = argc == 1;
        for (int k = 1; k < argc; ++k) run |= atoi(argv[k] + (argv[k][0] == 'E' ? 1 : 0)) == i + 1;
        if (run) all[i]();
    }
    printf("\n[done] sink=%.3f\n", (double)g_sink);
    return 0;
}
