// d3_isolation.cpp —— 进程隔离：同一虚拟地址，不同内容；子进程崩溃不影响父进程；线程崩溃带走整个进程
// 编译：cl /std:c++20 /EHsc /O2 /utf-8 d3_isolation.cpp
#include <windows.h>
#include <cstdio>
#include <cstring>
#include <string>
#include <thread>

int g_value = 1;

static DWORD RunChild(const char* mode)
{
    char self[MAX_PATH];
    GetModuleFileNameA(nullptr, self, MAX_PATH);
    std::string cmd = std::string("\"") + self + "\" " + mode;   // CreateProcess 要求可写缓冲区
    STARTUPINFOA si{ sizeof(si) };
    PROCESS_INFORMATION pi{};
    if (!CreateProcessA(nullptr, cmd.data(), nullptr, nullptr, FALSE, 0, nullptr, nullptr, &si, &pi)) {
        std::printf("CreateProcess 失败: %lu\n", GetLastError());
        return (DWORD)-1;
    }
    WaitForSingleObject(pi.hProcess, INFINITE);        // 相当于 Linux 的 waitpid
    DWORD code = 0;
    GetExitCodeProcess(pi.hProcess, &code);
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);                          // 关闭句柄后内核对象才能真正释放
    return code;
}

int main(int argc, char** argv)
{
    setvbuf(stdout, nullptr, _IONBF, 0);              // 无缓冲：父子进程输出按真实时间顺序出现
    if (argc > 1 && std::strcmp(argv[1], "child") == 0) {
        g_value = 999;
        std::printf("  [child pid=%lu] &g_value=%p  改成 %d\n", GetCurrentProcessId(), (void*)&g_value, g_value);
        return 7;
    }
    if (argc > 1 && std::strcmp(argv[1], "crash") == 0) {
        volatile int* p = nullptr;
        *p = 1;                                         // 访问违例
        return 0;
    }
    if (argc > 1 && std::strcmp(argv[1], "threadcrash") == 0) {
        std::thread t([] { Sleep(100); volatile int* p = nullptr; *p = 1; });
        for (int i = 0; ; ++i) { std::printf("  [threadcrash] 主线程仍在工作 %d\n", i); Sleep(40); }
    }

    std::printf("[parent pid=%lu] &g_value=%p  g_value=%d\n", GetCurrentProcessId(), (void*)&g_value, g_value);
    DWORD c1 = RunChild("child");
    std::printf("[parent] 子进程退出码=%lu，父进程 g_value 仍然=%d\n\n", c1, g_value);

    DWORD c2 = RunChild("crash");
    std::printf("[parent] 子进程空指针崩溃，退出码=0x%08lX (STATUS_ACCESS_VIOLATION)，父进程安然无恙\n\n", c2);

    DWORD c3 = RunChild("threadcrash");
    std::printf("[parent] 子进程的“某个工作线程”崩溃 → 整个子进程被终止，退出码=0x%08lX\n", c3);
}
