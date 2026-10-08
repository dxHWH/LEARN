// d1_share.cpp —— 同一进程内：哪些东西是共享的，哪些是线程私有的
// 编译：cl /std:c++20 /EHsc /O2 /utf-8 d1_share.cpp
#include <windows.h>
#include <cstdio>
#include <thread>
#include <vector>
#include <mutex>

int g_global = 100;                 // 数据段：全进程共享
thread_local int t_tls = 0;         // 线程局部存储：每个线程一份
std::mutex g_print;

void worker(int id, int* heapPtr)
{
    int local = id * 10;            // 栈：每个线程自己的栈
    t_tls = id;                     // 只改到“自己那一份”
    ULONG_PTR lo = 0, hi = 0;
    GetCurrentThreadStackLimits(&lo, &hi);   // 本线程栈的地址范围
    std::lock_guard<std::mutex> lk(g_print);
    std::printf("[T%d tid=%5lu] &g_global=%p  heap=%p  &local=%p  &t_tls=%p(t_tls=%d)\n"
                "              stack=[%p, %p)  size=%zu KB\n",
                id, GetCurrentThreadId(), (void*)&g_global, (void*)heapPtr,
                (void*)&local, (void*)&t_tls, t_tls,
                (void*)lo, (void*)hi, (size_t)(hi - lo) / 1024);
}

int main()
{
    int* heap = new int(42);        // 堆：全进程共享
    std::printf("pid=%lu  main tid=%lu\n", GetCurrentProcessId(), GetCurrentThreadId());
    std::vector<std::thread> ts;
    for (int i = 1; i <= 3; ++i) ts.emplace_back(worker, i, heap);
    for (auto& t : ts) t.join();
    std::printf("main: t_tls=%d (主线程自己的 TLS 未被子线程改动)\n", t_tls);
    delete heap;
}
