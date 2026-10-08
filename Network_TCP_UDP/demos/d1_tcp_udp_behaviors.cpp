// d1_tcp_udp_behaviors.cpp —— 在 Windows 回环地址上实测 TCP / UDP 的关键行为
// 编译：cl /nologo /std:c++20 /EHsc /O2 /utf-8 d1_tcp_udp_behaviors.cpp
// 实验：
//   E1 UDP 保留消息边界 vs TCP 字节流"粘包"
//   E2 UDP 无连接：sendto 到无人监听的端口照样"成功"，错误只能靠 ICMP 事后通知
//   E3 TCP connect 到无人监听的端口：收到 RST -> 连接被拒绝（并测耗时）
//   E4 用 GetTcpTable 观察四次挥手中双方的状态：ESTABLISHED -> FIN_WAIT_2 / CLOSE_WAIT -> TIME_WAIT
//   E5 半关闭：shutdown(SD_SEND) 之后仍然能收数据
//   E6 SO_LINGER{1,0} 的 closesocket 直接发 RST：对端 recv 报 WSAECONNRESET，且不进入 TIME_WAIT
#define WIN32_LEAN_AND_MEAN
#include <winsock2.h>
#include <ws2tcpip.h>
#include <iphlpapi.h>
#include <mstcpip.h>
#ifndef SIO_UDP_CONNRESET
#define SIO_UDP_CONNRESET _WSAIOW(IOC_VENDOR, 12)
#endif
#include <chrono>
#include <cstdio>
#include <cstring>
#include <string>
#include <thread>
#pragma comment(lib, "ws2_32.lib")
#pragma comment(lib, "iphlpapi.lib")

using namespace std::chrono;

static const char* StateName(DWORD s) {
    switch (s) {
        case MIB_TCP_STATE_CLOSED: return "CLOSED";
        case MIB_TCP_STATE_LISTEN: return "LISTEN";
        case MIB_TCP_STATE_SYN_SENT: return "SYN_SENT";
        case MIB_TCP_STATE_SYN_RCVD: return "SYN_RCVD";
        case MIB_TCP_STATE_ESTAB: return "ESTABLISHED";
        case MIB_TCP_STATE_FIN_WAIT1: return "FIN_WAIT_1";
        case MIB_TCP_STATE_FIN_WAIT2: return "FIN_WAIT_2";
        case MIB_TCP_STATE_CLOSE_WAIT: return "CLOSE_WAIT";
        case MIB_TCP_STATE_CLOSING: return "CLOSING";
        case MIB_TCP_STATE_LAST_ACK: return "LAST_ACK";
        case MIB_TCP_STATE_TIME_WAIT: return "TIME_WAIT";
        case MIB_TCP_STATE_DELETE_TCB: return "DELETE_TCB";
        default: return "?";
    }
}

// 打印所有本地端口或远端端口等于 port 的 TCP 连接（相当于 netstat -an | findstr port）
static void DumpTcp(const char* tag, unsigned short port) {
    ULONG size = 0;
    GetTcpTable(nullptr, &size, TRUE);
    std::string buf(size, '\0');
    auto* t = reinterpret_cast<MIB_TCPTABLE*>(buf.data());
    if (GetTcpTable(t, &size, TRUE) != NO_ERROR) { printf("GetTcpTable failed\n"); return; }
    printf("  [%s]\n", tag);
    for (DWORD i = 0; i < t->dwNumEntries; ++i) {
        auto& r = t->table[i];
        unsigned short lp = ntohs((u_short)r.dwLocalPort), rp = ntohs((u_short)r.dwRemotePort);
        if (lp != port && rp != port) continue;
        const char* who = (lp == port) ? (r.dwState == MIB_TCP_STATE_LISTEN ? "服务端监听" : "服务端") : "客户端";
        printf("    %-10s 127.0.0.1:%-5u -> 127.0.0.1:%-5u  %s\n", who, lp, rp, StateName(r.dwState));
    }
}

static sockaddr_in Addr(unsigned short port) {
    sockaddr_in a{}; a.sin_family = AF_INET; a.sin_port = htons(port);
    inet_pton(AF_INET, "127.0.0.1", &a.sin_addr); return a;
}

static SOCKET Listen(unsigned short port) {
    SOCKET s = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    auto a = Addr(port);
    bind(s, (sockaddr*)&a, sizeof a); listen(s, SOMAXCONN); return s;
}

static SOCKET Connect(unsigned short port) {
    SOCKET c = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    auto a = Addr(port);
    connect(c, (sockaddr*)&a, sizeof a); return c;
}

static void E1_Boundary() {
    printf("\n== E1 消息边界：UDP 保留边界，TCP 是字节流 ==\n");
    // UDP
    SOCKET r = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP), s = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    auto a = Addr(40001); bind(r, (sockaddr*)&a, sizeof a);
    const char* msgs[] = {"hello", "tcp-udp-book", "reliable-transport-over-udp"};
    for (auto m : msgs) sendto(s, m, (int)strlen(m), 0, (sockaddr*)&a, sizeof a);
    std::this_thread::sleep_for(100ms);
    char buf[4096];
    for (int i = 0; i < 3; ++i) {
        int n = recvfrom(r, buf, sizeof buf, 0, nullptr, nullptr);
        printf("  UDP recvfrom #%d -> %2d 字节: \"%.*s\"\n", i + 1, n, n, buf);
    }
    closesocket(r); closesocket(s);
    // TCP
    SOCKET l = Listen(40002), c = Connect(40002), sv = accept(l, nullptr, nullptr);
    for (auto m : msgs) send(c, m, (int)strlen(m), 0);
    std::this_thread::sleep_for(100ms);
    int n = recv(sv, buf, sizeof buf, 0);
    printf("  TCP 发送方 send() 3 次（5+12+27=44 字节），接收方 recv() 1 次 -> %d 字节: \"%.*s\"\n", n, n, buf);
    closesocket(c); closesocket(sv); closesocket(l);
}

static void E2_UdpNoConn() {
    printf("\n== E2 UDP 无连接：发给没人监听的端口 ==\n");
    SOCKET s = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    auto me = Addr(40011); bind(s, (sockaddr*)&me, sizeof me);
    auto dst = Addr(40012); // 没有任何进程绑定这个端口
    int n = sendto(s, "ping", 4, 0, (sockaddr*)&dst, sizeof dst);
    printf("  sendto 返回 %d（发送\"成功\"——UDP 不知道对方在不在）\n", n);
    DWORD tv = 500; setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, (char*)&tv, sizeof tv);
    char buf[64];
    int r = recvfrom(s, buf, sizeof buf, 0, nullptr, nullptr);
    printf("  随后 recvfrom 返回 %d，WSAGetLastError=%d（10054=WSAECONNRESET：内核收到 ICMP 端口不可达）\n", r, WSAGetLastError());
    // 关掉这个 Windows 特有行为再试一次
    BOOL off = FALSE; DWORD bytes = 0;
    WSAIoctl(s, SIO_UDP_CONNRESET, &off, sizeof off, nullptr, 0, &bytes, nullptr, nullptr);
    sendto(s, "ping", 4, 0, (sockaddr*)&dst, sizeof dst);
    r = recvfrom(s, buf, sizeof buf, 0, nullptr, nullptr);
    printf("  关闭 SIO_UDP_CONNRESET 后：recvfrom 返回 %d，WSAGetLastError=%d（10060=超时，错误被静默丢弃）\n", r, WSAGetLastError());
    closesocket(s);
}

static void E3_Refused() {
    printf("\n== E3 TCP connect 到没人监听的端口 ==\n");
    SOCKET c = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    auto a = Addr(40021);
    auto t0 = steady_clock::now();
    int r = connect(c, (sockaddr*)&a, sizeof a);
    auto ms = duration_cast<milliseconds>(steady_clock::now() - t0).count();
    printf("  connect 返回 %d，WSAGetLastError=%d（10061=WSAECONNREFUSED，对端回了 RST），耗时 %lld ms\n", r, WSAGetLastError(), (long long)ms);
    closesocket(c);
}

static void E4_E5_Close() {
    printf("\n== E4/E5 四次挥手的状态变化 + 半关闭 ==\n");
    const unsigned short P = 40031;
    SOCKET l = Listen(P), c = Connect(P), sv = accept(l, nullptr, nullptr);
    DumpTcp("① 三次握手完成，accept 返回后", P);

    shutdown(c, SD_SEND);                 // 客户端发 FIN（第一次挥手），但仍保留"读"方向
    std::this_thread::sleep_for(100ms);
    DumpTcp("② 客户端 shutdown(SD_SEND) 发出 FIN，服务端内核自动回 ACK 之后", P);

    char buf[256];
    int n = recv(sv, buf, sizeof buf, 0);
    printf("  服务端 recv 返回 %d（0 = 读到 FIN，对方不会再发数据了）\n", n);
    const char* tail = "server-still-can-send";
    send(sv, tail, (int)strlen(tail), 0); // 半关闭期间服务端继续发
    std::this_thread::sleep_for(50ms);
    n = recv(c, buf, sizeof buf, 0);
    printf("  客户端在 FIN_WAIT_2 中仍收到 %d 字节: \"%.*s\"  <- 这就是\"半关闭\"\n", n, n, buf);

    closesocket(sv);                      // 服务端发 FIN（第三次挥手）
    std::this_thread::sleep_for(100ms);
    n = recv(c, buf, sizeof buf, 0);
    printf("  客户端 recv 返回 %d（读到服务端 FIN）\n", n);
    closesocket(c);
    DumpTcp("③ 服务端 closesocket 发 FIN、客户端回最后一个 ACK 之后", P);
    closesocket(l);
}

static void E6_Rst() {
    printf("\n== E6 SO_LINGER{1,0}：用 RST 代替四次挥手 ==\n");
    const unsigned short P = 40041;
    SOCKET l = Listen(P), c = Connect(P), sv = accept(l, nullptr, nullptr);
    linger lg{1, 0};
    setsockopt(c, SOL_SOCKET, SO_LINGER, (char*)&lg, sizeof lg);
    closesocket(c);                       // 直接发送 RST
    std::this_thread::sleep_for(100ms);
    char buf[16];
    int n = recv(sv, buf, sizeof buf, 0);
    printf("  服务端 recv 返回 %d，WSAGetLastError=%d（10054=WSAECONNRESET）\n", n, WSAGetLastError());
    DumpTcp("RST 之后（注意：客户端没有 TIME_WAIT）", P);
    closesocket(sv); closesocket(l);
}

int main() {
    SetConsoleOutputCP(65001);
    WSADATA w; WSAStartup(MAKEWORD(2, 2), &w);
    E1_Boundary();
    E2_UdpNoConn();
    E3_Refused();
    E4_E5_Close();
    E6_Rst();
    WSACleanup();
    return 0;
}
