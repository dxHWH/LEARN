// d5_race.cpp —— 共享的代价：数据竞争
// 编译：cl /std:c++20 /EHsc /O2 /utf-8 d5_race.cpp
#include <cstdio>
#include <thread>
#include <vector>
#include <atomic>
#include <mutex>
#include <chrono>

constexpr int THREADS = 4, ITER = 1'000'000;

template <class F> static void run(const char* name, F body, long long (*get)())
{
    auto t0 = std::chrono::steady_clock::now();
    std::vector<std::thread> ts;
    for (int i = 0; i < THREADS; ++i) ts.emplace_back(body);
    for (auto& t : ts) t.join();
    double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
    std::printf("%-22s 结果=%9lld  期望=%d  耗时=%6.1f ms\n", name, get(), THREADS * ITER, ms);
}

volatile long long g_plain = 0;     // volatile 只阻止编译器把循环优化掉，不提供原子性！
std::atomic<long long> g_atomic{0};
long long g_locked = 0; std::mutex g_mtx;

int main()
{
    for (int round = 1; round <= 3; ++round) {
        g_plain = 0;
        run("普通变量 ++ (round)", [] { for (int i = 0; i < ITER; ++i) g_plain = g_plain + 1; },
            [] { return (long long)g_plain; });
    }
    run("std::atomic fetch_add", [] { for (int i = 0; i < ITER; ++i) g_atomic.fetch_add(1, std::memory_order_relaxed); },
        [] { return g_atomic.load(); });
    run("std::mutex 加锁", [] { for (int i = 0; i < ITER; ++i) { std::lock_guard<std::mutex> lk(g_mtx); ++g_locked; } },
        [] { return g_locked; });
}
