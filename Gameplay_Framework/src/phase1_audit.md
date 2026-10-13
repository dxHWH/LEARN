## 阅读约定：事实等级

| 标记 | 含义 |
|---|---|
| 【源码事实】 | 当前 commit 里代码真实的行为（逐行读过） |
| 【设计意图】 | 从命名、注释、提交信息推测的目标，**不等于已实现** |
| 【问题】 | 当前实现的缺陷 / 与注释不一致 / 边界漏洞 |
| 【改进方案】 | 建议的修改方向 |
| 【扩展知识】 | 项目没实现，但面试官会从这里追下去的知识 |
| 【实测 Ex】 | 把对应 C# 逻辑逐行移植为 C++ 后运行得到的结果，见 `demos/run_output.txt` 的第 x 节 |

> **关于实测的说明**：本机没有 Unity Editor，C# 编译器在当前环境被禁用，所以我把 `TimerSystem / RingBuffer+EventSystem / ObjectPool / World 遍历` 的控制流**逐行移植**成 C++（MSVC 14.44 编译），用来验证"句柄/队列/遍历"这一类**纯逻辑**结论。凡是依赖 C# 运行时的结论（闭包分配、`string.GetHashCode`、Unity 生命周期顺序）只标【源码事实】或【扩展知识】，**不冒充实测**；这些需要你后续在 Unity Profiler（GC Alloc 列）里验证。

---

## §0 30 秒结论（先看这个）

1. **这是一个"类 UE Gameplay Framework 的 Unity 学习原型"**：`AActor` 是 MonoBehaviour 组件（不是 UE 那种组件容器），`World` 是**每个场景懒加载**的单例、统一驱动 Actor 的 `BeginPlay/Tick`；`Controller ⇄ Pawn` 双向绑定；`GameMode` 动态生成 `GameState` 并向 World 担保注册；基础设施是 Slot-Map `TimerSystem`、双缓冲 `EventSystem`、`ObjectPool`、`ThreadSafeRingBuffer`、`Log`。
2. **技术含量最高、也最值得讲的是 TimerSystem**（dense array + sparse 映射 + generation + FIFO free list + swap-and-pop + 两阶段回调）。主干逻辑是对的，回调重入处理得也不错【实测 E4】。
3. **但它不是 Zero-GC、也不是"完全安全的句柄"**：`TimerData` 含两个 `Action` 引用（注释写 Blittable 是错的）；generation 从 0 开始，导致 `default(TimerHandle)==(0,0)` 会**别名到第一个计时器**——SoulDecay 里 AI 肉体的 `_decayTimer` 恰好就是这个默认值【实测 E1】。
4. **"统一 Tick" 只统一了 Actor**：`TimerSystem / EventSystem / UI` 各自有独立 `Update`，且没有设置执行顺序；**三个核心单例都 override 了 `Awake` 却没调 `base.Awake()`**，单例防重逻辑实际只在 `GameInstance` 上生效。
5. **ObjectPool 写好了但没启用**（初始化被注释），`ThreadSafeEventSystem` 没有任何调用方；"UnitTest" 目录里全是挂在 MonoBehaviour 上的**运行时测试脚本**，不是 NUnit / Unity Test Framework，而且**没有被任何场景引用**。

> **面试注意**：面试官最爱的不是"你做了什么"，而是"你知道它哪里不对"。把第 3、4 点主动讲出来，比被问出来加分得多。

---

## §1 Project Fact Sheet

### 1.1 基本信息【源码事实】

| 项 | 事实 |
|---|---|
| Unity 版本 | `2022.3.54f1c1`（中国版 LTS，`ProjectSettings/ProjectVersion.txt`） |
| C# 版本 | Unity 2022.3 默认 C# 9【扩展知识：影响"方法组转委托是否缓存"，见 §5 GC】 |
| 关键包 | Input System 1.11.2、TextMeshPro 3.0.7、Visual Scripting 1.9.4（只被 `Singleton.cs` 一句无用的 `using` 引用）、Test Framework 1.1.33（随 `feature.development` 间接装入，**未被任何代码使用**） |
| 宏 | `scriptingDefineSymbols: Standalone: ENABLE_CONSOLELOG`；`ENABLE_LOGSAVE` **未定义** |
| Enter Play Mode Options | 关闭（每次进 Play 都 Domain Reload，静态字段会被重置——这一点掩盖了 §5 的静态标志问题） |
| Build 场景 | `MainMenuScene` → `GamePlayTest2Scene` / `SoulDecayScene` |
| 自写代码量 | 约 4300 行 C#（不含自动生成的 `InputSystem_Actions.cs` 1838 行和 TMP 示例），其中 TimerSystem 507 行、其测试 783 行 |
| asmdef / CI | 无 asmdef、无 CI、无 README |

### 1.2 目录结构

```text
Assets/
├── InputSystem_Actions.cs/.inputactions   # 自动生成，类名 @GameInput
├── Scenes/  MainMenuScene / GamePlayTest2Scene / SoulDecayScene
└── Scripts/
    ├── GameplayArchitecture/              # ← 框架本体
    │   ├── GamePlayCore/   AActor APawn AController AUnityController
    │   │                   AGameModeBase AGameMode AGameStateBase AGameState
    │   │                   World GameInstance MatchStateChangedEventArgs
    │   ├── Framework/      TimerSystem EventSystem(+ThreadSafeEventSystem)
    │   │                   Singleton(Singleton<T>, MonoSingleton<T>) ObjectPool Log
    │   ├── DataStructure/  RingBuffer  ThreadSafe/ThreadSafeRingBuffer
    │   └── UnitTest/       TimerSystemTest RingBufferTests RingBufferStressTest
    │                       EventSystemTest TestPossess LogTest   (均为 MonoBehaviour)
    ├── GamePlayTest1/      DemoStarter MyHeroPawn                (无场景引用，早期 Demo)
    ├── GamePlayTest2/      DemoGameMode DemoHeroPawn DemoMatchUI PlayerRequestControlEventArgs + Prefab
    ├── SoulDecayDemo/      SurvivalGameMode SurvivalGameState CombatPawn SoulPlayerController
    │                       SimpleAIController SurvivalUIController SoulDecayEvents GameObjectWrapper
    └── MainMenuController InGameMenuController MovingSphere SphereSpawner
```

### 1.3 场景里到底挂了什么（用 `.meta` GUID 反查场景文件得到）

| 场景 / Prefab | 挂载的自写脚本 | 运行时才动态创建的 |
|---|---|---|
| MainMenuScene | `GameInstance`、`MainMenuController` | — |
| GamePlayTest2Scene | `DemoGameMode`、`DemoMatchUI`、`InGameMenuController` | World、TimerSystem、EventSystem（懒加载），DemoPawn / DemoController（Instantiate Prefab），GameState |
| SoulDecayScene | `EventSystem`、`TimerSystem`（**场景预置**）、`SurvivalGameMode`、`SurvivalUIController`、`InGameMenuController` | World（懒加载）、SurvivalGameState、所有 CombatPawn / Controller |
| DemoController.prefab / DemoPawn.prefab | `AUnityController` / `DemoHeroPawn` | — |

【源码事实】`UnitTest/` 下 6 个测试脚本、`GamePlayTest1/`、`MovingSphere`、`SphereSpawner` **没有被任何场景或 Prefab 引用**——要运行它们必须手动挂到某个物体上。

### 1.4 模块成熟度总表

成熟度：★ 原型 / ★★ 能用但有已知缺陷 / ★★★ 主干正确、边界待补

| 模块 | 关键源码 | 实际职责（源码事实） | Demo 使用 | 测试 | 成熟度 |
|---|---|---|---|---|---|
| `MonoSingleton<T>` | Framework/Singleton.cs | 懒查找/懒创建 + Awake 防重 + 退出标志 | 全部基础设施的基类 | 无 | ★★（子类没调 `base.Awake`） |
| `Singleton<T>` | 同上 | 纯 C# `??=` 懒单例 | **未使用** | 无 | ★ |
| `GameInstance` | GamePlayCore/GameInstance.cs | DontDestroyOnLoad + 初始化 Log；`CurrentWorld => World.Instance` | MainMenu 场景挂载，**没有任何调用方** | 无 | ★（占位） |
| `World` | GamePlayCore/World.cs | Actor 注册表（pending + actors）、统一 BeginPlay/Tick、GameMode/GameState 注册 | 两个 Demo | 无 | ★★ |
| `AActor` | GamePlayCore/AActor.cs | `Start` 注册、`OnDestroy` 注销、虚 `BeginPlay/Tick`、Tick 开关 | 全部 | 无 | ★★ |
| `APawn` / `AController` | APawn.cs / AController.cs | 双向绑定、Possess/UnPossess、回调钩子 | 两个 Demo | TestPossess（手动、未挂载） | ★★ |
| `AUnityController` | AUnityController.cs | 创建 `GameInput`、轮询 Move、默认 `transform.Translate` | 两个 Demo | 同上 | ★★ |
| `AGameModeBase` / `AGameMode` | AGameModeBase.cs / AGameMode.cs | 注册裁判 → 反射生成 GameState → StartPlay → 比赛阶段切换 | 两个 Demo | 无 | ★★ |
| `AGameStateBase` / `AGameState` | AGameStateBase.cs / AGameState.cs | 累加 `ServerWorldTimeSeconds`；MatchState 状态 + 广播事件 | 两个 Demo | 无 | ★★ |
| `TimerSystem` | Framework/TimerSystem.cs | Slot Map 计时器：命名、暂停、循环、间隔+追赶、scaled/unscaled | 两个 Demo + SphereSpawner | TimerSystemTest（22 例，运行时协程） | ★★★（有 P0 边界） |
| `EventSystem` | Framework/EventSystem.cs | 按类型多播、按优先级的双缓冲 RingBuffer 延迟派发 | 两个 Demo | EventSystemTest（无断言演示） | ★★ |
| `ThreadSafeEventSystem` | 同上 117–222 行 | 加锁版事件系统，主线程派发 | **未使用** | 无 | ★ |
| `RingBuffer<T>` | DataStructure/RingBuffer.cs | 定长环形队列，满了返回 false | 被 EventSystem 使用 | **无直接测试**（测试测的是 ThreadSafe 版） | ★★★ |
| `ThreadSafeRingBuffer<T>` | DataStructure/ThreadSafe/… | `lock` 包住每个操作的环形队列 | 仅被未使用的 ThreadSafeEventSystem 引用 | RingBufferTests（5 例单线程）+ StressTest（SPSC 计数） | ★★ |
| `ObjectPool<T>` | Framework/ObjectPool.cs | 数组 + 可用下标栈 + 翻倍扩容；`PoolObject{elem,index}` | **字段声明了但初始化被注释，接入代码是死代码** | 无 | ★ |
| `Log` | Framework/Log.cs | `[Conditional]` 剥离的分级日志 + 可选缓冲落盘 | 全局 | LogTest（OnGUI 手动） | ★★ |

### 1.5 实际使用到的设计模式【源码事实】

| 模式 | 出处 | 备注 |
|---|---|---|
| Singleton（泛型基类，"CRTP 式"） | `MonoSingleton<T>` | C# 泛型每个封闭类型各有一份 static 字段，这才是"每个子类一份单例"的根本原因 |
| Template Method | `AActor.BeginPlay/Tick`、`AGameModeBase.BeginPlay → InitGameState → StartPlay` | 子类重写钩子 |
| Facade | `World.RegisterAActor` 等静态 API | 注释自称 |
| Observer / Pub-Sub + Deferred Dispatch | `EventSystem` | 按 `Type` 分发 |
| Double Buffer | `EventSystem` front/back、`World` pending 列表 | World 那个严格说是"延迟加入队列"，不是双缓冲 |
| Slot Map / Generational Handle | `TimerSystem` | 全项目技术核心 |
| Object Pool | `ObjectPool<T>` | 未启用 |
| Strategy-ish（Type 作为工厂参数） | `AGameMode.GameStateClass` + `AddComponent(Type)` | 模仿 UE `GameStateClass` |
| Interface-based 阵营 | `IFactionMember` | 用接口而不是继承表达"阵营"，Demo 里的亮点 |

### 1.6 尚未真正投入使用的模块

- `ObjectPool` + `GameObjectWrapper` + `SurvivalGameMode.GetOrCreatePawn/GetOrCreateAIController/RecycleObject`：池子字段从未初始化（`SurvivalGameMode.cs:30-32` 注释），三个方法无调用方；**如果现在直接调用会 NullReferenceException**。
- `ThreadSafeEventSystem`：零调用方。
- `Singleton<T>`（纯 C#）：零使用。
- `GameInstance.CurrentWorld`：零调用方。
- `AGameModeBase.DefaultPawnClass/DefaultControllerClass`：只有 DemoGameMode 用，SoulDecay 自己 `CreatePrimitive`。
- `GamePlayTest1`（DemoStarter / MyHeroPawn）：不在任何场景。

### 1.7 实现了但没有充分验证的模块

- `TimerSystem` 的**追赶/非追赶**分支：两个测试都用默认 `isChase=true`，"非追逐模式测试"实际没测非追逐（注释还写着"CreateTimer 未暴露 isChase，默认 false"——与源码相反）。
- `RingBuffer<T>`（EventSystem 真正在用的那个）：没有任何测试；`RingBufferTests.cs` 里的类叫 `RingBufferTest`，测的是 `ThreadSafeRingBuffer`。
- 多线程：只有一个 SPSC（单生产者单消费者）计数压测，入队值恒为 `1`，所以**FIFO 顺序在并发下没被验证**；MPMC 完全没测。
- Log 落盘：`ENABLE_LOGSAVE` 没开；`LogTest` 期望 `Log.E` 立即落盘，而实现里 `Log.E` 只进缓冲区——**测试与实现不一致**。
- EventSystem 溢出、异常、注销时序：零测试。

---

## §2 核心源码地图（按阅读优先级）

| 优先级 | 文件 | 行数 | 必须看懂的函数 / 行 | 为什么重要 |
|---|---|---|---|---|
| ★★★ | Framework/TimerSystem.cs | 507 | `TimerHandle`(14-35)、`CreateTimer`(146-224)、`StopTimer`(243-283)、`IsHandleValid`(349-355)、`Tick`(374-505) | 全书重点；Slot Map / generation / 重入 / 追赶 / GC |
| ★★★ | Framework/EventSystem.cs | 224 | `Register`(49-62)、`Trigger`(83-97)、`Update`(99-114) | 双缓冲、闭包分配、溢出、快照语义 |
| ★★★ | GamePlayCore/World.cs | 172 | `Update`(51-88)、`RegisterAActor`(92-102)、`RegisterGameState`(132-150) | 统一 Tick、pending、倒序删除 |
| ★★★ | GamePlayCore/AController.cs + APawn.cs | 68+35 | `Possess`(15-40)、`UnPossess`(45-63)、`PossessedBy/UnPossessed` | 双向引用一致性、非事务性 |
| ★★ | Framework/Singleton.cs | 98 | `Awake`(43-55)、`Instance`(57-80)、`OnApplicationQuit`(91-94) | 生命周期 / 幽灵单例 / 防重 |
| ★★ | AGameModeBase.cs / AGameMode.cs / AGameState.cs | 50/69/54 | `BeginPlay`、`InitGameState`、`SetMatchState` | 规则 vs 状态的边界 |
| ★★ | SoulDecayDemo/CombatPawn.cs | 181 | `OnPossess`(33-61)、`OnUnPossess`(65-100)、`OnCollisionEnter`(110-151)、`Die`(153-181) | Demo 的数据流和最真实的 bug |
| ★★ | SoulDecayDemo/SurvivalGameMode.cs / SoulPlayerController.cs | 220/68 | `BeginPlay`、`SpawnRandomAI`、`ExecuteSoulTransfer` | 夺舍 = Possess 的业务化 |
| ★ | DataStructure/RingBuffer.cs / ThreadSafeRingBuffer.cs | 71/85 | `TryEnqueue/TryDequeue` | 环形队列基础题 |
| ★ | Framework/ObjectPool.cs | 105 | `Acquire/Recycle/ReAllocate` | 没 generation 的句柄 |
| ★ | Framework/Log.cs | 139 | `[Conditional]` | 零成本日志剥离 |
| ★ | UnitTest/TimerSystemTest.cs | 783 | 22 个用例 | 讲"测试体系差距"时用 |

---

## §3 项目真实架构图（按源码重建，不照抄继承关系）

### 3.1 静态结构：谁持有谁

你提示里的 `GameInstance → World → GameMode/GameState` **与源码不符**。真实情况是：

```text
                    ┌────────────────────────────── 跨场景 (DontDestroyOnLoad) ─┐
                    │  GameInstance  (只在 MainMenuScene 里有；不创建、不持有 World) │
                    └──────────────────────────────────────────────────────────────┘
 ┌─────────────────────────────── 每个场景各一套（切场景即销毁） ──────────────────────────────┐
 │                                                                                         │
 │   World  (第一个 AActor.Start 调 World.Instance 时懒创建)                                  │
 │    ├── _pendingAActors : List<AActor>  ──下一帧──►  _actors : List<AActor>               │
 │    ├── AuthorityGameMode : AGameModeBase   ← GameMode 在自己的 BeginPlay 里主动"报到"      │
 │    └── GameState : AGameState { get; set; } ← GameMode 生成后"担保注册"（但 setter 是 public）│
 │                                                                                         │
 │   TimerSystem   (MonoSingleton, 自己的 Update)   EventSystem (MonoSingleton, 自己的 Update)  │
 │                                                                                         │
 │   UI (SurvivalUIController / DemoMatchUI：普通 MonoBehaviour, 自己的 Update)               │
 └─────────────────────────────────────────────────────────────────────────────────────────┘
```

继承树（源码事实）：

```text
MonoBehaviour
 ├── MonoSingleton<T> ── World / TimerSystem / EventSystem / ThreadSafeEventSystem / GameInstance
 └── AActor
      ├── APawn ─────────── CombatPawn / DemoHeroPawn / MyHeroPawn
      ├── AController
      │    ├── AUnityController ── SoulPlayerController (+IFactionMember)
      │    └── SimpleAIController (+IFactionMember)
      ├── AGameModeBase ── AGameMode ── SurvivalGameMode / DemoGameMode
      └── AGameStateBase ── AGameState ── SurvivalGameState
```

**关键语义差异**：UE 的 `AActor` 是"组件的容器"；这里的 `AActor` **本身就是一个组件**（挂在 GameObject 上），一个 GameObject 理论上可以挂多个 AActor，各自独立注册。

### 3.2 运行时驱动：一帧里到底有几个"心脏"

```text
Unity PlayerLoop ── Update 阶段（这些脚本之间没有 DefaultExecutionOrder，先后顺序未定义）
   ├── World.Update ........ ① pending → _actors + BeginPlay   ② 倒序遍历 Tick(Time.deltaTime)
   ├── TimerSystem.Update .. Tick(deltaTime, unscaledDeltaTime) → 先间隔回调，后完成回调
   ├── EventSystem.Update .. swap(front, back) → 按优先级清空 front
   ├── SurvivalUIController.Update  每帧向 TimerSystem 拉取剩余时间
   └── InGameMenuController.Update  ESC → Time.timeScale = 0
Unity 物理阶段 ── OnCollisionEnter（CombatPawn 碰撞击杀、加分）——不经过 World
```

【问题】"统一 Tick"只统一了 Actor；Timer、Event、UI 各自独立，且相对顺序未定义 → 同一帧 Trigger 的事件，可能本帧派发、也可能下一帧派发（取决于 EventSystem 排在 World 前还是后）。

### 3.3 生命周期时间线（以 SoulDecayScene 启动为例，源码推演）

```text
帧 0 · 场景加载
  Awake/OnEnable（逐对象进行，对象间顺序不保证）
    EventSystem.Awake        → 分配 6 个 RingBuffer(2000) + 字典
    TimerSystem.Awake        → AllocateCapacity(128)        (二者都没调 base.Awake → _instance 仍为 null)
    SurvivalUIController.OnEnable → EventSystem.Instance → FindObjectOfType 找到场景里的 EventSystem → Register
         ⚠ 若 UI 先于 EventSystem 初始化：找到的是还没 Awake 的对象，_listenersDic == null → NRE（顺序隐患）
  Start
    SurvivalGameMode.Start (AActor.Start) → World.RegisterAActor
         → World.Instance 为空 → new GameObject("World") + AddComponent → World.Awake → 进入 pending
  Update
    World.Update：pending → _actors，调用 SurvivalGameMode.BeginPlay：
      AGameModeBase.BeginPlay → World.RegisterGameMode(this)
                              → AGameMode.InitGameState：new GameObject + AddComponent(SurvivalGameState) → World.RegisterGameState
                              → AGameMode.StartPlay → GameState.SetMatchState(WaitingToStart) → Trigger(事件入 back 缓冲)
      SurvivalGameMode.BeginPlay 续：StartMatch → SetMatchState(InProgress) → Trigger
                              → CreatePawn（AddComponent<CombatPawn>，Awake 拿 Renderer）
                              → new Player_Controller.AddComponent<SoulPlayerController>()
                              → playerSoul.Possess(playerBody)       ⚠ 此刻 Pawn/Controller 都还没 BeginPlay
                                    → CombatPawn.OnPossess → CreateTimer(5s) = (0,0) → Trigger(寿命句柄事件)
                              → CreateTimer("AISpawner", 3s, loop) = (1,0)
帧 1
  Start：SurvivalGameState / CombatPawn / SoulPlayerController 的 AActor.Start → 进入 pending
  Update：World 把它们加入 _actors 并 BeginPlay（AUnityController 此时才 new GameInput、启用输入；
          SurvivalGameState.BeginPlay 广播 3 个 0 分事件）→ 之后才开始 Tick
  EventSystem.Update：派发 MatchState×2、寿命句柄、分数事件 → UI 记录句柄
……
Destroy(go)：帧末 OnDestroy → AActor.OnDestroy → World.UnregisterAActor（List.Remove，O(n)）
切场景：World / TimerSystem / EventSystem 全部随场景销毁 → 计时器、监听全部消失（隐式的"场景级生命周期"）
```

> **面试注意**：UE 的 `RestartPlayer` 是先 `SpawnActor`（BeginPlay 已执行）再 `Possess`；这里是**先 Possess，下一帧才 BeginPlay**。被问"BeginPlay 和 Possess 谁先"时要能说清楚自己项目的顺序和后果（例如在 BeginPlay 里初始化的东西，OnPossess 里还拿不到——所以 CombatPawn 把 `GetComponent<Renderer>` 挪到了 Awake，DemoHeroPawn 注释里也写了这次修复）。

### 3.4 Controller / Pawn 关系

```text
           Possess(p)                                     UnPossess()
 Controller ─────────► ① 若自己有 Pawn → UnPossess()       ① ControlledPawn.UnPossessed()
   │                   ② 若 p 有别的 Controller → 它.UnPossess()    → Pawn.OnUnPossess()（此时 Controller 还在）
   │                   ③ p.PossessedBy(this)                       → Pawn.Controller = null
   │                        → Pawn.Controller = this              ② this.OnUnPossess(pawn)
   │                        → Pawn.OnPossess(this)                ③ ControlledPawn = null
   │                   ④ ControlledPawn = p
   ▼                   ⑤ this.OnPossess(p)
 Pawn.Controller ◄──── 双向引用：两边各存一份，没有单一真相源
```

【问题】`APawn.PossessedBy/UnPossessed` 是 `public`，任何人都能单边改写，绕过 Controller 侧；③之后若 `OnPossess` 抛异常，`Pawn.Controller` 已设置而 `ControlledPawn` 未设置 → **半绑定状态**（非事务）。Controller 被销毁时不会自动 UnPossess。

### 3.5 一次完整 Gameplay 数据流（SoulDecay）

```text
输入(Input System 轮询)
  │ AUnityController.Tick: _moveAction.ReadValue<Vector2>()      SoulPlayerController.Tick: 鼠标左键
  ▼                                                             ▼
HandleMovement → CombatPawn.Move → transform.Translate       ExecuteSoulTransfer: Raycast → 阵营判断
  │ (Pawn 带 Rigidbody，却在 Update 里直接改 Transform)          │ this.Possess(target) → 旧身体 3s 后销毁 / AI 灵魂自毁
  ▼                                                             │ GameState.AddScore(Player, +1)   ← Controller 直接写 GameState
物理 OnCollisionEnter（大吃小） ─► otherPawn.Die() ─► GameState.AddScore(...)   ← Pawn 直接写 GameState
  │                                  └─(玩家死亡)─► World.AuthorityGameMode.EndMatch()  ← Pawn 直接调 GameMode
  ▼
SurvivalGameState.AddScore → EventSystem.Trigger(ScoreChanged, Appearance)
  ▼ (下一次 EventSystem.Update)
SurvivalUIController.OnScoreChanged → TMP 文本
寿命：CombatPawn.OnPossess → Trigger(PlayerLifespanTimer{handle}) → UI 保存句柄 → UI.Update 每帧 pull GetTimeRemaining
结束：TimerSystem 完成回调 → OnBodyExploded → Die(true) → EndMatch → SetMatchState(WaitingPostMatch)
      → Trigger(MatchStateChanged) ⚠ SoulDecay 里没有任何监听者 → Trigger 第一步查字典失败，事件直接丢弃
```

【问题】规则（加分、判负）写在 Pawn 和 Controller 里，GameMode 只被动接收 `EndMatch`；GameState 既存状态又含规则（`AddScore` 里的 switch）。这与"GameMode=规则、GameState=可观察状态"的设计意图有偏差——这是 Chapter 5 的核心讨论点。

---

## §4 最值得进入电子书的 10 个技术主题

| # | 主题 | 为什么值得 | 向下追的基础知识 |
|---|---|---|---|
| 1 | **TimerSystem 的 Slot Map**（dense + sparse + 反向映射 + swap-and-pop） | 项目最有技术含量的部分，也是最能体现数据结构功底的地方 | 间接寻址、O(1) 删除、稳定顺序 vs 紧凑、ECS 稀疏集 |
| 2 | **Generation 句柄与 stale handle**（含 default 句柄 bug、伪造句柄、溢出） | 面试官 100% 会追；而且项目里有一个真实可复现的边界 bug | ABA、悬垂引用、UE `FTimerHandle`、ECS Entity ID、GPU 资源句柄 |
| 3 | **Timer 回调重入 + 两阶段提交**（先收集、后执行、二次校验） | 写得对的地方，要能讲清楚"为什么对" | 迭代器失效、结构性修改、命令缓冲 |
| 4 | **掉帧语义：追赶 / 不追赶 / 封顶 / 清零** | 能体现对游戏循环的理解；当前 interval 与 loop 语义不一致 | 固定步长、spiral of death、累加器模式 |
| 5 | **GC 审计：低分配 ≠ Zero-GC** | 注释里的"Blittable / 零GC"说法站不住，必须自己先纠正 | 托管堆、闭包类、委托分配、Boehm GC、struct 含引用 |
| 6 | **EventSystem 双缓冲 + 延迟派发** | 能解释"为什么事件里发事件不会栈溢出" | Observer、消息队列、立即派发 vs 延迟派发、重入 |
| 7 | **事件系统的工程边界**：溢出静默丢弃、监听快照、异常乱序、类型分发 | 面试官攻击点密集，且每个都能举出触发场景 | 背压、Drop 策略、类型擦除、协变/逆变 |
| 8 | **World 统一 Tick 与 Actor 生命周期** | 讲"为什么要包一层 Unity 生命周期"的主战场 | 调度、TickGroup、fixed timestep、迭代中删除 |
| 9 | **Controller / Pawn 分离 + Possess 一致性** | Gameplay 岗位必问，Demo（夺舍）正好是最佳案例 | 双向引用不变式、事务、所有权 |
| 10 | **Singleton 生命周期与替代方案** | 有一个确定的 bug（`base.Awake` 缺失），也是架构讨论题 | 初始化顺序、Service Locator、DI、UE Subsystem |

（ObjectPool、ThreadSafeRingBuffer、测试体系各成一章，但篇幅较短：前两者"未启用 / 未使用"，重点讲"如果启用会怎样"。）

---

## §5 潜在错误 / 技术债务（What I Would Change Today · 初版）

> 格式：问题 → 源码位置 → 当前行为 → 触发场景 → 是否影响当前 Demo → 最小修改。完整版（含 production 设计）放到 Chapter 16。

### P0 · Correctness

| # | 问题 | 源码位置 | 当前行为与触发场景 | 影响当前 Demo？ | 最小修改 |
|---|---|---|---|---|---|
| P0-1 | **`default(TimerHandle)` == `(0,0)` 是合法句柄** | TimerSystem.cs:197（generation 初始为 0）、CombatPawn.cs:13（`private TimerHandle _decayTimer;`）、:73/:174 | AI 肉体从没建过计时器，它的 `_decayTimer` 是 `(0,0)`，`!= Invalid` 为真 → AI 死亡时 `StopTimer((0,0))` 会**停掉 slot0 上的第一个计时器**【实测 E1-A：玩家寿命计时器被 AI 的死亡停掉，UI 显示 DIE、玩家永不爆炸】；若先建 AISpawner，则任意 AI 死亡会**停止刷怪**【E1-B】 | **当前参数下侥幸不触发**：玩家第一具身体寿命 5s，第一只 AI 3s 才出现、第二只 6s，5s 内不可能 AI 互撞；且 slot0 一旦复用过一次，默认句柄就永久失效【E1-C】。改任何参数/创建顺序都可能触发 | generation 从 1 开始（让 0 永远代表无效）；用户侧字段一律初始化为 `TimerHandle.Invalid` |
| P0-2 | **非 Editor 构建会编译失败** | MovingSphere.cs:1 `using UnityEditor;`、SphereSpawner.cs:3 `using TMPro.EditorUtilities;` | 运行时脚本引用 Editor 程序集；Player 构建时这两个命名空间不存在 | 能在 Editor 里跑，**打包会失败** | 删掉无用 using，或把脚本移进 `Editor/` / 用 `#if UNITY_EDITOR` |
| P0-3 | **EventSystem 派发无异常隔离** | EventSystem.cs:109-112 | 一个监听者抛异常 → 整个 `Update` 中断，剩余事件留在 front，下一帧 swap 后被排到新事件**之后**再派发【实测 E7-d：E2/E3 晚 2 帧且排在 E4 之后】 | 当前监听者简单，未观察到；但 UI 已销毁仍被回调（见 P1-6）正是会抛异常的场景 | 每个 task 外包 `try/catch + Debug.LogException`（TimerSystem 已经这样做了，二者应一致） |
| P0-4 | **事件溢出静默丢弃** | EventSystem.cs:95 忽略 `TryEnqueue` 返回值 | 单帧单优先级超过 2000 个事件，多出的直接消失、无日志【实测 E7-b：2001 → 派发 2000】 | Demo 事件量极小，不触发 | 失败时至少 `Log.E` + 计数；按事件类别选择 Resize / DropOldest / Assert |

### P1 · Architecture / 生命周期

| # | 问题 | 源码位置 | 说明 | 影响 Demo？ | 最小修改 |
|---|---|---|---|---|---|
| P1-1 | **子类 override `Awake` 未调 `base.Awake()`** | TimerSystem.cs:71、EventSystem.cs:37、World.cs:22（GameInstance 是对的） | 场景预置实例不会在 Awake 登记 `_instance`；重复实例不会自毁；`HasInstance` 在"有对象但没人访问过 Instance"时返回 false；两个 TimerSystem 并存时各 Tick 各的（split-brain） | SoulDecay 预置了 TimerSystem/EventSystem，靠 `Instance` 里的 `FindObjectOfType` 兜底才没出事 | 子类第一行 `base.Awake(); if (Instance != this) return;`；或基类把 Awake 设为非虚、暴露 `OnSingletonAwake()` 钩子 |
| P1-2 | **Awake/OnEnable 初始化顺序隐患** | EventSystem 的容器只在 Awake 创建；SurvivalUIController.cs:19 在 OnEnable 里 Register | 若 UI 对象先初始化，`FindObjectOfType` 返回尚未 Awake 的 EventSystem → `_listenersDic` 为 null → NRE | 取决于场景对象顺序，当前可能恰好正常 | 容器改为字段初始化器，或给基础设施加 `[DefaultExecutionOrder(-1000)]` |
| P1-3 | **四个"心脏"执行顺序未定义** | World / TimerSystem / EventSystem / UI 各自 `Update` | 事件派发延迟 0～1 帧不确定；计时器在创建当帧是否就被计时也不确定 | 表现为偶发 1 帧差异 | 由 World（或一个 GameLoop）按固定顺序显式调用 `TimerSystem.Tick / EventSystem.Pump` |
| P1-4 | **Possess 非事务、双向引用可被单边改写** | AController.cs:15-40、APawn.cs:13-26（public） | 回调抛异常会留下半绑定；外部可直接 `pawn.PossessedBy(x)`；Controller 销毁不 UnPossess；Pawn 销毁不通知 Controller（靠 Unity 的 fake-null 侥幸） | Demo 里 CombatPawn.Die 手动 UnPossess，SimpleAIController 失去 Pawn 自毁，所以没暴露 | `PossessedBy/UnPossessed` 改 `internal`；先改两侧引用、最后统一回调；`OnDestroy` 里自动解绑 |
| P1-5 | **规则散落在 Pawn/Controller，GameState 公开可写** | CombatPawn.cs:145-148/:164、SoulPlayerController（Possess 后加分）、World.cs:21 `GameState { get; set; }` | "Authority" 只体现在注册时校验，写入路径完全不经过 GameMode | 功能正常，但这是面试必被追的设计问题 | Pawn 只上报事实（"我被谁撞死"），GameMode 裁决并写 GameState；`World.GameState` setter 改 private |
| P1-6 | **事件监听快照语义** | EventSystem.cs:92 在 Trigger 时捕获委托 | Trigger 之后、派发之前注销的监听者仍会被调用【实测 E7-c】；若它是已销毁 UI → MissingReferenceException（叠加 P0-3） | 切场景/关 UI 的那一帧可能触发 | 闭包只捕获事件，派发时再查字典；或记录注销版本号 |
| P1-7 | **注册用 `typeof(T)`、派发用 `e.GetType()` + `is Action<T>`** | EventSystem.cs:52/87/90 | 用基类变量 Trigger（`Trigger<AbstractEventArgs>(evt)`）时，查到的是 `Action<Derived>`，`is Action<AbstractEventArgs>` 为 false → **静默丢弃**；也不支持"监听基类收派生事件" | Demo 都用具体类型，未触发 | 统一用 `typeof(T)`；或存 `Action<IEventArgs>` 包装 |
| P1-8 | **`AuthorityGameMode` 只注册不注销** | World.cs:105-115 | GameMode 被销毁后 World 仍持有它；之后注册的新 GameMode 会被当成"多个裁判"拒绝 | World 随场景销毁，当前不触发 | GameMode.OnDestroy 里注销 |
| P1-9 | **AActor 的启用状态与 World Tick 脱钩** | World.cs:83 只看 `HasBegunPlay && IsTickEnabled` | `enabled=false` 或 `SetActive(false)` 的 Actor 仍被 World Tick（原生 Update 会停）；未激活物体不会 Start → 永远不注册 | GameObjectWrapper 用 `SetActorTickEnabled` 手动补救 | Tick 前检查 `isActiveAndEnabled`，或在 OnEnable/OnDisable 同步 Tick 开关 |

### P2 · Performance / GC

| # | 问题 | 源码位置 | 说明 |
|---|---|---|---|
| P2-1 | **"Blittable / 避免 GC 扫描"注释不成立** | TimerSystem.cs:38/42/53 | `TimerData` 含 `onTrigger`、`onComplete` 两个引用（注释说"唯一的引用类型字段"也错了），整个数组都要被 GC 扫描；"List&lt;Struct&gt;"注释也过时了（实际是数组） |
| P2-2 | **每次 Trigger 至少 3 次堆分配** | EventSystem.cs:92 + 各处 `new XxxEventArgs` | 事件对象本身（class）+ 闭包对象（捕获 `callback`、`e`）+ `Action` 委托。【源码推断，需 Profiler 验证】 |
| P2-3 | **调用方的委托分配** | CombatPawn.cs:44 `CreateTimer(MaxLifespan, OnBodyExploded)` | 方法组转委托在 C# 9 中**每次都 new**；DemoGameMode/SurvivalGameMode 缓存了 `Action` 字段，是正确做法 |
| P2-4 | **World 每帧有新 Actor 时 `new List<AActor>(_pendingAActors)`** | World.cs:57 | 有新 Actor 的帧必分配；`Contains` 让注册是 O(n)、批量加入是 O(n·m) |
| P2-5 | **扩容分配** | `Array.Resize`、`List/Queue/Dictionary` 增长 | 首次超过 128 个计时器或单帧间隔回调 >64 次时分配；steady-state Tick 本身不分配 |
| P2-6 | **TimerSystem Tick 线性扫描含暂停计时器** | TimerSystem.cs:378-453 | 每帧 O(N)，与"有多少即将到期"无关；UE 的 TimerManager 用按到期时间排序的堆【扩展知识】 |
| P2-7 | **按名查找每次 `string.GetHashCode()`** | DemoMatchUI 每帧 `GetTimeRemaining("MatchWarmupTimer")` | O(字符串长度) 的哈希，不分配但不必要；应缓存句柄 |
| P2-8 | **EventSystem 多一级空缓冲** | EventSystem.cs:26 `PRIORITY_LEVEL_NUM = Max + 1 = 3` | `Max` 本是哨兵，却也分配了 2×2000 槽 |
| P2-9 | **移动绕过物理** | CombatPawn.cs:29、AUnityController.cs:55 | 有 Rigidbody 的物体在 Update 里 `transform.Translate`，绕过插值、可能穿透；应在 FixedUpdate 用 `MovePosition` 或设速度 |

### P3 · Code Quality / 健壮性

| # | 问题 | 源码位置 |
|---|---|---|
| P3-1 | **`string.GetHashCode()` 当唯一 ID**：不同名字可能碰撞（后建的会把先建的"同名覆盖"掉、`StopTimer("X")` 可能停掉 Y）；且不同运行时/进程的哈希值不保证稳定，不能持久化；哈希恰为 0 时会被当成匿名计时器；注释说"用 Unity 内置高效 Hash"与代码不符 | TimerSystem.cs:158-159 |
| P3-2 | **`IsHandleValid` 不检查槽位是否在用 + 停止后不清 `_handleToIndex`**：用"空闲槽的当前 generation"构造的句柄能通过校验，`StopTimer` 会删掉别人的 dense 槽，制造**僵尸句柄**和 free list 重复下标【实测 E2】。正常用户拿不到这种句柄，但 generation 回绕后会自然出现 | TimerSystem.cs:281-282、349-355 |
| P3-3 | **generation 溢出**：`int++` 默认 unchecked，回绕需要同一槽 2³² 次复用（每秒 60 次复用约 829 天）【实测 E6】——现实中可忽略，但要能答 | TimerSystem.cs:281 |
| P3-4 | **参数无校验**：`duration<=0` 的循环计时器每帧触发【实测 E3】；`interval<=0` 静默忽略；`onComplete` 可为 null | CreateTimer |
| P3-5 | **interval 与 loop 的掉帧语义不一致**：interval 追赶封顶 5 次后**清零丢相位**（0.6s 卡顿只触发 5 次）；非追赶模式也清零；loop 却是每帧补 1 次、不丢账【实测 E3】 | TimerSystem.cs:401-449 |
| P3-6 | **同帧回调顺序 ≠ 创建顺序**（swap-and-pop 打乱 dense 顺序）【实测 E4】 | StopTimer |
| P3-7 | **World 倒序遍历只防"删自己/删已遍历的"**：Tick 中同步注销更低下标的 Actor，会让当前 Actor 被 Tick 两次【实测 E9】；`World.Start` 遍历的 `_actors` 永远为空（Actor 都先进 pending），是死代码；`RegisterAActor` 注释"只有 World 存在时才注册"不对——`Instance` 会懒创建 | World.cs:40-49、74-87、95 |
| P3-8 | **单例退出标志是 per-T 的 static、只置不复位**：某类型在退出时没有存活实例就不会置位；开启"关闭 Domain Reload"后第二次 Play 所有 `Instance` 返回 null | Singleton.cs:37、91-94 |
| P3-9 | **ObjectPool**：`PoolObject` 无 generation，旧拷贝能回收别人正在用的对象【实测 E8】；`Recycle` 不校验 `elem` 是否匹配；`default(PoolObject)` 的 index=0；`ReAllocate` 注释"深拷贝"实为引用浅拷贝；capacity=0 时永远扩不了容 | ObjectPool.cs |
| P3-10 | **测试与实现不一致**：非追逐测试实际是追逐模式；`Test_GenerationIncrement/IndexReuse` 依赖 FIFO free list 里恰好 ≤1 个空闲槽，预留 2 个就双双失败【实测 E5】；`WaitUntil` 无超时，失败表现为卡死而不是报错；LogTest 期望 `Log.E` 立即落盘而实现没有 Flush；`TestPossess.Update` 在 Controller BeginPlay 之前访问 `_moveAction`（null） | UnitTest/* |
| P3-11 | `AUnityController` 每个实例 `new GameInput()` 但只 Disable 不 Dispose；`GameInstance` 无实际职责；`RingBuffer` 不在命名空间；`Singleton.cs` 多余的 `using Unity.VisualScripting`；命名 `EventSystem` 与 `UnityEngine.EventSystems.EventSystem` 同名易混 | 多处 |

### 注释 ≠ 实现 一览（面试前务必自己先改掉或能解释）

| 注释原话 | 实际 |
|---|---|
| TimerData "纯值类型 (Blittable)"、"唯一的引用类型字段" | 有两个 `Action` 引用，不是 blittable |
| "使用 Unity 内置的高效 Hash" | 用的是 `string.GetHashCode()` |
| Test_ChaseMode："CreateTimer 未暴露 isChase 参数，默认 false" | 已暴露，默认 **true** |
| World："双缓冲思想" | 是 pending 延迟加入，不是双缓冲 |
| World.RegisterAActor："只有 World 存在时才注册" | `Instance` 不存在会自动创建 |
| World.Start："通知场景中现存的 AActor" | `_actors` 在此时恒为空 |
| ObjectPool.ReAllocate："深拷贝" | 结构体拷贝 = 引用浅拷贝 |
| DemoGameMode："实现完全无 GC 分配" | 只是避免了"每次传委托都 new"，事件、闭包仍在分配 |

---

## §6 预计面试官最可能攻击的 20 个问题

> 每题给出"攻击点 → 你必须先承认/澄清的事实"。四层答案与追问树在 Phase 4 展开。

| # | 面试官会怎么问 | 你回答时必须包含的事实 |
|---|---|---|
| 1 | 计时器为什么不用 `List<Timer>`，删除复杂度是多少？ | `RemoveAt` 是 O(n) 搬移；swap-and-pop 是 O(1) 但**不保序**，所以需要 handle→dense 映射和反向映射 |
| 2 | Swap-and-Pop 之后原来指向最后一个元素的 Handle 不就失效了吗？ | Handle 存的是稀疏 index，不是 dense 下标；搬移时用 `_indexToHandle` 找到它并改 `_handleToIndex` |
| 3 | slot 被新 Timer 复用，旧 Handle 怎么办？generation 解决的到底是什么？ | stale handle / ABA 式槽位复用；比对 generation |
| 4 | 那 `default(TimerHandle)` 是不是一个合法句柄？ | **是**，(0,0) 会命中第一个计时器——CombatPawn 正好踩到；修复是 generation 从 1 起 |
| 5 | generation 溢出怎么办？ | 2³² 次复用才回绕，可估算；production 可用 64 位或 index/serial 位域，耗尽的槽退役 |
| 6 | 你说 TimerSystem 是 Zero-GC，证据是什么？ | 不能这么说：TimerData 含引用；创建处委托/闭包会分配；扩容会分配；只有 steady-state Tick 不分配；需 Profiler 证明 |
| 7 | 回调里 `StopTimer` 自己 / 别人 / 再 Create 一个，会发生什么？ | 两阶段：先收集 handle 再执行，每次执行前 `IsHandleValid` 二次校验；新建的不在本帧缓存里 |
| 8 | interval=0.1、一帧卡了 0.6s，触发几次？为什么这么设计？ | 追赶模式触发 5 次后清零丢 1 次；非追赶 1 次并清零；loop 每帧补 1 次——语义不一致，各自适用什么业务 |
| 9 | `string.GetHashCode` 能当唯一 ID 吗？ | 不能：碰撞、跨运行时不稳定、0 值歧义；应用 `Dictionary<string, Handle>` 或编译期常量 ID |
| 10 | 事件回调里再发事件为什么不会栈溢出？延迟派发有什么代价？ | 双缓冲：新事件进 back，下一帧 swap；代价是 0～1 帧延迟、状态可能已变化、调试栈断开 |
| 11 | RingBuffer 满了怎么办？ | 当前静默丢弃；按事件类型选 Resize / DropOldest / DropNewest / Assert + 监控 |
| 12 | `() => callback(e)` 分配了什么？怎么做低 GC 的事件队列？ | 闭包类实例 + 委托（+事件对象）；typed queue / struct envelope / 预分配节点 |
| 13 | Priority 保证了什么？会饿死吗？ | 只保证"同一批次内 Logic 先于 Appearance"；Logic 回调里新发的 Appearance 去下一帧；每帧清空 front，不会饿死，但无法限流 |
| 14 | `World` 统一 Tick 比每个 MonoBehaviour 自己 Update 好在哪？代价？ | 可控顺序/开关/统一 dt、减少 Native→Managed 调用；代价：只统一了 Actor、启用状态脱钩、注册 O(n)、新 Actor 晚一帧 |
| 15 | 遍历时删除元素怎么保证安全？你的倒序遍历真的安全吗？ | 只对删自己/已遍历元素安全；删更低下标会重复 Tick（实测）；正解是延迟删除/墓碑标记 |
| 16 | 为什么 Controller 和 Pawn 要分开？Pawn 死了 Controller 怎么办？ | Who decides vs who exists；夺舍 = 换身体不换灵魂；当前 AI 失去身体自毁、玩家灵魂保留 |
| 17 | 为什么 UI 不直接访问 GameMode？单机里的 Authority 是什么意思？ | 单机里 Authority = 规则的唯一写入者/裁决者，不是网络权威；UE 中 GameMode 只存在于服务器，客户端拿不到 |
| 18 | `MonoSingleton` 子类 override Awake 为什么没调 base？会怎样？ | 承认 bug：防重失效、HasInstance 失真；修复方式 |
| 19 | ObjectPool 为什么没有 generation？它真的被用了吗？ | 没启用；旧拷贝能回收他人对象（实测）；可复用 Timer 的 Index+Generation |
| 20 | 你的 UnitTest 为什么不是 NUnit？测试覆盖了什么？ | 是运行时协程 harness，不在任何场景、无汇总、依赖帧率、部分用例名不副实；迁移到 Unity Test Framework（EditMode 测纯逻辑，需先把 Tick 从 MonoBehaviour 拆出来） |

> **面试注意**：第 4、6、15、18 题是"你自己代码里的确定性问题"，如果被面试官先发现，会非常被动；如果你主动说"我复盘时发现……并且我知道怎么修"，反而是最好的加分项。

---

## §7 基于源码重新设计的电子书目录（Phase 2 草案）

> 调整原则：① 按"真实存在且被使用"分配篇幅；② TimerSystem 拆成两章（结构 + 行为/GC）；③ 未启用模块压缩为"设计评审"章；④ 每章统一 18 段模板，但未实现的部分只写"如果实现"。

### Part I 项目全景
- **Ch1 项目目标、事实基线与真实架构**（本 Phase 1 精简版 + 架构图三视图）
- **Ch2 Unity 生命周期 → Gameplay 生命周期**（Awake/OnEnable/Start/Update/OnDestroy 与 BeginPlay/Tick 的映射、"先 Possess 后 BeginPlay"、场景级生命周期）
- **Ch3 World 与 Actor 统一 Tick**（pending、倒序遍历的真实安全边界、启用状态脱钩、TickGroup/Fixed Tick 扩展）★

### Part II Gameplay Framework
- **Ch4 Pawn / Controller / Possess**（双向引用不变式、非事务、夺舍 Demo、PlayerController vs AIController）★
- **Ch5 GameMode / GameState：规则、权威与可观察状态**（单机 Authority 的真实含义、规则散落问题、延伸到 Server/Client/PlayerState）★
- **Ch6 Singleton / GameInstance / 初始化顺序**（`base.Awake` bug、Awake/OnEnable 顺序隐患、ghost singleton、替代方案）

### Part III 基础设施
- **Ch7 EventSystem：Observer → 双缓冲延迟派发**（重入、优先级、快照语义、类型分发）★
- **Ch8 RingBuffer：环形队列与 cache locality**（head/tail/size、取模 vs 位与、清引用防泄漏）
- **Ch9 TimerSystem（上）：Slot Map 数据结构**（dense/sparse/reverse、swap-and-pop、FIFO free list、generation、stale/ABA/default/forged handle、溢出）★★★
- **Ch10 TimerSystem（下）：行为语义与 GC 审计**（两阶段回调与重入、interval/chase/loop、scaled/unscaled、逐项 GC 审计、Code Review → 最小修复 → production 方案、与 UE TimerManager 对比）★★★
- **Ch11 ObjectPool 设计评审：为什么没启用、启用后会出什么事**（stale PoolObject、复用 generation、Unity 内置 `UnityEngine.Pool`）
- **Ch12 ThreadSafeRingBuffer 与 ThreadSafeEventSystem**（lock-based ≠ lock-free、contention、SPSC lock-free 怎么写、MPSC/MPMC 难点）
- **Ch13 Log 与工具**（`[Conditional]` 零成本剥离、缓冲落盘的主线程 IO、DCL）

### Part IV Demo
- **Ch14 GamePlayTest2：一条事件驱动的开局流程**（GameMode → Timer 热身 → 事件 → UI 按钮 → Possess）
- **Ch15 SoulDecay：夺舍生存战**（阵营接口、寿命计时器、大吃小、对象池死代码）
- **Ch16 一次完整数据流追踪**（输入 → Controller → Pawn → 物理 → GameState → Event → UI，以及"游戏结束事件无人监听"）

### Part V Code Review
- **Ch17 What I Would Change Today**（P0～P3 完整版，每条：问题/位置/行为/触发/影响/最小修改/成熟设计）★
- **Ch18 性能与 GC 总账**（初始化 / 扩容 / 创建 / steady-state / 业务回调五类分配）
- **Ch19 生命周期与边界情况**（销毁顺序、切场景、退出、异常、重入）
- **Ch20 测试体系：从运行时脚本到 Unity Test Framework**（真实覆盖矩阵 + 迁移方案）
- **Ch21 如果重新设计一次**（GameLoop 显式调度、可测试的纯 C# 内核、Service/Subsystem）

### Part VI 面试
- Ch22 项目 30 秒 / 1 分钟 / 3 分钟介绍
- Ch23 高频基础问题（P0/P1）
- Ch24 深度追问树（Timer / Event / World / Possess / Singleton）
- Ch25 压力面试（质疑、反例、边界、性能、替代设计）
- Ch26 UE 对比问题（UWorld/AActor/APawn/AController/AGameMode/AGameState/UGameInstance/FTimerManager）
- Ch27 C++ / C# / 数据结构 / OS 延伸

### Part VII Final Interview Bank + Full Mock Interview
- P0 必会 / P1 高频 / P2 深挖 / P3 扩展
- 60 分钟全流程模拟面试

### 哪些章节最重要、为什么

| 排名 | 章节 | 原因 |
|---|---|---|
| 1 | Ch9 + Ch10 TimerSystem | 技术密度最高；面试官能连续追 8～10 层；项目里有可复现的真 bug（default 句柄） |
| 2 | Ch7 EventSystem | 双缓冲是亮点，但溢出/快照/异常/闭包每一个都是攻击点 |
| 3 | Ch4 + Ch5 Possess 与 GameMode/GameState | Gameplay 岗"框架理解"的必考区，也最容易被追到联网 |
| 4 | Ch17 Code Review | 决定你在压力面试里是"被动挨打"还是"主动复盘" |
| 5 | Ch3 World Tick + Ch6 Singleton | 生命周期题；`base.Awake` 是确定性 bug |

**最容易被追问的地方**：generation 与默认句柄、"Zero-GC"说法、swap-and-pop 后的映射维护、事件里发事件、`base.Awake`、为什么 UI 不读 GameMode、倒序遍历是否真安全、测试是不是真单元测试。

---

## §8 本阶段的面试注意事项

1. **措辞要降级**：简历/口述里把"Zero-GC TimerSystem"改成"**Tick 阶段零分配**的 Slot-Map 计时器"；把"单元测试"改成"运行时测试脚本（22 个计时器用例）"。
2. **先讲对的，再讲错的**：Timer 的两阶段回调 + 二次校验是写对了的，【实测 E4】可以作为底气；然后主动补一句"复盘时发现 generation 从 0 开始导致默认句柄有效"。
3. **"Authority"别说成网络权威**：单机里它只是"规则的唯一裁决者"，而且当前实现里写入路径并没有收口到 GameMode。
4. **不要说"完全复刻 UE"**：AActor 是组件而非容器、没有 TickGroup、没有复制、没有 PlayerState、World 是场景级懒单例。
5. **数字要有出处**：书里所有"触发几次 / 谁被停掉"的数字都来自 `demos/run_output.txt`；GC 字节数这一类需要你在 Unity Profiler 里补测，在此之前只说"会分配几次"，不报字节数。

---

## 附：实测输出摘录（`demos/run_output.txt`）

```text
E1 [A] playerDecay=(0,0) spawner=(1,0)；AI 肉体 _decayTimer=(0,0) IsHandleValid=true(危险)
       AI 死亡后：玩家寿命计时器 IsHandleValid=false → UI 显示 DIE，且玩家永不爆炸
   [B] 若刷怪器先创建：任意 AI 死亡后 spawner 失效 → 刷怪停止
   [C] slot0 被复用过一次之后 default 句柄失效（当前 Demo 能侥幸跑通的原因）
E2 伪造句柄 (1,1) IsHandleValid=true → StopTimer 后 C 成为僵尸句柄，free list 出现重复下标，C 的句柄读到 D 的数据
E3 isChase=true 卡顿 0.6s 触发 5 次并清零；isChase=false 触发 1 次；dt=0.35 触发 3 次、余量 0.05 保留
   dt=0.5 触发 5 次、余量 1.49e-08（float 误差）；Loop 卡顿后每帧补 1 次：1 1 1 0 1 0；Loop(duration=0) 每帧触发
E4 回调中停自己 + 停同帧的 B + 新建 C：B 被 generation 拦截，C=(0,1)；同帧回调顺序 ≠ 创建顺序
E5 free list 预留 0/1 个槽：两个测试 PASS；预留 2 个槽：GenerationIncrement FAIL、IndexReuse FAIL
E6 每秒复用 60 次 → 829 天回绕；int.MaxValue+1 = -2147483648
E7 A↔B 互发：每帧 1 个、最大调用深度 1；2001 个事件丢 1 个；注销后仍被调用 1 次；
   异常后 E2/E3 排到 E4 之后；Logic 回调里发的 Appearance 去下一帧
E8 ObjectPool 旧拷贝 Recycle 成功 → 别人正在用的对象被回收
E9 Tick 中删除更低下标 Actor：当前 Actor 被 Tick 2 次
```

---
