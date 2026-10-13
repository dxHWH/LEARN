// coop_net_demo.cpp —— 《UE 双人联机射击 Demo：面试复盘与正确实现》配套实验
//
// 不依赖 UE。用离散事件模型复现 UE 网络里与本 Demo 相关的行为，所有书中“实测”数字都来自这里：
//   E1 开火链路时序：原蓝图方案 / ShooterGame 式预测 / 预测 + 序号对账（RTT 0~200 ms）
//   E2 逐发 Reliable RPC 在丢包下的队头阻塞（处理延迟分位数、同帧扎堆）
//   E3 延迟补偿：不补偿 / 服务器回溯 / 客户端判定 + 容差，命中率与“受害者位移”
//   E4 换弹：把原蓝图「剩余子弹计算 + Server_Change」逐节点移植，和修正版比子弹守恒
//   E5 弹药 UI：服务器值 / 直接覆盖 / pending 对账三种显示的回跳次数
//   E6 CharacterMovement 纠正：MaxWalkSpeed 两端不一致时的纠正频率与回拉距离
//   E7 每秒消息量估算：每发一个复制 Actor vs 命中扫描 + BurstCounter
//
// 模型约定（与 UE 默认值对齐，书中逐条说明）：
//   服务器网络 Tick 30 Hz（NetServerMaxTickRate=30）；属性与 unreliable multicast 在下一次网络 Tick 发出；
//   reliable RPC 有序：后一条必须等前一条处理完；丢包后约 1 RTT + 1 帧重传；
//   客户端每帧 60 Hz；MAXPOSITIONERRORSQUARED=3（误差 > 1.73 cm 才纠正）；
//   两次纠正最小间隔 NetworkMinTimeBetweenClientAdjustments=0.1 s（误差 > 15 cm 时 0.05 s）。
//
// 编译：cl /nologo /std:c++latest /EHsc /O2 /utf-8 coop_net_demo.cpp
// 运行：coop_net_demo.exe            全部实验
//       coop_net_demo.exe E3 E6      只跑指定实验（新增实验时不必重跑旧的，避免数字漂移）
#include <algorithm>
#include <cstdarg>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <functional>
#include <map>
#include <queue>
#include <random>
#include <string>
#include <vector>

// ------------------------------------------------------------------ 通用
static std::string gJson;  // figdata.json 内容（给电子书画图用）
static void J(const char* fmt, ...) {
    va_list ap; va_start(ap, fmt);
    va_list ap2; va_copy(ap2, ap);
    int n = vsnprintf(nullptr, 0, fmt, ap); va_end(ap);
    std::string buf(n + 1, '\0'); vsnprintf(buf.data(), n + 1, fmt, ap2); va_end(ap2);
    buf.resize(n); gJson += buf;
}

struct Link {
    double rtt, jitter, loss; std::mt19937 rng;
    Link(double rtt_ms, double jit_ms, double loss_, unsigned seed) : rtt(rtt_ms / 1000), jitter(jit_ms / 1000), loss(loss_), rng(seed) {}
    double u01() { return std::uniform_real_distribution<double>(0, 1)(rng); }
    double oneway() { return rtt / 2 + jitter * u01(); }
    bool drop() { return u01() < loss; }
};

static const double NET_DT = 1.0 / 30.0;     // 服务器网络 Tick
static double nextTick(double t) { return std::ceil(t / NET_DT - 1e-9) * NET_DT; }

static double pct(std::vector<double> v, double p) {
    if (v.empty()) return 0;
    std::sort(v.begin(), v.end());
    size_t k = (size_t)std::min<double>(v.size() - 1, std::floor(p * (v.size() - 1) + 0.5));
    return v[k];
}
static double mean(const std::vector<double>& v) { double s = 0; for (double x : v) s += x; return v.empty() ? 0 : s / v.size(); }

// ------------------------------------------------------------------ E1 / E5：开火链路
enum Scheme { ORIGINAL = 0, NAIVE_PRED = 1, RECONCILE = 2 };
static const char* SCHEME_NAME[] = {"原蓝图方案", "预测+直接覆盖", "预测+序号对账"};

struct Ev { double t; int lane; std::string kind; int v; };  // lane 0 本机 1 服务器 2 另一端
struct E1Out {
    double ownLat = 0, remLat = 0, bulletPop = 0, dryDelay = 0;
    int rpc = 0, ownerFx = 0, bullets = 0, ghostOwner = 0, ghostRemote = 0, remoteFx = 0, rollbacks = 0, maxJump = 0;
    std::vector<Ev> evs; std::vector<std::pair<double, int>> display;
};

// 一次按住开火：t=0 按下，HOLD 秒后松开；弹匣 10 发，射速 0.12 s/发（与原 BP_Weapon 一致）
static E1Out e1_run(Scheme sc, double rtt_ms, double jit_ms, unsigned seed, double hold = 1.5) {
    const int MAG = 10; const double INTERVAL = 0.12; const double BULLET_V = 200.0; // 20000 cm/s
    Link up(rtt_ms, jit_ms, 0, seed), down(rtt_ms, jit_ms, 0, seed * 7 + 1), rem(rtt_ms, jit_ms, 0, seed * 13 + 5);
    E1Out o;
    struct Q { double t; long long id; int type; int a, b, c; bool operator<(const Q& q) const { return t != q.t ? t > q.t : id > q.id; } };
    std::priority_queue<Q> pq; long long nid = 0;
    auto push = [&](double t, int type, int a = 0, int b = 0, int c = 0) { pq.push({t, nid++, type, a, b, c}); };
    enum { C_TIMER, S_RECV, S_TICK, C_REP, C_MCAST, R_MCAST, R_BURST, C_BULLET, R_BULLET };
    // ---- 服务器状态
    int ammoS = MAG, lastSeqS = 0, burstS = 0; int sentAmmo = MAG, sentSeq = 0, sentBurst = 0, remSentBurst = 0;
    std::vector<int> pendingMcast;      // 本 Tick 待发的 multicast（1=有子弹 0=空放）
    std::vector<double> pendingSpawn;   // 本 Tick 新生成的子弹（生成时间）
    double lastArrive = 0;
    std::map<int, double> inputTime;    // seq -> 客户端按下时刻
    std::map<int, bool> serverOk;       // seq -> 服务器是否接受
    // ---- 客户端状态
    int ammoC = MAG; double repTickSeen = -1; int repAmmo = MAG, repSeq = 0; int seq = 0; bool firing = true; int lastDisplay = MAG;
    double stopTime = -1;
    auto display = [&]() -> int {
        if (sc == ORIGINAL) return repAmmo;
        if (sc == NAIVE_PRED) return ammoC;
        return repAmmo - (seq - repSeq);
    };
    auto recordDisplay = [&](double t) {
        int d = display();
        if (d != lastDisplay) { if (d > lastDisplay) { o.rollbacks++; o.maxJump = std::max(o.maxJump, d - lastDisplay); } lastDisplay = d; o.display.push_back({t, d}); }
    };
    o.display.push_back({0, MAG});
    push(0, C_TIMER);
    for (double t = NET_DT; t < 4.0; t += NET_DT) push(t, S_TICK);
    std::vector<double> ownLat, remLat, pops;
    while (!pq.empty()) {
        Q q = pq.top(); pq.pop(); double t = q.t;
        switch (q.type) {
        case C_TIMER: {
            if (!firing || t >= hold - 1e-9) break;
            bool can = sc == ORIGINAL ? repAmmo > 0 : display() > 0;
            if (!can) {  // 空仓：原蓝图 PlaySound2D(dry fire) + ClearTimer
                firing = false; stopTime = t; o.evs.push_back({t, 0, "dry", 0});
                break;
            }
            ++seq; inputTime[seq] = t; o.rpc++;
            o.evs.push_back({t, 0, "input", seq});
            if (sc != ORIGINAL) { o.ownerFx++; ownLat.push_back(0); o.evs.push_back({t, 0, "fx", seq}); if (sc == NAIVE_PRED) ammoC--; recordDisplay(t); }
            double a = std::max(t + up.oneway(), lastArrive + 1e-6); lastArrive = a;   // reliable 有序
            push(a, S_RECV, seq);
            push(t + INTERVAL, C_TIMER);
            break;
        }
        case S_RECV: {
            int s = q.a;
            bool ok = ammoS > 0;   // CheckCanFire
            serverOk[s] = ok;
            if (sc == ORIGINAL) {
                // 原蓝图 Server_Fire：先 Multicast_Fire（动画），再 Server_SpawnBullet 里才 CheckCanFire
                pendingMcast.push_back(ok ? s : -s);
                if (ok) { ammoS--; pendingSpawn.push_back(t); }
            } else {
                if (ok) { ammoS--; burstS++; }
                lastSeqS = s;
            }
            o.evs.push_back({t, 1, ok ? "ok" : "reject", s});
            if (ok) o.bullets++;
            break;
        }
        case S_TICK: {
            // 属性复制（变化才发）：弹药 COND_OwnerOnly；BurstCounter COND_SkipOwner
            if (ammoS != sentAmmo || (sc == RECONCILE && lastSeqS != sentSeq)) { push(t + down.oneway(), C_REP, ammoS, lastSeqS, (int)std::lround(t * 1e6)); sentAmmo = ammoS; sentSeq = lastSeqS; }
            if (sc != ORIGINAL && burstS != remSentBurst) { push(t + rem.oneway(), R_BURST, burstS - remSentBurst, burstS); remSentBurst = burstS; }
            for (int m : pendingMcast) { push(t + down.oneway(), C_MCAST, m); push(t + rem.oneway(), R_MCAST, m); }
            pendingMcast.clear();
            for (double sp : pendingSpawn) { double pop = BULLET_V * (t - sp); pops.push_back(pop); push(t + down.oneway(), C_BULLET, (int)std::lround(pop * 100)); push(t + rem.oneway(), R_BULLET); }
            pendingSpawn.clear();
            break;
        }
        case C_REP: {
            double tickT = q.c / 1e6;
            if (tickT <= repTickSeen) break;   // 乱序到达的旧包：UE 默认丢弃
            repTickSeen = tickT; repAmmo = q.a; repSeq = q.b;
            if (sc == NAIVE_PRED) ammoC = q.a;   // OnRep 直接覆盖本地预测值
            o.evs.push_back({t, 0, "rep", q.a});
            recordDisplay(t);
            break;
        }
        case C_MCAST: {
            int s = q.a; o.ownerFx++;
            if (s > 0) ownLat.push_back(t - inputTime[s]); else o.ghostOwner++;
            o.evs.push_back({t, 0, s > 0 ? "fx" : "ghost", std::abs(s)});
            break;
        }
        case R_MCAST: {
            int s = q.a; o.remoteFx++;
            if (s > 0) remLat.push_back(t - inputTime[s]); else o.ghostRemote++;
            o.evs.push_back({t, 2, s > 0 ? "fx" : "ghost", std::abs(s)});
            break;
        }
        case R_BURST: {
            // 远端按 BurstCounter 的增量播放开火表现；这里用“第 k 发有效射击”的按下时刻算延迟
            int n = q.a, upto = q.b;
            for (int k = upto - n + 1; k <= upto; ++k) {
                int cnt = 0; double it = 0;
                for (auto& [s, okk] : serverOk) if (okk && ++cnt == k) { it = inputTime[s]; break; }
                remLat.push_back(t - it); o.remoteFx++;
                o.evs.push_back({t, 2, "fx", k});
            }
            break;
        }
        case C_BULLET: o.evs.push_back({t, 0, "bullet", q.a}); break;
        case R_BULLET: break;
        }
    }
    if (sc != ORIGINAL) for (auto& [s, okk] : serverOk) if (!okk) o.ghostOwner++;
    o.ownLat = mean(ownLat) * 1000; o.remLat = mean(remLat) * 1000; o.bulletPop = mean(pops);
    int lastOk = 0; for (auto& [s, okk] : serverOk) if (okk) lastOk = s;
    o.dryDelay = (stopTime >= 0 && lastOk) ? (stopTime - inputTime[lastOk]) * 1000 : -1;
    return o;
}

static void E1() {
    printf("\n=== E1 开火链路时序（按住 1.5 s，弹匣 10 发，射速 0.12 s；抖动 0~20 ms，无丢包；每格取 5 个随机种子平均）===\n");
    printf("%-14s %6s | %10s %10s | %6s %6s %6s %8s %8s | %10s %10s\n", "方案", "RTT", "本机反馈ms", "另一端ms", "RPC数", "有效弹", "本机FX", "本机空放", "另端空放", "空仓发现ms", "子弹突现m");
    J("\"E1\":[");
    bool first = true;
    for (int sc = 0; sc < 3; ++sc)
        for (double rtt : {0.0, 50.0, 100.0, 200.0}) {
            double a[11] = {0}; const int N = 5;
            for (int k = 0; k < N; ++k) {
                E1Out o = e1_run((Scheme)sc, rtt, rtt > 0 ? 20 : 0, 1000 + k * 17);
                a[0] += o.ownLat; a[1] += o.remLat; a[2] += o.rpc; a[3] += o.bullets; a[4] += o.ownerFx; a[5] += o.ghostOwner; a[6] += o.ghostRemote; a[7] += o.dryDelay; a[8] += o.bulletPop;
            }
            for (double& x : a) x /= N;
            printf("%-14s %6.0f | %10.1f %10.1f | %6.1f %6.1f %6.1f %8.1f %8.1f | %10.1f %10.2f\n", SCHEME_NAME[sc], rtt, a[0], a[1], a[2], a[3], a[4], a[5], a[6], a[7], a[8]);
            J("%s{\"s\":%d,\"rtt\":%.0f,\"own\":%.1f,\"rem\":%.1f,\"rpc\":%.1f,\"ok\":%.1f,\"fx\":%.1f,\"gown\":%.1f,\"grem\":%.1f,\"dry\":%.1f,\"pop\":%.2f}", first ? "" : ",", sc, rtt, a[0], a[1], a[2], a[3], a[4], a[5], a[6], a[7], a[8]);
            first = false;
        }
    J("],\n");
    // 时间线（RTT 200 ms，种子固定）给泳道图用
    J("\"E1_lanes\":{");
    for (int sc = 0; sc < 3; ++sc) {
        E1Out o = e1_run((Scheme)sc, 200, 20, 1000);
        J("%s\"%d\":[", sc ? "," : "", sc);
        bool f = true;
        for (auto& e : o.evs) { J("%s[%.4f,%d,\"%s\",%d]", f ? "" : ",", e.t, e.lane, e.kind.c_str(), e.v); f = false; }
        J("]");
    }
    J("},\n");
}

static void E5() {
    printf("\n=== E5 弹药 UI 显示（RTT 150 ms，抖动 30 ms；连发至打空；每方案 20 个种子）===\n");
    printf("%-14s | %8s %8s %8s | %s\n", "方案", "回跳次数", "最大回跳", "本机空放", "种子 1000 的显示序列（时刻s:弹数）");
    J("\"E5\":{");
    for (int sc = 0; sc < 3; ++sc) {
        double rb = 0, mj = 0, go = 0; const int N = 20;
        for (int k = 0; k < N; ++k) { E1Out o = e1_run((Scheme)sc, 150, 30, 1000 + k * 17, 2.0); rb += o.rollbacks; mj = std::max<double>(mj, o.maxJump); go += o.ghostOwner; }
        E1Out o = e1_run((Scheme)sc, 150, 30, 1000, 2.0);
        std::string seqs;
        for (auto& [t, v] : o.display) { char b[32]; snprintf(b, sizeof b, "%.2f:%d ", t, v); seqs += b; }
        printf("%-14s | %8.2f %8.0f %8.2f | %s\n", SCHEME_NAME[sc], rb / N, mj, go / N, seqs.c_str());
        J("%s\"%d\":{\"rb\":%.2f,\"mj\":%.0f,\"go\":%.2f,\"pts\":[", sc ? "," : "", sc, rb / N, mj, go / N);
        bool f = true;
        for (auto& [t, v] : o.display) { J("%s[%.4f,%d]", f ? "" : ",", t, v); f = false; }
        J("],\"inputs\":[");
        f = true;
        for (auto& e : o.evs) if (e.kind == "input") { J("%s%.4f", f ? "" : ",", e.t); f = false; }
        J("]}");
    }
    J("},\n");
}

// ------------------------------------------------------------------ E2：Reliable RPC 队头阻塞
static void E2() {
    printf("\n=== E2 逐发 RPC（8.33 发/秒，持续 2000 发）在丢包下：reliable 有序 vs unreliable ===\n");
    printf("%6s %6s | %9s %9s %9s %9s | %10s %8s | %12s\n", "RTT", "丢包", "平均ms", "P95 ms", "P99 ms", "最大ms", "同帧扎堆%", "单帧最多", "unreliable丢%");
    J("\"E2\":[");
    bool first = true;
    for (double rtt : {100.0, 200.0})
        for (double loss : {0.0, 0.02, 0.05, 0.10, 0.20}) {
            Link L(rtt, 20, loss, 4242 + (unsigned)(rtt + loss * 1000));
            const int N = 2000; std::vector<double> lat; double prev = 0; std::map<long long, int> frame; int lostU = 0;
            for (int i = 0; i < N; ++i) {
                double ts = i * 0.12, t = ts;
                while (L.drop()) t += L.rtt + 1.0 / 60 + L.jitter * L.u01();   // 发现丢包后重传
                double arr = t + L.oneway();
                double proc = std::max(arr, prev); prev = proc;                   // 有序：等前一条
                lat.push_back((proc - ts) * 1000);
                frame[(long long)std::ceil(proc * 30 - 1e-9)]++;
                if (L.drop()) lostU++;                                            // 同一条走 unreliable 是否丢
            }
            int burst = 0, mx = 0; for (auto& [f, c] : frame) { if (c >= 2) burst += c; mx = std::max(mx, c); }
            printf("%6.0f %5.0f%% | %9.1f %9.1f %9.1f %9.1f | %9.1f%% %8d | %11.1f%%\n", rtt, loss * 100, mean(lat), pct(lat, .95), pct(lat, .99), pct(lat, 1.0), 100.0 * burst / N, mx, 100.0 * lostU / N);
            J("%s{\"rtt\":%.0f,\"loss\":%.2f,\"avg\":%.1f,\"p95\":%.1f,\"p99\":%.1f,\"max\":%.1f,\"burst\":%.1f,\"mx\":%d,\"ulost\":%.1f}", first ? "" : ",", rtt, loss, mean(lat), pct(lat, .95), pct(lat, .99), pct(lat, 1.0), 100.0 * burst / N, mx, 100.0 * lostU / N);
            first = false;
        }
    J("],\n");
}

// ------------------------------------------------------------------ E3：延迟补偿
static void E3() {
    printf("\n=== E3 延迟补偿：瞄准“本机看到的目标”开枪，服务器判定是否命中（目标横移、随机折返；胶囊半径 30 cm）===\n");
    printf("模拟代理显示：最新快照位置 + 速度 × 收到后经过的时间（CMC SimulatedProxy 外推）；回溯上限 200 ms；容差方案 = 半径 + 200 cm（ShooterGame ClientSideHitLeeway）\n");
    printf("%6s %6s | %9s %9s %9s | %12s %12s | %12s\n", "速度", "RTT", "不补偿%", "回溯%", "容差%", "显示误差均cm", "显示误差P95", "受害者位移cm");
    J("\"E3\":[");
    bool first = true;
    const double R = 30, LEEWAY = 200, MAXREWIND = 0.200;
    for (double v : {200.0, 480.0})
        for (double rtt : {0.0, 50.0, 100.0, 150.0, 200.0, 300.0}) {
            std::mt19937 rng(777);
            auto U = [&](double a, double b) { return std::uniform_real_distribution<double>(a, b)(rng); };
            // 目标运动：分段匀速，折返间隔 0.25~0.9 s
            const double T = 60;
            std::vector<double> flips; double tt = 0; while (tt < T + 2) { tt += U(0.25, 0.9); flips.push_back(tt); }
            auto P = [&](double t) { double x = 0, last = 0, dir = 1; for (double f : flips) { if (f >= t) break; x += dir * v * (f - last); last = f; dir = -dir; } return x + dir * v * (t - last); };
            auto V = [&](double t) { int n = 0; for (double f : flips) { if (f >= t) break; ++n; } return (n % 2 ? -1 : 1) * v; };
            Link down(rtt, rtt > 0 ? 20 : 0, 0, 99), up(rtt, rtt > 0 ? 20 : 0, 0, 199);
            struct Snap { double ts, ta, p, vel; };
            std::vector<Snap> snaps;
            for (double ts = 0; ts < T + 1; ts += NET_DT) snaps.push_back({ts, ts + down.oneway(), P(ts), V(ts)});
            auto hist = [&](double t) {  // 服务器每个网络 Tick 记一帧历史，回溯时在相邻两帧之间线性插值
                double k = std::floor(t / NET_DT); double a = P(k * NET_DT), b = P((k + 1) * NET_DT);
                double al = t / NET_DT - k; return a + (b - a) * al; };
            int n = 0, hit0 = 0, hitR = 0, hitL = 0; std::vector<double> dispErr, victim;
            for (double t = 1.0; t < T; t += 0.05) {
                // 客户端此刻显示：已到达的最新快照（按服务器时间）外推
                const Snap* s = nullptr;
                for (auto& sn : snaps) if (sn.ta <= t && (!s || sn.ts > s->ts)) s = &sn;
                if (!s) continue;
                double disp = s->p + s->vel * (t - s->ta);
                double viewT = s->ts + (t - s->ta);          // 客户端“看到的是服务器哪个时刻”
                double arrive = t + up.oneway();              // 开火请求到服务器
                double now = P(arrive);
                double rewindT = std::max(viewT, arrive - MAXREWIND);
                double past = hist(rewindT);
                n++;
                if (std::fabs(disp - now) <= R) hit0++;
                if (std::fabs(disp - past) <= R) { hitR++; victim.push_back(std::fabs(now - past)); }
                if (std::fabs(disp - now) <= R + LEEWAY) hitL++;
                dispErr.push_back(std::fabs(disp - now));
            }
            printf("%6.0f %6.0f | %8.1f%% %8.1f%% %8.1f%% | %12.1f %12.1f | %12.1f\n", v, rtt, 100.0 * hit0 / n, 100.0 * hitR / n, 100.0 * hitL / n, mean(dispErr), pct(dispErr, .95), mean(victim));
            J("%s{\"v\":%.0f,\"rtt\":%.0f,\"none\":%.1f,\"rew\":%.1f,\"lee\":%.1f,\"err\":%.1f,\"err95\":%.1f,\"victim\":%.1f}", first ? "" : ",", v, rtt, 100.0 * hit0 / n, 100.0 * hitR / n, 100.0 * hitL / n, mean(dispErr), pct(dispErr, .95), mean(victim));
            first = false;
        }
    J("],\n");
}

// ------------------------------------------------------------------ E4：换弹逻辑移植
struct Gun { int cur, total, maxMag = 10, saved = 0; bool reloading = false; };
// 原蓝图 BP_Weapon.剩余子弹计算（#120~#187）：false 分支没有连到 Return 节点 → 输出参数保持默认值 0
static int origRemain(Gun& g) {
    int need = g.maxMag - g.saved;
    if (g.total - need > 0) { g.total -= need; g.saved = g.maxMag; return g.saved; }
    g.saved = g.saved + g.total; g.total = 0;
    return 0;   // 未经过 Return 节点：蓝图函数输出为该类型默认值
}
// 原蓝图 BP_Weapon.Server_Change（#105）：总子弹>0 且 弹匣未满 且 不在换弹 → 子弹保存=当前；当前=0；…Delay…；当前=剩余子弹计算()
static bool origReload(Gun& g) {
    if (!(g.total > 0)) return false;
    if (g.maxMag == g.cur) return false;
    if (g.reloading) return false;
    g.saved = g.cur; g.cur = 0; g.reloading = true;
    /* Multicast_Change + Delay(蒙太奇时长) */
    g.reloading = false; g.cur = origRemain(g);
    return true;
}
static bool fixedReload(Gun& g) {
    if (g.total <= 0 || g.cur >= g.maxMag || g.reloading) return false;
    int take = std::min(g.maxMag - g.cur, g.total);
    g.cur += take; g.total -= take;
    return true;
}
static void E4() {
    printf("\n=== E4 换弹：原蓝图逻辑逐节点移植 vs 修正版（弹匣 10）===\n");
    printf("%-18s | %-22s | %-22s\n", "换弹前 当前/备弹", "原蓝图 → 当前/备弹(丢失)", "修正版 → 当前/备弹");
    J("\"E4\":{\"cases\":[");
    int cases[][2] = {{0, 30}, {3, 30}, {3, 7}, {3, 5}, {0, 10}, {0, 4}, {9, 1}, {10, 20}};
    bool first = true;
    for (auto& c : cases) {
        Gun a{c[0], c[1]}, b{c[0], c[1]};
        bool ra = origReload(a), rb = fixedReload(b);
        int lost = (c[0] + c[1]) - (a.cur + a.total);
        printf("%8d / %-8d | %5d / %-5d (丢 %2d)%s | %5d / %-5d%s\n", c[0], c[1], a.cur, a.total, lost, ra ? "" : " 未换", b.cur, b.total, rb ? "" : " 未换");
        J("%s[%d,%d,%d,%d,%d,%d,%d]", first ? "" : ",", c[0], c[1], a.cur, a.total, lost, b.cur, b.total);
        first = false;
    }
    J("],\"session\":[");
    // 整局：起始 10/30。策略 A：打空再换；策略 B：剩 3 发就换（战术换弹）
    first = true;
    for (int strat = 0; strat < 2; ++strat) {
        for (int ver = 0; ver < 2; ++ver) {
            Gun g{10, 30}; int fired = 0, reloads = 0;
            for (int guard = 0; guard < 100; ++guard) {
                int keep = strat == 0 ? 0 : 3;
                while (g.cur > keep) { g.cur--; fired++; }
                bool ok = ver == 0 ? origReload(g) : fixedReload(g);
                if (!ok) { while (g.cur > 0) { g.cur--; fired++; } break; }
                reloads++;
                if (g.cur == 0 && g.total == 0) break;
            }
            printf("整局 %-10s %-6s：共打出 %2d / 40 发，换弹 %d 次\n", strat == 0 ? "打空再换" : "剩3发就换", ver == 0 ? "原蓝图" : "修正版", fired, reloads);
            J("%s[%d,%d,%d,%d]", first ? "" : ",", strat, ver, fired, reloads);
            first = false;
        }
    }
    J("]},\n");
}

// ------------------------------------------------------------------ E6：CharacterMovement 纠正
// 1D 模型：客户端 60 Hz 每帧移动、保存 Move、发 ServerMove（假设 CMC 的冗余重发保证移动包不丢）；
// 服务器用自己的速度重放，误差 > sqrt(3) cm 就记一次待发纠正，按 NetworkMinTimeBetweenClientAdjustments 限频发出；
// 客户端收到 ClientAdjustPosition 后回到服务器位置，并用自己的速度重放之后的 Move。
// 情形 2（半修复）：冲刺切换走 reliable Server RPC，和当帧移动包同包到达；若那个包丢了，RPC 约 1 RTT 后才重传到达。
struct E6Stat { int corrections = 0; std::vector<double> snaps; std::vector<std::pair<double, double>> trace; };
static E6Stat e6_run(int cs, double rtt, double loss, unsigned seed) {
    Link up(rtt, 10, 0, seed), down(rtt, 10, 0, seed * 3 + 7); std::mt19937 lr(seed * 11 + 3);
    auto sprint = [](double t) { return (t >= 1.0 && t < 3.0) || (t >= 4.0); };
    const double dt = 1.0 / 60, TOL = std::sqrt(3.0), TEND = 5.0;
    struct Move { double ts, d; bool fl; };
    struct Pkt { double arrive, ts, d, end; bool fl; };
    struct Adj { double arrive, ts, pos; };
    std::vector<Move> saved; std::vector<Pkt> inflight; std::vector<Adj> adjs;
    std::vector<std::pair<double, bool>> rpcs;   // (服务器处理时刻, 新状态)
    double pc = 0, ps = 0, adjTs = 0, adjPos = 0, adjErr = 0, lastAdjSent = -1, lastTick = 0;
    bool pendingAdj = false, prevFl = false, serverFlag = false;
    E6Stat st;
    auto vClient = [&](bool fl) { if (cs == 0) return 150.0; if (cs == 1) return 120.0; return fl ? 500.0 : 150.0; };
    for (int f = 0; f < (int)std::lround(TEND / dt); ++f) {
        double t = f * dt;
        for (auto it = adjs.begin(); it != adjs.end();) {
            if (it->arrive <= t + 1e-9) {
                double before = pc, p = it->pos; std::vector<Move> keep;
                for (auto& m : saved) if (m.ts > it->ts + 1e-9) { keep.push_back(m); p += vClient(m.fl) * m.d; }
                saved.swap(keep); pc = p; st.corrections++; st.snaps.push_back(std::fabs(pc - before));
                it = adjs.erase(it);
            } else ++it;
        }
        bool fl = sprint(t + 1e-9);
        pc += vClient(fl) * dt;
        saved.push_back({t, dt, fl});
        if (saved.size() > 120) saved.erase(saved.begin());
        double arr = t + up.oneway();
        inflight.push_back({arr, t, dt, pc, fl});
        if (fl != prevFl) {   // 冲刺切换：Server_StartSprint / Server_StopSprint（reliable）
            double ra = arr; bool lost = std::uniform_real_distribution<double>(0, 1)(lr) < loss;
            while (lost) { ra += up.rtt + dt; lost = std::uniform_real_distribution<double>(0, 1)(lr) < loss; }
            rpcs.push_back({ra, fl}); prevFl = fl;
        }
        std::sort(inflight.begin(), inflight.end(), [](const Pkt& a, const Pkt& b) { return a.arrive < b.arrive; });
        while (!inflight.empty() && inflight.front().arrive <= t + 1e-9) {
            Pkt k = inflight.front(); inflight.erase(inflight.begin());
            for (auto& [ra, v] : rpcs) if (ra <= k.arrive + 1e-9) serverFlag = v;   // 同包里 RPC 先于移动处理
            double vs;
            if (cs == 0) vs = sprint(k.ts + 1e-9) ? 500.0 : 150.0;   // 服务器 IsRunning 由 Server RPC 置位（近似按移动时刻）
            else if (cs == 1) vs = 150.0;                            // 服务器上 AnimBP.是否瞄准 永远是 false
            else if (cs == 2) vs = serverFlag ? 500.0 : 150.0;       // 半修复：以 RPC 到达为准
            else vs = k.fl ? 500.0 : 150.0;                          // 正确：用这一帧移动包里携带的标志
            ps += vs * k.d;
            if (std::fabs(ps - k.end) > TOL) { pendingAdj = true; adjTs = k.ts; adjPos = ps; adjErr = std::fabs(ps - k.end); }
        }
        if (t - lastTick >= NET_DT - 1e-9) {
            lastTick = t;
            double minGap = adjErr > 15.0 ? 0.05 : 0.10;   // NetworkMinTimeBetweenClientAdjustments(LargeCorrection)
            if (pendingAdj && (lastAdjSent < 0 || t - lastAdjSent >= minGap - 1e-9)) { adjs.push_back({t + down.oneway(), adjTs, adjPos}); pendingAdj = false; lastAdjSent = t; }
        }
        st.trace.push_back({t, pc});
    }
    return st;
}
static void E6() {
    printf("\n=== E6 CharacterMovement 纠正：两端 MaxWalkSpeed 不一致（持续前进 5 s；1.0~3.0 s、4.0~5.0 s 按住冲刺）===\n");
    printf("%-30s %6s %6s | %10s %12s %12s %12s\n", "情形", "RTT", "丢包", "纠正次/秒", "平均回拉cm", "最大回拉cm", "回拉总距离cm");
    const char* names[] = {"原方案：冲刺只在服务器生效", "原方案：瞄准只在本机生效", "半修复：本地+Server RPC 切换", "正确：冲刺标志随移动包预测"};
    auto sprint = [](double t) { return (t >= 1.0 && t < 3.0) || (t >= 4.0); };
    J("\"E6\":[");
    bool first = true; std::string traceJson;
    struct Row { int cs; double rtt, loss; };
    std::vector<Row> rows = {{0, 100, 0}, {0, 200, 0}, {1, 100, 0}, {1, 200, 0}, {2, 100, 0}, {2, 100, 0.10}, {2, 200, 0.10}, {3, 100, 0.10}, {3, 200, 0.10}};
    for (auto& r : rows) {
        const int N = r.loss > 0 ? 200 : 1;
        double cps = 0, tot = 0, mx = 0; std::vector<double> all;
        for (int k = 0; k < N; ++k) {
            E6Stat s = e6_run(r.cs, r.rtt, r.loss, 31 + k * 101);
            cps += s.corrections / 5.0; for (double x : s.snaps) { tot += x; mx = std::max(mx, x); all.push_back(x); }
        }
        cps /= N; tot /= N;
        printf("%-30s %6.0f %5.0f%% | %10.2f %12.2f %12.2f %12.1f\n", names[r.cs], r.rtt, r.loss * 100, cps, mean(all), mx, tot);
        J("%s{\"c\":%d,\"rtt\":%.0f,\"loss\":%.2f,\"cps\":%.2f,\"avg\":%.2f,\"max\":%.2f,\"tot\":%.1f}", first ? "" : ",", r.cs, r.rtt, r.loss, cps, mean(all), mx, tot);
        first = false;
    }
    J("],\n");
    // 给图用：RTT 200、无丢包，本机位置相对“两端一致时应在的位置”的偏差
    for (int cs = 0; cs < 4; ++cs) {
        // 情形 2 取第一个“切换 RPC 丢包”的种子，情形 3 用同一个种子对照
        static unsigned lossSeed = 0;
        if (cs == 2) for (int k = 0; k < 200 && !lossSeed; ++k) if (e6_run(2, 200, 0.10, 31 + k * 101).corrections > 0) lossSeed = 31 + k * 101;
        E6Stat s = e6_run(cs, 200, cs >= 2 ? 0.10 : 0, cs >= 2 ? lossSeed : 31);
        if (cs == 2) printf("（图 E6 情形 2/3 使用种子 %u：该局冲刺切换 RPC 首包丢失）\n", lossSeed);
        char b[64]; traceJson += (cs ? ",\"" : "\"") + std::to_string(cs) + "\":[";
        double ideal = 0; bool f = true; int k = 0;
        for (auto& [t, p] : s.trace) {
            double vi = cs == 1 ? 150.0 : (sprint(t + 1e-9) ? 500.0 : 150.0);
            ideal += vi / 60.0;
            if (k++ % 2 == 0) { snprintf(b, sizeof b, "%s[%.3f,%.2f]", f ? "" : ",", t, p - ideal); traceJson += b; f = false; }
        }
        traceJson += "]";
    }
    J("\"E6_trace\":{%s},\n", traceJson.c_str());
}

// ------------------------------------------------------------------ E7：消息量估算
static void E7() {
    printf("\n=== E7 每秒网络消息量估算（2 名玩家同时连射；Listen Server：主机 + 1 个远端客户端；子弹平均存活 0.25 s）===\n");
    printf("字节数为量级假设（RPC 头 ~8B、Actor 首包 ~60B、移动更新 ~20B、关闭 ~4B），真实值请用 Networking Insights 测\n");
    const double rate = 1 / 0.12; const double life = 0.25; const int shooters = 2; const int remoteConns = 1;
    // 原方案：远端客户端开火 -> Server_Fire（上行）；每发 Multicast_Fire 下发给远端连接；每发 1 个复制 Actor（开通道、移动更新、关闭）
    double upRpc = rate * 1;                      // 只有远端客户端那一位走上行
    double mcast = rate * shooters * remoteConns; // 两人的每一发都要多播给远端连接
    double actorOpen = rate * shooters * remoteConns;
    double moveUpd = actorOpen * life * 30;
    double bytesOrig = upRpc * 8 + mcast * 8 + actorOpen * (60 + 4) + moveUpd * 20;
    // 推荐：命中扫描；上行逐发 ServerFire(seq, 方向)~16B；下行 BurstCounter(SkipOwner)/HitNotify ~12B，≤30 Hz 合包
    double upNew = rate * 1;
    double burstNew = std::min(rate, 30.0) * shooters * remoteConns;
    double bytesNew = upNew * 16 + burstNew * 12;
    printf("%-26s | %8s %10s %10s %10s | %10s\n", "方案", "上行RPC/s", "下行多播/s", "新开通道/s", "移动更新/s", "估算B/s");
    printf("%-26s | %8.1f %10.1f %10.1f %10.1f | %10.0f\n", "原方案：每发一个复制子弹", upRpc, mcast, actorOpen, moveUpd, bytesOrig);
    printf("%-26s | %8.1f %10.1f %10.1f %10.1f | %10.0f\n", "推荐：命中扫描+BurstCounter", upNew, burstNew, 0.0, 0.0, bytesNew);
    J("\"E7\":[[%.1f,%.1f,%.1f,%.1f,%.0f],[%.1f,%.1f,0,0,%.0f]]\n", upRpc, mcast, actorOpen, moveUpd, bytesOrig, upNew, burstNew, bytesNew);
}

int main(int argc, char** argv) {
    setvbuf(stdout, nullptr, _IONBF, 0);
    std::vector<std::string> want; for (int i = 1; i < argc; ++i) want.push_back(argv[i]);
    auto on = [&](const char* e) { return want.empty() || std::find(want.begin(), want.end(), e) != want.end(); };
    printf("coop_net_demo —— UE 双人联机射击 Demo 网络行为模拟（MSVC %d）\n", _MSC_VER);
    gJson = "{\n";
    if (on("E1")) E1();
    if (on("E2")) E2();
    if (on("E3")) E3();
    if (on("E4")) E4();
    if (on("E5")) E5();
    if (on("E6")) E6();
    if (on("E7")) E7(); else gJson += "\"_\":0\n";
    gJson += "}\n";
    if (want.empty()) {
        FILE* f = fopen("figdata.json", "wb");
        if (f) { fwrite(gJson.data(), 1, gJson.size(), f); fclose(f); }
    }
    return 0;
}
