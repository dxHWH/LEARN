// d2_rudp_sim.cpp —— 可靠 UDP 核心机制的离散时间仿真（虚拟时钟，1 tick = 1 ms，固定随机种子，结果可复现）
// 编译：cl /nologo /std:c++20 /EHsc /O2 /utf-8 d2_rudp_sim.cpp
//
// 为什么用仿真而不是真的 socket：本机回环没有延迟也几乎不丢包，看不出协议之间的差异。
// 这里把"网络"建模成：单向时延 30~30.5 ms（RTT≈60 ms），同一方向先进先出；1% 的包"绕路"额外延迟 0~10 ms
// （这是乱序的唯一来源）；两个方向独立随机丢包；发送端链路有带宽上限（重传也要占带宽）。协议逻辑（序号、确认、RTO 估计、快速重传、窗口、乱序缓存、去重、拥塞控制）
// 与真实实现同构，只是"发包/收包"换成了函数调用。
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <map>
#include <random>
#include <set>
#include <vector>

enum class Mode { StopWait, GoBackN, Selective, Redundant };
enum class Timer { PerSegment, Connection };   // KCP：每段一个定时器；TCP（RFC 6298）：整条连接一个定时器

struct Config {
    const char* name;
    Mode mode = Mode::Selective;
    Timer timer = Timer::PerSegment;
    int wnd = 64;            // 发送窗口（个包）
    int ackDelay = 0;        // 0 = 每包立即 ACK；>0 = 延迟确认（ms），攒满 2 个或乱序时立即发
    double minRto = 200;     // RTO 下限
    double initRto = 200;    // 首个 RTT 样本之前的 RTO
    double backoff = 2.0;    // 超时后 RTO 乘数（TCP 2.0，KCP nodelay 1.5）
    int fastThresh = 0;      // 快速重传阈值：被后面多少个 ACK "跳过"就重传；0 = 关闭
    bool reno = false;       // 是否启用 Reno 式拥塞控制（丢包 = 拥塞 -> 减窗）
    int redundancy = 0;      // Redundant 模式：每个包额外携带前 k 条消息
    bool kcpFastack = false; // true：照搬 KCP 的计数方式（只要有更大序号被确认就 +1，快重传次数上限 5）
                             // false：只有"在它之后才发出"的段被确认才算跳过（避免对刚重传的段重复计数）
};

struct Pkt { double arrive; int sn; int una; std::vector<int> sns; };

struct Net {
    std::mt19937 rng;
    double loss;
    std::vector<Pkt> toRecv, toSend;   // 两个方向在途的包
    double fifo[2] = {0, 0};           // 每个方向上一个"正常"包的到达时间，保证先进先出
    std::uniform_real_distribution<double> U{0.0, 1.0};
    Net(double p, unsigned seed) : rng(seed), loss(p) {}
    double Arrive(int dir, double now) {
        double t = now + 30.0 + 0.5 * U(rng);
        if (U(rng) < 0.01) return t + 10.0 * U(rng);          // 绕路的包：可能超车，也可能被超车
        t = std::max(t, fifo[dir] + 0.001); fifo[dir] = t; return t;
    }
    bool Lost() { return U(rng) < loss; }
};

struct Result { double doneMs = 0; long long sends = 0; int n = 0; double avgLat = 0, p99 = 0, maxLat = 0; int undelivered = 0; };

Result Run(const Config& c, int N, double genIntervalMs, double loss, int ratePerMs, unsigned seed) {
    Net net(loss, seed);
    double now = 0;
    auto submitTime = [&](int i) { return i * genIntervalMs; };
    // ---------- 发送端状态 ----------
    struct Seg { int sn; double lastSend = 0, rto = 0, resendAt = 0; int xmit = 0, fast = 0; bool acked = false, need = false; };
    std::map<int, Seg> sbuf;                // 已发送、未被累计确认的段（有的可能已被单独确认）
    int nextSn = 0, sndUna = 0;
    double srtt = 0, rttvar = 0, rto = c.initRto; bool hasRtt = false;
    double connRto = c.initRto, connTimer = -1;   // 连接级定时器（Timer::Connection）
    double cwnd = 10, ssthresh = 1e9; int recoverPoint = -1;
    // ---------- 接收端状态 ----------
    int rcvNxt = 0; std::set<int> rbuf; std::vector<int> pendingAck; double ackTimer = -1;
    std::vector<double> deliverAt(N, -1);
    long long sends = 0;

    auto sendData = [&](int sn) { ++sends; if (!net.Lost()) net.toRecv.push_back({net.Arrive(0, now), sn, 0, {}}); };
    auto sendAck = [&]() {
        if (!net.Lost()) net.toSend.push_back({net.Arrive(1, now), 0, rcvNxt, pendingAck});
        pendingAck.clear(); ackTimer = -1;
    };
    auto rttSample = [&](double r) {            // RFC 6298
        if (!hasRtt) { srtt = r; rttvar = r / 2; hasRtt = true; }
        else { rttvar = 0.75 * rttvar + 0.25 * std::fabs(srtt - r); srtt = 0.875 * srtt + 0.125 * r; }
        rto = std::clamp(srtt + std::max(1.0, 4 * rttvar), c.minRto, 60000.0);
    };
    auto takeReady = [&](std::vector<Pkt>& q) {
        std::vector<Pkt> keep, ready;
        for (auto& p : q) (p.arrive <= now ? ready : keep).push_back(p);
        q.swap(keep);
        std::sort(ready.begin(), ready.end(), [](auto& a, auto& b) { return a.arrive < b.arrive; });
        return ready;
    };
    int budget = 0;
    auto xmit = [&](Seg& s) { s.xmit++; s.lastSend = now; s.fast = 0; s.need = false; s.resendAt = now + s.rto; sendData(s.sn); --budget; };

    for (; now < 600000; now += 1) {
        // ===== 1) 接收端：处理到达的数据包 =====
        for (auto& p : takeReady(net.toRecv)) {
            int sn = p.sn;
            if (c.mode == Mode::Redundant) {                         // 冗余包：一个包里有 sn, sn-1, ..., sn-k
                for (int k = 0; k <= c.redundancy; ++k) if (sn - k >= 0 && deliverAt[sn - k] < 0) deliverAt[sn - k] = now;
                continue;
            }
            if (c.mode != Mode::Selective) {                         // 停等 / GBN：只收按序的包，乱序直接丢
                if (sn == rcvNxt) { deliverAt[sn] = now; ++rcvNxt; }
                sendAck();                                           // 累计确认
                continue;
            }
            bool inOrder = (sn == rcvNxt);
            if (sn >= rcvNxt) rbuf.insert(sn);                       // 新包进乱序缓存；set 天然去重
            while (rbuf.count(rcvNxt)) { deliverAt[rcvNxt] = now; rbuf.erase(rcvNxt); ++rcvNxt; }   // 按序交付
            pendingAck.push_back(sn);                                // 重复包也要再确认一次
            if (c.ackDelay == 0 || !inOrder || pendingAck.size() >= 2) sendAck();
            else if (ackTimer < 0) ackTimer = now + c.ackDelay;
        }
        if (ackTimer >= 0 && now >= ackTimer) sendAck();
        if (c.mode != Mode::Redundant && rcvNxt >= N) break;

        // ===== 2) 发送端：处理到达的 ACK =====
        for (auto& a : takeReady(net.toSend)) {
            int maxAck = -1, newly = 0, oldUna = sndUna; double maxAckSent = -1;
            auto ackOne = [&](Seg& s, bool sample) {
                if (s.acked) return;
                s.acked = true; ++newly;
                if (sample && s.xmit == 1) rttSample(now - s.lastSend);   // Karn 算法：重传过的段不采样
            };
            for (auto& [sn, s] : sbuf) if (sn < a.una) ackOne(s, sn == a.una - 1);
            for (int sn : a.sns) if (auto it = sbuf.find(sn); it != sbuf.end()) {
                if (!it->second.acked) maxAckSent = std::max(maxAckSent, it->second.lastSend);
                ackOne(it->second, true); maxAck = std::max(maxAck, sn);
            }
            if (c.fastThresh > 0)                                    // 被后面的段"跳过"一次
                for (auto& [sn, s] : sbuf)
                    if (!s.acked && sn < maxAck && (c.kcpFastack || maxAckSent > s.lastSend)) ++s.fast;
            if (c.reno) for (int i = 0; i < newly; ++i) cwnd += (cwnd < ssthresh) ? 1.0 : 1.0 / cwnd;     // 慢启动 / 拥塞避免
            while (!sbuf.empty() && sbuf.begin()->second.acked) sbuf.erase(sbuf.begin());
            sndUna = sbuf.empty() ? nextSn : sbuf.begin()->first;
            if (c.timer == Timer::Connection && sndUna > oldUna) {     // 有新数据被确认：清除退避，重启定时器
                connRto = rto; connTimer = sbuf.empty() ? -1 : now + connRto;
            }
        }

        // ===== 3) 发送端：重传（优先）+ 发新数据；budget 模拟链路带宽 =====
        budget = ratePerMs;
        if (c.mode == Mode::Redundant) {
            while (nextSn < N && submitTime(nextSn) <= now) { sendData(nextSn); ++nextSn; }   // 只发一次，从不重传
            if (nextSn >= N && now > submitTime(N - 1) + 200) break;
            continue;
        }
        bool fastEvent = false, timeoutEvent = false;
        // 3a) 判定哪些段需要重传（打标记），真正发送受带宽约束
        if (c.fastThresh > 0)
            for (auto& [sn, s] : sbuf)
                if (!s.acked && !s.need && s.fast >= c.fastThresh && (!c.kcpFastack || s.xmit <= 5)) { fastEvent = true; s.need = true; }
        if (c.timer == Timer::Connection) {
            if (connTimer >= 0 && now >= connTimer) {
                timeoutEvent = true;
                for (auto& [sn, s] : sbuf) {
                    if (s.acked) continue;
                    s.need = true;
                    if (c.mode == Mode::Selective) break;              // TCP：只重传最老的那个；GBN：全部重传
                }
                connRto = std::min(connRto * c.backoff, 60000.0);    // 指数退避
                connTimer = now + connRto;
            }
        } else {
            for (auto& [sn, s] : sbuf)
                if (!s.acked && !s.need && now >= s.resendAt) { s.rto = std::min(s.rto * c.backoff, 60000.0); timeoutEvent = true; s.need = true; }
        }
        // 3b) 先发重传
        for (auto& [sn, s] : sbuf) { if (budget <= 0) break; if (s.need && !s.acked) xmit(s); }
        if (c.reno && (fastEvent || timeoutEvent) && sndUna > recoverPoint) {   // 一个 RTT 内只减一次窗
            ssthresh = std::max((double)sbuf.size() / 2, 2.0);
            cwnd = timeoutEvent ? 1.0 : ssthresh;
            recoverPoint = nextSn;
        }
        double win = (c.mode == Mode::StopWait) ? 1 : c.wnd;
        if (c.reno) win = std::min(win, std::max(1.0, std::floor(cwnd)));
        while (budget > 0 && nextSn < N && submitTime(nextSn) <= now && nextSn < sndUna + (int)win) {
            Seg& s = sbuf[nextSn]; s.sn = nextSn; s.rto = rto;
            xmit(s);
            if (c.timer == Timer::Connection && connTimer < 0) { connRto = rto; connTimer = now + connRto; }
            ++nextSn;
        }
    }
    // ---------- 统计 ----------
    Result r; r.sends = sends; r.n = N; r.doneMs = now;
    std::vector<double> lat;
    for (int i = 0; i < N; ++i) { if (deliverAt[i] < 0) ++r.undelivered; else lat.push_back(deliverAt[i] - submitTime(i)); }
    if (!lat.empty()) {
        std::sort(lat.begin(), lat.end());
        double sum = 0; for (double v : lat) sum += v;
        r.avgLat = sum / lat.size(); r.p99 = lat[std::min(lat.size() - 1, (size_t)(lat.size() * 0.99))]; r.maxLat = lat.back();
    }
    return r;
}

int main() {
    Config sw{"A1 停等 ARQ（窗口=1）"};             sw.mode = Mode::StopWait; sw.timer = Timer::Connection;
    Config gbn{"A2 回退N步 GBN（窗口64）"};          gbn.mode = Mode::GoBackN; gbn.timer = Timer::Connection;
    Config sr{"A3 选择重传 SR（窗口64，仅超时重传）"};
    Config srf{"A4 SR + 快速重传（阈值3）"};          srf.fastThresh = 3;
    Config tcp{"T  类TCP：A4+SACK+延迟ACK+RTO×2+Reno"};
    tcp.timer = Timer::Connection; tcp.fastThresh = 3; tcp.ackDelay = 40; tcp.reno = true; tcp.minRto = 200; tcp.initRto = 1000;
    Config tcpNc = tcp; tcpNc.name = "T' 类TCP，但关闭拥塞控制"; tcpNc.reno = false;
    Config kcp{"K  类KCP极速：快重传2+立即ACK+RTO×1.5"};
    kcp.fastThresh = 2; kcp.minRto = 30; kcp.backoff = 1.5; kcp.kcpFastack = true;

    printf("实验 A：批量传输 2000 个包 | 链路 1 包/ms | RTT≈60ms | 双向丢包 5%% | 理论下限≈2060ms\n");
    printf("%-40s %10s %8s %8s\n", "方案", "完成(ms)", "发送次数", "重传占比");
    for (auto* c : {&sw, &gbn, &sr, &srf, &tcp, &tcpNc, &kcp}) {
        Result r = Run(*c, 2000, 0, 0.05, 1, 7);
        printf("%-40s %10.0f %8lld %7.1f%%\n", c->name, r.doneMs, r.sends, 100.0 * (r.sends - r.n) / r.sends);
    }

    Config red{"R  冗余发送k=3（不可靠）"}; red.mode = Mode::Redundant; red.redundancy = 3;
    printf("\n实验 B：每 10ms 产生一条消息（100Hz，共 1000 条）| RTT≈60ms | 测\"产生 -> 按序交付\"时延\n");
    printf("%-5s %-40s %8s %8s %8s %6s %6s\n", "丢包", "方案", "平均ms", "P99ms", "最大ms", "发送", "丢失");
    for (double loss : {0.0, 0.05, 0.10, 0.20}) {
        for (auto* c : {&tcp, &tcpNc, &kcp, &red}) {
            Result r = Run(*c, 1000, 10, loss, 1000, 11);
            printf("%4.0f%% %-40s %8.1f %8.0f %8.0f %6lld %6d\n", loss * 100, c->name, r.avgLat, r.p99, r.maxLat, r.sends, r.undelivered);
        }
    }
    return 0;
}
