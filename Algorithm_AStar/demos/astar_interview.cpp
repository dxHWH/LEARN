// astar_interview.cpp —— 面试手写版 A*（电子书 2.4 节原文），附一个小测试
// 编译：cl /nologo /std:c++17 /EHsc /O2 /utf-8 astar_interview.cpp
#include <algorithm>
#include <climits>
#include <cstdio>
#include <cstdlib>
#include <cstdint>
#include <queue>
#include <string>
#include <vector>

struct Grid {
    int w, h;
    std::vector<uint8_t> blocked;
    bool walkable(int x, int y) const { return x >= 0 && y >= 0 && x < w && y < h && !blocked[y * w + x]; }
};

struct Node { int f, g, idx; };
struct Cmp {   // priority_queue 是大顶堆：返回 true 表示 a 的优先级更低
    bool operator()(const Node& a, const Node& b) const { return a.f != b.f ? a.f > b.f : a.g < b.g; }   // f 小优先；f 相同 g 大优先
};

std::vector<int> astar(const Grid& grid, int sx, int sy, int tx, int ty) {
    const int W = grid.w, H = grid.h;
    auto id = [W](int x, int y) { return y * W + x; };
    auto h = [&](int x, int y) {                               // Octile：8 方向、直走 10、斜走 14
        int dx = std::abs(x - tx), dy = std::abs(y - ty);
        return 10 * std::max(dx, dy) + 4 * std::min(dx, dy);
    };
    std::vector<int> g(W * H, INT_MAX), parent(W * H, -1);
    std::vector<char> closed(W * H, 0);
    std::priority_queue<Node, std::vector<Node>, Cmp> open;
    g[id(sx, sy)] = 0;
    open.push({h(sx, sy), 0, id(sx, sy)});
    static const int DX[8] = {1, -1, 0, 0, 1, 1, -1, -1}, DY[8] = {0, 0, 1, -1, 1, -1, 1, -1};
    while (!open.empty()) {
        Node cur = open.top(); open.pop();
        if (closed[cur.idx]) continue;                         // 过期条目：该节点已用更小的 g 展开过
        closed[cur.idx] = 1;
        if (cur.idx == id(tx, ty)) break;                      // 出队时才判断终点
        int x = cur.idx % W, y = cur.idx / W;
        for (int d = 0; d < 8; ++d) {
            int nx = x + DX[d], ny = y + DY[d];
            if (!grid.walkable(nx, ny)) continue;
            if (d >= 4 && (!grid.walkable(nx, y) || !grid.walkable(x, ny))) continue;   // 禁止切角
            int j = id(nx, ny);
            if (closed[j]) continue;
            int ng = g[cur.idx] + (d < 4 ? 10 : 14);
            if (ng >= g[j]) continue;                          // 没有变短：忽略
            g[j] = ng; parent[j] = cur.idx;
            open.push({ng + h(nx, ny), ng, j});                // 允许重复入队（惰性删除）
        }
    }
    std::vector<int> path;
    if (g[id(tx, ty)] == INT_MAX) return path;                 // 不可达
    for (int i = id(tx, ty); i != -1; i = parent[i]) path.push_back(i);
    std::reverse(path.begin(), path.end());
    return path;
}

int main() {
    const char* rows[] = {".......", "...#...", ".S.#.G.", "...#...", "......."};
    Grid gr{7, 5, std::vector<uint8_t>(35, 0)};
    int sx = 0, sy = 0, tx = 0, ty = 0;
    for (int y = 0; y < 5; ++y)
        for (int x = 0; x < 7; ++x) {
            char c = rows[y][x];
            if (c == '#') gr.blocked[y * 7 + x] = 1;
            if (c == 'S') { sx = x; sy = y; }
            if (c == 'G') { tx = x; ty = y; }
        }
    auto p = astar(gr, sx, sy, tx, ty);
    std::printf("路径 %zu 个格子：", p.size());
    for (int i : p) std::printf("(%d,%d) ", i % 7, i / 7);
    std::printf("\n");
    for (int y = 0; y < 5; ++y) {
        std::string line = rows[y];
        for (int i : p) if (i / 7 == y && line[i % 7] == '.') line[i % 7] = '*';
        std::printf("  %s\n", line.c_str());
    }
    gr.blocked[3 * 7 + 3] = gr.blocked[4 * 7 + 3] = gr.blocked[0 * 7 + 3] = 1;   // 把墙补成一整列：终点不可达
    std::printf("封死以后：路径 %zu 个格子（空 = 不可达）\n", astar(gr, sx, sy, tx, ty).size());
}
