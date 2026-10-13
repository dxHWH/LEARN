// astar_engineering.cpp —— 工程版 GridPathfinder（电子书 5.5 节原文）+ 正确性 / 性能自测
// 编译：cl /nologo /std:c++17 /EHsc /O2 /utf-8 astar_engineering.cpp
#include <algorithm>
#include <chrono>
#include <climits>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <queue>
#include <random>
#include <vector>

struct Grid {
    int w, h;
    std::vector<uint8_t> blocked;
    bool walkable(int x, int y) const { return x >= 0 && y >= 0 && x < w && y < h && !blocked[y * w + x]; }
};

// ---------------------------------------------------------------- 电子书 5.5 节
class GridPathfinder {
public:
    explicit GridPathfinder(const Grid& grid)
        : grid_(grid), n_(grid.w * grid.h), g_(n_), parent_(n_), seenGen_(n_, 0), closedGen_(n_, 0) {
        const int W = grid.w;
        const int off[8] = {1, -1, W, -W, W + 1, -W + 1, W - 1, -W - 1};   // 与 DX/DY 顺序一致
        std::copy(off, off + 8, offset_);
        heap_.reserve(4096);
    }

    // 找到返回 true，path 为格子下标序列（含起终点）；maxExpand 限制单次查询的展开数
    bool find(int s, int t, std::vector<int>& path, int maxExpand = INT_MAX) {
        if (++gen_ == 0) {                                   // 40 亿次查询后回绕：整体清零一次
            std::fill(seenGen_.begin(), seenGen_.end(), 0u);
            std::fill(closedGen_.begin(), closedGen_.end(), 0u);
            gen_ = 1;
        }
        heap_.clear();                                       // 保留容量，不重新分配
        path.clear();
        const int W = grid_.w, tx = t % W, ty = t / W;
        auto h = [&](int i) {                                // Octile，直走 1000、斜走 1414
            int dx = std::abs(i % W - tx), dy = std::abs(i / W - ty);
            return 1000 * std::max(dx, dy) + 414 * std::min(dx, dy);
        };
        push(s, 0, -1, h(s));
        int expanded = 0;
        while (!heap_.empty()) {
            std::pop_heap(heap_.begin(), heap_.end(), Cmp{});
            OpenE e = heap_.back(); heap_.pop_back();
            if (closedGen_[e.i] == gen_) continue;           // 惰性删除
            closedGen_[e.i] = gen_;
            if (e.i == t) { build(t, path); lastExpanded_ = expanded; return true; }
            if (++expanded > maxExpand) break;
            const int x = e.i % W, y = e.i / W;
            for (int d = 0; d < 8; ++d) {
                if (!canMove(x, y, d)) continue;
                const int j = e.i + offset_[d];
                if (closedGen_[j] == gen_) continue;
                const int ng = g_[e.i] + (d < 4 ? 1000 : 1414);   // int32 足够：最长路径 × 1414 远小于 2^31
                if (seenGen_[j] == gen_ && ng >= g_[j]) continue;
                push(j, ng, e.i, h(j));
            }
        }
        lastExpanded_ = expanded;
        return false;
    }
    int cost(int t) const { return g_[t]; }
    int lastExpanded() const { return lastExpanded_; }

private:
    struct OpenE { int f, h, i; };
    struct Cmp {   // 全序：f 小 → h 小 → 下标小。任何平台、任何堆实现结果都相同
        bool operator()(const OpenE& a, const OpenE& b) const {
            if (a.f != b.f) return a.f > b.f;
            if (a.h != b.h) return a.h > b.h;
            return a.i > b.i;
        }
    };
    static constexpr int DX[8] = {1, -1, 0, 0, 1, 1, -1, -1};
    static constexpr int DY[8] = {0, 0, 1, -1, 1, -1, 1, -1};
    bool canMove(int x, int y, int d) const {
        if (!grid_.walkable(x + DX[d], y + DY[d])) return false;
        return d < 4 || (grid_.walkable(x + DX[d], y) && grid_.walkable(x, y + DY[d]));   // 禁止切角
    }
    void push(int i, int g, int p, int h) {
        seenGen_[i] = gen_; g_[i] = g; parent_[i] = p;
        heap_.push_back({g + h, h, i});
        std::push_heap(heap_.begin(), heap_.end(), Cmp{});
    }
    void build(int t, std::vector<int>& path) const {
        for (int i = t; i != -1; i = parent_[i]) path.push_back(i);
        std::reverse(path.begin(), path.end());
    }

    const Grid& grid_;
    int n_;
    std::vector<int> g_, parent_;
    std::vector<uint32_t> seenGen_, closedGen_;   // == gen_ 才算本次查询的有效数据
    uint32_t gen_ = 0;
    std::vector<OpenE> heap_;
    int offset_[8];
    int lastExpanded_ = 0;
};

// ---------------------------------------------------------------- 自测：与 Dijkstra 对比代价
static int dijkstra(const Grid& gr, int s, int t) {
    const int W = gr.w, N = gr.w * gr.h;
    const int DX[8] = {1, -1, 0, 0, 1, 1, -1, -1}, DY[8] = {0, 0, 1, -1, 1, -1, 1, -1};
    std::vector<int> d(N, INT_MAX);
    std::priority_queue<std::pair<int, int>, std::vector<std::pair<int, int>>, std::greater<>> pq;
    d[s] = 0; pq.push({0, s});
    while (!pq.empty()) {
        auto [dist, i] = pq.top(); pq.pop();
        if (dist != d[i]) continue;
        if (i == t) return dist;
        int x = i % W, y = i / W;
        for (int k = 0; k < 8; ++k) {
            int nx = x + DX[k], ny = y + DY[k];
            if (!gr.walkable(nx, ny)) continue;
            if (k >= 4 && (!gr.walkable(nx, y) || !gr.walkable(x, ny))) continue;
            int nd = dist + (k < 4 ? 1000 : 1414), j = ny * W + nx;
            if (nd < d[j]) { d[j] = nd; pq.push({nd, j}); }
        }
    }
    return -1;
}

int main() {
    Grid gr{256, 256, std::vector<uint8_t>(256 * 256)};
    std::mt19937 rng(5);
    for (auto& b : gr.blocked) b = (rng() % 100) < 25;
    GridPathfinder pf(gr);
    std::vector<int> path;
    int ok = 0, bad = 0, unreachable = 0;
    double ms = 0;
    for (int q = 0; q < 300; ++q) {
        int s = rng() % (256 * 256), t = rng() % (256 * 256);
        if (gr.blocked[s] || gr.blocked[t]) { --q; continue; }
        auto t0 = std::chrono::steady_clock::now();
        bool found = pf.find(s, t, path);
        ms += std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
        int ref = dijkstra(gr, s, t);
        if (!found) { if (ref == -1) ++unreachable; else ++bad; continue; }
        if (pf.cost(t) == ref && path.front() == s && path.back() == t) ++ok; else ++bad;
    }
    std::printf("GridPathfinder 自测（256x256，随机障碍 25%%，300 次查询）：最短且正确 %d，不可达 %d，错误 %d，平均 %.3f ms/次\n", ok, unreachable, bad, ms / 300);
    return bad ? 1 : 0;
}
