# Q04：ECS 怎么 debug？

> **面试岗位**：游戏客户端开发（秋招）
> **记录时间**：2026-10-09
> **配套代码**：同目录 `ecs_debug_demo.cpp`：约 400 行的稀疏集迷你 ECS，内置 6 种调试手段。MSVC 14.44（`_MSC_VER=1944`）/ VS2022 Release `/O2` 实测，文中「实测」数字都来自它，原始输出见 `run_output.txt`
> **编译**：`cl /nologo /std:c++latest /EHsc /O2 /utf-8 ecs_debug_demo.cpp`

## 提问记录

| 时间 | 原始提问 | 回答对应章节 |
|---|---|---|
| 2026-10-09 14:57 | 详细地为我解析「ECS 怎么 debug」，控制好深度，提高可读性，并点出面试需要注意的问题。 | 全文（§0 口述版 → §1 为什么难 → §2 工具箱全景 → §3 七类典型 bug + 实测 → §4 引擎工具对照 → §5 排查流程 → §6 面试注意点 → §7 追问 → §8 速记卡） |

---

## 0. 两分钟口述版（先背这个）

> ECS 难调的根源在于：**数据和逻辑拆开了**。实体只是一个 ID，组件是挤在数组里的纯数据，System 按批处理它们。所以 OOP 里「在对象的方法上下断点、看调用栈」这一套基本不管用：调用栈里只有 `MoveSystem::Update → 循环`，看不出是**哪个实体**，也看不出**是谁先改坏了数据**。而且组件会因为 swap-and-pop 或 Archetype 迁移**换内存地址**，所以 VS 的数据断点也靠不住。
>
> 所以我会分五层搭调试手段：
> 1. **观察**：实体检查器（Inspector），能看任意实体身上有哪些组件、值是多少；能看每个 Query 匹配到哪些实体；能看 System 列表和执行顺序。
> 2. **追踪**：按「实体 + 组件」下逻辑 watchpoint，记录**哪一帧、哪个 System** 把值从多少改成多少；结构变更（增删组件、销毁实体）也记进日志。
> 3. **校验**：用「索引 + 代际」的句柄抓悬空引用；遍历期间禁止结构变更（直接断言），结构变更统一走 CommandBuffer、到同步点再执行；System 声明读写集，调度器检查数据竞争和顺序歧义。
> 4. **回放**：帧同步项目每帧按 NetId 算状态哈希，两端对比找出**第一帧**不一致，再做组件级 diff 定位到字段；配合输入录制做确定性回放，把 bug 稳定复现出来。
> 5. **性能**：按 System 统计耗时、匹配实体数、Archetype 和 Chunk 的利用率，接入 Unreal Insights 或 Tracy。
>
> 这些调试代码全部用编译宏包起来，Shipping 包里剔除，因为它们有实打实的开销（我实测光一个 watch 判断就让遍历慢了约 45%）。

后面每一节都是在展开这段话。**面试时先给框架，再挑 1～2 个讲深**，最好挑「悬空句柄」和「desync 定位」，这两个最能体现你真懂 ECS。

---

## 1. 为什么 ECS 比 OOP 难调？

| 维度 | OOP（Actor/GameObject） | ECS | 对调试的影响 |
|---|---|---|---|
| 「对象」是什么 | 一个有地址的对象，`this` 指向它 | 一个整数 ID（索引 + 代际），本身不带任何东西 | 调试器里看不到「这个实体」，只能看到几个分散的数组 |
| 逻辑在哪 | 对象的成员函数 | System 批量遍历 | 调用栈只有 `XxxSystem::Update`，**丢失了「是哪个实体」** |
| 数据地址 | 对象生命周期内基本固定 | swap-and-pop、Archetype 迁移、Chunk 重排都会**换地址** | 指针缓存会失效；**硬件数据断点不可靠** |
| 增删组件 | 改成员变量 | 是**结构变更**，常延迟到同步点才执行 | 「刚加的组件为什么本帧没生效？」 |
| 执行顺序 | 显式调用链 | System 由调度器排序，很多时候是**隐式**的 | 一帧延迟、读写竞争、顺序依赖 |
| 并行 | 多数情况单线程 | Job 化并行很常见 | 竞态问题，复现概率低 |
| 引用别人 | 指针/弱引用 | 存 Entity ID | ID 被复用后**悄悄指向别人** |

一句话总结：**ECS 把「谁、什么时候、改了什么」这三个信息都打散了，调试工具要做的事，就是把它们重新拼回来。**

---

## 2. 调试工具箱全景

| 层 | 要回答的问题 | 手段 | 本文 demo |
|---|---|---|---|
| 观察 | 这个实体现在长什么样？这个 Query 匹配到了谁？ | Entity Inspector、Query/Archetype 浏览器、System 列表、Debug Draw | `dumpEntity` |
| 追踪 | 谁在哪一帧改了它？ | 逻辑 watchpoint、变更日志（Journaling）、结构变更日志、System trace | Part3 / Part4 |
| 校验 | 有没有违反 ECS 的约束？ | 代际句柄、遍历保护断言、CommandBuffer、读写集冲突检测、Job 安全检查 | Part1 / Part2 |
| 回放 | 能不能稳定复现？两端从哪里开始分叉？ | 帧状态哈希、组件 diff、输入录制回放、World 快照 | Part5 |
| 性能 | 时间花在哪？ | 分 System 计时、匹配数、Chunk 利用率、Insights/Tracy 埋点 | Part4 / Part6 |

> 核心思想：**在 ECS 的「咽喉」位置埋点**。所有组件读写都经过 Query/View，所有结构变更都经过 World/CommandBuffer，所有 System 都经过 Scheduler。在这三个地方加钩子，就能覆盖几乎全部数据流，这其实是 ECS **比 OOP 更好调**的地方。

---

## 3. 七类典型 bug，以及怎么抓

### 3.1 悬空实体引用：「导弹打中了友军」

**现象**：A 存着 B 的 Entity。B 死了，B 的槽位被新实体 C 复用，A 再通过这个 ID 去访问时，**拿到的是 C，而且不报错**。

**根因**：Entity 只存了索引。实体池为了让数组保持紧凑会复用索引，这在 ECS 里很常见。

**手段**：Entity = `{index, generation}`。每次销毁时把这个槽位的 generation 加 1，访问时校验 `gens[index] == e.gen`，不一致就说明句柄已经失效。EnTT 的 `entt::entity` 低位是实体编号、高位是版本号；Unity 的 `Entity` 是 `{Index, Version}`；UE Mass 的 `FMassEntityHandle` 是 `{Index, SerialNumber}`，都是这个思路。

**实测（Part1）**：
```
导弹锁定：E1v0 { Name{missile} Target{E0v0} }
敌人销毁后新建友军：E0v1 { Name{ally_hero} Health{100} }
[反例·只存索引] 导弹命中槽位 0 -> E0v1 { Name{ally_hero} Health{50} }  ← 打中了友军，且没有任何报错
[正例·索引+代际] [ECS-CHECK] 失效句柄 E0v0（槽位当前 v1，已被新实体复用） system=<main>
```

**延伸要点**：
- generation 的位数有限，会**回绕**（比如 Unity 的 Version 是 int，EnTT 默认 12 位版本号）。槽位被反复复用、某个旧句柄又长期没人访问时，理论上会碰上「回绕后 generation 恰好相等」。工程上可以让 generation 到达上限后**退役**这个槽位，不再复用。
- 失效时**不要静默返回默认值**。应该返回空或 `optional`，Debug 下打日志，并带上 System 名和帧号。业务层要显式处理「目标丢失」。

### 3.2 遍历时做结构变更：「死掉的单位有的没被清理」

**现象**：CleanupSystem 遍历 `Health`，遇到 `hp<=0` 就直接 `destroy`，结果**有的尸体留了下来**。

**根因**：稀疏集删除用的是 swap-and-pop，把最后一个元素搬到当前位置，而循环下标 `i++` 之后，**被搬过来的那个元素就被跳过了**。Archetype 型 ECS 的问题更严重：增删组件会把实体搬到另一个 Chunk，正在遍历的 Chunk 指针直接失效（崩溃或读到脏数据）。

**手段**：
1. **遍历保护**：World 维护一个 `iterDepth`，遍历时如果发生 add/remove/destroy 就**直接断言**，并带上 System 名和帧号。Unity 的做法类似：主线程上结构变更会让之前拿到的组件数组失效，Safety System 会报错。
2. **CommandBuffer**：遍历时只「记账」，到**同步点**统一回放（Unity `EntityCommandBuffer`、UE Mass `FMassCommandBuffer` / `Context.Defer()`、Bevy `Commands`）。
3. 回放时还要校验目标是否仍然有效（同一帧被两个 System 各销毁一次是常见情况）。

**实测（Part2）**：hp = `[0,30,0,60,0,0]`，期望只剩 E1、E3：
```
[反例] CleanupSystem 在 each 里直接 destroy：
  [ECS-ASSERT] 遍历中发生结构变更：destroy<*>(E0)  system=CleanupSystem frame=0
  [ECS-ASSERT] 遍历中发生结构变更：destroy<*>(E2)  system=CleanupSystem frame=0
  实际存活 4 个: E5(hp=0) E1(hp=30) E4(hp=0) E3(hp=60)  僵尸=2
[正例] 用 CommandBuffer 延迟到同步点：
  实际存活 2 个: E3(hp=60) E1(hp=30)  僵尸=0
```
E5、E4 是被 swap 到当前位置后跳过的。**留意副作用**：延迟执行意味着「本帧加的组件，本帧后面的 System 看不到」。这是 3.5 节「为什么我的 System 没跑」最常见的原因之一。

### 3.3 「到底是谁改了这个值」：逻辑 watchpoint

**现象**：Boss 死了却还在回血，或者血量莫名变成负数。

**为什么不用 VS 数据断点**：组件会搬家，数据断点盯住的是一个**地址**，搬家以后它就盯着别人了。另外 x64 只有 4 个调试寄存器，批量遍历时命中次数也太多。

**手段**：在 Query 遍历的咽喉位置，对**被 watch 的实体**在调用 System 回调前后各拍一次组件快照，做 memcmp，有变化就记录 `帧号 / System 名 / 旧值 → 新值`。没被 watch 的实体只多一次字节判断。结构变更（add/remove/destroy）在 World 入口一起记录。Unity Entities 的 **Journaling** 就是这个思路的工业版：记录结构变更和 `SetComponentData` 等写操作，可以按实体、组件、System 筛选。

**实测（Part3）**，只 watch 了 Boss：
```
[watch] f4  PoisonSystem         E0 Health{10} -> Health{-25}
[watch] f4  RegenSystem          E0 Health{-25} -> Health{-20}
[watch] f4  DeathSystem(延迟)    E0 add<Dead>
[watch] f4  DeathSystem(延迟)    E0 remove<Poison>
[watch] f5  RegenSystem          E0 Health{-20} -> Health{-15}   ← 已经 Dead 了还在回血
[watch] f6  RegenSystem          E0 Health{-15} -> Health{-10}
6 帧后：E0v0 { Name{boss} ... Health{-10} Dead }
```
一眼就能看出：**RegenSystem 的 Query 没有排除 `Dead`**。修复方法是给 Query 加 `Without<Dead>`（EnTT `exclude<Dead>`、Unity `WithNone<Dead>`、Mass `FMassFragmentRequirements::AddTagRequirement<..>(EMassFragmentPresence::None)`）。如果再往下跑，hp 回到正数，就会变成「尸体满血」，后面再接一个复活逻辑就是线上事故。

> 实用技巧：watch 日志带上 System 名后，可以配合**条件断点**：`g_curSystem == "RegenSystem" && e.index == 0`，一步跳到出事现场，这时调用栈就有意义了。

### 3.4 System 顺序问题：一帧延迟、读写竞争

**典型症状**：
- 相机抖动或画面落后一帧：`CameraFollow` 跑在 `Movement` 前面，读到的是上一帧的位置。
- 输入延迟一帧：`InputSystem` 排在 `Movement` 后面。
- 偶现的数值错乱：两个并行 System 同时写同一个组件（数据竞争）。

**手段**：
1. **执行顺序 trace**：每帧打印 System 序号、匹配实体数、待执行命令数（Part4）。
2. **显式依赖**：用 `UpdateBefore/After`（Unity）、`ExecuteBefore/After` + 处理阶段（Mass Processor）、`.before()/.after()`（Bevy）把顺序写死，不依赖注册顺序。
3. **读写集声明 + 冲突检测**：System 声明自己读写哪些组件，调度器据此建依赖图；同一阶段内两个 System 写同一组件、又没有顺序约束，就报「歧义」（Bevy 的 ambiguity detection、Unity Job Safety System 在 `ENABLE_UNITY_COLLECTIONS_CHECKS` 下抛 `InvalidOperationException`）。
4. **固定种子 + 强制单线程**模式：先排除并行因素，再二分问题。

**实测 trace（Part4）**：
```
[trace] f7 #1 MoveSystem     匹配实体=20001  待执行命令=0
[trace] f7 #2 PoisonSystem   匹配实体=0      待执行命令=0
[trace] f7 #3 RegenSystem    匹配实体=20001  待执行命令=0
[trace] f7 #4 DeathSystem    匹配实体=20001  待执行命令=0
[trace] f7 同步点：回放 0 条延迟命令
```

### 3.5 「我的 System 为什么不跑 / 没处理到这个实体」

这是新手最常遇到的问题，**排查顺序**：
1. 用 Inspector 看这个实体**实际有哪些组件**：是不是少了一个，或者多了一个被 `Without` 排除的标签？
2. 组件是不是**本帧刚加**、还躺在 CommandBuffer 里（3.2 的副作用）？
3. System 有没有被禁用、有没有被 `RequireForUpdate` / 运行条件挡住、是不是在别的 World 或别的阶段里？
4. Query 匹配数 = 0？看 trace（上面 `PoisonSystem 匹配实体=0` 就是因为 Boss 死后 Poison 被移除了，属于正常情况）。
5. 有没有 Enableable 组件被关掉了（Unity `IEnableableComponent`）？

### 3.6 帧同步 desync：两端从哪一帧开始分叉？

MOBA 这类帧同步游戏，ECS 逻辑层必须**确定性**。desync 是客户端最难查的 bug：表现可能是十几秒后才看出来，比如两端看到的血量不一样，但**根因早就发生了**。

**手段**：
1. **每帧逻辑状态哈希**：两端（或者回放与线上）逐帧对比，二分出**第一帧**不一致。
2. **哈希的对象要对**：只哈希**逻辑实体**，按**稳定的 NetId 排序**，不能按 Entity 索引，也不能按 dense 顺序；表现层实体（特效、UI）不进哈希。
3. **组件级 diff**：在第一帧不一致处，dump 两端全部逻辑组件，按 NetId 对齐后比较，定位到「哪个实体的哪个字段」。
4. **输入录制回放**：录下每帧输入，本地反复重放来稳定复现，再配合 watchpoint 查根因。

**实测（Part5）**：两端逻辑完全相同，只有 B 端多创建又销毁了一个**只在本端存在的特效实体**，swap-and-pop 让 B 端 Position 的遍历顺序变成了 `u0 u1 u3 u2`。`PickupSystem` 的规则是「遍历中第一个到达道具点的单位拾取」，**结果依赖遍历顺序**：
```
[有 bug]  A 的 Position 遍历顺序: u0 u1 u2 u3 | B: u0 u1 u3 u2
  帧  A.netHash  B.netHash  一致?  | 含实体索引的哈希一致?
   4   44539D0B   44539D0B   ✓   | ✗
   5   23F92D28   2A6A2279   ✗   | ✗
  >>> 第一次不一致在第 5 帧，组件级 diff：
    单例 itemOwner : A=102  B=103
    NetId 102 (u2) Velocity: A=Velocity{2.0,0.0}  B=Velocity{1.0,0.0}
    NetId 103 (u3) Velocity: A=Velocity{1.0,0.0}  B=Velocity{2.0,0.0}
  结论：NetId 哈希首次分歧帧=5；含实体索引的哈希首次分歧帧=1（第 1 帧就报，属于误报）
[修复后] 候选按 NetId 决胜 → 8 帧全部 ✓
```
两个结论值得在面试里讲：
- **用 Entity 索引做哈希会从第 1 帧就误报**：本端多建一个表现实体，索引就全部错开了。所以**跨端身份只能用 NetId**。
- **「依赖遍历顺序」是 ECS 帧同步的头号 desync 源**：稀疏集的 swap-and-pop、Archetype 的 Chunk 重排、`unordered_map` 的遍历顺序都不稳定。凡是「先到先得 / 取第一个 / 累加浮点」的逻辑，都要先**按稳定 key 排序**。其他常见来源还有：浮点（跨平台、FMA、SIMD 路径不同，逻辑层用定点数）、未初始化的组件字段、逻辑层读了渲染层的数据或本地时间、随机数没走同步种子。

### 3.7 性能：时间花在哪？

**手段**：按 System 统计耗时、匹配实体数、最大帧耗时；Archetype ECS 还要看 **Chunk 利用率**（碎片化会让 Chunk 半空，缓存命中率下降）；每个 System 的 Update 都埋 `TRACE_CPUPROFILER_EVENT_SCOPE` / `ZoneScoped`，在 Insights 或 Tracy 里看时间线和线程占用。

**实测（Part4，20001 个实体，207 帧平均）**：
```
System         平均us/帧   最大us 平均实体  占比
MoveSystem           23.8       43.7      20001   18.6%
PoisonSystem          8.9       70.5          0    7.0%
RegenSystem          15.2       26.1      20001   11.8%
DeathSystem          80.2      294.4      20001   62.6%
```
这张表能读出两个**真实的设计问题**：
- **PoisonSystem 匹配 0 个实体，却每帧花 8.9us**：`each<Health, Poison>` 是以 Health（2 万个）为主循环，逐个检查有没有 Poison。正确做法是**用最小的那个池驱动遍历**（EnTT 的 view 就会自动挑最短的池），或者改用 Archetype 型 ECS 直接按 Archetype 过滤。
- **DeathSystem 为了找 1 个死者，每帧扫描 2 万个实体，占了 62%**：它的逻辑本身很轻，贵在「全量扫描找稀有事件」（这个 lambda 里还带着 CommandBuffer 调用，可能没被内联，每个实体多一次函数调用；要确认得看 `/FAs` 汇编，这里属于推测）。正确做法是**变更过滤**（Unity 的 `WithChangeFilter`、Bevy 的 `Changed<Health>`），或者在扣血的地方打一个 `PendingDeath` 标签，只处理带标签的实体。

**调试代码本身的开销（Part6，100 万个实体，取 15 次最小值）**：

| 版本 | 耗时 | 每实体 |
|---|---|---|
| 调试代码编译期剔除（`if constexpr`） | 0.85 ms | 0.85 ns |
| 保留 watch 判断，但 0 个实体被 watch | 1.23 ms | 1.23 ns（**+45%**） |
| 所有实体都被 watch（快照 + memcmp） | 7.38 ms | 7.38 ns（**约 8.7×**） |

结论：**哪怕是「一个字节的判断」，放在最热的循环里也很贵**，所以调试代码要用宏或 `if constexpr` 在编译期去掉，而不是靠运行时开关。Unity 的 `ENABLE_UNITY_COLLECTIONS_CHECKS` 和 UE 的 `WITH_MASSENTITY_DEBUG` 都是这个做法。

---

## 4. 主流 ECS 框架自带的调试工具（面试时能点名加分）

| 框架 | 观察 | 追踪/校验 | 性能 |
|---|---|---|---|
| **Unity DOTS (Entities)** | Entities Hierarchy、Inspector 的 Runtime 模式、Components/Archetypes/Query 窗口 | **Journaling**（记录结构变更和组件写入）；**Job Safety System**（`AtomicSafetyHandle`，检测竞争和失效访问，Editor/Development 下开启） | Systems 窗口（每个 System 的耗时和匹配数）、Profiler |
| **UE5 Mass** | **Mass Debugger** 窗口（Archetype、Fragment、Processor 依赖关系）、Gameplay Debugger 的 Mass 分类 | `WITH_MASSENTITY_DEBUG` 下的校验、`FMassCommandBuffer` 延迟变更、Visual Logger | Unreal Insights（Processor 自带 trace scope） |
| **EnTT** | 没有自带 UI，常用 `imgui_entt_entity_editor` 等社区工具 | `registry.valid(e)` 校验版本号；Debug 下有大量 `ENTT_ASSERT` | 自己埋点 |
| **Bevy** | `bevy-inspector-egui` | System **顺序歧义检测**（`ambiguity_detection`）；借用规则在调度阶段检查读写冲突 | 接入 tracy 的 `trace` feature |
| **flecs** | **Flecs Explorer**（通过 REST 连接运行中游戏的网页版浏览器） | 运行期断言、`ecs_log` | 内置统计模块 |

> 自研 ECS（不少大厂项目都是）通常就是把上面这些拼起来：**Inspector（ImGui）+ watch 日志 + 帧哈希 + 回放 + 宏开关**。

---

## 5. 实战排查流程（遇到 ECS bug 时按这个顺序走）

1. **先能复现**：固定随机种子，录制输入，必要时强制单线程、固定帧率。
2. **观察**：用 Inspector 看出问题实体的组件集合和数值，确认它被哪些 Query 匹配。
3. **缩小时间**：帧同步项目先用帧哈希二分出第一帧；单机项目用 watch 日志找出第一次出现异常值的帧。
4. **缩小嫌疑**：watch 日志给出「哪个 System」；trace 给出「顺序是否合理」。
5. **到现场**：用条件断点（System 名 + 实体 ID + 帧号）停住，这时调用栈才有意义。
6. **判断类型**：悬空句柄？遍历中做了结构变更？顺序依赖？Query 条件写错？非确定性？
7. **修复后加一个回归**：把这段录像或用例加进 CI 的确定性回放测试。

---

## 6. 面试需要注意的问题（重点）

### 6.1 回答结构上
- **不要只说「打断点、打日志」**，这是零分回答。要先讲「为什么 ECS 让传统调试失效」，再讲「我怎么在咽喉位置把信息拼回来」。展现的是**对 ECS 内存模型的理解**。
- **先给分层框架**（观察 / 追踪 / 校验 / 回放 / 性能），再**挑 1～2 个讲深**。面试官通常会顺着你讲深的那个追问，所以挑你最有把握的。
- **结合项目讲**：哪怕只是自己写过一个小 ECS，也能讲「我实现了代际句柄 + 遍历断言 + watch 日志，抓到过 XX 问题」。

### 6.2 必须讲到的 ECS 特有点（体现你真懂）
1. **数据会搬家**：swap-and-pop、Archetype 迁移 → 指针和引用不能跨帧保存，数据断点不可靠。
2. **实体只是 ID** → 一定要有**代际**，否则复用后的悬空引用是**静默**的。
3. **结构变更要延迟** → CommandBuffer + 同步点，以及由此带来的「本帧看不到」。
4. **顺序是调度出来的** → 显式依赖 + 读写集冲突检测。
5. **调试代码要在编译期剔除**，并且能说出它的代价。

### 6.3 容易说错 / 被追问打脸的点
| 错误说法 | 正确理解 |
|---|---|
| 「ECS 数据都是 POD，所以天然不会出 bug」 | 数据简单了，可是**时序和身份**的问题变多了（顺序、延迟、复用） |
| 「ECS 天然线程安全」 | 只有在**正确声明读写集、由调度器保证**的前提下才安全；手写并行一样会竞争 |
| 「Entity 就是一个 int 索引」 | 至少是**索引 + 代际**；并且跨端同步要用 **NetId**，不能直接用 Entity |
| 「用 VS 数据断点盯住组件就行」 | 组件会搬家，要用**逻辑 watch（实体 + 组件）**，再配合条件断点 |
| 「desync 了就对比最终状态」 | 要**每帧哈希 + 二分找第一帧 + 组件 diff**；最终状态早就面目全非了 |
| 「帧哈希把所有实体都哈希进去」 | 只哈希**逻辑实体**，按 **NetId 排序**；表现实体和 Entity 索引不能进哈希（实测第 1 帧就误报） |
| 「调试开关用一个运行时 bool 控制就好」 | 热循环里一次判断就 +45%（实测），要**编译期剔除** |

### 6.4 游戏客户端（尤其 MOBA / 帧同步）加分项
- 讲清楚**逻辑层与表现层分离**：逻辑层 ECS 要确定性，表现层可以随便用浮点和本地实体，但**表现实体绝不能影响逻辑实体的遍历顺序和 ID**（Part5 的 bug 正是这样来的）。
- 讲 **desync 上报**：线上发现哈希不一致时，自动上传那一帧附近的状态快照和输入，用于离线复现。
- 讲 **确定性回放进 CI**：每次提交都回放一批历史录像，比对每帧哈希。

---

## 7. 追问预测（附答题要点）

1. **代际号溢出怎么办？** → 回绕后可能撞上旧句柄。解法：位数给够；generation 到达上限后**退役该槽位**；Debug 下加全局序列号做二次校验。
2. **CommandBuffer 回放顺序重要吗？** → 重要。同一个实体先 add 后 destroy，和先 destroy 后 add，结果不同。多线程录制时（Unity 的 `ParallelWriter` 要求传 sortKey）**要按确定的 key 排序再回放**，否则会引入非确定性。
3. **watchpoint 怎么做到「零开销」？** → 不可能做到零开销，但可以：调试代码编译期剔除；保留时只对被 watch 的实体做快照；或者用**版本号**做变更检测（Unity 按 Chunk 记录组件的 change version，只比较一个整数）。
4. **Archetype ECS 和稀疏集 ECS 的调试差异？** → Archetype：增删组件就迁移、Chunk 指针会失效，适合按 Chunk 做 change version；稀疏集：swap-and-pop 改变顺序，单个组件池的遍历顺序依赖历史操作。两者的遍历顺序**都不稳定**。
5. **怎么检测两个并行 System 的数据竞争？** → 静态方面：读写集声明 + 调度时检查；动态方面：每个组件数组挂一个安全句柄，Job 调度时登记读写，冲突就报错（Unity 的 AtomicSafetyHandle）；最后兜底用 TSan。
6. **ECS 的 Inspector 怎么实现？组件没有反射啊？** → 注册组件时一起注册「名字 + 序列化/打印函数」（demo 中的 `reg<T>("Name")` + `toStr`），或者用引擎反射（UE 的 `USTRUCT` Fragment 天然支持）。
7. **desync 定位到第 5 帧之后，怎么找到具体哪行代码？** → 在第 5 帧对分歧字段下 watch → 得到「哪个 System 写的」→ 用输入回放把条件断点停到第 5 帧这个 System 的这个实体 → 看是什么输入造成了分歧（比如 Part5 是遍历顺序）。

---

## 8. 速记卡

```
ECS 为什么难调：数据逻辑分离 → 调用栈无"谁"；数据搬家 → 指针/数据断点失效；
                ID 复用 → 静默悬空；结构变更延迟；顺序隐式；并行
五层工具：观察(Inspector/Query) 追踪(watch+Journaling) 校验(代际/遍历断言/读写集)
          回放(帧哈希+diff+录像) 性能(分System计时/Chunk利用率/Insights)
咽喉埋点：Query 遍历 / World 结构变更入口 / Scheduler
实测：只存索引 → 打中友军无报错；遍历中destroy → 6个漏2个；
      Regen 没排除 Dead → watch 日志一眼看出；
      本端特效实体打乱 dense 顺序 → "先到先得"第5帧desync，按NetId决胜修复；
      Entity 索引进哈希 → 第1帧误报；
      watch 判断 +45%、全量 watch 8.7× → 编译期剔除
帧同步四件套：NetId 排序哈希 / 二分第一帧 / 组件 diff / 输入回放
```
