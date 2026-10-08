// zombie_orphan.c —— 亲手制造僵尸进程与孤儿进程，并演示正确回收
// 编译：gcc -Wall zombie_orphan.c -o zo
// 运行：./zo zombie   另开终端：ps -o pid,ppid,stat,cmd --ppid <父pid>   可见 STAT=Z <defunct>
//      ./zo orphan   观察子进程打印的 ppid 由父进程变为 1（或 systemd --user 等 subreaper）
//      ./zo reap     SIGCHLD + waitpid(WNOHANG) 循环回收，不会留下僵尸
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <signal.h>
#include <sys/wait.h>

static void on_sigchld(int sig)
{
    (void)sig;
    int saved = errno;                         // 信号处理函数可能打断主流程，保护 errno
    while (waitpid(-1, NULL, WNOHANG) > 0) {}  // 必须循环：多个 SIGCHLD 可能被合并成一个
    errno = saved;
}

int main(int argc, char** argv)
{
    setvbuf(stdout, NULL, _IONBF, 0);
    if (argc > 1 && strcmp(argv[1], "zombie") == 0) {
        if (fork() == 0) { printf("child %d exit now\n", getpid()); _exit(0); }
        printf("parent %d 不调用 wait，睡 60s；此时子进程是僵尸\n", getpid());
        sleep(60);                             // 父进程不回收 → 子进程停留在 Z 状态
        return 0;                              // 父进程退出后，僵尸被 init 收养并立即回收
    }
    if (argc > 1 && strcmp(argv[1], "orphan") == 0) {
        if (fork() == 0) {
            for (int i = 0; i < 4; ++i) { printf("child %d: ppid=%d\n", getpid(), getppid()); sleep(1); }
            return 0;
        }
        sleep(1);
        printf("parent %d exit, child becomes orphan\n", getpid());
        return 0;
    }
    if (argc > 1 && strcmp(argv[1], "reap") == 0) {
        struct sigaction sa;
        memset(&sa, 0, sizeof sa);
        sa.sa_handler = on_sigchld;
        sa.sa_flags = SA_RESTART | SA_NOCLDSTOP;
        sigaction(SIGCHLD, &sa, NULL);
        for (int i = 0; i < 5; ++i) if (fork() == 0) _exit(i);
        sleep(2);
        printf("parent %d: 5 个子进程均已被异步回收，ps 中看不到 defunct\n", getpid());
        return 0;
    }
    fprintf(stderr, "usage: %s zombie|orphan|reap\n", argv[0]);
    return 1;
}
