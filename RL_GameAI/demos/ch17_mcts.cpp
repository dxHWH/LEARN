// 第 17 章实测：井字棋上的搜索
//   E1 完整博弈树：minimax 访问的节点数 vs α-β 剪枝；完美对局的结果（平局）
//   E2 UCT（随机 rollout）vs 完美对手：每步模拟次数 10 … 3000 时的不败率；探索常数 c 的扫描
//   E3 AlphaZero 思路：PUCT + 不完美的“策略先验”和“价值估计”（代替 rollout）vs 纯 UCT，同样模拟次数下的不败率
//   E4 空棋盘上 2000 次模拟后根节点各着法的访问次数
// 输出：stdout + ch17_output.json
// 编译：cl /nologo /std:c++latest /EHsc /O2 /utf-8 ch17_mcts.cpp
#include "rlcommon.h"
#include <array>
#include <unordered_map>
using namespace std;

using Board = array<int, 9>;   // 0 空，1 = X，2 = O
static const int LINES[8][3] = {{0,1,2},{3,4,5},{6,7,8},{0,3,6},{1,4,7},{2,5,8},{0,4,8},{2,4,6}};
static int winner(const Board& b) { for (auto& l : LINES) if (b[l[0]] && b[l[0]] == b[l[1]] && b[l[1]] == b[l[2]]) return b[l[0]]; for (int x : b) if (!x) return 0; return 3; }   // 3 = 平局
static int encode(const Board& b) { int k = 0; for (int x : b) k = k * 3 + x; return k; }

// ---------------- minimax（带记忆）：返回“轮到 p 走”时 p 的最优收益（+1 赢 / 0 平 / −1 输）
static unordered_map<int, int> memo;
static int mm(Board& b, int p) {
    int w = winner(b); if (w == 3) return 0; if (w) return w == p ? 1 : -1;
    int key = encode(b) * 3 + p; auto it = memo.find(key); if (it != memo.end()) return it->second;
    int best = -2;
    for (int i = 0; i < 9; ++i) if (!b[i]) { b[i] = p; best = max(best, -mm(b, 3 - p)); b[i] = 0; }
    memo[key] = best; return best;
}
static long long nodes_mm = 0, nodes_ab = 0;
static int mm_plain(Board& b, int p) { ++nodes_mm; int w = winner(b); if (w == 3) return 0; if (w) return w == p ? 1 : -1; int best = -2; for (int i = 0; i < 9; ++i) if (!b[i]) { b[i] = p; best = max(best, -mm_plain(b, 3 - p)); b[i] = 0; } return best; }
static int alphabeta(Board& b, int p, int alpha, int beta) {
    ++nodes_ab; int w = winner(b); if (w == 3) return 0; if (w) return w == p ? 1 : -1;
    for (int i = 0; i < 9; ++i) if (!b[i]) { b[i] = p; int v = -alphabeta(b, 3 - p, -beta, -alpha); b[i] = 0; if (v > alpha) alpha = v; if (alpha >= beta) break; }
    return alpha;
}
static int perfect_move(Board b, int p, Rng& r) {
    int best = -2; vector<int> c;
    for (int i = 0; i < 9; ++i) if (!b[i]) { b[i] = p; int v = -mm(b, 3 - p); b[i] = 0; if (v > best) { best = v; c.clear(); } if (v == best) c.push_back(i); }
    return c[r.below((int)c.size())];
}

// ---------------- MCTS
struct Node { Board b; int p; int move; int parent; vector<int> kids; vector<int> untried; double N = 0, W = 0, P = 1; bool expanded = false; };
struct Cfg { int sims; double c; bool puct; double noise_v; double prior_mix; };
static double value_net(const Board& b, int p, Rng& r, double sd) {   // “价值网络”：真值 + 高斯噪声，截断到 [−1, 1]
    Board t = b; double v = mm(t, p) + sd * r.normal(); return max(-1.0, min(1.0, v));
}
static vector<double> policy_net(const Board& b, int p, double mix) {   // “策略网络”：按真值做 softmax，再与均匀分布混合
    vector<double> z, pr(9, 0); vector<int> legal;
    for (int i = 0; i < 9; ++i) if (!b[i]) { Board t = b; t[i] = p; z.push_back(-2.0 * mm(t, 3 - p)); legal.push_back(i); }
    auto s = softmax(z); for (size_t k = 0; k < legal.size(); ++k) pr[legal[k]] = (1 - mix) * s[k] + mix / legal.size();
    return pr;
}
static int mcts_move(const Board& root_b, int root_p, const Cfg& cfg, Rng& r, vector<double>* visits = nullptr) {
    vector<Node> T; T.reserve(cfg.sims * 2 + 16);
    Node root; root.b = root_b; root.p = root_p; root.move = -1; root.parent = -1;
    for (int i = 0; i < 9; ++i) if (!root_b[i]) root.untried.push_back(i);
    T.push_back(root);
    for (int s = 0; s < cfg.sims; ++s) {
        int cur = 0;
        // 选择
        while (true) {
            Node& n = T[cur];
            if (winner(n.b)) break;
            if (!cfg.puct && !n.untried.empty()) break;
            if (cfg.puct && !n.expanded) break;
            int best = -1; double bv = -1e18;
            for (int k : n.kids) {
                Node& ch = T[k]; double q = ch.N ? ch.W / ch.N : 0.0, u;
                if (cfg.puct) u = q + cfg.c * ch.P * sqrt(n.N) / (1 + ch.N);
                else u = q + cfg.c * sqrt(log(n.N) / ch.N);
                if (u > bv) { bv = u; best = k; }
            }
            cur = best;
        }
        double val;   // 从“走进 cur 的那一方”的视角
        int w = winner(T[cur].b);
        if (w) { int mover = 3 - T[cur].p; val = w == 3 ? 0.0 : (w == mover ? 1.0 : -1.0); }
        else if (!cfg.puct) {
            // 扩展一个子节点 + 随机 rollout
            Node& n = T[cur]; int idx = r.below((int)n.untried.size()); int mv = n.untried[idx]; n.untried.erase(n.untried.begin() + idx);
            Node ch; ch.b = n.b; ch.b[mv] = n.p; ch.p = 3 - n.p; ch.move = mv; ch.parent = cur;
            for (int i = 0; i < 9; ++i) if (!ch.b[i]) ch.untried.push_back(i);
            T.push_back(ch); int ci = (int)T.size() - 1; T[cur].kids.push_back(ci); cur = ci;
            Board b = T[cur].b; int p = T[cur].p, ww;
            while (!(ww = winner(b))) { vector<int> m; for (int i = 0; i < 9; ++i) if (!b[i]) m.push_back(i); b[m[r.below((int)m.size())]] = p; p = 3 - p; }
            int mover = 3 - T[cur].p; val = ww == 3 ? 0.0 : (ww == mover ? 1.0 : -1.0);
        } else {
            // PUCT：一次性展开所有子节点并赋先验，用价值估计代替 rollout
            auto pr = policy_net(T[cur].b, T[cur].p, cfg.prior_mix);
            for (int mv = 0; mv < 9; ++mv) if (!T[cur].b[mv]) {
                Node ch; ch.b = T[cur].b; ch.b[mv] = T[cur].p; ch.p = 3 - T[cur].p; ch.move = mv; ch.parent = cur; ch.P = pr[mv];
                T.push_back(ch); T[cur].kids.push_back((int)T.size() - 1);
            }
            T[cur].expanded = true;
            val = -value_net(T[cur].b, T[cur].p, r, cfg.noise_v);   // value_net 是“轮到 p 走的一方”的视角，取反得到走进来那一方
        }
        // 回传：每上一层视角翻转一次
        for (int k = cur; k >= 0; k = T[k].parent) { T[k].N += 1; T[k].W += val; val = -val; }
    }
    int best = -1; double bn = -1;
    for (int k : T[0].kids) if (T[k].N > bn) { bn = T[k].N; best = T[k].move; }
    if (visits) { visits->assign(9, 0); for (int k : T[0].kids) (*visits)[T[k].move] = T[k].N; }
    return best;
}
// MCTS 与完美对手对弈 n 局（一半执 X、一半执 O），返回不败率
static double play(const Cfg& cfg, int games, uint64_t seed) {
    Rng r(seed); int nonloss = 0;
    for (int g = 0; g < games; ++g) {
        Board b{}; int p = 1, me = g % 2 == 0 ? 1 : 2, w;
        while (!(w = winner(b))) { int mv = p == me ? mcts_move(b, p, cfg, r) : perfect_move(b, p, r); b[mv] = p; p = 3 - p; }
        nonloss += (w == 3 || w == me);
    }
    return (double)nonloss / games;
}

int main() {
    Board e{};
    int v = mm(e, 1);
    mm_plain(e, 1); alphabeta(e, 1, -2, 2);
    printf("[E1] 空棋盘 minimax 值 %d（完美对局为平局）；不剪枝访问 %lld 个节点，α-β 剪枝访问 %lld 个（%.1f%%）；记忆化后不同局面 %zu 个\n",
           v, nodes_mm, nodes_ab, 100.0 * nodes_ab / nodes_mm, memo.size());
    vector<int> sims = {10, 30, 100, 300, 1000, 3000};
    const int G = 200;
    vector<double> sx, uct, puct;
    vector<double> res_u(sims.size()), res_p(sims.size());
    parallel_for((int)sims.size() * 2, [&](int i) {
        int k = i / 2; Cfg c{sims[k], 1.4, false, 0, 0}; Cfg cp{sims[k], 1.5, true, 0.5, 0.5};
        if (i % 2 == 0) res_u[k] = play(c, G, 100 + k); else res_p[k] = play(cp, G, 200 + k);
    });
    for (size_t k = 0; k < sims.size(); ++k) { sx.push_back(sims[k]); uct.push_back(res_u[k]); puct.push_back(res_p[k]);
        printf("[E2/E3] 每步 %4d 次模拟：UCT（随机 rollout）不败率 %.3f；PUCT（先验 + 带噪声的价值估计）不败率 %.3f\n", sims[k], res_u[k], res_p[k]); }
    vector<double> cs = {0.0, 0.3, 0.7, 1.4, 2.8, 5.6}, cres(cs.size());
    parallel_for((int)cs.size(), [&](int i) { Cfg c{100, cs[i], false, 0, 0}; cres[i] = play(c, 400, 300 + i); });
    for (size_t i = 0; i < cs.size(); ++i) printf("[E2] 每步 100 次模拟，c = %.1f：不败率 %.3f\n", cs[i], cres[i]);
    Rng r(9); vector<double> vis; Cfg c{2000, 1.4, false, 0, 0}; mcts_move(e, 1, c, r, &vis);
    printf("[E4] 空棋盘 2000 次模拟的访问次数：\n"); for (int y = 0; y < 3; ++y) printf("      %5.0f %5.0f %5.0f\n", vis[3 * y], vis[3 * y + 1], vis[3 * y + 2]);
    vector<string> lab = {js("左上"), js("上"), js("右上"), js("左"), js("中心"), js("右"), js("左下"), js("下"), js("右下")};
    JObj root;
    root.num("v", v).num("nodes_mm", (double)nodes_mm).num("nodes_ab", (double)nodes_ab).num("states", (double)memo.size())
        .add("uct", jpts(sx, uct, 4)).add("puct", jpts(sx, puct, 4)).add("c_sweep", jpts(cs, cres, 4)).add("visits", ja(vis, 6)).add("labels", jlist(lab)).num("G", G)
        .add("uct_v", ja(uct, 4)).add("puct_v", ja(puct, 4)).add("sims", jai(sims));
    write_file("ch17_output.json", root.str());
    printf("写出 ch17_output.json\n");
}
