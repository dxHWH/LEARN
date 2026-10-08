# 《进程与线程：共享与独立的边界》电子书 · 产出说明

## 交付物
| 文件 | 说明 |
|---|---|
| `OS_Process_Thread.html` | 单文件离线电子书（侧边目录 + 筛选、阅读进度条、代码高亮、2 个交互实验） |
| `OS_Process_Thread.pdf` | Chrome 打印版，75 页 |
| `demos/d1~d6_*.cpp` + `*.out.txt` | 6 个 Windows 实测案例的源码与真实输出；`build_and_run.bat` 一键编译运行 |
| `demos/linux/*.c` | fork/exec/wait、僵尸/孤儿/SIGCHLD 回收示例（需要在 Linux/WSL 上自己运行） |
| `src/` | 分章源码 + `build.py`（拼装与 SVG 校验）+ `labs.js` + `shots.py`（截图校验） |

## 结构（6 部分 11 章 + 附录）
1. 基本概念：程序/进程/线程、并发与并行、Amdahl 定律
2. **资源归属（核心）**：共享/独立对照表、栈私有 ≠ 不可访问、数据竞争、崩溃半径
3. 生命周期与管理：五状态 + 六条边、Linux R/S/D/T/Z、上下文切换、PCB/TCB、task_struct、EPROCESS/TEB
4. 实现与抽象：ULT/KLT、1:1 / N:1 / M:N、有栈/无栈协程、任务
5. 平台补充：fork/COW/exec/wait、僵尸/孤儿、fork 笔试题；CreateProcess、句柄、Job 对象
6. 实战：选型的六个维度，Chrome/Nginx/PG vs MySQL/游戏引擎案例；两道重点自测题完整作答 + 16 道题库；附录是 16 张速记卡

## 关键实测数据（Windows 11，MSVC 14.44 /O2）
- d2：线程 B 改写了线程 A 栈上的数组，A 读到 {100,200,300,400}
- d3：父子进程 `&g_value` 相同、值不同；工作线程空指针访问导致整个进程以 0xC0000005 退出
- d4：同核切换：线程 0.68μs，进程 1.24–1.55μs；进程创建约 300ms（受企业安全软件影响，书中已注明）
- d5：volatile 计数在 4 线程下只得到 118 万～339 万（期望 400 万）
- d6：Fiber 中调用 Sleep(300)，另一个 Fiber 停了 307ms

## 说明
- Linux 案例的输出是典型输出，不是本机实测（本机不能用 WSL），书中已标注。
- 修改内容：编辑 `src/chapters/*.html` → `python src/build.py` → 用 Chrome 重新打印 PDF。
