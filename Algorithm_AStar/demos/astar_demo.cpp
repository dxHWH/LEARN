// astar_demo.cpp —— 《A* 寻路（游戏客户端面试向）》配套实测
// 编译（MSVC）：cl /nologo /std:c++latest /EHsc /O2 /utf-8 astar_demo.cpp
// 编译（GCC） ：g++ -std=c++20 -O2 astar_demo.cpp
//
// 统一约定：512x512 格子图，8 方向移动，禁止“切角”（斜走时两侧的直邻格都必须可走），
//           直走代价 1000、斜走 1414（整数 ×1000，避免浮点误差导致平局判断不一致）。
//           禁止切角时，“8 方向可达” 等价于 “4 连通”，所以用 4 连通标记连通块。
//
// 实验清单
//   E1 五种地图 × 五种算法总表：BFS / Dijkstra / 贪心最佳优先 / A*(Octile) / JPS
//   E2 启发函数对比：h=0 / 切比雪夫 / Octile / 欧几里得(整数) / 曼哈顿
//   E3 加权 A*：w = 1 ~ 5 与贪心，展开数 vs 代价超出
//   E4 平局决胜（tie-breaking）：只比 f / f 相同取 h 小 / f 相同取 h 大
//   E5 工程数据结构：open list 四种实现 × 节点状态三种复位方式（长查询 / 短查询）
//   E6 JPS vs A*：展开数、扫描格子数、耗时
//   E7 分帧寻路（time-slicing）：每帧展开预算 vs 帧数与单帧最大耗时
//   E8 目标不可达：A* 搜遍整个连通块 vs 连通块编号 O(1) 预判
//   E9 帧同步确定性：两种堆实现 + 只比 f 时路径不同；全序比较后一致
//   E10 路径平滑（拉绳 / string pulling）：拐点数、路径长度
#include <algorithm>
#include <chrono>
#include <climits>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <queue>
#include <random>
#include <set>
#include <string>
#include <tuple>
#include <unordered_map>
#include <vector>

using i64 = long long;
using u32 = uint32_t;
using u64 = uint64_t;
using Clock = std::chrono::steady_clock;
static double ms_since(Clock::time_point t0) { return std::chrono::duration<double, std::milli>(Clock::now() - t0).count(); }

static const int CO = 1000, CD = 1414;   // 直走 / 斜走代价

// ================================================================ 地图
struct Map {
    std::string name;
    int W = 0, H = 0;
    std::vector<uint8_t> blk;   // 1 = 障碍
    bool walk(int x, int y) const { return (unsigned)x < (unsigned)W && (unsigned)y < (unsigned)H && !blk[y * W + x]; }
};
static const int DX[8] = {1, -1, 0, 0, 1, 1, -1, -1};
static const int DY[8] = {0, 0, 1, -1, 1, -1, 1, -1};
static inline bool canMove(const Map& m, int x, int y, int d) {
    if (!m.walk(x + DX[d], y + DY[d])) return false;
    if (d >= 4) return m.walk(x + DX[d], y) && m.walk(x, y + DY[d]);   // 禁止切角
    return true;
}

static Map make_open(int n) { Map m; m.name = "开阔地"; m.W = m.H = n; m.blk.assign(n * n, 0); return m; }
static Map make_random(int n, double p, u32 seed) {
    Map m; m.name = "随机障碍25%"; m.W = m.H = n; m.blk.assign(n * n, 0);
    std::mt19937 rng(seed); std::uniform_real_distribution<double> U(0, 1);
    for (auto& b : m.blk) b = U(rng) < p;
    return m;
}
// U 型陷阱：杯口朝左，起点在杯内，终点在杯底右侧 —— 启发函数会把搜索“吸”进杯底
static const int TRAP_X0 = 150, TRAP_X1 = 300, TRAP_Y0 = 100, TRAP_Y1 = 412;
static Map make_trap(int n) {
    Map m = make_open(n); m.name = "U型陷阱";
    for (int y = TRAP_Y0; y <= TRAP_Y1; ++y) m.blk[y * n + TRAP_X1] = 1;
    for (int x = TRAP_X0; x <= TRAP_X1; ++x) { m.blk[TRAP_Y0 * n + x] = 1; m.blk[TRAP_Y1 * n + x] = 1; }
    return m;
}
static Map make_maze(int n, u32 seed) {   // 递归回溯（迭代版）完美迷宫，通道宽 1
    Map m; m.name = "迷宫"; m.W = m.H = n; m.blk.assign(n * n, 1);
    int C = (n - 1) / 2;
    std::mt19937 rng(seed);
    std::vector<uint8_t> vis(C * C, 0);
    std::vector<int> st = {0};
    vis[0] = 1; m.blk[1 * n + 1] = 0;
    while (!st.empty()) {
        int c = st.back(), cx = c % C, cy = c / C;
        int cand[4], k = 0;
        const int ddx[4] = {1, -1, 0, 0}, ddy[4] = {0, 0, 1, -1};
        for (int d = 0; d < 4; ++d) {
            int nx = cx + ddx[d], ny = cy + ddy[d];
            if (nx >= 0 && ny >= 0 && nx < C && ny < C && !vis[ny * C + nx]) cand[k++] = d;
        }
        if (!k) { st.pop_back(); continue; }
        int d = cand[rng() % k];
        int nx = cx + ddx[d], ny = cy + ddy[d];
        vis[ny * C + nx] = 1;
        m.blk[(2 * cy + 1 + ddy[d]) * n + (2 * cx + 1 + ddx[d])] = 0;
        m.blk[(2 * ny + 1) * n + (2 * nx + 1)] = 0;
        st.push_back(ny * C + nx);
    }
    return m;
}
static Map make_rooms(int n, u32 seed) {  // 32x32 的房间网格，每面墙 2 扇宽 3 的门，房间里放家具
    Map m = make_open(n); m.name = "房间";
    std::mt19937 rng(seed);
    for (int y = 0; y < n; ++y) for (int x = 0; x < n; ++x) if (x % 32 == 0 || y % 32 == 0) m.blk[y * n + x] = 1;
    int R = n / 32;
    for (int ry = 0; ry < R; ++ry) for (int rx = 0; rx < R; ++rx) {
        for (int k = 0; k < 2; ++k) {
            if (rx + 1 < R) { int o = 2 + rng() % 26; for (int t = 0; t < 3; ++t) m.blk[(ry * 32 + o + t) * n + (rx + 1) * 32] = 0; }
            if (ry + 1 < R) { int o = 2 + rng() % 26; for (int t = 0; t < 3; ++t) m.blk[((ry + 1) * 32) * n + rx * 32 + o + t] = 0; }
        }
        for (int k = 0; k < 2; ++k) {
            int w = 2 + rng() % 5, h = 2 + rng() % 5, x0 = rx * 32 + 4 + rng() % (24 - w), y0 = ry * 32 + 4 + rng() % (24 - h);
            for (int y = y0; y < y0 + h; ++y) for (int x = x0; x < x0 + w; ++x) m.blk[y * n + x] = 1;
        }
    }
    return m;
}

// 4 连通标记：返回每格的连通块编号，sizes 为各块大小
static std::vector<int> label4(const Map& m, std::vector<int>& sizes) {
    std::vector<int> c(m.W * m.H, -1), st;
    sizes.clear();
    const int ddx[4] = {1, -1, 0, 0}, ddy[4] = {0, 0, 1, -1};
    for (int i = 0; i < m.W * m.H; ++i) {
        if (m.blk[i] || c[i] >= 0) continue;
        int id = int(sizes.size()), sz = 0;
        c[i] = id; st.assign(1, i);
        while (!st.empty()) {
            int k = st.back(); st.pop_back(); ++sz;
            int x = k % m.W, y = k / m.W;
            for (int d = 0; d < 4; ++d) {
                int nx = x + ddx[d], ny = y + ddy[d];
                if (!m.walk(nx, ny)) continue;
                int j = ny * m.W + nx;
                if (c[j] < 0) { c[j] = id; st.push_back(j); }
            }
        }
        sizes.push_back(sz);
    }
    return c;
}

// ================================================================ 启发函数
enum Heur { H_ZERO, H_CHEB, H_OCTILE, H_EUCLID, H_MANH };
static const char* kHName[] = {"h=0（即 Dijkstra）", "切比雪夫 max(dx,dy)", "Octile 八方向距离", "欧几里得（取整）", "曼哈顿 dx+dy"};
static inline i64 heur(Heur h, int x, int y, int gx, int gy) {
    i64 dx = std::abs(x - gx), dy = std::abs(y - gy), mx = std::max(dx, dy), mn = std::min(dx, dy);
    switch (h) {
    case H_ZERO:   return 0;
    case H_CHEB:   return CO * mx;
    case H_OCTILE: return CO * mx + (CD - CO) * mn;
    case H_EUCLID: return (i64)(CO * std::sqrt(double(dx * dx + dy * dy)));
    case H_MANH:   return CO * (dx + dy);
    }
    return 0;
}
static inline i64 octile(int a, int b, int W) { return heur(H_OCTILE, a % W, a / W, b % W, b / W); }

// ================================================================ A* 主实现（惰性删除 + generation 复位，可分帧）
struct Cfg {
    Heur h = H_OCTILE;
    double w = 1.0;       // 加权 A*：f = g + w·h
    bool greedy = false;  // 贪心最佳优先：f = h
    int tie = 1;          // 0 只比 f；1 f 相同取 h 小（即 g 大）；2 f 相同取 h 大；3 全序 (f, h, 下标)
};
struct OpenE { i64 f, h; int i; };
struct OpenCmp {
    int tie;
    bool operator()(const OpenE& a, const OpenE& b) const {   // true = a 的优先级低于 b
        if (a.f != b.f) return a.f > b.f;
        if (tie == 1) return a.h > b.h;
        if (tie == 2) return a.h < b.h;
        if (tie == 3) return a.h != b.h ? a.h > b.h : a.i > b.i;
        return false;
    }
};

struct Search {
    const Map* m = nullptr;
    int W = 0, H = 0, N = 0;
    std::vector<i64> g;
    std::vector<int> par;
    std::vector<u32> seen, closed;   // == gen 表示“本次查询中有效”
    u32 gen = 0;
    Cfg cfg;
    int s = -1, t = -1, tx = 0, ty = 0;
    std::priority_queue<OpenE, std::vector<OpenE>, OpenCmp> open{OpenCmp{1}};
    i64 expanded = 0, pushes = 0, scanned = 0;

    void bind(const Map& mp) {
        m = &mp; W = mp.W; H = mp.H; N = W * H;
        g.assign(N, 0); par.assign(N, -1); seen.assign(N, 0); closed.assign(N, 0); gen = 0;
    }
    i64 hval(int i) const { return heur(cfg.h, i % W, i / W, tx, ty); }
    i64 fval(i64 gg, i64 hh) const {
        if (cfg.greedy) return hh;
        if (cfg.w == 1.0) return gg + hh;
        return gg + (i64)(cfg.w * double(hh));
    }
    void newGen() {
        if (++gen == 0) { std::fill(seen.begin(), seen.end(), 0); std::fill(closed.begin(), closed.end(), 0); gen = 1; }
    }
    void begin(int s_, int t_, const Cfg& c) {
        cfg = c; s = s_; t = t_; tx = t % W; ty = t / W;
        newGen();
        open = decltype(open)(OpenCmp{c.tie});
        expanded = pushes = scanned = 0;
        g[s] = 0; par[s] = -1; seen[s] = gen;
        i64 h = hval(s);
        open.push({fval(0, h), h, s}); pushes = 1;
    }
    // 返回 0 = 预算用完、尚未结束；1 = 找到；2 = 无解
    int step(i64 budget) {
        i64 done = 0;
        while (!open.empty()) {
            OpenE e = open.top(); open.pop();
            int i = e.i;
            if (closed[i] == gen) continue;            // 惰性删除：过期条目直接丢弃
            closed[i] = gen; ++expanded;
            if (i == t) return 1;
            int x = i % W, y = i / W;
            for (int d = 0; d < 8; ++d) {
                if (!canMove(*m, x, y, d)) continue;
                int j = i + DY[d] * W + DX[d];
                if (closed[j] == gen) continue;        // 不重开 closed 节点
                i64 ng = g[i] + (d < 4 ? CO : CD);
                if (seen[j] == gen && ng >= g[j]) continue;
                seen[j] = gen; g[j] = ng; par[j] = i;
                i64 h = hval(j);
                open.push({fval(ng, h), h, j}); ++pushes;
            }
            if (++done >= budget) return 0;
        }
        return 2;
    }
    i64 run(int s_, int t_, const Cfg& c) { begin(s_, t_, c); return step(LLONG_MAX) == 1 ? g[t] : -1; }

    std::vector<int> path() const {
        std::vector<int> p;
        for (int i = t; i != -1; i = par[i]) p.push_back(i);
        std::reverse(p.begin(), p.end());
        return p;
    }

    // ---------------- BFS：8 方向等权（只数步数），返回真实代价
    i64 bfs(int s_, int t_) {
        s = s_; t = t_; newGen(); expanded = 0;
        std::vector<int> q; q.reserve(1024); q.push_back(s);
        seen[s] = gen; par[s] = -1;
        for (size_t h = 0; h < q.size(); ++h) {
            int i = q[h]; ++expanded;
            if (i == t) break;
            int x = i % W, y = i / W;
            for (int d = 0; d < 8; ++d) {
                if (!canMove(*m, x, y, d)) continue;
                int j = i + DY[d] * W + DX[d];
                if (seen[j] == gen) continue;
                seen[j] = gen; par[j] = i; q.push_back(j);
            }
        }
        if (seen[t] != gen) return -1;
        i64 c = 0;
        for (int i = t; par[i] != -1; i = par[i]) { int d = std::abs(i - par[i]); c += (d == 1 || d == W) ? CO : CD; }
        return c;
    }

    // ---------------- JPS（跳点搜索），禁止切角的版本
    int jump(int x, int y, int dx, int dy) {
        for (;;) {
            ++scanned;
            if (!m->walk(x, y)) return -1;
            if (x == tx && y == ty) return y * W + x;
            if (dx && dy) {
                if (jump(x + dx, y, dx, 0) >= 0 || jump(x, y + dy, 0, dy) >= 0) return y * W + x;
                if (!(m->walk(x + dx, y) && m->walk(x, y + dy))) return -1;
            } else if (dx) {
                if ((m->walk(x, y - 1) && !m->walk(x - dx, y - 1)) || (m->walk(x, y + 1) && !m->walk(x - dx, y + 1))) return y * W + x;
            } else {
                if ((m->walk(x - 1, y) && !m->walk(x - 1, y - dy)) || (m->walk(x + 1, y) && !m->walk(x + 1, y - dy))) return y * W + x;
            }
            x += dx; y += dy;
        }
    }
    i64 jps(int s_, int t_) {
        begin(s_, t_, Cfg{});
        while (!open.empty()) {
            OpenE e = open.top(); open.pop();
            int i = e.i;
            if (closed[i] == gen) continue;
            closed[i] = gen; ++expanded;
            if (i == t) return g[t];
            int x = i % W, y = i / W;
            int dirs[8][2], k = 0;
            auto add = [&](int a, int b) { dirs[k][0] = a; dirs[k][1] = b; ++k; };
            if (par[i] < 0) {
                for (int d = 0; d < 8; ++d) if (canMove(*m, x, y, d)) add(DX[d], DY[d]);
            } else {
                int px = par[i] % W, py = par[i] / W;
                int dx = (x > px) - (x < px), dy = (y > py) - (y < py);
                if (dx && dy) {
                    bool v = m->walk(x, y + dy), hz = m->walk(x + dx, y);
                    if (v) add(0, dy);
                    if (hz) add(dx, 0);
                    if (v && hz) add(dx, dy);
                } else if (dx) {
                    bool nx = m->walk(x + dx, y), up = m->walk(x, y + 1), dn = m->walk(x, y - 1);
                    if (nx) { add(dx, 0); if (up) add(dx, 1); if (dn) add(dx, -1); }
                    if (up) add(0, 1);
                    if (dn) add(0, -1);
                } else {
                    bool nx = m->walk(x, y + dy), rt = m->walk(x + 1, y), lf = m->walk(x - 1, y);
                    if (nx) { add(0, dy); if (rt) add(1, dy); if (lf) add(-1, dy); }
                    if (rt) add(1, 0);
                    if (lf) add(-1, 0);
                }
            }
            for (int q = 0; q < k; ++q) {
                int j = jump(x + dirs[q][0], y + dirs[q][1], dirs[q][0], dirs[q][1]);
                if (j < 0 || closed[j] == gen) continue;
                i64 ng = g[i] + octile(i, j, W);
                if (seen[j] == gen && ng >= g[j]) continue;
                seen[j] = gen; g[j] = ng; par[j] = i;
                i64 h = hval(j);
                open.push({ng + h, h, j}); ++pushes;
            }
        }
        return -1;
    }
};

// ================================================================ 查询集
struct Query { int s, t; };
struct Case {
    Map m;
    std::vector<int> comp, sizes;
    int big = 0;
    std::vector<int> bigCells;
    std::vector<Query> qs;
    std::vector<i64> opt;   // Dijkstra 得到的最优代价
};
static void prepare(Case& c, int Q, u32 seed, bool trap) {
    c.comp = label4(c.m, c.sizes);
    c.big = int(std::max_element(c.sizes.begin(), c.sizes.end()) - c.sizes.begin());
    for (int i = 0; i < c.m.W * c.m.H; ++i) if (c.comp[i] == c.big) c.bigCells.push_back(i);
    std::mt19937 rng(seed);
    int W = c.m.W;
    while ((int)c.qs.size() < Q) {
        int s, t;
        if (trap) {
            s = (TRAP_Y0 + 10 + int(rng() % (TRAP_Y1 - TRAP_Y0 - 20))) * W + TRAP_X0 + 10 + int(rng() % (TRAP_X1 - TRAP_X0 - 20));
            t = (TRAP_Y0 + 10 + int(rng() % (TRAP_Y1 - TRAP_Y0 - 20))) * W + TRAP_X1 + 20 + int(rng() % 180);
        } else {
            s = c.bigCells[rng() % c.bigCells.size()];
            t = c.bigCells[rng() % c.bigCells.size()];
        }
        if (s != t) c.qs.push_back({s, t});
    }
}

struct Agg {
    double ms = 0; i64 exp = 0, scan = 0; int n = 0, sub = 0; double ratioSum = 0, ratioMax = 1;
    void add(double t, i64 e, i64 cost, i64 opt, i64 sc = 0) {
        ms += t; exp += e; scan += sc; ++n;
        double r = double(cost) / double(opt);
        ratioSum += r; ratioMax = std::max(ratioMax, r);
        if (cost != opt) ++sub;
    }
};

// ================================================================ E1
static void E1(std::vector<Case>& cs) {
    std::printf("\n========== E1 五种地图 × 五种算法（512x512，8 方向禁止切角，每张图 %zu 次随机查询）==========\n", cs[0].qs.size());
    Search S;
    for (auto& c : cs) {
        S.bind(c.m);
        int walk = 0; for (auto b : c.m.blk) walk += !b;
        // 先用 Dijkstra 求最优代价
        c.opt.clear();
        Agg dj;
        for (auto& q : c.qs) {
            Cfg cf; cf.h = H_ZERO;
            auto t0 = Clock::now(); i64 cost = S.run(q.s, q.t, cf); double t = ms_since(t0);
            c.opt.push_back(cost); dj.add(t, S.expanded, cost, cost);
        }
        double avgOpt = 0; for (auto o : c.opt) avgOpt += o / 1000.0; avgOpt /= c.opt.size();
        std::printf("\n  [%s] 可走 %.1f%%，最大连通块 %d 格，平均最优路径长 %.1f 格\n", c.m.name.c_str(), 100.0 * walk / c.m.blk.size(), c.sizes[c.big], avgOpt);
        std::printf("    %-22s %12s %12s %12s %10s\n", "算法", "平均展开", "平均耗时ms", "平均代价/最优", "非最优");
        auto row = [&](const char* nm, Agg& a) {
            std::printf("    %-22s %12.0f %12.3f %12.4f %9.0f%%\n", nm, double(a.exp) / a.n, a.ms / a.n, a.ratioSum / a.n, 100.0 * a.sub / a.n);
        };
        Agg bf, gr, as, jp;
        for (size_t k = 0; k < c.qs.size(); ++k) {
            auto& q = c.qs[k];
            auto t0 = Clock::now(); i64 cost = S.bfs(q.s, q.t); double t = ms_since(t0); bf.add(t, S.expanded, cost, c.opt[k]);
            Cfg g; g.greedy = true;
            t0 = Clock::now(); cost = S.run(q.s, q.t, g); t = ms_since(t0); gr.add(t, S.expanded, cost, c.opt[k]);
            t0 = Clock::now(); cost = S.run(q.s, q.t, Cfg{}); t = ms_since(t0); as.add(t, S.expanded, cost, c.opt[k]);
            t0 = Clock::now(); cost = S.jps(q.s, q.t); t = ms_since(t0); jp.add(t, S.expanded, cost, c.opt[k], S.scanned);
        }
        row("BFS（8 向等权）", bf);
        row("Dijkstra", dj);
        row("贪心最佳优先 f=h", gr);
        row("A*（Octile）", as);
        row("JPS 跳点搜索", jp);
    }
}

// ================================================================ E2
static void E2(std::vector<Case>& cs) {
    std::printf("\n========== E2 启发函数对比（A*，tie=取 h 小）==========\n");
    Search S;
    for (int ci : {1, 4}) {
        auto& c = cs[ci]; S.bind(c.m);
        std::printf("  [%s]\n    %-26s %12s %12s %10s %14s\n", c.m.name.c_str(), "启发函数", "平均展开", "平均耗时ms", "最优率", "最大代价超出");
        for (Heur h : {H_ZERO, H_CHEB, H_OCTILE, H_EUCLID, H_MANH}) {
            Agg a;
            for (size_t k = 0; k < c.qs.size(); ++k) {
                Cfg cf; cf.h = h;
                auto t0 = Clock::now(); i64 cost = S.run(c.qs[k].s, c.qs[k].t, cf); a.add(ms_since(t0), S.expanded, cost, c.opt[k]);
            }
            std::printf("    %-26s %12.0f %12.3f %9.0f%% %13.4f%%\n", kHName[h], double(a.exp) / a.n, a.ms / a.n, 100.0 * (a.n - a.sub) / a.n, (a.ratioMax - 1) * 100);
        }
    }
}

// ================================================================ E3
static void E3(std::vector<Case>& cs) {
    std::printf("\n========== E3 加权 A*：f = g + w·h（Octile）==========\n");
    Search S;
    for (int ci : {1, 2}) {
        auto& c = cs[ci]; S.bind(c.m);
        std::printf("  [%s]\n    %-14s %12s %12s %14s %14s\n", c.m.name.c_str(), "权重", "平均展开", "平均耗时ms", "平均代价超出", "最大代价超出");
        for (double w : {1.0, 1.2, 1.5, 2.0, 3.0, 5.0, -1.0}) {
            Agg a;
            for (size_t k = 0; k < c.qs.size(); ++k) {
                Cfg cf; cf.w = w > 0 ? w : 1; cf.greedy = w < 0;
                auto t0 = Clock::now(); i64 cost = S.run(c.qs[k].s, c.qs[k].t, cf); a.add(ms_since(t0), S.expanded, cost, c.opt[k]);
            }
            char nm[32]; if (w > 0) std::snprintf(nm, 32, "w = %.1f", w); else std::snprintf(nm, 32, "贪心 (w=∞)");
            std::printf("    %-14s %12.0f %12.3f %13.3f%% %13.3f%%\n", nm, double(a.exp) / a.n, a.ms / a.n, (a.ratioSum / a.n - 1) * 100, (a.ratioMax - 1) * 100);
        }
    }
}

// ================================================================ E4
static void E4(std::vector<Case>& cs) {
    std::printf("\n========== E4 平局决胜（A*，Octile）：f 相同时先展开谁 ==========\n");
    Search S;
    const char* nm[] = {"只比 f（交给堆决定）", "f 同取 h 小（g 大）", "f 同取 h 大（g 小）"};
    for (int ci : {0, 1, 4}) {
        auto& c = cs[ci]; S.bind(c.m);
        std::printf("  [%s]\n", c.m.name.c_str());
        for (int tie : {0, 1, 2}) {
            Agg a;
            for (size_t k = 0; k < c.qs.size(); ++k) {
                Cfg cf; cf.tie = tie;
                auto t0 = Clock::now(); i64 cost = S.run(c.qs[k].s, c.qs[k].t, cf); a.add(ms_since(t0), S.expanded, cost, c.opt[k]);
            }
            std::printf("    %-24s 平均展开 %9.0f   平均耗时 %8.3f ms   最优率 %3.0f%%\n", nm[tie], double(a.exp) / a.n, a.ms / a.n, 100.0 * (a.n - a.sub) / a.n);
        }
    }
}

// ================================================================ E5 数据结构
struct StoreGen {
    std::vector<i64> g; std::vector<int> par; std::vector<u32> seen, closed; u32 gen = 0;
    void init(int N) { g.assign(N, 0); par.assign(N, -1); seen.assign(N, 0); closed.assign(N, 0); }
    void begin() { ++gen; }
    bool isClosed(int i) const { return closed[i] == gen; }
    void close(int i) { closed[i] = gen; }
    bool has(int i) const { return seen[i] == gen; }
    i64 getG(int i) const { return g[i]; }
    void set(int i, i64 gg, int p) { seen[i] = gen; g[i] = gg; par[i] = p; }
};
struct StoreFresh {   // 朴素写法：每次查询重新分配并清零整张图的数组
    int N = 0; std::vector<i64> g; std::vector<int> par; std::vector<uint8_t> st;   // st: 0 未见 1 open 2 closed
    void init(int n) { N = n; }
    void begin() { std::vector<i64>(N, 0).swap(g); std::vector<int>(N, -1).swap(par); std::vector<uint8_t>(N, 0).swap(st); }
    bool isClosed(int i) const { return st[i] == 2; }
    void close(int i) { st[i] = 2; }
    bool has(int i) const { return st[i] != 0; }
    i64 getG(int i) const { return g[i]; }
    void set(int i, i64 gg, int p) { st[i] = 1; g[i] = gg; par[i] = p; }
};
struct StoreHash {    // 只为访问过的节点建记录
    struct R { i64 g; int par; bool closed; };
    std::unordered_map<int, R> mp;
    void init(int) {}
    void begin() { mp.clear(); }
    bool isClosed(int i) const { auto it = mp.find(i); return it != mp.end() && it->second.closed; }
    void close(int i) { mp[i].closed = true; }
    bool has(int i) const { return mp.count(i) != 0; }
    i64 getG(int i) const { return mp.find(i)->second.g; }
    void set(int i, i64 gg, int p) { auto& r = mp[i]; r.g = gg; r.par = p; }
};

struct OpenLazy {     // std::priority_queue，重复入队 + 出队时丢弃过期条目
    std::priority_queue<OpenE, std::vector<OpenE>, OpenCmp> pq{OpenCmp{1}};
    void init(int) {}
    void begin() { pq = decltype(pq)(OpenCmp{1}); }
    void push(i64 f, i64 h, int i) { pq.push({f, h, i}); }
    int pop() { if (pq.empty()) return -1; int i = pq.top().i; pq.pop(); return i; }
};
struct OpenSet {      // std::set，decrease-key = 删除旧键再插入
    std::set<std::tuple<i64, i64, int>> st; std::vector<i64> kf, kh; std::vector<u32> stamp; u32 gen = 0;
    void init(int N) { kf.assign(N, 0); kh.assign(N, 0); stamp.assign(N, 0); }
    void begin() { st.clear(); ++gen; }
    void push(i64 f, i64 h, int i) {
        if (stamp[i] == gen) st.erase({kf[i], kh[i], i});
        stamp[i] = gen; kf[i] = f; kh[i] = h; st.insert({f, h, i});
    }
    int pop() { if (st.empty()) return -1; int i = std::get<2>(*st.begin()); st.erase(st.begin()); return i; }
};
struct OpenIdxHeap {  // 带位置索引的二叉堆，原地 decrease-key
    std::vector<int> hp, pos; std::vector<i64> kf, kh; std::vector<u32> stamp; u32 gen = 0;
    void init(int N) { pos.assign(N, -1); kf.assign(N, 0); kh.assign(N, 0); stamp.assign(N, 0); }
    void begin() { hp.clear(); ++gen; }
    bool less(int a, int b) const { return kf[a] != kf[b] ? kf[a] < kf[b] : kh[a] < kh[b]; }
    void up(int k) { int v = hp[k]; while (k > 0) { int p = (k - 1) / 2; if (!less(v, hp[p])) break; hp[k] = hp[p]; pos[hp[k]] = k; k = p; } hp[k] = v; pos[v] = k; }
    void down(int k) {
        int v = hp[k], n = int(hp.size());
        for (;;) { int c = 2 * k + 1; if (c >= n) break; if (c + 1 < n && less(hp[c + 1], hp[c])) ++c; if (!less(hp[c], v)) break; hp[k] = hp[c]; pos[hp[k]] = k; k = c; }
        hp[k] = v; pos[v] = k;
    }
    void push(i64 f, i64 h, int i) {
        if (stamp[i] == gen && pos[i] >= 0) { kf[i] = f; kh[i] = h; up(pos[i]); return; }
        stamp[i] = gen; kf[i] = f; kh[i] = h; hp.push_back(i); up(int(hp.size()) - 1);
    }
    int pop() {
        if (hp.empty()) return -1;
        int i = hp[0]; pos[i] = -1;
        int last = hp.back(); hp.pop_back();
        if (!hp.empty()) { hp[0] = last; pos[last] = 0; down(0); }
        return i;
    }
};
struct OpenLinear {   // 无序数组，出队时线性扫描最小值（很多初学者的写法）
    std::vector<int> v, pos; std::vector<i64> kf, kh; std::vector<u32> stamp; u32 gen = 0;
    void init(int N) { pos.assign(N, -1); kf.assign(N, 0); kh.assign(N, 0); stamp.assign(N, 0); }
    void begin() { v.clear(); ++gen; }
    void push(i64 f, i64 h, int i) {
        if (!(stamp[i] == gen && pos[i] >= 0)) { stamp[i] = gen; pos[i] = int(v.size()); v.push_back(i); }
        kf[i] = f; kh[i] = h;
    }
    int pop() {
        if (v.empty()) return -1;
        size_t b = 0;
        for (size_t k = 1; k < v.size(); ++k) { int a = v[k], c = v[b]; if (kf[a] < kf[c] || (kf[a] == kf[c] && kh[a] < kh[c])) b = k; }
        int i = v[b]; v[b] = v.back(); pos[v[b]] = int(b); v.pop_back(); pos[i] = -1;
        return i;
    }
};

template <class Open, class Store>
static i64 astar_v(const Map& m, int s, int t, Open& open, Store& st, i64& exp) {
    int W = m.W, tx = t % W, ty = t / W;
    st.begin(); open.begin(); exp = 0;
    st.set(s, 0, -1);
    i64 hs = heur(H_OCTILE, s % W, s / W, tx, ty);
    open.push(hs, hs, s);
    for (;;) {
        int i = open.pop();
        if (i < 0) return -1;
        if (st.isClosed(i)) continue;
        st.close(i); ++exp;
        if (i == t) return st.getG(t);
        int x = i % W, y = i / W;
        for (int d = 0; d < 8; ++d) {
            if (!canMove(m, x, y, d)) continue;
            int j = i + DY[d] * W + DX[d];
            if (st.isClosed(j)) continue;
            i64 ng = st.getG(i) + (d < 4 ? CO : CD);
            if (st.has(j) && ng >= st.getG(j)) continue;
            st.set(j, ng, i);
            i64 h = heur(H_OCTILE, x + DX[d], y + DY[d], tx, ty);
            open.push(ng + h, h, j);
        }
    }
}

static void E5(std::vector<Case>& cs) {
    std::printf("\n========== E5 工程数据结构（[随机障碍25%%] 512x512，A* Octile）==========\n");
    auto& c = cs[1];
    int N = c.m.W * c.m.H, W = c.m.W;
    // 短查询：起终点相距不超过 16 格（游戏里最常见：追击、走到附近、技能落点）
    std::vector<Query> shortQ;
    std::mt19937 rng(77);
    while (shortQ.size() < 400) {
        int s = c.bigCells[rng() % c.bigCells.size()];
        int x = s % W + int(rng() % 33) - 16, y = s / W + int(rng() % 33) - 16;
        if (!c.m.walk(x, y) || c.comp[y * W + x] != c.big || y * W + x == s) continue;
        shortQ.push_back({s, y * W + x});
    }
    std::vector<Query> longQ(c.qs.begin(), c.qs.end());
    auto bench = [&](const char* name, auto& open, auto& st) {
        open.init(N); st.init(N);
        double tl = 0, ts = 0; i64 e, el = 0, es = 0, chk = 0;
        for (size_t k = 0; k < longQ.size(); ++k) { auto t0 = Clock::now(); i64 cst = astar_v(c.m, longQ[k].s, longQ[k].t, open, st, e); tl += ms_since(t0); el += e; if (cst != c.opt[k]) ++chk; }
        for (auto& q : shortQ) { auto t0 = Clock::now(); astar_v(c.m, q.s, q.t, open, st, e); ts += ms_since(t0); es += e; }
        std::printf("    %-34s 长查询 %8.3f ms/次（展开 %6lld）  短查询 %8.2f us/次（展开 %4lld）%s\n", name, tl / longQ.size(), el / (i64)longQ.size(),
                    ts * 1000 / shortQ.size(), es / (i64)shortQ.size(), chk ? "  !!代价错误" : "");
    };
    std::printf("  --- open list 实现（节点状态统一用 generation 复位）---\n");
    { OpenLazy o; StoreGen s; bench("priority_queue + 惰性删除", o, s); }
    { OpenIdxHeap o; StoreGen s; bench("索引二叉堆 + decrease-key", o, s); }
    { OpenSet o; StoreGen s; bench("std::set + 删除再插入", o, s); }
    { OpenLinear o; StoreGen s; bench("无序数组 + 线性找最小", o, s); }
    std::printf("  --- 节点状态（g / parent / closed）复位方式（open 统一用 priority_queue）---\n");
    { OpenLazy o; StoreGen s; bench("整图数组 + generation 计数", o, s); }
    { OpenLazy o; StoreFresh s; bench("每次查询重新分配并清零整图数组", o, s); }
    { OpenLazy o; StoreHash s; bench("unordered_map 只存访问过的节点", o, s); }
}

// ================================================================ E6 JPS
static void E6(std::vector<Case>& cs) {
    std::printf("\n========== E6 JPS vs A*（Octile，同一批查询）==========\n");
    Search S;
    for (int ci : {0, 1, 3, 4}) {
        auto& c = cs[ci]; S.bind(c.m);
        Agg a, j;
        for (size_t k = 0; k < c.qs.size(); ++k) {
            auto t0 = Clock::now(); i64 cost = S.run(c.qs[k].s, c.qs[k].t, Cfg{}); a.add(ms_since(t0), S.expanded, cost, c.opt[k]);
            t0 = Clock::now(); cost = S.jps(c.qs[k].s, c.qs[k].t); j.add(ms_since(t0), S.expanded, cost, c.opt[k], S.scanned);
        }
        std::printf("  [%-10s] A*  展开 %8.0f  %8.3f ms | JPS 展开(跳点) %6.0f  扫描格子 %8.0f  %8.3f ms | 加速 %5.1fx  JPS 最优率 %3.0f%%\n",
                    c.m.name.c_str(), double(a.exp) / a.n, a.ms / a.n, double(j.exp) / j.n, double(j.scan) / j.n, j.ms / j.n, a.ms / j.ms, 100.0 * (j.n - j.sub) / j.n);
    }
}

// ================================================================ E7 分帧
static void E7(std::vector<Case>& cs) {
    std::printf("\n========== E7 分帧寻路（[迷宫] 左上角 → 右下角，A* Octile）==========\n");
    auto& c = cs[3]; Search S; S.bind(c.m);
    int s = 1 * c.m.W + 1, t = (c.m.H - 3) * c.m.W + (c.m.W - 3);
    for (i64 budget : {LLONG_MAX, (i64)20000, (i64)5000, (i64)1000}) {
        S.begin(s, t, Cfg{});
        int frames = 0, r = 0; double mx = 0, tot = 0;
        do { auto t0 = Clock::now(); r = S.step(budget); double d = ms_since(t0); tot += d; mx = std::max(mx, d); ++frames; } while (r == 0);
        char nm[32]; if (budget == LLONG_MAX) std::snprintf(nm, 32, "不分帧"); else std::snprintf(nm, 32, "每帧 %lld 个节点", budget);
        std::printf("    %-18s 帧数 %4d   单帧最大 %7.3f ms   总耗时 %7.3f ms   路径长 %.0f 格   总展开 %lld\n", nm, frames, mx, tot, r == 1 ? S.g[t] / 1000.0 : -1.0, S.expanded);
    }
}

// ================================================================ E8 不可达
static void E8(std::vector<Case>& cs) {
    std::printf("\n========== E8 目标不可达（[随机障碍25%%]：终点在一个被围死的小区域里）==========\n");
    auto& c = cs[1]; Search S; S.bind(c.m);
    int t = -1;
    for (int i = 0; i < c.m.W * c.m.H; ++i) if (c.comp[i] >= 0 && c.comp[i] != c.big && c.sizes[c.comp[i]] >= 3 && c.sizes[c.comp[i]] <= 20) { t = i; break; }
    int s = c.bigCells[c.bigCells.size() / 2];
    double tA = 0; i64 eA = 0;
    for (int r = 0; r < 5; ++r) { auto t0 = Clock::now(); S.run(s, t, Cfg{}); tA += ms_since(t0); eA = S.expanded; }
    std::printf("    A* 直接搜：展开 %lld 个节点（= 起点所在连通块 %d 格），耗时 %.3f ms，结果：无路径\n", eA, c.sizes[c.big], tA / 5);
    auto t0 = Clock::now(); std::vector<int> sz; auto comp = label4(c.m, sz); double tl = ms_since(t0);
    t0 = Clock::now(); volatile bool same = false; for (int r = 0; r < 1000000; ++r) same = comp[s] == comp[t]; double tq = ms_since(t0);
    std::printf("    连通块预计算：加载时 flood fill 一次 %.3f ms（%zu 个连通块）；之后每次判定 %.6f us（一次数组比较），本例 same=%d\n", tl, sz.size(), tq * 1000 / 1000000, (int)same);
    // 有上限的搜索
    Cfg cf; S.begin(s, t, cf);
    t0 = Clock::now(); int r = S.step(20000); double tc = ms_since(t0);
    std::printf("    展开上限 20000：%.3f ms 后放弃（状态 %d=未完成），可退而求其次走向“h 最小的已探索点”\n", tc, r);
}

// ================================================================ E9 确定性
struct OpenLazyQuad {  // 另一种堆实现：4 叉堆（模拟“另一个平台的 STL / 另一份代码”）
    std::vector<OpenE> hp; OpenCmp cmp{0};
    void init(int) {}
    void begin() { hp.clear(); }
    void push(i64 f, i64 h, int i) {
        hp.push_back({f, h, i});
        size_t k = hp.size() - 1;
        while (k > 0) { size_t p = (k - 1) / 4; if (!cmp(hp[p], hp[k])) break; std::swap(hp[p], hp[k]); k = p; }
    }
    int pop() {
        if (hp.empty()) return -1;
        int r = hp[0].i; hp[0] = hp.back(); hp.pop_back();
        size_t k = 0, n = hp.size();
        for (;;) {
            size_t b = k;
            for (size_t c = 4 * k + 1; c <= 4 * k + 4 && c < n; ++c) if (cmp(hp[b], hp[c])) b = c;
            if (b == k) break;
            std::swap(hp[b], hp[k]); k = b;
        }
        return r;
    }
};
struct OpenLazyStd {
    std::priority_queue<OpenE, std::vector<OpenE>, OpenCmp> pq{OpenCmp{0}}; int tie = 0;
    void init(int) {}
    void begin() { pq = decltype(pq)(OpenCmp{tie}); }
    void push(i64 f, i64 h, int i) { pq.push({f, h, i}); }
    int pop() { if (pq.empty()) return -1; int i = pq.top().i; pq.pop(); return i; }
};
static u64 path_hash(const StoreGen& st, int t) { u64 h = 1469598103934665603ull; for (int i = t; i != -1; i = st.par[i]) { h ^= u32(i); h *= 1099511628211ull; } return h; }

static void E9(std::vector<Case>& cs) {
    std::printf("\n========== E9 帧同步确定性：两份不同的堆实现，同一张图、同一批查询 ==========\n");
    for (int ci : {0, 4}) {
        auto& c = cs[ci]; int N = c.m.W * c.m.H;
        std::printf("  [%s] %zu 次查询\n", c.m.name.c_str(), c.qs.size());
        for (int tie : {0, 1, 3}) {
            OpenLazyStd a; a.tie = tie; OpenLazyQuad b; b.cmp.tie = tie;
            StoreGen sa, sb; sa.init(N); sb.init(N);
            int diff = 0, costDiff = 0; i64 e;
            for (auto& q : c.qs) {
                i64 ca = astar_v(c.m, q.s, q.t, a, sa, e), cb = astar_v(c.m, q.s, q.t, b, sb, e);
                if (ca != cb) ++costDiff;
                if (path_hash(sa, q.t) != path_hash(sb, q.t)) ++diff;
            }
            const char* nm = tie == 0 ? "只比 f" : tie == 1 ? "比 (f, h)" : "全序 (f, h, 格子下标)";
            std::printf("    %-24s 路径不同 %3d / %zu 次，其中代价不同 %d 次\n", nm, diff, c.qs.size(), costDiff);
        }
    }
}

// ================================================================ E10 路径平滑
static bool los(const Map& m, int x0, int y0, int x1, int y1) {   // 格子级视线检测（经过格角时两侧都必须可走）
    int dx = std::abs(x1 - x0), dy = std::abs(y1 - y0), sx = x1 > x0 ? 1 : -1, sy = y1 > y0 ? 1 : -1;
    int x = x0, y = y0, n = 1 + dx + dy, err = dx - dy;
    dx *= 2; dy *= 2;
    while (n > 0) {
        if (!m.walk(x, y)) return false;
        if (err > 0) { x += sx; err -= dy; --n; }
        else if (err < 0) { y += sy; err += dx; --n; }
        else if (n >= 3) { if (!m.walk(x + sx, y) || !m.walk(x, y + sy)) return false; x += sx; y += sy; err += dx - dy; n -= 2; }
        else { y += sy; err += dx; --n; }
    }
    return true;
}
static void E10(std::vector<Case>& cs) {
    std::printf("\n========== E10 路径平滑：拉绳（视线检测，贪心找最远可见点）==========\n");
    Search S;
    for (int ci : {0, 1, 4}) {
        auto& c = cs[ci]; S.bind(c.m); int W = c.m.W;
        double rawTurn = 0, smTurn = 0, rawLen = 0, smLen = 0, tsm = 0; int n = 0;
        for (auto& q : c.qs) {
            if (S.run(q.s, q.t, Cfg{}) < 0) continue;
            auto p = S.path();
            int turns = 0;
            for (size_t k = 2; k < p.size(); ++k) if (p[k] - p[k - 1] != p[k - 1] - p[k - 2]) ++turns;
            rawTurn += turns + 2; rawLen += S.g[q.t] / 1000.0;
            auto t0 = Clock::now();
            std::vector<int> sm = {p[0]};
            size_t a = 0;
            while (a + 1 < p.size()) {
                size_t b = a + 1;
                while (b + 1 < p.size() && los(c.m, p[a] % W, p[a] / W, p[b + 1] % W, p[b + 1] / W)) ++b;
                sm.push_back(p[b]); a = b;
            }
            tsm += ms_since(t0);
            smTurn += sm.size();
            for (size_t k = 1; k < sm.size(); ++k) { double dx = sm[k] % W - sm[k - 1] % W, dy = sm[k] / W - sm[k - 1] / W; smLen += std::sqrt(dx * dx + dy * dy); }
            ++n;
        }
        std::printf("  [%-10s] 原始路径：路点(含起终点) %6.1f 个，长 %7.1f 格 | 拉绳后：路点 %5.1f 个，长 %7.1f 格（缩短 %.2f%%） | 平滑耗时 %.3f ms/条\n",
                    c.m.name.c_str(), rawTurn / n, rawLen / n, smTurn / n, smLen / n, (1 - smLen / rawLen) * 100, tsm / n);
    }
}

int main() {
    std::setvbuf(stdout, nullptr, _IONBF, 0);
#ifdef _MSC_VER
    std::printf("A* 寻路实测  (MSVC %d, x64 /O2)\n", _MSC_VER);
#endif
    const int N = 512, Q = 40;
    std::vector<Case> cs(5);
    cs[0].m = make_open(N);
    cs[1].m = make_random(N, 0.25, 2026);
    cs[2].m = make_trap(N);
    cs[3].m = make_maze(N, 7);
    cs[4].m = make_rooms(N, 11);
    for (int i = 0; i < 5; ++i) prepare(cs[i], Q, 100 + i, i == 2);
    E1(cs); E2(cs); E3(cs); E4(cs); E5(cs); E6(cs); E7(cs); E8(cs); E9(cs); E10(cs);
    return 0;
}
