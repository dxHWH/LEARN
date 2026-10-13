// 第 15 章实测：自博弈与 Elo
//   E1 石头剪刀布：朴素自博弈（和“当前的自己”打，softmax 策略梯度）/ 迭代最优响应 / 虚拟自博弈（FSP：对历史平均做最优响应），
//      记录 p(石头) 轨迹与可利用度（最优响应能赢多少）
//   E2 Elo 收敛：20 名真实实力已知的选手随机配对，K = 8 / 16 / 32 / 64，评分误差随对局数的变化
//   E3 非传递性：A 克 B、B 克 C、C 克 A（各 70% 胜率）+ 一个各打五五开的 D，Elo 分数与预测胜率 vs 真实胜率
// 输出：stdout + ch15_output.json
// 编译：cl /nologo /std:c++latest /EHsc /O2 /utf-8 ch15_selfplay.cpp
#include "rlcommon.h"
using namespace std;

static const double Apay[3][3] = {{0, -1, 1}, {1, 0, -1}, {-1, 1, 0}};   // 行玩家收益：石头 / 布 / 剪刀
static vector<double> Ap(const vector<double>& p) { vector<double> u(3, 0); for (int i = 0; i < 3; ++i) for (int j = 0; j < 3; ++j) u[i] += Apay[i][j] * p[j]; return u; }
static double expl(const vector<double>& p) { auto u = Ap(p); return *max_element(u.begin(), u.end()); }   // 对称零和博弈，均衡值为 0

static JObj e1() {
    const int K = 3000;
    // 朴素自博弈：logits 沿“对当前自己的收益”梯度上升（精确梯度，softmax 策略梯度）
    vector<double> th = {log(0.5), log(0.3), log(0.2)};
    vector<double> it, nr, ne, br_r, fsp_r, fsp_e, fsp_cur_e;
    const double eta = 0.05;
    for (int k = 0; k <= K; ++k) {
        auto p = softmax(th); auto u = Ap(p);
        if (k % 10 == 0) { it.push_back(k); nr.push_back(p[0]); ne.push_back(expl(p)); }
        double v = 0; for (int i = 0; i < 3; ++i) v += p[i] * u[i];
        for (int i = 0; i < 3; ++i) th[i] += eta * p[i] * (u[i] - v);
    }
    // 迭代最优响应：每轮对上一轮的策略做最优响应（纯策略）
    int cur = 0; vector<double> bx, by;
    for (int k = 0; k <= 30; ++k) { bx.push_back(k); by.push_back(cur == 0 ? 1.0 : 0.0); vector<double> p(3, 0); p[cur] = 1; auto u = Ap(p); cur = (int)(max_element(u.begin(), u.end()) - u.begin()); }
    // 虚拟自博弈：对历史平均策略做最优响应，平均策略收敛到纳什
    vector<double> avg = {0.5, 0.3, 0.2}; vector<double> fx;
    for (int k = 1; k <= K; ++k) {
        auto u = Ap(avg); int b = (int)(max_element(u.begin(), u.end()) - u.begin());
        for (int i = 0; i < 3; ++i) avg[i] += ((i == b) - avg[i]) / (k + 1);
        if (k % 10 == 0) { fx.push_back(k); fsp_r.push_back(avg[0]); fsp_e.push_back(expl(avg)); }
    }
    printf("[E1] 朴素自博弈 %d 步后 p = (%.3f, %.3f, %.3f)，可利用度 %.3f（初始 %.3f）\n", K, softmax(th)[0], softmax(th)[1], softmax(th)[2], ne.back(), ne.front());
    printf("[E1] 迭代最优响应：石头 → 布 → 剪刀 → 石头 …（周期 3，可利用度恒为 1）\n");
    printf("[E1] 虚拟自博弈 %d 轮后平均策略 (%.3f, %.3f, %.3f)，可利用度 %.4f\n", K, avg[0], avg[1], avg[2], fsp_e.back());
    return JObj().add("naive_r", jpts(it, nr, 5)).add("naive_e", jpts(it, ne, 5)).add("br", jpts(bx, by, 3)).add("fsp_r", jpts(fx, fsp_r, 5)).add("fsp_e", jpts(fx, fsp_e, 5))
        .num("naive_e_end", ne.back(), 4).num("naive_e0", ne.front(), 4).num("fsp_e_end", fsp_e.back(), 4).num("K", K)
        .add("fsp_avg", ja(avg, 4)).add("naive_p", ja(softmax(th), 4));
}

static double expect(double ra, double rb) { return 1.0 / (1.0 + pow(10.0, (rb - ra) / 400.0)); }

static JObj e2() {
    const int P = 20, G = 20000, REP = 50;
    vector<double> Ks = {8, 16, 32, 64};
    vector<string> out;
    for (double Kf : Ks) {
        vector<double> xs, err;
        vector<double> acc(G / 200 + 1, 0), tail(REP, 0);
        for (int rep = 0; rep < REP; ++rep) {
            Rng r(1000 + rep);
            vector<double> tru(P), est(P, 1500);
            for (int i = 0; i < P; ++i) tru[i] = 1500 + 200 * r.normal();
            double mt = mean(tru); for (auto& x : tru) x += 1500 - mt;   // 真实实力的均值对齐到 1500（Elo 只确定到一个常数）
            for (int g = 0; g <= G; ++g) {
                if (g % 200 == 0) { double e = 0; for (int i = 0; i < P; ++i) e += (est[i] - tru[i]) * (est[i] - tru[i]); acc[g / 200] += sqrt(e / P) / REP; }
                if (g == G) break;
                int a = r.below(P), b = r.below(P - 1); if (b >= a) ++b;
                double s = r.u() < expect(tru[a], tru[b]) ? 1.0 : 0.0, ea = expect(est[a], est[b]);
                est[a] += Kf * (s - ea); est[b] -= Kf * (s - ea);
            }
        }
        for (int i = 0; i <= G / 200; ++i) xs.push_back(i * 200.0);
        // 稳态误差：最后 25% 的平均
        double st = 0; int c = 0; for (size_t i = acc.size() * 3 / 4; i < acc.size(); ++i) { st += acc[i]; ++c; } st /= c;
        int reach = -1; for (size_t i = 0; i < acc.size(); ++i) if (acc[i] < 100) { reach = (int)xs[i]; break; }
        printf("[E2] K=%2.0f：评分误差（RMSE）降到 100 以下需要 %d 局，稳态误差 %.1f\n", Kf, reach, st);
        out.push_back(JObj().num("K", Kf).add("err", jpts(xs, acc, 5)).num("steady", st, 4).num("reach", reach).str());
    }
    return JObj().add("runs", jlist(out)).num("P", P);
}

static JObj e3() {
    // 胜率矩阵：A>B>C>A 各 0.7；D 对所有人 0.5
    const int N = 4; const char* nm[N] = {"A", "B", "C", "D"};
    double W[N][N] = {{0.5, 0.7, 0.3, 0.5}, {0.3, 0.5, 0.7, 0.5}, {0.7, 0.3, 0.5, 0.5}, {0.5, 0.5, 0.5, 0.5}};
    vector<double> est(N, 1500); Rng r(15);
    const int G = 200000; const double K = 16;
    vector<double> avgR(N, 0); int cnt = 0;
    for (int g = 0; g < G; ++g) {
        int a = r.below(N), b = r.below(N - 1); if (b >= a) ++b;
        double s = r.u() < W[a][b] ? 1.0 : 0.0, ea = expect(est[a], est[b]);
        est[a] += K * (s - ea); est[b] -= K * (s - ea);
        if (g >= G / 2) { for (int i = 0; i < N; ++i) avgR[i] += est[i]; ++cnt; }
    }
    for (auto& x : avgR) x /= cnt;
    vector<string> rows; double maxerr = 0;
    for (int i = 0; i < N; ++i) for (int j = i + 1; j < N; ++j) {
        double pr = expect(avgR[i], avgR[j]); maxerr = max(maxerr, fabs(pr - W[i][j]));
        rows.push_back(JObj().add("pair", js(string(nm[i]) + " vs " + nm[j])).num("true", W[i][j], 3).num("pred", pr, 4).num("err", pr - W[i][j], 4).str());
    }
    printf("[E3] 非传递环 A>B>C>A：Elo 分数 A %.1f B %.1f C %.1f D %.1f；预测胜率与真实胜率最大偏差 %.3f\n", avgR[0], avgR[1], avgR[2], avgR[3], maxerr);
    return JObj().add("rating", ja(avgR, 6)).add("rows", jlist(rows)).num("maxerr", maxerr, 4);
}

int main() {
    JObj root;
    root.add("e1", e1().str());
    root.add("e2", e2().str());
    root.add("e3", e3().str());
    write_file("ch15_output.json", root.str());
    printf("写出 ch15_output.json\n");
}
