// fork_exec_wait.c —— Linux 进程三件套：fork 复制、exec 替换、wait 回收
// 编译运行：gcc -Wall -O2 fork_exec_wait.c -o few && ./few
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/wait.h>

int g_value = 1;

int main(void)
{
    printf("[parent] pid=%d  &g_value=%p\n", getpid(), (void*)&g_value);
    fflush(stdout);                      // 关键：fork 前清空 stdio 缓冲，否则缓冲区会被复制一份

    pid_t pid = fork();                  // 调用一次，返回两次
    if (pid < 0) { perror("fork"); exit(1); }

    if (pid == 0) {                      // ---- 子进程 ----
        g_value = 999;                   // 写时复制：此刻才真正复制这一页
        printf("[child ] pid=%d ppid=%d  &g_value=%p  g_value=%d\n",
               getpid(), getppid(), (void*)&g_value, g_value);
        fflush(stdout);
        execlp("ls", "ls", "-l", "/proc/self/exe", (char*)NULL);  // 用 ls 替换自身映像，PID 不变
        perror("execlp");                // 只有 exec 失败才会走到这里
        _exit(127);                      // 子进程里用 _exit，避免重复刷新父进程复制来的缓冲
    }

    int status;                          // ---- 父进程 ----
    pid_t w = waitpid(pid, &status, 0);  // 阻塞等待并回收，避免僵尸
    if (WIFEXITED(status))
        printf("[parent] 回收子进程 %d，退出码=%d，g_value 仍然=%d\n", w, WEXITSTATUS(status), g_value);
    else if (WIFSIGNALED(status))
        printf("[parent] 子进程 %d 被信号 %d 终止\n", w, WTERMSIG(status));
    return 0;
}
