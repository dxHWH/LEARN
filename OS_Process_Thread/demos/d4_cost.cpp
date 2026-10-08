// d4_cost.cpp —— 创建开销：线程 vs 进程；切换开销：线程间 vs 进程间 ping-pong
// 编译：cl /std:c++20 /EHsc /O2 /utf-8 d4_cost.cpp
#include <windows.h>
#include <cstdio>
#include <cstring>
#include <string>
#include <chrono>

using clk = std::chrono::steady_clock;
static double us(clk::duration d) { return std::chrono::duration<double, std::micro>(d).count(); }

static DWORD WINAPI Noop(LPVOID) { return 0; }

static const int PING = 100000;
static DWORD WINAPI Ponger(LPVOID p)
{
    HANDLE* ev = (HANDLE*)p;               // ev[0]=ping  ev[1]=pong
    for (int i = 0; i < PING; ++i) { WaitForSingleObject(ev[0], INFINITE); SetEvent(ev[1]); }
    return 0;
}

int main(int argc, char** argv)
{
    if (argc > 1 && std::strcmp(argv[1], "noop") == 0) return 0;
    if (argc > 1 && std::strcmp(argv[1], "pong") == 0) {          // 跨进程 ping-pong 的另一端
        HANDLE ev[2] = { OpenEventA(SYNCHRONIZE | EVENT_MODIFY_STATE, FALSE, "Local\\pt_ping"),
                         OpenEventA(SYNCHRONIZE | EVENT_MODIFY_STATE, FALSE, "Local\\pt_pong") };
        Ponger(ev);
        return 0;
    }
    SetProcessAffinityMask(GetCurrentProcess(), 1);   // 绑到同一个核：每次交互都必然发生一次上下文切换

    // ---- 1. 创建 + 销毁 ----
    const int N = 200;
    auto t0 = clk::now();
    for (int i = 0; i < N; ++i) {
        HANDLE h = CreateThread(nullptr, 0, Noop, nullptr, 0, nullptr);
        WaitForSingleObject(h, INFINITE); CloseHandle(h);
    }
    double thr = us(clk::now() - t0) / N;

    char self[MAX_PATH]; GetModuleFileNameA(nullptr, self, MAX_PATH);
    t0 = clk::now();
    for (int i = 0; i < N; ++i) {
        std::string cmd = std::string("\"") + self + "\" noop";
        STARTUPINFOA si{ sizeof(si) }; PROCESS_INFORMATION pi{};
        CreateProcessA(nullptr, cmd.data(), nullptr, nullptr, FALSE, 0, nullptr, nullptr, &si, &pi);
        WaitForSingleObject(pi.hProcess, INFINITE); CloseHandle(pi.hThread); CloseHandle(pi.hProcess);
    }
    double proc = us(clk::now() - t0) / N;
    std::printf("创建+等待+销毁  线程: %8.1f us/次   进程: %8.1f us/次   比值 ≈ %.0fx\n", thr, proc, proc / thr);

    // ---- 2. 同核 ping-pong：线程间 ----
    HANDLE ev[2] = { CreateEventA(nullptr, FALSE, FALSE, nullptr), CreateEventA(nullptr, FALSE, FALSE, nullptr) };
    HANDLE h = CreateThread(nullptr, 0, Ponger, ev, 0, nullptr);
    t0 = clk::now();
    for (int i = 0; i < PING; ++i) { SetEvent(ev[0]); WaitForSingleObject(ev[1], INFINITE); }
    double tsw = us(clk::now() - t0) / (PING * 2.0);
    WaitForSingleObject(h, INFINITE); CloseHandle(h);

    // ---- 3. 同核 ping-pong：进程间（命名事件）----
    HANDLE pe[2] = { CreateEventA(nullptr, FALSE, FALSE, "Local\\pt_ping"), CreateEventA(nullptr, FALSE, FALSE, "Local\\pt_pong") };
    std::string cmd = std::string("\"") + self + "\" pong";
    STARTUPINFOA si{ sizeof(si) }; PROCESS_INFORMATION pi{};
    CreateProcessA(nullptr, cmd.data(), nullptr, nullptr, FALSE, 0, nullptr, nullptr, &si, &pi);  // 子进程继承亲和性
    Sleep(200);
    t0 = clk::now();
    for (int i = 0; i < PING; ++i) { SetEvent(pe[0]); WaitForSingleObject(pe[1], INFINITE); }
    double psw = us(clk::now() - t0) / (PING * 2.0);
    WaitForSingleObject(pi.hProcess, INFINITE); CloseHandle(pi.hThread); CloseHandle(pi.hProcess);

    std::printf("单次切换(含事件系统调用)  线程间: %.2f us   进程间: %.2f us\n", tsw, psw);
}
