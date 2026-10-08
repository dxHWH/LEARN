// d6_fiber_coro.cpp —— 用户级线程（Windows Fiber）与 C++20 无栈协程
// 编译：cl /std:c++20 /EHsc /O2 /utf-8 d6_fiber_coro.cpp
#include <windows.h>
#include <cstdio>
#include <coroutine>
#include <deque>
#include <chrono>

// ===================== Part 1: Fiber = 用户态调度的“线程” =====================
LPVOID g_mainFiber, g_f[2];
int g_round = 0;

VOID CALLBACK FiberProc(LPVOID arg)
{
    int id = (int)(INT_PTR)arg;
    int local = id * 1000;                       // 每个 Fiber 有自己独立的栈
    for (int i = 0; i < 3; ++i) {
        std::printf("  Fiber%d 第%d步  tid=%lu  &local=%p\n", id, i, GetCurrentThreadId(), (void*)&local);
        if (id == 1 && i == 1) {
            std::printf("  Fiber1 调用 Sleep(300) —— 阻塞式系统调用，整个内核线程都睡了……\n");
            auto t0 = std::chrono::steady_clock::now();
            Sleep(300);
            std::printf("  ……300ms 内 Fiber0 一步也没走（耗时 %lld ms）\n",
                (long long)std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - t0).count());
        }
        SwitchToFiber(g_mainFiber);              // 主动让出：协作式，不让就永远不切
    }
    SwitchToFiber(g_mainFiber);
}

// ===================== Part 2: C++20 协程 + 单线程调度器 =====================
struct Task {
    struct promise_type {
        Task get_return_object() { return { std::coroutine_handle<promise_type>::from_promise(*this) }; }
        std::suspend_always initial_suspend() noexcept { return {}; }
        std::suspend_always final_suspend() noexcept { return {}; }
        void return_void() {}
        void unhandled_exception() { std::terminate(); }
    };
    std::coroutine_handle<promise_type> h;
};
std::deque<std::coroutine_handle<>> g_ready;     // 就绪队列
struct Yield {                                   // co_await Yield{} = 让出并重新排队
    bool await_ready() const noexcept { return false; }
    void await_suspend(std::coroutine_handle<> h) { g_ready.push_back(h); }
    void await_resume() const noexcept {}
};
Task Download(const char* name, int chunks)
{
    for (int i = 1; i <= chunks; ++i) {
        std::printf("  [%s] 下载块 %d/%d  tid=%lu\n", name, i, chunks, GetCurrentThreadId());
        co_await Yield{};                        // 假装在等 I/O：挂起，把 CPU 让给别的任务
    }
    std::printf("  [%s] 完成\n", name);
}

int main()
{
    std::printf("== Part1: Fiber（用户级线程，M:1）==\n");
    g_mainFiber = ConvertThreadToFiber(nullptr);
    for (int i = 0; i < 2; ++i) g_f[i] = CreateFiber(64 * 1024, FiberProc, (LPVOID)(INT_PTR)i);
    for (int r = 0; r < 4; ++r) for (int i = 0; i < 2; ++i) SwitchToFiber(g_f[i]);   // 用户态轮转调度
    for (int i = 0; i < 2; ++i) DeleteFiber(g_f[i]);
    ConvertFiberToThread();

    std::printf("\n== Part2: C++20 无栈协程，单线程交替推进 ==\n");
    Task a = Download("A", 3), b = Download("B", 2);
    g_ready.push_back(a.h); g_ready.push_back(b.h);
    while (!g_ready.empty()) {                   // 最简事件循环
        auto h = g_ready.front(); g_ready.pop_front();
        h.resume();
    }
    a.h.destroy(); b.h.destroy();
    std::printf("  协程帧大小(由编译器决定, 堆上分配)，无独立栈；全部运行在同一个 tid 上\n");
}
