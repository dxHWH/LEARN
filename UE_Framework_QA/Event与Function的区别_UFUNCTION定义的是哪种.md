# UE 中的事件（Event）和函数（Function）有什么区别？C++ 里的 UFUNCTION() 定义的是哪一种？

## 提问记录

| 时间 | 原始提问 | 回答对应章节 |
|---|---|---|
| 2026-10-14 11:32 | UE中的事件Event和函数Function的区别是什么？我们在C++中声明了UFUNCTION()宏，定义的是哪一种？ | 全文（§0 30 秒版 → §1 先拆概念 → §2 蓝图层面的区别 → §3 底层：UberGraph → §4 UFUNCTION 定义的是哪种 → §5 UHT 生成了什么 → §6 选型 → §7 坑位 → §8 自测） |

> 基准：UE 5.x。配套 demo：`demos/event_vs_function_sim.cpp`，用纯 C++ 模拟了 UHT 生成的分派代码、UberGraphFrame 和 ProcessEvent。它**不是 UE 源码**，只用来把机制跑出现象。MSVC 14.44 `/O2` 实测，输出见 `demos/run_output.txt`。

---

## 0. 30 秒面试版答案

- **底层上两者都是 `UFunction`**。区别不在于是不是函数，而在于**实现放在哪里、怎么被调用**。
- **蓝图里的区别**：
  - 事件（Event）放在事件图（EventGraph）里，**没有返回值**。它可以挂起：Delay、Timeline、AI MoveTo 这类延迟节点只能在事件里用。它可以设置成 RPC，可以绑定到事件分发器（Event Dispatcher）。
  - 函数（Function）有自己独立的图，**可以有返回值和局部变量**，调用后**同步执行完毕再返回**，不能用延迟节点。它可以是 Pure（纯函数）和 Const。
- **为什么会这样**：一个蓝图里的所有事件，会被编译进同一个大函数 `ExecuteUbergraph_XXX`，每个事件只是这个大函数的一个入口点。事件图里所有的临时值都存在**对象身上**的 UberGraphFrame 里，跨帧也还在，所以执行到一半可以挂起、之后再接着跑。函数的局部变量在**本次调用的栈帧**里，函数一返回就没了，因此不能挂起。
- **UFUNCTION() 定义的是哪一种**：`UFUNCTION()` 本身只是把这个函数登记进反射系统，生成一个 `UFunction`。它在蓝图里表现成哪种，要看说明符：
  - `BlueprintCallable` / `BlueprintPure` → **函数**：C++ 实现，蓝图调用；
  - `BlueprintImplementableEvent` / `BlueprintNativeEvent` → **事件**：C++ 发起调用，实现交给蓝图（Native 版本在 C++ 里有默认实现）。这类函数的函数体由 UHT 生成，内容就是一次 `ProcessEvent`；
  - `Server` / `Client` / `NetMulticast`（RPC）在引擎内部也打了 `FUNC_Event` 标志，同样走 `ProcessEvent`。
- **一句话**：函数是「C++ 写好，给蓝图调」；事件是「C++ 发起，蓝图（或远端）来实现」。

---

## 1. 先拆概念：UE 里的「Event」至少有四个意思

面试里先把这一点讲清楚，能避免答非所问：

| 说法 | 是什么 | 本文是否讨论 |
|---|---|---|
| 蓝图事件节点（红色标题） | EventGraph 里的 `Event BeginPlay`、Custom Event | ✅ 主角 |
| `BlueprintImplementableEvent` / `BlueprintNativeEvent` | C++ 声明、蓝图实现的 UFunction | ✅ 主角 |
| 事件分发器（Event Dispatcher） | 蓝图里的动态多播委托，C++ 对应 `DECLARE_DYNAMIC_MULTICAST_DELEGATE` | 只在 §2 提一下绑定 |
| `DECLARE_EVENT(OwnerType, Name)` | C++ 多播委托的一个变体，只有声明它的类能 `Broadcast` | ❌ 和本题无关，提一句即可 |

> 还有个历史原因：UnrealScript 时代就分 `function` 和 `event` 两个关键字，`event` 指「可以被 C++ 调到的脚本函数」。这就是为什么引擎里所有反射调用的入口叫 `ProcessEvent`，UHT 生成的参数结构体也叫 `XXX_eventFoo_Parms`。

---

## 2. 蓝图层面的区别（最常被问的一张表）

| 维度 | 事件 Event | 函数 Function |
|---|---|---|
| 放在哪 | 事件图（多个事件共用一张图） | 每个函数一张独立的图 |
| 返回值 / 输出参数 | ❌ 没有，只有输入 | ✅ 可以有多个输出 |
| 局部变量 | ❌ 没有真正的局部变量，临时值都存在对象的 UberGraphFrame 里 | ✅ 有，每次调用一份 |
| 延迟节点（Delay、Timeline、MoveTo、异步加载） | ✅ 可以 | ❌ 不可以（编译报错） |
| 执行方式 | 可能跨多帧执行完（中间挂起） | 一次执行完，立刻返回 |
| 网络复制（Run on Server / Multicast / Owning Client） | ✅ 自定义事件可以设置 | ❌ 蓝图函数不能设置复制 |
| 绑定事件分发器 | ✅ 直接连红色委托引脚 | 要通过 Create Event 节点选择签名匹配的函数 |
| Pure / Const | ❌ | ✅ 纯函数没有执行引脚 |
| 被其他蓝图调用 | 可以（作为 Custom Event 调用） | 可以，而且更规范；接口、库函数都是函数形式 |
| 覆写父类 | Override 事件 | Override 函数 |

**一个经验规律**：只要有返回值，蓝图就会把它当函数处理。比如蓝图接口里的函数：没有输出时实现出来是一个事件节点；有输出时只能在 Functions 面板里覆写成函数。C++ 里带返回值的 `BlueprintImplementableEvent` 也是这样。

---

## 3. 底层：为什么事件能挂起、函数不能

### 3.1 事件图会被编译成一个大函数

蓝图编译器（Kismet Compiler）会把一个蓝图事件图里的**所有事件**合并编译成一个函数 `ExecuteUbergraph_BP_Door(int32 EntryPoint)`。每个事件节点只生成一个很小的入口函数（stub），内容就是：

```
Event OnInteract(Who)  →  把 Who 写进 UberGraphFrame  →  ExecuteUbergraph_BP_Door(EntryPoint = OnInteract 的偏移)
```

事件图里每个节点的输出引脚、临时值，都存在一块叫 **UberGraphFrame** 的内存里。这块内存**挂在每个蓝图实例上**，跟对象同生共死。

### 3.2 Delay 怎么「跨帧接着跑」

Delay 节点会向 `FLatentActionManager` 登记一条记录：`{回调对象, 回调函数 = ExecuteUbergraph_XXX, Linkage = 续点偏移, UUID}`。时间到了，引擎就用这个续点偏移重新调用 ubergraph。因为所有中间值都还在 UberGraphFrame 里，执行可以从 Delay 后面接着往下走。

函数没有这块常驻内存，局部变量随调用栈帧一起销毁，没有地方保存「执行到了哪里、中间值是多少」，所以蓝图编译器直接禁止在函数里放延迟节点。

### 3.3 实测 D2：UberGraphFrame 的副作用

```
t=0.0 OnInteract(who=1)：开门动画，Delay 2.0s 后自动关门
t=0.5 OnInteract(who=2)：Delay 正在等待，本次执行被忽略（但 Who 引脚已被改成 2）
t=2.0 Delay 完成，从续点继续：关门，读取 Who=2  <- 读到的是最后一次调用写进来的值
```

这就是事件图里的经典坑：**事件参数存在对象身上，后一次调用会覆盖前一次**。Delay 之后再去读事件引脚，读到的不一定是触发这次 Delay 时的值。另外，普通 Delay 在等待期间再次触发会被**忽略**；想要重新计时用 Retriggerable Delay。需要保存每次调用自己的数据时，应该先存进变量或数组，不要依赖事件引脚。

---

## 4. 回到第二问：C++ 里的 UFUNCTION() 定义的是哪一种？

**`UFUNCTION()` 定义的是一个 `UFunction`（反射函数对象），本身不分事件还是函数。** 在蓝图里表现成哪一种，由说明符决定：

| C++ 写法 | 蓝图里看到的 | 实现在哪 | C++ 侧要写什么 | 内部标志（主要） |
|---|---|---|---|---|
| `UFUNCTION()`（不加说明符） | 看不到 | C++ | 正常写函数体 | `FUNC_Native` |
| `UFUNCTION(BlueprintCallable)` | **函数**节点（蓝色，带执行引脚） | C++ | 正常写函数体 | `FUNC_Native \| FUNC_BlueprintCallable` |
| `UFUNCTION(BlueprintPure)` | **纯函数**节点（绿色，无执行引脚） | C++ | 正常写函数体，一般是 const | `+ FUNC_BlueprintPure` |
| `UFUNCTION(BlueprintImplementableEvent)` | **事件**（没有返回值时），或可覆写的函数（有返回值时） | 蓝图 | **不能写函数体**，UHT 会生成 | `FUNC_Event \| FUNC_BlueprintEvent` |
| `UFUNCTION(BlueprintNativeEvent)` | 同上，蓝图可以选择覆写 | C++ 默认实现 + 蓝图可覆写 | 写 `Foo_Implementation()` | `FUNC_Event \| FUNC_BlueprintEvent \| FUNC_Native` |
| `UFUNCTION(Server/Client/NetMulticast, Reliable)` | — | 远端执行 | 写 `Foo_Implementation()`（加 `WithValidation` 时还要写 `Foo_Validate()`） | `FUNC_Net \| FUNC_Event \| ...` |

引擎里最典型的例子是 `BeginPlay`：

```cpp
// Actor.h（简化）
virtual void BeginPlay();                                   // C++ 虚函数
UFUNCTION(BlueprintImplementableEvent, meta=(DisplayName="BeginPlay"))
void ReceiveBeginPlay();                                     // 蓝图里看到的 Event BeginPlay
// AActor::BeginPlay() 里会调用 ReceiveBeginPlay()，把这个时机“通知”给蓝图
```

C++ 子类覆写的是 `BeginPlay()` 这个虚函数；蓝图里的红色 `Event BeginPlay` 实际实现的是 `ReceiveBeginPlay`。`Tick` 和 `ReceiveTick` 也是同样的关系。这就是「C++ 发起、蓝图实现」。

不加任何蓝图说明符的 `UFUNCTION()` 也有用，因为很多机制是**按名字通过反射调用**的：
- 动态委托 `AddDynamic` / `BindUFunction`：绑定的目标必须是 UFUNCTION，否则运行时绑定失败；
- `FTimerDelegate::CreateUFunction`、`SetTimer` 按函数名设置定时器；
- RPC、`Exec` 控制台命令（只在 PlayerController、CheatManager、GameMode 等少数类上生效）；
- `FindFunction` + `ProcessEvent` 按名字调用。

---

## 5. UHT 到底生成了什么（区分两条调用路径）

### 5.1 函数（BlueprintCallable）：生成一个 exec thunk，供蓝图调用

```cpp
// .gen.cpp（简化）
DEFINE_FUNCTION(AMyActor::execAddScore)
{
    P_GET_PROPERTY(FIntProperty, Z_Param_Delta);   // 从蓝图 VM 的栈帧里读参数
    P_FINISH;
    P_NATIVE_BEGIN;
    *(int32*)Z_Param__Result = P_THIS->AddScore(Z_Param_Delta);   // 调你写的 C++ 函数
    P_NATIVE_END;
}
```

- 蓝图调用时：VM → `execAddScore` → 你的 `AddScore`；
- **C++ 调用时：就是一次普通的成员函数调用，不经过反射**。

### 5.2 事件（ImplementableEvent / NativeEvent / RPC）：UHT 替你写函数体

```cpp
// .gen.cpp（简化）
static FName NAME_AMyActor_OnHit = FName(TEXT("OnHit"));
void AMyActor::OnHit(int32 Damage)              // 你在头文件里声明的 OnHit，函数体是 UHT 写的
{
    MyActor_eventOnHit_Parms Parms;
    Parms.Damage = Damage;
    ProcessEvent(FindFunctionChecked(NAME_AMyActor_OnHit), &Parms);
}
// NativeEvent 另有 execOnHit thunk，里面调用 P_THIS->OnHit_Implementation(Damage)
```

关键在 `FindFunctionChecked`：它**从对象的实际类开始往上找**。如果这个对象是蓝图子类，而蓝图覆写了 OnHit，找到的就是蓝图编译出来的 UFunction（执行蓝图字节码）；如果没有覆写，找到的是 C++ 的 UFunction，经过 thunk 调到 `OnHit_Implementation`。RPC 的生成代码长得一样，只是 `ProcessEvent` 内部发现带 `FUNC_Net` 标志，会转去 `CallRemoteFunction` 把调用发到远端。

**实测 D1**（蓝图子类覆写了 OnHit，并且调用了父类实现）：

```
纯 C++ 类：OnHit(10)            hp=90
    - C++ 默认实现：扣血 10
蓝图子类：OnHit(10) + OnHit_Impl(10) hp=80
    - 蓝图覆写：播受击特效，然后调用父类
    - C++ 默认实现：扣血 10
    - C++ 默认实现：扣血 10          <- 直接调 _Implementation，蓝图的特效没播
```

→ **在 C++ 里要调用 `OnHit()`，不要直接调 `OnHit_Implementation()`**，否则会绕过蓝图的覆写。只有在 C++ 子类覆写 `OnHit_Implementation` 时，才在里面调用 `Super::OnHit_Implementation()`。

### 5.3 实测 D3：反射调用比直接调用贵

```
C++ 直调 AddScore                 9.7 ms  (0.10 ns/次)
ProcessEvent + 每次查表         166.4 ms  (1.66 ns/次)
ProcessEvent + 缓存 UFunction   108.1 ms  (1.08 ns/次)
```

直接调用那一行被编译器内联、合并了，所以几乎为零；但趋势很清楚：反射调用要查表、打包参数、再跳一次函数指针，没法内联。真实引擎的 `ProcessEvent` 还要准备 FFrame 栈帧、按属性初始化和析构参数，进入蓝图后还要逐条解释执行字节码，开销比这个玩具大得多。所以**高频逻辑（每帧、每个子弹）不要放在 BlueprintImplementableEvent 或蓝图 Tick 里**。

---

## 6. 实际项目怎么选

| 需求 | 用什么 |
|---|---|
| C++ 写好的能力，给策划和蓝图用（加血、发技能、查询状态） | `BlueprintCallable`（没有副作用的查询用 `BlueprintPure`） |
| C++ 在某个时机通知蓝图，由蓝图做表现（受击特效、UI 动画） | `BlueprintImplementableEvent`（不调用也不会出错，蓝图没实现就什么都不做） |
| C++ 有默认逻辑，但允许个别蓝图改掉 | `BlueprintNativeEvent` + `_Implementation` |
| 一对多的通知，多个对象都要监听 | 事件分发器 / 动态多播委托，不要硬写成 Event |
| 跨端调用 | RPC（`Server` / `Client` / `NetMulticast`），注意不能有返回值 |
| 蓝图里需要等待（Delay、Timeline、异步加载） | 只能用事件（或者 C++ 写一个异步节点 `UBlueprintAsyncActionBase`） |
| 蓝图里需要返回值、需要被复用 | 函数 |

---

## 7. 坑位清单（面试注意点）

1. **不要说「UFUNCTION 定义的就是函数」或「就是事件」**。正确说法：它定义一个 UFunction，是事件还是函数由说明符决定。
2. **`BlueprintImplementableEvent` 不能写函数体**。函数体是 UHT 生成的，自己再写一个会报重复定义。
3. **调 NativeEvent 要调 `Foo()`**，不要调 `Foo_Implementation()`（D1）。
4. **事件不能有返回值**，RPC 也不能有返回值（因为是异步发到远端执行的）。带返回值的 ImplementableEvent 在蓝图里会出现在 Functions 的 Override 列表里，而不是事件节点。
5. **纯函数（Pure）每连一次就算一次**：一个纯函数的输出连到三个节点，就会被执行三次；放进 ForEach 循环里，每次迭代都会重新算。代价大的逻辑不要做成 Pure。
6. **事件图里的值是共享的**（D2）：Delay 之后读事件引脚，可能已经被后一次调用覆盖了。
7. **动态委托绑定的函数必须加 UFUNCTION()**：`OnComponentBeginOverlap.AddDynamic(this, &AMyActor::OnOverlap)` 里的 `OnOverlap` 不加 UFUNCTION，编译能过，运行时绑定会失败（开发版本会报 ensure）。
8. **高频逻辑不要走蓝图事件**：每次调用都要经过反射和蓝图 VM（D3）。
9. **RPC 也算事件**：它的函数体同样由 UHT 生成，你实现的是 `_Implementation`。这一点能和第 4 节串起来答。

---

## 8. 自测题

1. 为什么蓝图函数里不能放 Delay？（提示：UberGraphFrame 和栈帧的区别）
2. 蓝图里的 `Event BeginPlay` 对应 C++ 的哪个函数？C++ 子类该覆写哪个？
3. `BlueprintNativeEvent` 在蓝图覆写之后，C++ 调用 `Foo()` 会走哪条路径？是谁决定的？
4. 一个带返回值的蓝图接口函数，在实现它的蓝图里长什么样？
5. 没有任何说明符的 `UFUNCTION()` 有什么用？举两个例子。
6. 一个纯函数连接了 3 个输出，执行了几次？

<details><summary>参考答案</summary>

1. 函数的局部变量在本次调用的栈帧里，返回就销毁，没有地方保存续点和中间值；事件图的中间值存在对象的 UberGraphFrame 里，Delay 登记续点偏移后，可以重新进入 ubergraph 接着执行。
2. 对应 `ReceiveBeginPlay`（BlueprintImplementableEvent，DisplayName 是 BeginPlay）。C++ 子类覆写虚函数 `BeginPlay()`，并调用 `Super::BeginPlay()`，否则蓝图的 BeginPlay 不会触发。
3. UHT 生成的 `Foo()` 调用 `ProcessEvent(FindFunctionChecked("Foo"))`。查找从对象的实际类开始，蓝图子类覆写了就找到蓝图的 UFunction，执行蓝图字节码；没覆写就找到 C++ 的 UFunction，经 exec thunk 调到 `Foo_Implementation`。
4. 不会是事件节点，而是在 Functions 面板里覆写成一个函数，有 Return 节点。
5. 动态委托绑定（AddDynamic / BindUFunction）、按名字设置定时器、按名字反射调用（FindFunction + ProcessEvent）、Exec 控制台命令；加上 RPC 说明符时就是 RPC。
6. 3 次，每个使用它的节点各求值一次。

</details>
