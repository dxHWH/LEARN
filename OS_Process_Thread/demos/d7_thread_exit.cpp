// d7_thread_exit.cpp —— 线程的终止与回收：主线程 return、ExitThread、std::thread 未 join、线程内核对象
// 编译：cl /std:c++20 /EHsc /O2 /utf-8 d7_thread_exit.cpp
#include <windows.h>
#include <cstdio>
#include <cstring>
#include <string>
#include <thread>

// 工作线程：每 30ms 打一个点，共 8 次，最后返回 42
static DWORD WINAPI Worker(LPVOID)
{
    for (int i = 0; i < 8; ++i) {
        std::printf("    [worker tid=%lu] tick %d\n", GetCurrentThreadId(), i);
        Sleep(30);
    }
    std::printf("    [worker] 正常跑完，返回 42\n");
    return 42;
}

static DWORD RunChild(const char* mode)
{
    char self[MAX_PATH];
    GetModuleFileNameA(nullptr, self, MAX_PATH);
    std::string cmd = std::string("\"") + self + "\" " + mode;
    STARTUPINFOA si{ sizeof(si) };
    PROCESS_INFORMATION pi{};
    if (!CreateProcessA(nullptr, cmd.data(), nullptr, nullptr, FALSE, 0, nullptr, nullptr, &si, &pi)) return (DWORD)-1;
    WaitForSingleObject(pi.hProcess, INFINITE);
    DWORD code = 0;
    GetExitCodeProcess(pi.hProcess, &code);
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
    return code;
}

int main(int argc, char** argv)
{
    setvbuf(stdout, nullptr, _IONBF, 0);
    const char* mode = argc > 1 ? argv[1] : "";

    if (std::strcmp(mode, "mainret") == 0) {           // ① 主线程从 main 返回
        CloseHandle(CreateThread(nullptr, 0, Worker, nullptr, 0, nullptr));
        Sleep(100);
        std::printf("  [main] return 0 —— CRT 随后调用 exit() → ExitProcess()\n");
        return 0;
    }
    if (std::strcmp(mode, "exitthread") == 0) {        // ② 主线程只结束自己
        CloseHandle(CreateThread(nullptr, 0, Worker, nullptr, 0, nullptr));
        Sleep(100);
        std::printf("  [main] ExitThread(0) —— 只结束主线程，进程还有别的线程\n");
        ExitThread(0);
    }
    if (std::strcmp(mode, "joinable") == 0) {          // ③ std::thread 析构时仍 joinable
        std::printf("  [main] 创建 std::thread 后既不 join 也不 detach，直接离开作用域\n");
        {
            std::thread t([] { Sleep(50); });
        }   // ~thread() 发现 joinable() == true → std::terminate()
        std::printf("  [main] 这一行不会被打印\n");
        return 0;
    }

    // ---- 父进程：依次运行三个子进程实验 ----
    std::printf("== 实验①：主线程 return，工作线程还没跑完 ==\n");
    DWORD c1 = RunChild("mainret");
    std::printf("  [parent] 子进程退出码=%lu → 工作线程在 tick 中途被强制终止，没打印“正常跑完”\n\n", c1);

    std::printf("== 实验②：主线程 ExitThread，工作线程继续 ==\n");
    DWORD c2 = RunChild("exitthread");
    std::printf("  [parent] 子进程退出码=%lu → 最后一个线程退出时进程才结束，退出码取自最后那个线程\n\n", c2);

    std::printf("== 实验③：std::thread 未 join 就析构 ==\n");
    DWORD c3 = RunChild("joinable");
    std::printf("  [parent] 子进程退出码=%lu → std::terminate() → abort()（退出码 3）\n\n", c3);

    std::printf("== 实验④：线程退出后，线程内核对象仍然保留退出码 ==\n");
    HANDLE h = CreateThread(nullptr, 0, [](LPVOID) -> DWORD { return 7; }, nullptr, 0, nullptr);
    WaitForSingleObject(h, INFINITE);                  // 线程已结束：对象变为有信号
    DWORD tc = 0;
    GetExitCodeThread(h, &tc);
    std::printf("  [parent] 线程早已结束，但句柄未关：GetExitCodeThread=%lu（相当于线程版的“僵尸”）\n", tc);
    CloseHandle(h);                                    // 关闭最后一个句柄 → ETHREAD 才被释放
    std::printf("  [parent] CloseHandle 之后线程对象才真正被释放\n");
    return 0;
}
