// d2_stack_ptr.cpp —— “线程有自己的栈” ≠ “别的线程访问不到”
// 编译：cl /std:c++20 /EHsc /O2 /utf-8 d2_stack_ptr.cpp
#include <windows.h>
#include <cstdio>
#include <thread>
#include <atomic>
#include <latch>

int main()
{
    std::latch ready(1), modified(1);
    int* shared = nullptr;                 // 将指向 A 线程栈上的数组

    std::thread A([&] {
        int buf[4] = {1, 2, 3, 4};         // 位于 A 线程的栈上
        ULONG_PTR lo, hi;
        GetCurrentThreadStackLimits(&lo, &hi);
        std::printf("[A] buf=%p  A的栈范围=[%p, %p)\n", (void*)buf, (void*)lo, (void*)hi);
        shared = buf;                      // 把栈地址“泄露”给别的线程
        ready.count_down();
        modified.wait();                   // 等 B 改完（A 的栈帧此时仍然存活）
        std::printf("[A] 被 B 改写后的 buf = {%d, %d, %d, %d}\n", buf[0], buf[1], buf[2], buf[3]);
    });

    std::thread B([&] {
        ready.wait();
        ULONG_PTR lo, hi;
        GetCurrentThreadStackLimits(&lo, &hi);
        std::printf("[B] 我的栈范围=[%p, %p)，但我拿着指针 %p\n", (void*)lo, (void*)hi, (void*)shared);
        for (int i = 0; i < 4; ++i) shared[i] *= 100;   // 跨线程直接写别人的栈
        std::printf("[B] 写入成功：同一进程 = 同一地址空间，硬件不拦截\n");
        modified.count_down();
    });

    A.join(); B.join();
    std::printf("结论：栈“私有”只是约定（每个线程各用各的区域），不是硬件隔离。\n");
}
