# 秋招面试问答记录（游戏客户端岗）

每道题一个目录：`QNN_<主题>/`，里面有问答文档（`.md`），需要时附可运行的 Demo 和实测输出。

| 编号 | 面试题 | 文档 | 配套 Demo |
|---|---|---|---|
| Q01 | 如何设计一个内存泄漏检测工具？ | [Q01_如何设计内存泄漏检测工具.md](Q01_memory_leak_tool/Q01_如何设计内存泄漏检测工具.md) | `leak_tracker_demo.cpp`（替换 new/delete + 栈去重 + 区间快照 + 越界/重复释放检测） |
| Q02 | 介绍一下模板的特化和偏特化（含我的回答点评，约 65 分） | [Q02_模板的特化与偏特化.md](Q02_template_specialization/Q02_模板的特化与偏特化.md) | `template_spec_demo.cpp`（偏特化各形状、Dimov/Abrahams 声明顺序、重载优先级、concepts）+ `compile_fail/` 5 个反例 |
| Q03 | `new int()` 和 `new int` 的区别？（含我的回答点评，约 55 分） | [Q03_new_int()与new_int的区别.md](Q03_new_init/Q03_new_int()与new_int的区别.md) | `new_init_demo.cpp`（0xAB 刷底观察 9 种类、Debug/Release 对比、256MB 清零代价）+ `compile_fail/` 5 个反例 |
| Q04 | ECS 怎么 debug？ | [Q04_ECS怎么debug.md](Q04_ecs_debug/Q04_ECS怎么debug.md) | `ecs_debug_demo.cpp`（稀疏集迷你 ECS：代际句柄、遍历保护+CommandBuffer、组件 watchpoint、System trace/分System计时、NetId 帧哈希+组件 diff 定位 desync、调试开销实测） |
| Q05 | UE 的 GC 底层实现方式？GC 本身有什么缺点？ | [Q05_UE的GC底层实现与缺点.md](Q05_ue_gc/Q05_UE的GC底层实现与缺点.md) | `ue_gc_sim_demo.cpp`（玩具 GC：对象数组+序列号、UPROPERTY vs 裸指针、弱指针、MarkAsGarbage 自动置空、GC 簇、增量 purge、增量标记+写屏障） |
