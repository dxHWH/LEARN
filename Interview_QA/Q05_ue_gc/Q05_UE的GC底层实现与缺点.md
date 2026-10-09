# Q05：UE 的 GC 底层是怎么实现的？GC 本身有什么缺点？

> **面试岗位**：游戏客户端开发（秋招）
> **记录时间**：2026-10-09
> **配套代码**：同目录 `ue_gc_sim_demo.cpp`，约 300 行，用 C++ 照着 UE 的结构写了一个玩具 GC：对象数组、标志位、反射引用、裸指针、弱指针、MarkAsGarbage、GC 簇、增量清除、增量标记加写屏障都有。它**不是 UE 源码**，作用是把机制跑出可观察的现象。MSVC 14.44（`_MSC_VER=1944`）/ Release `/O2` 实测，文中「实测」数字都来自它，原始输出见 `run_output.txt`
> **编译**：`cl /nologo /std:c++latest /EHsc /O2 /utf-8 ue_gc_sim_demo.cpp`
> **版本说明**：主线按 UE4.27 ～ UE5.4 讲。不同版本有差异的地方（PendingKill→Garbage、Token Stream→Schema、增量可达性分析）会单独标出来

## 提问记录

| 时间 | 原始提问 | 回答对应章节 |
|---|---|---|
| 2026-10-09 15:04 | 介绍一下 UE 的 GC 的底层实现方式？GC 本身有什么缺点？（要求：游戏客户端岗背景、深层次技术解答、控制深度、提高可读性、点出面试注意点） | 全文（§0 口述版 → §1 定性 → §2 数据结构 → §3 一次 GC 的完整流程 → §4 四个加速机制 → §5 引用怎么写才安全 → §6 缺点 → §7 项目实战 → §8 和其他 GC 对比 → §9 面试注意点 → §10 追问 → §11 速记卡） |

---

## 0. 两分钟口述版（先背这个）

> **定性**：UE 的 GC 只管 `UObject`，是**追踪式、精确式的标记-清除 GC**。它**不移动对象、不分代**。标记阶段要暂停游戏线程（Stop-The-World），但会用多个工作线程并行标记；清除阶段是增量的，分摊到多帧执行。UE5.4 起还提供了实验性的「增量可达性分析」，可以把标记也拆到多帧。
>
> **它怎么知道谁引用了谁**：靠反射。UHT 会为每个 `UPROPERTY` 生成元数据，UClass 启动时把「本类哪些偏移上放着对象引用」整理成一份紧凑的引用描述：UE4 里叫 Token Stream，UE5 里重构成了 Schema。GC 扫描对象时就照着这张表读引用，不用逐个遍历 FProperty。反射照顾不到的引用，靠 `AddReferencedObjects` 手动上报；不是 UObject 的类要持有 UObject，就继承 `FGCObject`。
>
> **流程**：所有 UObject 都登记在全局数组 `GUObjectArray` 里，每个对象对应一个 `FUObjectItem`（对象指针 + 内部标志位 + 簇根索引 + 序列号）。一次 GC 分四步：
> 1. 拿到 GC 锁，等异步加载等线程让出；
> 2. 把根集以外的所有对象标成 `Unreachable`；
> 3. 从根集出发并行遍历引用，碰到的对象清掉 `Unreachable`。碰到已经 `MarkAsGarbage` 的对象，就把指向它的 UPROPERTY 置空；
> 4. 仍然带 `Unreachable` 的对象就是垃圾。依次调用 `BeginDestroy` → 等 `IsReadyForFinishDestroy`（比如等渲染线程把资源释放完）→ `FinishDestroy` → 析构，最后释放内存。这一步按时间预算分到多帧。
>
> **加速手段**：GC 簇（Cluster）把一个资产的一大串子对象当成一个整体来标记；DisregardForGC 让启动期创建的常驻对象不参与扫描；并行标记；增量清除。
>
> **缺点**：
> - ① 标记阶段 STW，耗时和存活对象数成正比，会造成卡顿；
> - ② 不分代，每次都要把常驻对象重扫一遍；
> - ③ 内存回收不及时，析构时机也不确定；
> - ④ 只认反射引用，裸指针 GC 看不见，会变成野指针；
> - ⑤ 每个对象都有额外的元数据开销，GC 锁还会和异步加载互相等待；
> - ⑥ GC 簇是用内存换时间，一个成员被引用整簇都放不掉；
> - ⑦ GC 不防逻辑泄漏，忘掉的引用照样把对象拖住。
>
> 项目里的应对：对象池、减少 UObject 数量（纯数据改用 USTRUCT）、在加载界面里手动 GC、调整 GC 间隔和簇配置、用 Insights 定位 GC 尖峰。

**答题策略**：先用一句话定性（追踪式 / 精确式 / 标记-清除 / 不移动 / 不分代），再讲数据结构和流程，最后讲缺点和项目里怎么应对。定性那一句最容易被追问「为什么不分代？为什么不移动？」，§10 有答案。

---

## 1. 先给 UE GC 定性

| 维度 | UE 的选择 | 一句话理由 |
|---|---|---|
| 管理范围 | **只管 UObject**。FString、TArray、普通 `new` 出来的结构体仍然靠 RAII 和手动管理；非 UObject 的共享所有权用 `TSharedPtr` 引用计数 | UObject 需要反射、序列化、蓝图交互，统一由 GC 托管 |
| 判定方式 | **追踪式**（Tracing），从根集出发看能不能走到 | 循环引用天然能回收，这一点比引用计数好 |
| 精确度 | **精确式**（Precise），只看反射登记过的引用 | 不像 Boehm 那样把栈上和内存里任何长得像指针的值都当引用（保守式） |
| 回收算法 | **标记-清除**（Mark-Sweep） | 实现简单，对象不用搬家 |
| 是否移动 | **不移动、不压缩** | C++ 代码里到处是裸 `UObject*`，对象一移动这些指针全部失效 |
| 是否分代 | **不分代** | 分代需要在每次指针写入时加写屏障，C++ 的裸指针赋值拦不住（§10 Q6） |
| 停顿方式 | 标记阶段 STW（多线程并行）+ 清除阶段增量；5.4+ 可选增量标记 | 标记阶段一旦中途放开，游戏逻辑改了引用就会漏标（§4.4） |

> ⚠️ 面试最常见的错误：说「UE 用引用计数管理 UObject」。引用计数的是 `TSharedPtr/TSharedRef`，管的是**非 UObject** 的 C++ 对象。把这两套东西分清楚，本身就是加分点。

---

## 2. 底层数据结构

### 2.1 全局对象数组 `GUObjectArray`

每个 UObject 在构造时都会在全局的 `FUObjectArray` 里占一个槽位，`UObjectBase::InternalIndex` 记录下标。数组按块（chunk）分配，扩容时不会搬动已有元素。槽位上放的是 `FUObjectItem`：

```cpp
// 简化示意（不同版本字段略有差异）
struct FUObjectItem {
    UObjectBase* Object;        // 对象本体
    int32        Flags;         // EInternalObjectFlags：RootSet / Unreachable / Garbage(5.x) / PendingKill(4.x)
                                //   / ClusterRoot / ReachableInCluster / Native / Async ...
    int32        ClusterRootIndex; // 属于哪个 GC 簇（簇根的下标）
    int32        SerialNumber;  // 槽位的“身份证号”，弱指针靠它判断槽位有没有换人
};
```

为什么要单独开一个数组，而不是直接遍历对象本身：

- **遍历快**：标记的第一步要把所有对象都设成 Unreachable。顺序扫一个紧凑数组对 cache 友好，比沿着散落在堆上的对象去跳转快得多。
- **标志位和对象分离**：GC 线程改的是数组里的 Flags，不会去写对象本体的内存，多线程标记时竞争小（标志位用原子操作修改）。
- **弱指针的基础**：`TWeakObjectPtr` 里只存 `{ObjectIndex, SerialNumber}`，判断有效时就是查这个数组（§5）。

> 本 demo 的 `ObjItem` 在 x64 下 `sizeof=24`（实测）。这一小块就是每个 UObject 在 GC 上「额外交的税」之一。

### 2.2 GC 怎么知道引用：反射 → 引用描述表

GC 是精确式的，它必须准确知道对象的哪些字节是对象引用。信息来源有三个：

| 来源 | 写法 | GC 怎么用 |
|---|---|---|
| **反射属性** | `UPROPERTY() UObject* / TObjectPtr<> / TArray<TObjectPtr<>> / TMap / TSet / USTRUCT 里的 UPROPERTY` | UClass 链接时调用 `AssembleReferenceTokenStream`，把「偏移 + 类型（单个对象 / 数组 / 结构体 / Map…）」编成一串紧凑指令。UE4 叫 **Token Stream**，UE5 后来重构成 **Schema**。GC 扫描时直接按偏移取值，不用逐个查 FProperty |
| **手动上报** | 重写静态函数 `AddReferencedObjects(UObject* This, FReferenceCollector& C)` | 引用描述里有一条「调用 ARO」的指令，GC 会回调它。适合反射表达不了的容器或缓存 |
| **非 UObject 持有者** | 继承 `FGCObject`，实现 `AddReferencedObjects` + `GetReferencerName` | FGCObject 构造时把自己注册到一个**在根集里**的 `UGCObjectReferencer`。GC 扫到它时，会挨个回调所有 FGCObject |

> 关键结论：**没有 `UPROPERTY`、也没有手动上报的 `UObject*`，在 GC 眼里根本不存在。** 所以 UPROPERTY 不只是给蓝图和编辑器看的，更重要的作用是让 GC 能看见这条引用。

### 2.3 根集（Root Set）从哪来

- `AddToRoot()` 打了 `RootSet` 标志的对象（比如 GameInstance 一类的长期对象，或者手动钉住的对象）；
- 所有 `FGCObject`（通过 `UGCObjectReferencer` 间接成为根）；
- 带有 `KeepFlags` 的对象（`CollectGarbage(KeepFlags)` 传进来的，比如编辑器里的 `RF_Standalone`）；
- **DisregardForGC 对象**：引擎初始化阶段创建的对象（原生 UClass、CDO 等）放在永久对象池里，GC 认为它们永远存活，不参与标记（§4.2）；
- World、Level 等通过以上方式间接挂在根上，场景里的 Actor 再通过 `ULevel::Actors` 这类 UPROPERTY 被连到根集。

---

## 3. 一次 GC 的完整流程

```
            触发：定时（gc.TimeBetweenPurgingPendingKillObjects，默认约 61 秒）
                 / 切关卡 LoadMap / 手动 ForceGarbageCollection / 低内存阈值
                                   │
 ┌──────────────── 游戏线程暂停（标记阶段 STW） ────────────────┐
 │ ① 获取 GC 锁：等待持有 FGCScopeGuard 的线程（异步加载等）让出 │
 │ ② MarkObjectsAsUnreachable：并行扫 GUObjectArray，            │
 │    根集 / KeepFlags / DisregardForGC 之外的对象全部标 Unreachable │
 │ ③ 可达性分析：从根集出发，多个工作线程并行遍历                │
 │    （TFastReferenceCollector，工作线程之间互相窃取任务）      │
 │    · 按引用描述表读每个引用 → 清掉目标的 Unreachable 并入队 │
 │    · 目标已经 MarkAsGarbage → 把这个 UPROPERTY 置成 nullptr │
 │    · 碰到簇成员 → 直接按簇根处理（§4.1）                     │
 │ ④ 收集仍带 Unreachable 的对象，从名字哈希表里移除            │
 └──────────────────────────────────────────────────────────────┘
                                   │
 ┌──────────────── 增量清除（Incremental Purge，分到多帧） ──────┐
 │ ⑤ ConditionalBeginDestroy：通知对象开始释放（比如向渲染线程发释放命令） │
 │ ⑥ 每帧检查 IsReadyForFinishDestroy（渲染资源释放完、异步任务结束了没有） │
 │ ⑦ ConditionalFinishDestroy → 析构 → 交还给 GUObjectAllocator，槽位回收  │
 │    每帧时间预算：gc.IncrementalGCTimePerFrame                 │
 └──────────────────────────────────────────────────────────────┘
```

几个容易被追问的细节：

1. **为什么先把所有对象都标成不可达，再反过来清标志？** 这样做之后，「可达」就只是一个原子的清位操作，多线程并行标记时谁先清到都一样，不需要额外的 visited 集合。
2. **为什么析构要拆成 BeginDestroy 和 FinishDestroy？** UObject 往往持有渲染线程的资源（纹理、网格的 RHI 资源）。游戏线程不能直接删，得先发命令让渲染线程释放，再用渲染线程的 fence 等它真正处理完，所以中间可能隔好几帧。这也是「析构时机不确定」的来源之一。
3. **`Actor->Destroy()` 不会马上释放内存**。它会把 Actor 从 World 里移除、调用 EndPlay，然后 `MarkAsGarbage()`（UE4 叫 `MarkPendingKill()`）。真正的内存回收要等下一次 GC。

> **版本差异：PendingKill → Garbage**
> UE5 把 `PendingKill` 废弃了，改成 `MarkAsGarbage()` 和 `EInternalObjectFlags::Garbage`。开关 `gc.GarbageEliminationEnabled` 默认开着，GC 仍然会把指向 Garbage 对象的强引用置空，行为和 UE4 的 PendingKill 基本一样。代码里判断对象能不能用，统一写 `IsValid(Obj)`。

**实测 D2（MarkAsGarbage 和自动置空）**：

```
B1 Destroy 后、GC 前：Target 指针=非空，IsValid(Target)=false，Weak.Get()=nullptr
B2 GC 后：Target 指针=nullptr（被 GC 自动置空 1 处），释放 1 个，Weak.Get()=nullptr
```

→ 在 Destroy 之后、下一次 GC 之前的这段窗口里，**UPROPERTY 指针仍然非空**，所以必须用 `IsValid()` 判断，不能只判空。

---

## 4. 四个加速机制（实测）

### 4.1 GC 簇（Cluster）：一大串对象当一个来标记

**问题**：一个材质、粒子系统或蓝图类，加载后会带出几十上百个子对象，而且它们基本同生共死。每次 GC 都把这些子对象之间的引用重新走一遍，纯属浪费。

**做法**：可以作为簇根的对象（`CanBeClusterRoot()` 返回 true，比如材质、粒子系统、蓝图生成类，以及开启 Actor 簇后关卡里的静态 Actor）在加载完成时调用 `CreateCluster()`，把它能走到的子对象收进簇里，同时**预先算好整个簇对外引用了哪些对象**。之后 GC 扫到任何一个簇成员，都直接当成簇根处理：簇根可达，整个簇就都可达，然后只扫描簇的外部引用列表。

**实测 D3**（2000 个资产 × 101 个对象，共约 20 万个）：

| 模式 | 标记耗时 | 扫描对象 | 访问引用 | 卸载资产后只留一个叶子子对象的引用 → 存活数 |
|---|---|---|---|---|
| 无簇 | 3.615 ms | 202009 | 210008 | **2**（只剩管理器和那个叶子） |
| 有簇 | **0.016 ms** | 2009 | 10008 | **106**（整个簇 101 个 + 它引用的 4 张贴图 + 管理器） |

→ 标记快了两个数量级（这里只计了引用遍历这一步，第一步扫数组的开销两种模式都有）。代价也很直观：**只要簇里有一个成员被引用，整簇都释放不掉**。相关开关是 `gc.CreateGCClusters`、`gc.ActorClusteringEnabled`、`gc.BlueprintClusteringEnabled`。另外，簇成员如果在运行时被改成引用簇外的新对象，引擎要处理簇失效（dissolve），这也是 Actor 簇要求 Actor 本身是静态的、需要 `bCanBeInCluster` 的原因。

### 4.2 DisregardForGC：常驻对象不参与扫描

引擎启动阶段创建的对象（原生类、CDO、启动时加载的包）放进一个永久对象池，GC 直接把它们当成永远存活，标记时跳过。相关配置是 `gc.MaxObjectsNotConsideredByGC` 和 `gc.SizeOfPermanentObjectPool`。这其实是**手动版的「老年代」**：既然没有分代，就靠人工划出一块不扫的区域。

### 4.3 并行标记 + 增量清除

- 标记阶段用多个工作线程并行，线程之间互相窃取任务（`gc.AllowParallelGC`）。但**游戏线程仍然在等**，所以并行只能把停顿时间变短，消除不了停顿。
- 清除阶段按时间预算切片。**实测 D4**（20 万个对象，每个带 512B 资源，关卡卸载时同时变成垃圾）：

| 方式 | 单帧最长 | 帧数 | 总耗时 |
|---|---|---|---|
| 一次性 purge | **28.58 ms**（30 fps 下一帧就爆了） | 1 | 28.58 ms |
| 增量 purge（2ms/帧） | **2.04 ms** | 14 | 28.15 ms |

→ 总工作量没少，只是**摊平了**。代价是垃圾对象多占了 14 帧内存。

### 4.4 增量可达性分析（UE5.4+，实验性）与写屏障

STW 标记的根本矛盾在于：标记做到一半，如果放游戏逻辑继续跑，引用关系会被改。经典的三色标记（白 = 还没碰到，灰 = 已碰到但它的引用还没扫，黑 = 已扫完）在下面这种情况会漏标：

> **黑色对象 A 新引用了白色对象 C，同时唯一指向 C 的灰色 B 断开了 B→C** → A 已经扫过，不会再扫；B 扫的时候 C 已经不在了 → C 被当成垃圾回收，A 手里拿着悬空指针。

**实测 D5**（复现了上面这一幕）：

```
写屏障关：C 被回收=是，A->props[0]->name="<freed>"  <- A 持有已回收对象（悬空）
写屏障开：C 被回收=否，A->props[0]->name="C"
```

解决办法是**写屏障**：增量标记进行中，每次给引用赋值时，顺手把新指向的对象标成可达（类似 Dijkstra 插入屏障）。UE5.4 的增量可达性分析（`gc.AllowIncrementalReachability`，每帧时间由 `gc.IncrementalReachabilityTimeLimit` 控制）就是把屏障放在 **`TObjectPtr` 的赋值操作**里。这也解释了为什么 UE5 一直推动把 `UPROPERTY() UObject*` 换成 `TObjectPtr<>`：裸指针赋值是一条普通的 mov 指令，引擎没机会插入任何代码。

---

## 5. 引用怎么写才安全（这一节最贴近日常开发）

| 写法 | GC 能看到吗 | 会阻止回收吗 | 对象被回收后 | 典型用途 |
|---|---|---|---|---|
| `UPROPERTY() TObjectPtr<UFoo>` / `UFoo*` | ✅ | ✅ 强引用 | Garbage 对象会被**自动置空** | 成员变量的默认写法 |
| 裸 `UFoo*`（没加 UPROPERTY） | ❌ | ❌ | **野指针**，槽位还可能被别的对象复用 | ❌ 除了函数内的临时变量，都不要这么写 |
| `TWeakObjectPtr<UFoo>` | 不参与标记 | ❌ | `Get()` 返回 nullptr（比对序列号） | 观察者、缓存、异步回调里捕获 |
| `TStrongObjectPtr<UFoo>` | ✅（内部帮你注册成引用） | ✅ | — | 非 UObject 类里临时持有 |
| `FGCObject` + `AddReferencedObjects` | ✅ | ✅ | 需要自己处理 | 非 UObject 的管理器、Slate 控件 |
| `AddToRoot()` | ✅ 直接成为根 | ✅ 永远不回收 | — | 少用，忘了 `RemoveFromRoot` 就泄漏 |
| `TSoftObjectPtr` / `FSoftObjectPath` | ❌（存的是路径） | ❌ | 需要时按路径重新加载 | 配置表里引用资源、按需加载 |

**实测 D2（裸指针的两种死法）**：

```
A1 GC 后释放 1 个对象；HUD->raw 仍非空，指向的内容 name="<freed>"（已被回收）
A2 新建 Ally_B 复用了同一槽位：HUD->raw==Ally_B ? 是，HUD->raw->name="Ally_B"  <- 野指针“打中别人”
B3 Enemy_C 复用槽位后：Weak.Get()=nullptr（序列号 2 != 1，弱指针不会认错人）
```

→ 第二种比直接崩溃更可怕：**不崩，但逻辑错了**（HUD 以为在追踪敌人，实际追踪的是队友）。真实 UE 里内存由分配器复用，现象一样。另外，**`IsValid(裸指针)` 救不了你**：对一个已经释放的地址调用 IsValid 本身就是未定义行为，它能识别的只是「还活着但已经 MarkAsGarbage」的对象。

几个高频坑：

1. **lambda / 异步任务捕获 `this` 或 UObject\***：任务回来时对象可能早就被回收了。应该捕获 `TWeakObjectPtr`，或者用 `CreateWeakLambda`、`BindUObject`（委托绑定 UObject 时会自动按弱引用检查）。
2. **普通 C++ struct 里放 UObject\***：struct 没有 USTRUCT/UPROPERTY，GC 看不到。改成 USTRUCT + UPROPERTY；或者让外层通过 `AddReferencedObjects` 上报。
3. **嵌套容器 `TArray<TArray<UObject*>>`**：UPROPERTY 不支持，要么包一层 USTRUCT，要么用 ARO 上报。
4. **在别的线程 `NewObject`**：GC 期间创建对象有竞争问题。异步加载线程靠 `FGCScopeGuard` 和 GC 互斥，自己的线程不要随意创建或销毁 UObject。

---

## 6. GC 本身的缺点（问题的第二问）

按「性能 → 内存 → 正确性 → 工程」四类来答，层次最清楚：

### ① 性能：标记阶段 STW，停顿和存活对象数成正比

- 每次 GC 都要把**所有存活对象**走一遍。几十万个 UObject 的项目，在移动端一次全量 GC 停顿几十毫秒并不罕见，表现就是周期性的掉帧，Insights 里每隔约 60 秒冒出一个尖峰。
- **实测 D1**（单线程，每个对象约 2 条引用，引用是随机的）：

| 存活对象 | 标记耗时 | 每对象 |
|---|---|---|
| 1 万 | 0.17 ms | 17 ns |
| 5 万 | 1.37 ms | 27 ns |
| 20 万 | 11.5 ms | 57 ns |
| 50 万 | **80.1 ms** | **160 ns** |

→ 比线性还差：对象一多，工作集超出 cache，**每个对象的成本也在变大**，因为随机引用基本上每次都 cache miss。对比 D3 无簇的情况，20 万个对象只要 3.6ms，原因是那里的子对象按顺序分配、引用局部性好。所以 **GC 的成本不光看对象数量，还看内存布局**。

### ② 性能：不分代，常驻对象每次都要重扫

大多数对象「要么朝生夕死，要么活到最后」（分代假说）。UE 不分代，常驻的资源对象每次 GC 都要重新标记一遍。簇和 DisregardForGC 本质上都是**手动补一个老年代**。

### ③ 内存：回收不及时，析构时机也不确定

- 对象从「没人引用」到「内存真正释放」，要等下一次 GC（默认约 61 秒），再加上增量清除的若干帧，还要等渲染线程。**内存峰值会被抬高**，移动端更容易触发系统的低内存查杀，所以引擎有 `gc.LowMemory.*` 这类低内存时提前触发 GC 的配置。
- 不能依赖析构函数做 RAII 式的「立刻释放」：析构什么时候发生、按什么顺序发生，都不确定。
- **对象不移动** → 不做压缩，长期运行会产生碎片（主要靠分配器按大小分桶缓解）。

### ④ 正确性：只认反射引用，裸指针 GC 看不见

精确式 GC 的代价：GC 正确的前提是**程序员把每一条引用都如实上报**。漏写一个 UPROPERTY，问题不会在写错的地方出现，而是在几十秒后某次 GC 之后、在另一个模块里崩溃，或者像 D2 那样不崩但逻辑错误。**问题出现的地方离出错的地方很远**，是 UE 里最难查的一类 bug。

### ⑤ 开销：每个对象都要交「税」，GC 锁会和其他线程互相等待

- 每个 UObject 都带着：`UObjectBase` 头部（ClassPrivate、OuterPrivate、NamePrivate、Flags、InternalIndex…）、`FUObjectItem` 条目、名字哈希表项。拿 UObject 表示子弹、格子这类海量小对象，又费内存又拖慢 GC。
- GC 期间要拿全局锁。异步加载线程持有 `FGCScopeGuard` 时，GC 必须等它；反过来 GC 进行时，加载也会被挡住。表现是「GC 卡顿里还夹着一段等锁时间」。

### ⑥ 簇的副作用：用内存换时间

D3 已经展示过：只引用了一个叶子子对象，整簇 101 个对象都放不掉。簇还要求成员基本不变，运行时改了引用关系可能导致簇失效。

### ⑦ GC 不防逻辑泄漏

GC 只回收「不可达」的对象，不回收「不再需要」的对象。忘了 `RemoveFromRoot`、一个全局单例的 UPROPERTY 数组只加不删、静态的 `TStrongObjectPtr`……这些都会让对象永远存活。**有 GC ≠ 不会内存泄漏**，排查要用 `obj refs` 找出到底是谁拖住了对象。

### 一张表收尾：缺点 → 引擎的缓解 → 项目的做法

| 缺点 | 引擎提供的缓解 | 项目里的做法 |
|---|---|---|
| STW 停顿 | 并行标记、簇、DisregardForGC、5.4+ 增量可达性 | 减少 UObject 数量；在加载界面或切关时手动 GC |
| 不分代 | 簇、永久对象池 | 常驻资源合并成簇根；静态 Actor 开启 Actor 簇 |
| 回收滞后 | 增量清除、低内存时提前触发 | 调整 GC 间隔；大批量卸载后主动 `ForceGarbageCollection` |
| 裸指针野指针 | `TObjectPtr`、`TWeakObjectPtr`、自动置空 | 代码规范 + 静态检查；测试时开 `gc.CollectGarbageEveryFrame 1` 把问题逼出来 |
| 每对象开销 | — | 纯数据用 USTRUCT / 普通结构体；子弹、特效走对象池 |
| GC 锁竞争 | `FGCScopeGuard` 粒度优化 | 避免大规模加载和 GC 撞在一起 |
| 逻辑泄漏 | `obj refs`、`memreport -full` | 每次切关检查对象数是否回落 |

---

## 7. 游戏客户端实战：GC 卡顿怎么查、怎么优化

**排查四步**：
1. **确认是 GC**：Unreal Insights 时间线里找 `CollectGarbage` 相关的 scope，看尖峰周期是否约 60 秒；也可以打开 `LogGarbage` 日志，它会打印每次 GC 的标记时间和回收数量。
2. **拆开看**：标记慢 → 存活对象太多，或者引用图太大；清除慢 → 一次性回收太多，或者 BeginDestroy 做了重活；等锁 → 和异步加载冲突了。
3. **找到大户**：`obj list` 按类统计对象数，看哪个类有几万个实例；`obj refs name=XXX` 看某个对象为什么没被回收。
4. **对照修改前后**：每次切关后对象总数应该回落到基线，不回落就是泄漏。

**优化清单（按收益排序）**：
- **对象池**：子弹、伤害数字、特效 Actor 都复用，不反复 Spawn/Destroy。这同时减少了 GC 压力和 Spawn 本身的开销。
- **减少 UObject 数量**：纯数据（技能参数、Buff 数据）用 USTRUCT 或普通 C++ 对象，不要每条数据 `NewObject` 一个。
- **控制 GC 时机**：在加载界面、结算界面这些玩家感知不到卡顿的地方主动 GC；战斗中适当拉长间隔（但要盯住内存峰值）。
- **用好簇**：静态场景开启 Actor 簇；大资产作为簇根。
- **UE5 项目**：成员统一写 `TObjectPtr`，为以后开启增量可达性分析做准备。
- **MOBA 类项目的特点**：一局之内对象种类相对固定，适合「局前预热对象池 + 局内尽量不触发 GC + 局后统一回收」。

---

## 8. 和其他 GC 方案对比（被问到「为什么 UE 这么设计」时用）

| 方案 | 代表 | 循环引用 | 停顿 | 释放时机 | 对比 UE |
|---|---|---|---|---|---|
| 引用计数 | `std::shared_ptr`、UE 的 `TSharedPtr`、Python | ❌ 要靠 weak 打破 | 无集中停顿，但每次赋值都要改计数 | 立即、确定 | UE 用它管理非 UObject 对象 |
| 保守式标记-清除 | Unity（Boehm，可开增量模式） | ✅ | 有 | 滞后 | UE 是精确式，不会把整数误认成指针 |
| 分代 + 压缩 | .NET CoreCLR、JVM | ✅ | 年轻代 GC 很快 | 滞后 | 托管语言能拦截所有指针写入；C++ 不行 |
| 增量三色标记 | Lua 5.x | ✅ | 分摊到多步 | 滞后 | 写屏障在虚拟机里实现，成本低；UE 直到 TObjectPtr 才有了插屏障的位置 |

---

## 9. 面试注意点（易错说法 → 正确说法）

| ❌ 常见错误说法 | ✅ 正确说法 |
|---|---|
| UE 用引用计数管理 UObject | UObject 用追踪式标记-清除；引用计数的是 TSharedPtr 管的非 UObject |
| UE 的 GC 会压缩内存 / 移动对象 | 不移动；正因为不移动，裸指针在对象存活期间才一直有效 |
| UE 是分代 GC | 不分代；簇和 DisregardForGC 是手动的「老年代」 |
| Destroy 会马上释放 Actor | Destroy 只是 MarkAsGarbage，内存要等下一次 GC 和增量清除 |
| 加 UPROPERTY 是为了蓝图能访问 | 更重要的是让 **GC 能看见**这条引用（还有序列化） |
| `IsValid()` 能检测野指针 | 只能识别 Garbage 对象；对已释放的地址调用是未定义行为 |
| TWeakObjectPtr 和 std::weak_ptr 一样是计数式 | 存的是 {对象索引, 序列号}，查全局数组判断有效 |
| 循环引用会导致 UObject 泄漏 | 追踪式 GC 能处理环；泄漏一般来自根集或长生命周期对象上忘掉的引用 |
| UE 的 GC 在后台线程并发执行 | 标记时游戏线程是阻塞的（工作线程帮忙并行）；只有清除是跨帧增量的（5.4+ 才有实验性的增量标记） |

**答题注意**：
- 先定性，再讲细节。面试官最想听的是你能把「为什么不分代、为什么不移动」和 C++ 裸指针联系起来。
- 讲缺点时一定要带上**应对办法**，否则只是背书；能举出项目里遇到过的 GC 尖峰或野指针例子更好。
- 版本差异说清楚（PendingKill → Garbage、Token Stream → Schema、5.4 增量可达性），记不准具体版本号时说「UE5 后期」即可，不要编版本号。

---

## 10. 高频追问

**Q1：TWeakObjectPtr 是怎么判断失效的？**
它存的是 `ObjectIndex + ObjectSerialNumber`。`Get()` 时去 `GUObjectArray[Index]` 查：槽位为空、序列号对不上（槽位已经被新对象复用），或者对象被标成了 Garbage，都返回 nullptr。它不增加任何计数，也不阻止回收。D2-B3 实测，槽位被 Enemy_C 复用后，弱指针正确返回了 nullptr。

**Q2：为什么非 UObject 持有 UObject 要用 FGCObject？**
GC 的遍历从根集出发，只会沿着反射引用和 ARO 走。普通 C++ 对象不在这张图里。FGCObject 把自己挂到一个根对象上，GC 扫到这个根对象时，会回调所有 FGCObject 的 `AddReferencedObjects`，这样普通对象持有的 UObject 就接进了引用图。

**Q3：GC 会把哪些指针置空？**
只有 GC 能看到的强引用：UPROPERTY、ARO 里以引用方式上报的。裸指针不会被置空，弱指针也不会被置空，但会通过序列号或标志位判定失效。

**Q4：怎么定位「对象该回收却没回收」？**
`obj refs name=对象名` 打印从根集到它的引用链；`memreport -full` 看各类对象的数量和内存。常见原因有：静态容器、AddToRoot 忘了移除、全局委托没解绑、被簇连带拖住。

**Q5：怎么定位「对象被提前回收导致崩溃」？**
测试时开 `gc.CollectGarbageEveryFrame 1`，让每帧都 GC，把潜伏的裸指针问题尽早暴露；崩溃时看指针指向的对象是不是 `<freed>` 或者已经变成别的类，然后回头去找哪个成员漏写了 UPROPERTY，或者哪个 lambda 捕获了裸指针。

**Q6：为什么 UE 不做分代 / 移动式 GC？**
- **移动**要求更新所有指向这个对象的指针，包括 GC 看不到的裸指针，C++ 做不到。
- **分代**要求在每次「老对象指向新对象」的写入时记录下来（写屏障 + 记忆集），否则年轻代 GC 会漏掉从老年代来的引用；C++ 的裸指针赋值没地方插这段代码。UE5 的 `TObjectPtr` 第一次提供了这个插入点，所以先有了增量可达性分析这一步。

**Q7：UE5.4 的增量可达性分析有什么前提？**
引用必须通过 `TObjectPtr` 修改，这样写屏障才会生效。仍然在用 `UPROPERTY() UObject*` 裸指针的旧代码，开了增量标记就有 D5 那样漏标的风险。所以它目前是实验性的、需要手动开启的。

---

## 11. 速记卡

```
定性：只管 UObject｜追踪式｜精确式（靠反射）｜标记-清除｜不移动｜不分代
数据：GUObjectArray → FUObjectItem{Object, Flags, ClusterRootIndex, SerialNumber}
引用来源：UPROPERTY → TokenStream(UE4)/Schema(UE5)｜AddReferencedObjects｜FGCObject
根集：AddToRoot｜FGCObject｜KeepFlags｜DisregardForGC
流程：拿锁 → 全部标 Unreachable → 从根并行标记（Garbage 引用置空） → 收集
     → BeginDestroy → IsReadyForFinishDestroy → FinishDestroy → 析构（增量，多帧）
加速：簇（实测标记 3.6ms→0.016ms）｜DisregardForGC｜并行标记｜增量清除（28ms→每帧 2ms）
     ｜5.4 增量可达性 + TObjectPtr 写屏障
缺点：STW 停顿和存活数成正比（实测 50 万对象 80ms）｜不分代｜回收滞后、析构时机不定
     ｜裸指针看不见 → 野指针 / 槽位复用打中别人｜每对象开销 + GC 锁｜簇整体拖住｜不防逻辑泄漏
应对：对象池｜少用 UObject 存数据｜加载界面手动 GC｜簇｜TObjectPtr/TWeakObjectPtr｜Insights + obj refs
```

---

## 附：demo 实测输出（`run_output.txt` 原文）

```
ue_gc_sim_demo  _MSC_VER=1944  sizeof(ObjItem)=24

[D1] 标记阶段耗时 vs 存活对象数（单线程，全部可达，每对象约 2 条引用）
     N        mark(ms)   ns/对象   扫描对象   访问引用
    10000      0.172      17.2      10000      19999
    50000      1.368      27.4      50000      99999
   200000     11.495      57.5     200000     399999
   500000     80.109     160.2     500000     999999

[D2] 裸指针 / 弱指针 / MarkAsGarbage 的行为差异
  A1 GC 后释放 1 个对象；HUD->raw 仍非空，指向的内容 name="<freed>"（已被回收）
  A2 新建 Ally_B 复用了同一槽位：HUD->raw==Ally_B ? 是，HUD->raw->name="Ally_B"  <- 野指针“打中别人”
  B1 Destroy 后、GC 前：Target 指针=非空，IsValid(Target)=false，Weak.Get()=nullptr
  B2 GC 后：Target 指针=nullptr（被 GC 自动置空 1 处），释放 1 个，Weak.Get()=nullptr
  B3 Enemy_C 复用槽位后：Weak.Get()=nullptr（序列号 2 != 1，弱指针不会认错人）

[D3] GC 簇：2000 个资产 x (1 根 + 100 子对象)，共 202009 个对象
  无簇 mark=  3.615 ms  扫描对象= 202009  访问引用= 210008
         卸载后只引用资产0的1个叶子：释放 202007，存活 2（原 202009）
  有簇 mark=  0.016 ms  扫描对象=   2009  访问引用=  10008
         卸载后只引用资产0的1个叶子：释放 201903，存活 106（原 202009）

[D4] 关卡卸载：200000 个对象（各带 512B 资源）同时变成垃圾
  一次性 purge：单帧耗时 28.58 ms
  增量 purge（预算 2.0ms/帧）：分 14 帧完成，单帧最长 2.04 ms，总计 28.15 ms

[D5] 增量标记 + 写屏障（三色不变式）
  写屏障关：C 被回收=是，A->props[0]->name="<freed>"  <- A 持有已回收对象（悬空）
  写屏障开：C 被回收=否，A->props[0]->name="C"
```

> 数字是本机单线程玩具实现的结果，用来看**趋势和现象**，不能当作 UE 的真实耗时。真实引擎是多线程标记，对象也更大，具体以项目里 Insights 的数据为准。
