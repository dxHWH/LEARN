# C++ 四种 cast 完全指南（游戏客户端面试向）

> 配套代码：同目录 `cast_demo.cpp`（MSVC 14.44 / VS2022 x64，`/std:c++latest /EHsc /O2` 编译运行；文中「实测」都来自它，完整输出见 `run_output.txt`）
> 反例：`compile_fail/` 下 8 个（cf1～cf6 编译失败、cf7/cf8 关闭 RTTI 后的运行期表现），诊断与运行结果见 `compile_fail/compile_errors.txt`
> 编译：`cl /nologo /std:c++latest /EHsc /O2 /utf-8 cast_demo.cpp`
> 相关：`std::move` / `std::forward` 的本质就是 `static_cast`，见 [std::forward 教学文档](../std_forward/std_forward_教学文档.md)

---

## 提问记录

| 时间 | 原始提问 | 回答对应章节 |
|---|---|---|
| 2026-10-10 | 同理，再为我详细的解析 C++中的四种cast（承接上一问：游戏客户端面试准备，详细解析、适当控制深度、增加可读性、点出面试注意点、深层次技术解答、问答落盘为 md） | 第 0～10 章 |

---

## 目录

0. 30 秒面试版答案（先背这个）
1. 为什么要有四种 cast：C 风格转换的问题
2. `static_cast`：编译期的「合理转换」
3. `dynamic_cast`：运行期带检查的多态转换
4. `const_cast`：只改 const / volatile
5. `reinterpret_cast`：按位重新解释
6. C 风格转换到底做了什么
7. 周边：智能指针 cast、`std::bit_cast`、`std::move` 的真身
8. 性能实测：类型判断到底多贵
9. 游戏客户端实战
10. 面试注意点：易错说法 + 坑位清单 + 自测题

---

## 0. 30 秒面试版答案（先背这个）

> C++ 有四种具名转换，用来替代语义模糊的 C 风格转换，每一种只做一类事情，代码里一眼能看出意图，也方便搜索和审查：
>
> 1. **`static_cast`**：**编译期**完成的「合理」转换。数值类型互转、枚举与整数互转、`void*` 与具体指针互转、调用 `explicit` 构造/转换函数、类层次中的上行和下行转换。**下行转换不做运行期检查**，类型不对就是未定义行为。
> 2. **`dynamic_cast`**：**运行期**借助 RTTI 做带检查的多态转换。要求源类型是**多态类型**（有虚函数）。指针转换失败返回 `nullptr`，引用转换失败抛 `std::bad_cast`。能做下行转换、多继承下的交叉转换、`dynamic_cast<void*>` 取最派生对象地址。代价是一次函数调用加继承树查找，比虚函数调用慢一个数量级。
> 3. **`const_cast`**：**唯一**能去掉（或加上）`const` / `volatile` 的转换，不能改变类型。只有当对象**本身不是 const**、只是通过 const 指针/引用访问时，去 const 后修改才合法；修改真正的 const 对象是未定义行为。
> 4. **`reinterpret_cast`**：在指针、引用、整数之间**按位重新解释**，不做任何地址调整和检查，最危险。典型用途是指针和 `uintptr_t` 互转、把对象当字节数组看、对接底层 API。读写时还要注意**严格别名规则**，想看 float 的位模式应该用 `memcpy` 或 C++20 的 `std::bit_cast`。
>
> **一句话选型**：优先不转；必须转时用 `static_cast`；需要运行期判断类型用 `dynamic_cast`（游戏引擎里通常换成自研类型系统）；`const_cast` 只用来对接老接口；`reinterpret_cast` 只用在底层代码，并且封装起来。
>
> **最常考的点**：多继承下 `static_cast` 会**调整指针地址**而 `reinterpret_cast` 不会；`dynamic_cast` 的实现和开销；C 风格转换会悄悄退化成 `reinterpret_cast` 或 `const_cast`；游戏引擎为什么关闭 RTTI。

后面每一章都是把这段话里的一句展开讲透。

---

## 1. 为什么要有四种 cast：C 风格转换的问题

C 风格转换 `(T)expr` 和函数式转换 `T(expr)` 是「一个语法做所有事」：

```cpp
float f = 3.7f;
int a  = (int)f;        // 数值转换：3
int* p = (int*)&f;      // 位重解释：*p = 0x406CCCCD
```

实测这两行用的括号一模一样，语义却完全不同（见 demo 第 9 节）。问题在于：

| 问题 | 说明 |
|---|---|
| 意图不清 | 看代码猜不出作者想做数值转换、去 const 还是位重解释 |
| 静默降级 | 编不过 `static_cast` 时，会悄悄换成 `reinterpret_cast`，错误被掩盖 |
| 无视访问控制 | 可以转到 `private` 基类（`static_cast` 会报 C2243） |
| 难以搜索 | 无法 grep 出项目里所有危险转换 |

具名 cast 的设计思想是：**按危险程度把转换拆开，越危险的写起来越长越显眼。**

### 1.1 四种 cast 总览

| | `static_cast` | `dynamic_cast` | `const_cast` | `reinterpret_cast` |
|---|---|---|---|---|
| 检查时机 | 编译期 | **运行期** | 编译期 | 几乎不检查 |
| 能否去 const | ❌ | ❌ | ✅（唯一） | ❌ |
| 数值转换 | ✅ | ❌ | ❌ | ❌ |
| 类层次上行 | ✅ | ✅ | ❌ | ⚠️ 不调整地址 |
| 类层次下行 | ✅ 不检查 | ✅ 检查 | ❌ | ⚠️ 不调整地址 |
| 无关类之间（交叉） | ❌ | ✅ 需要多态 | ❌ | ⚠️ 位重解释 |
| 指针 ↔ 整数 | ❌ | ❌ | ❌ | ✅ |
| 需要 RTTI | ❌ | ✅ | ❌ | ❌ |
| 运行开销 | 0 或一次加法 | 函数调用 + 查找 | 0 | 0 |
| 危险程度 | 中 | 低 | 高 | 最高 |

### 1.2 选型流程

```
需要转换吗？ ──否──▶ 别转（很多转换是设计问题的信号）
   │是
   ├─ 只是去掉 const/volatile？ ───────────────▶ const_cast（确认对象本身不是 const）
   ├─ 数值、枚举、void*、explicit、上行转换？ ──▶ static_cast
   ├─ 基类指针 → 派生类，类型不确定？ ─────────▶ dynamic_cast（或引擎的 Cast<T>）
   ├─ 基类指针 → 派生类，逻辑上 100% 确定？ ───▶ static_cast（Debug 下可加 assert(dynamic_cast)）
   ├─ 指针 ↔ 整数、看对象字节、对接底层 API？ ─▶ reinterpret_cast（封装起来）
   └─ 想看 float 的位模式？ ──────────────────▶ std::bit_cast / memcpy，不要 reinterpret_cast
```

---

## 2. `static_cast`：编译期的「合理转换」

### 2.1 能做的事情

| 用途 | 例子 | 实测 |
|---|---|---|
| 数值类型互转 | `static_cast<int>(3.99)` | `3`（**向 0 截断**，不是四舍五入） |
| | `static_cast<int>(-3.99)` | `-3` |
| | `static_cast<unsigned>(-1)` | `4294967295`（按模 2³² 转换） |
| | `static_cast<uint8_t>(300)` | `44`（按模 256 截断） |
| | `static_cast<float>(16777217)` | `16777216.0`（float 只有 24 位尾数） |
| 枚举 ↔ 整数 | `static_cast<ESkillSlot>(3)` | 枚举类必须显式转 |
| | `static_cast<ESkillSlot>(7)` | **照样转换成功，值为 7**（不检查是否是合法枚举项） |
| 调用 explicit 构造/转换函数 | `static_cast<Meter>(2.5)`、`static_cast<double>(m)` | `2.5` |
| `void*` ↔ `T*` | `static_cast<int*>(vp)` | 往返得到原值 |
| 转成右值引用 | `static_cast<std::string&&>(s1)` | 就是 `std::move` 的实现，之后 `s1.size()=0` |
| 类层次上行/下行 | `static_cast<Warrior*>(unitPtr)` | 见 2.2 |
| 丢弃返回值 | `static_cast<void>(f())` | 消除「未使用返回值」警告 |

面试要点：

- **数值转换的结果规则**：浮点转整数向 0 截断，超出目标范围是**未定义行为**；整数转无符号是按模；有符号窄化在 C++20 之前是实现定义（C++20 起规定为按模）。游戏里做伤害、坐标计算时「`(int)` 截断」和「`std::round`」要分清楚。
- **枚举转换不做范围检查**：服务器下发了新版本才有的枚举值、或者存档损坏，`static_cast` 都会照转，后面 `switch` 落入 `default` 或者数组越界。网络和存档读到的枚举值要先校验范围再转。

### 2.2 类层次转换：下行不检查

```cpp
struct Unit    { virtual ~Unit() = default; int hp = 100; };
struct Warrior : Unit { int rage = 50; };
struct Mage    : Unit { int mana = 300; };

Warrior w;
Unit* u = &w;
Warrior* pw  = static_cast<Warrior*>(u);   // 类型对：没问题
Mage*    pmS = static_cast<Mage*>(u);      // 类型错：编译通过、运行不报错
Mage*    pmD = dynamic_cast<Mage*>(u);     // nullptr
```

实测：

```
static_cast<Mage*>     = 非空   <- 错误类型也不报错
dynamic_cast<Mage*>    = nullptr
[UB] 通过错误指针读 mana = 50   (实际读到的是 Warrior::rage)
```

`static_cast` 只看**静态类型**：`Mage` 是 `Unit` 的派生类，转换在语法上成立，它就按 `Mage` 的布局算偏移，根本不知道对象实际是什么。读 `mana` 读到的是同一偏移处的 `rage`，这种 bug 不会立刻崩溃，而是数据悄悄错乱，最难排查。

**什么时候可以用 `static_cast` 下行？** 逻辑上能保证类型正确时，比如已经通过类型标记判断过（第 9 章的自研类型系统就是这样做的），或者 CRTP 中 `static_cast<Derived*>(this)`。常见做法是在 Debug 版本里加断言：

```cpp
template <class To, class From>
To* checked_cast(From* p) {
    assert(p == nullptr || dynamic_cast<To*>(p) != nullptr);   // 只在 Debug 检查
    return static_cast<To*>(p);
}
```

### 2.3 多继承：`static_cast` 会调整指针

```cpp
struct IRenderable { virtual ~IRenderable(); virtual const char* Name() const; int layer = 1; };
struct ITickable   { virtual ~ITickable();   virtual const char* Tick() const; int tickGroup = 2; };
struct Sprite : IRenderable, ITickable { int frame = 7; };
```

`Sprite` 在内存里依次放着两个基类子对象：

```
Sprite 对象（x64）
偏移 0  ┌──────────────────────────┐ ◀── &sp、IRenderable* 都指向这里
        │ vptr (IRenderable 部分)   │
        │ layer = 1                 │
偏移 16 ├──────────────────────────┤ ◀── ITickable* 必须指向这里
        │ vptr (ITickable 部分)     │
        │ tickGroup = 2             │
        ├──────────────────────────┤
        │ frame = 7                 │
        └──────────────────────────┘
```

实测：

```
&sp                                = ...FC20
static_cast<ITickable*>(&sp)       = ...FC30   (偏移 +16)
reinterpret_cast<ITickable*>(&sp)  = ...FC20   (偏移 +0)
tS->tickGroup = 2,  tS->Tick() = ITickable::Tick
[UB] tR->tickGroup = 1 (读到 layer),  tR->Tick() = IRenderable::Name  <- 调错了虚函数
static_cast<Sprite*>(tS) == &sp    : 1 (下行时反向调整 -16)
static_cast<ITickable*>(空 Sprite*) = 0000000000000000 (空指针不加偏移)
```

三个结论，都是面试高频：

1. **转换不一定是「同一个地址换个类型」**。多继承下，派生类指针转成第二个基类指针，地址会变。隐式转换和 `static_cast` 都会正确加上偏移。
2. **`reinterpret_cast` 不加偏移**，拿到的指针指向的是 `IRenderable` 子对象，通过它调虚函数，查的是 `IRenderable` 的虚表，同一个槽位上放的是 `Name()`，于是**调错了函数**。实际项目里这类 bug 表现为「调了 A 接口的方法却执行了 B 接口的代码」，非常诡异。
3. **空指针特判**：编译器生成的代码是 `p ? p + 16 : nullptr`，否则空指针会变成 `0x10`。所以带偏移的 `static_cast` 不是完全零开销，有一次判空和一次加法。

> 推论：**用 `void*` 中转时，必须转回原来的类型**。`Sprite*` → `void*` → `ITickable*` 是错的（`static_cast<ITickable*>(void*)` 不知道要加偏移），必须先转回 `Sprite*` 再隐式转换。游戏里把对象指针塞进 `void* userData`（物理引擎、Lua userdata、UI 控件 tag）时经常踩这个坑。

---

## 3. `dynamic_cast`：运行期带检查的多态转换

### 3.1 用法与规则

```cpp
Mage* pm = dynamic_cast<Mage*>(unitPtr);       // 失败返回 nullptr
if (pm) pm->CastFireball();

try {
    Mage& rm = dynamic_cast<Mage&>(*unitPtr);  // 失败抛 std::bad_cast
} catch (const std::bad_cast& e) {             // 实测 what() = "Bad dynamic_cast!"
}
```

| 规则 | 说明 |
|---|---|
| 源类型必须是多态类型 | 至少一个虚函数，否则下行转换编译失败（cf3：**C2683 “Base”不是多态类型**）。上行转换不需要多态，等价于隐式转换 |
| 指针失败返回 `nullptr` | 引用不能为空，所以引用失败抛 `std::bad_cast` |
| 输入空指针 | 返回空指针，不会崩溃 |
| 能做交叉转换 | `IRenderable*` → `ITickable*`（实测结果与 `static_cast<ITickable*>(&sp)` 相同）。`static_cast` 做不到（cf1：C2440） |
| 能从虚基类下行 | `static_cast` 做不到（cf2：**C2635 暗含了从虚拟基类的转换**），因为虚基类的偏移只能运行期从虚表里查 |
| `dynamic_cast<void*>` | 返回**最派生对象**的首地址（实测等于 `&sp`），可用于判断两个不同接口指针是否指向同一个对象 |
| 访问控制 | 目标必须是 public 基类，私有继承的转换会失败 |

### 3.2 构造和析构期间：动态类型会变

实测：

```
BaseC 构造函数中    typeid=struct BaseC    dynamic_cast<DerivedC*>=nullptr
DerivedC 构造函数中 typeid=struct DerivedC dynamic_cast<DerivedC*>=非空
BaseC 析构函数中    typeid=struct BaseC    dynamic_cast<DerivedC*>=nullptr
```

基类构造时派生类部分还没构造，析构时派生类部分已经析构，所以在这两个阶段对象的动态类型就是基类本身。虚表指针在每一层构造函数开始时被改写，`dynamic_cast`、`typeid`、虚函数调用都看到同一个结果。这也是「**不要在构造/析构函数里调用虚函数**」的同一个原理。

### 3.3 实现原理：它到底查了什么

`dynamic_cast` 依赖 **RTTI**（运行期类型信息）。编译器会给每个多态类生成一份类型描述，挂在虚表上：

```
对象                 虚表                          类型信息（MSVC）
┌─────────┐        ┌──────────────────────┐
│ vptr ───┼──────▶ │ [-1] CompleteObject- │──▶ RTTICompleteObjectLocator
│ 成员 ... │        │      Locator 指针     │      ├ offset：本子对象到完整对象的偏移
└─────────┘        │ [0] 虚函数 1          │      ├ pTypeDescriptor：本类的 type_info
                   │ [1] 虚函数 2          │      └ pClassHierarchyDescriptor
                   └──────────────────────┘            ├ 属性：是否多继承 / 虚继承
                                                       └ BaseClassArray：所有基类的描述
                                                            每项含 type_info、偏移(PMD)、访问属性
```

`dynamic_cast<Target*>(p)` 在 MSVC 上编译成对运行库函数 `__RTDynamicCast` 的调用，大致流程：

1. 通过 `p` 的虚表 `[-1]` 找到 Locator，用 `offset` 算出**完整对象**的地址；
2. 拿到完整对象的类层次描述，在 `BaseClassArray` 里**逐项比较** `type_info`，找目标类型；
3. 找到后检查它是不是 public、是否有歧义（同一个基类出现多次），再加上对应偏移返回；找不到就返回 `nullptr`。

单继承、多继承、虚继承各有一条搜索路径，后两种更慢。GCC/Clang（Itanium ABI）思路相同：虚表 `[-1]` 存 `type_info*`，类型信息分为 `__class_type_info`、`__si_class_type_info`（单继承）、`__vmi_class_type_info`（多继承/虚继承），由 `__dynamic_cast` 遍历。

所以 `dynamic_cast` 的开销来自：

- **一次不能内联的函数调用**；
- **遍历继承树并比较类型**，层次越深、越宽、有多继承就越慢；
- **跨模块（DLL / so）时可能退化成字符串比较**：同一个类在不同模块里可能有各自的 `type_info` 对象，地址不同，只能比较类型名字符串。这也是插件式架构里 `dynamic_cast` 偶尔「莫名失败」或变慢的原因（GCC 下用 `RTLD_LOCAL` 加载 so 时尤其常见）。

### 3.4 关闭 RTTI 会怎样（cf7 / cf8 实测）

很多游戏工程用 `/GR-`（MSVC）或 `-fno-rtti`（GCC/Clang）关闭 RTTI。MSVC 下：

```
cf7  dynamic_cast：编译只有 warning C4541（可能导致不可预知的行为），
     运行期抛出异常: Access violation - no RTTI data!
cf8  typeid(*b)  ：编译只有 warning C4541，
     运行直接崩溃 exit=0xC0000005（访问冲突）
```

**只是警告，不是错误**。如果工程没有开「警告视为错误」，这种代码能顺利进包，直到运行到那一行才出问题。GCC/Clang 在 `-fno-rtti` 下对 `dynamic_cast` 下行和 `typeid` 是直接编译报错的，这是两家编译器的一个差异。

---

## 4. `const_cast`：只改 const / volatile

### 4.1 规则

- **唯一**能去掉 `const` / `volatile` 的转换。`static_cast` 和 `reinterpret_cast` 都不行（cf4：**C2440 转换丢失限定符**）。
- **只能改 cv 限定，不能改类型**（cf4：`const_cast<double*>(int*)` 报 C2440）。
- 只能用于指针、引用、成员指针。

### 4.2 合法与非法的分界线

**看对象本身是不是 const，而不是看你拿到的指针是不是 const。**

```cpp
int real = 1;
const int& cref = real;
const_cast<int&>(cref) = 2;           // ✅ 合法：对象本身不是 const。实测 real = 2

const int local = 10;
int* p = const_cast<int*>(&local);
*p = 20;                              // ❌ 未定义行为
```

实测未定义行为的两种表现：

```
[UB] const int local=10 被改写后: local = 10, *p = 20   (编译器常量折叠了 local)
[UB] 改写全局 const kMaxLevel -> 异常码 0xC0000005 (EXCEPTION_ACCESS_VIOLATION，只读页，直接崩溃)
```

- 局部 `const int`：编译器认定它永远是 10，用到的地方直接替换成常量，所以「改了但没改」，内存和代码里的值不一致。
- 全局 `const`：被放进只读段 `.rdata`，操作系统把这一页标成只读，一写就触发访问冲突崩溃。

### 4.3 正当用途

**1. 对接没写 const 的老接口**（C 库、老代码、第三方 SDK），前提是确认它不会修改：

```cpp
size_t LegacyStrLen(char* s);                         // 实际只读不写
LegacyStrLen(const_cast<char*>(name.c_str()));        // 实测返回 6
```

**2. 用 const 版本实现非 const 版本，避免代码重复**（《Effective C++》条款 3）：

```cpp
class Inventory {
public:
    const int& At(size_t i) const { /* 大段校验、日志 */ return items_.at(i); }
    int& At(size_t i) {
        return const_cast<int&>(std::as_const(*this).At(i));   // 先加 const 调用，再把结果去 const
    }
};
```

这里是安全的，因为调用非 const 版本的对象本身一定不是 const。**反过来**（用非 const 版本实现 const 版本）是错误的，因为那样会让 const 对象走进可能修改的代码。C++23 可以用 deducing this 一个函数同时写两个版本，不需要这个技巧了。

### 4.4 不该用 `const_cast` 的地方

| 想做的事 | 正确做法 |
|---|---|
| const 成员函数里修改缓存、计数器、互斥锁 | 成员声明为 `mutable` |
| 修改 `std::set` / `std::map` 的 key | 不能改，会破坏红黑树有序性；C++17 用 `extract` 取出节点改完再插回 |
| 修改字符串字面量 | 永远不行，它在只读段里 |
| 绕过别人接口的 const 承诺 | 修改接口设计 |

---

## 5. `reinterpret_cast`：按位重新解释

### 5.1 能做什么

| 用途 | 例子 |
|---|---|
| 指针 ↔ 足够大的整数 | `reinterpret_cast<uintptr_t>(p)`，往返保证得到原指针（实测相等） |
| 无关类型指针互转 | `reinterpret_cast<const unsigned char*>(&f)` 看对象字节 |
| 函数指针互转 | 对接 `GetProcAddress` / `dlsym` 返回值 |
| 引用互转 | `reinterpret_cast<int&>(f)`，同指针 |

**不能做的**：数值转换（cf6：`reinterpret_cast<float>(int)` 报 C2440，编译器提示「请用 static_cast」）、去 const（cf4）。

它**不调整地址、不检查类型**，只告诉编译器「把这串位当成另一种类型」。转换本身几乎总能编译通过，问题在于**之后通过新指针访问对象**是否合法。

### 5.2 严格别名规则（Strict Aliasing）——深度加分点

标准规定：通过一个类型的左值访问对象，只有以下情况是合法的：

- 对象本身的类型（可加 cv、可以是对应的有/无符号版本）；
- `char`、`unsigned char`、`std::byte`（可以查看**任何**对象的字节）；
- 其他一些情况（如聚合体成员）。

**用 `float*` 读一个 `int` 对象就是未定义行为。** 编译器正是利用这条规则做优化：它假定不同类型的指针不会指向同一块内存。

```cpp
int AliasTest(int* i, float* f) {
    *i = 1;
    *f = 0.0f;
    return *i;      // 编译器可以假定 f 不指向 i，直接 return 1
}
int x = 0;
AliasTest(&x, reinterpret_cast<float*>(&x));   // UB
```

实测 MSVC 返回 `0`，因为 **MSVC 不做基于类型的别名分析**（相当于一直开着 `-fno-strict-aliasing`）；GCC/Clang 在 `-O2` 下通常返回 `1`。**同一份代码在 Windows 上正常、在 Android/iOS（Clang）上出错**，这对多平台游戏客户端是真实存在的风险。

### 5.3 想看 float 的位模式：`memcpy` 或 `std::bit_cast`

```cpp
float f = 1.0f;
auto bytes = reinterpret_cast<const unsigned char*>(&f);   // ✅ 合法：unsigned char 可别名任何对象
// 实测：00 00 80 3F（小端）

uint32_t bits;
std::memcpy(&bits, &f, sizeof f);                          // ✅ 合法，编译器会优化成一条 mov
uint32_t bits2 = std::bit_cast<uint32_t>(f);               // ✅ C++20，还能用于 constexpr
// 实测都是 0x3F800000
uint32_t bad = *reinterpret_cast<uint32_t*>(&f);           // ❌ 违反严格别名
```

游戏领域的经典例子是《雷神之锤 III》的快速平方根倒数，原版代码是 `i = *(long*)&y;`，就是违反严格别名的写法。现代安全写法：

```cpp
float Q_rsqrt(float number) {
    float x2 = number * 0.5f, y = number;
    uint32_t i = std::bit_cast<uint32_t>(y);   // 原版：i = *(long*)&y;
    i = 0x5f3759df - (i >> 1);
    y = std::bit_cast<float>(i);
    return y * (1.5f - x2 * y * y);
}
// 实测 Q_rsqrt(4.0f) = 0.499154，精确值 0.5
```

> 用 `union` 做类型双关（写 `float` 成员再读 `uint32_t` 成员）在 C 里合法，在 **C++ 里是未定义行为**，虽然主流编译器都支持。面试时说「C++ 中推荐 `std::bit_cast` 或 `memcpy`」最稳妥。

### 5.4 指针 ↔ 整数的实际用途

- **哈希和排序**：用对象地址作为 `unordered_map` 的 key。
- **指针打标记**：对象至少 8 字节对齐，地址低 3 位恒为 0（实测 `addr & 7 = 0`），可以借来存 3 个标志位，用之前清零。一些 GC、无锁数据结构、引擎的弱引用句柄都这样做。
- **对齐计算**：`reinterpret_cast<uintptr_t>(p) % alignof(T)`，内存分配器里很常见。

---

## 6. C 风格转换到底做了什么

`(T)expr` 会**按顺序尝试**，用第一个能编译通过的：

1. `const_cast<T>(expr)`
2. `static_cast<T>(expr)`（**且忽略访问控制**）
3. `static_cast` + `const_cast`
4. `reinterpret_cast<T>(expr)`
5. `reinterpret_cast` + `const_cast`

实测：

```
(Unit*)&sec 绕过 private 继承: hp = 100          ← static_cast 会报 C2243（cf5）
(int*)&ci 悄悄去掉了 const                        ← 相当于 const_cast
(int)3.7f = 3, *(int*)&3.7f = 0x406CCCCD          ← 一个是 static_cast，一个是 reinterpret_cast
```

**最隐蔽的坑：不完整类型。** 如果某个文件里只有 `class Derived;` 前置声明，看不到继承关系，`(Base*)derivedPtr` 不会报错，而是**悄悄变成 `reinterpret_cast`**，多继承时就少加了偏移。同样的代码，在包含了完整定义的文件里是正确的 `static_cast`。换成 `static_cast` 的话，看不到定义时会直接编译报错。

> 项目规范建议：开启 `-Wold-style-cast`（GCC/Clang）或 clang-tidy 的 `cppcoreguidelines-pro-type-cstyle-cast`，禁止 C 风格转换。函数式转换 `int(x)` 和 C 风格转换完全等价，同样危险。`int{x}`（列表初始化）则会**禁止窄化**，是更安全的写法。

---

## 7. 周边：智能指针 cast、`std::bit_cast`、`std::move` 的真身

### 7.1 智能指针要用专门的 cast

```cpp
std::shared_ptr<Unit> u = std::make_shared<Warrior>();
auto w = std::dynamic_pointer_cast<Warrior>(u);   // 实测：非空，use_count = 2（共享同一个控制块）
auto m = std::dynamic_pointer_cast<Mage>(u);      // 实测：空
auto s = std::static_pointer_cast<Warrior>(u);    // 实测：use_count = 3
// 还有 const_pointer_cast、reinterpret_pointer_cast（C++17）

std::shared_ptr<Warrior> bad(static_cast<Warrior*>(u.get()));   // ❌ 新建第二个控制块 → 重复释放
```

`xxx_pointer_cast` 内部用的是 `shared_ptr` 的**别名构造函数**：新指针和原指针共享引用计数，只是存的裸指针不同（多继承时地址可能不同）。`unique_ptr` 没有对应函数，需要手动 `release()` 后转换再重新包装。

### 7.2 你天天在用的 cast

| 工具 | 本质 |
|---|---|
| `std::move(x)` | `static_cast<std::remove_reference_t<T>&&>(x)` |
| `std::forward<T>(x)` | `static_cast<T&&>(x)` |
| `std::bit_cast<To>(from)`（C++20） | 按位复制成新对象，要求大小相同且都可平凡复制，可用于 `constexpr` |
| `std::chrono::duration_cast` | 时间单位转换（向 0 截断） |
| `gsl::narrow_cast` / `gsl::narrow` | 显式窄化 / 窄化时检查数值是否丢失 |
| `std::to_underlying(e)`（C++23） | 等价于 `static_cast<std::underlying_type_t<E>>(e)` |
| UE `Cast<T>` / `CastChecked<T>` / `ExactCast<T>` | 引擎自研类型系统的转换，见第 9 章 |

---

## 8. 性能实测：类型判断到底多贵

场景：1024 个对象（`Hero` 和 `Prop` 各一半），循环 10000 轮，共 1024 万次判断。继承关系：`Entity → Actor → Pawn → Character → Hero`（`Hero` 还继承了 `IDamageable`），`Entity → Actor → Prop`。

```
方法                                         耗时        每次      命中
static_cast<Hero*>（不检查，仅基线）           3.7 ms   0.36 ns   10240000
castFlags & CF_Hero（位标志）                  3.7 ms   0.37 ns    5120000
e->AsHero()（虚函数）                         11.4 ms   1.11 ns    5120000
typeid(*e) == typeid(Hero)（只能精确匹配）     21.9 ms   2.14 ns    5120000
dynamic_cast<Actor*>（目标是浅层基类）        135.0 ms  13.18 ns   10240000
dynamic_cast<Hero*>（下行 4 层）              146.6 ms  14.32 ns    5120000
dynamic_cast<IDamageable*>（交叉转换）        191.6 ms  18.71 ns    5120000
```

解读：

- `dynamic_cast` 比虚函数慢约 **13 倍**，比位标志慢约 **40 倍**；交叉转换走多继承搜索路径，最慢。
- 即使目标是很浅的基类 `Actor`，也要 13 ns，说明大头是**函数调用 + 找完整对象 + 搜索基类数组**的固定开销。
- `typeid ==` 很快，但**只能判断精确类型**，`Hero` 的派生类就判断不出来，不能替代 `dynamic_cast`。
- **14 ns 单次看并不慢**，问题在于规模：每帧对上万个对象做多次判断，就是零点几毫秒。在手机上，16.6 ms 的帧预算里这不是小数目。另外别忘了跨 DLL 时可能的字符串比较。

结论：**偶尔用一次 `dynamic_cast` 没问题；每帧、每个对象都调用的热路径上不要用。** 更深层的问题往往是设计：需要频繁向下转换，通常说明应该用虚函数或者组件化来表达行为差异。

---

## 9. 游戏客户端实战

### 9.1 为什么很多游戏引擎关闭 RTTI

| 原因 | 说明 |
|---|---|
| 包体与内存 | 每个多态类都要生成 `type_info` 和修饰名字符串，类多的时候体积可观，移动端敏感 |
| 性能 | `dynamic_cast` 的开销见第 8 章 |
| 跨模块不可靠 | DLL / so 之间 `type_info` 可能不唯一 |
| 引擎有更好的方案 | 引擎本来就需要反射（序列化、编辑器、蓝图、网络同步），反射系统自带类型信息，顺便提供更快的转换 |

例子：**UE** 默认关闭 RTTI（模块的 `bUseRTTI = false`），UObject 体系用 `Cast<T>`；**LLVM** 也用 `-fno-rtti` 编译，自己实现了 `isa<>` / `cast<>` / `dyn_cast<>`。

### 9.2 UE 的 `Cast<T>`

| 函数 | 失败时 | 用途 |
|---|---|---|
| `Cast<T>(Obj)` | 返回 `nullptr` | 类型不确定时 |
| `CastChecked<T>(Obj)` | 断言崩溃（可配置成在 Shipping 版本里不检查） | 逻辑上一定是这个类型 |
| `ExactCast<T>(Obj)` | 返回 `nullptr` | 精确类型匹配，不接受派生类 |

实现上不走 C++ RTTI，而是查 UObject 的 `UClass` 层次：

- **常用的引擎核心类**（Actor、Pawn、PlayerController、各类 Component 等，具体见 `EClassCastFlags` 枚举）在 `UClass` 里有一个 `ClassCastFlags` 位掩码，`Cast` 直接按位与，O(1)，相当于第 8 章里的「位标志」方案；
- 其他类调用 `IsChildOf`，引擎的 `FStructBaseChain` 记录每个类的整条父类链和深度，判断只需一次数组下标比较，也接近 O(1)。

面试时可以说：**UE 的 Cast 是基于反射系统的 O(1) 类型判断，比 `dynamic_cast` 快，而且关闭 RTTI 也能用。**

### 9.3 自研类型系统：LLVM 风格的 `dyn_cast`

不依赖 RTTI、支持继承判断、O(1)，思路是**按继承树深度优先顺序给每个类编号，一个类和它所有子类的编号是连续区间**：

```cpp
struct Entity {
    enum Kind : uint8_t {
        K_Actor,                     // Actor 区间：[K_Actor, K_LastActor]
          K_Pawn,                    // Pawn 区间：[K_Pawn, K_LastPawn]
            K_Character,             // Character 区间：[K_Character, K_LastCharacter]
              K_Hero,
            K_LastCharacter = K_Hero,
          K_LastPawn = K_Hero,
          K_Prop,
        K_LastActor = K_Prop,
    };
    const Kind kind;
    virtual ~Entity() = default;
protected:
    explicit Entity(Kind k) : kind(k) {}
};

struct Pawn : Actor {
    static bool classof(const Entity* e) { return e->kind >= K_Pawn && e->kind <= K_LastPawn; }
    // ...
};

template <class To, class From> bool isa(const From* p) { return To::classof(p); }
template <class To, class From> To* dyn_cast(From* p) {
    return p && To::classof(p) ? static_cast<To*>(p) : nullptr;   // 判断过了，static_cast 是安全的
}
```

已在 `/GR-` 下编译验证：`isa<Pawn>(hero)=1`、`isa<Pawn>(prop)=0`、`isa<Actor>(prop)=1`、`dyn_cast<Character>(hero)` 非空、`dyn_cast<Hero>(prop)` 为空。

**本质**：先用廉价的类型标记判断，确认之后再 `static_cast`。这是游戏代码里 `static_cast` 下行转换最常见的正确用法。缺点是新增类型要维护枚举顺序，适合类型集合相对稳定的体系（如 AST、实体类型、消息类型）。

### 9.4 ECS / 组件系统里的 `GetComponent<T>`

```cpp
template <class T>
T* GameObject::GetComponent() {
    for (auto& c : components_)
        if (c->TypeId() == T::StaticTypeId())       // 编译期生成的类型 ID
            return static_cast<T*>(c.get());
    return nullptr;
}
```

`StaticTypeId()` 常见实现：每个类型一个静态变量的地址，或者编译期字符串哈希。查找结果最好在 `Start/Awake` 里缓存，不要每帧查。

### 9.5 网络包 / 资源文件解析：`reinterpret_cast` 套结构体

很多老代码这样解析收到的数据：

```cpp
auto* h = reinterpret_cast<const HeaderPacked*>(buf + 1);   // 直接把字节流当结构体
```

实测在 x64 上能正确读出 `msgId=0x1234 seq=0x12345678 len=16`，即使头部地址 `% 4 = 1`（未对齐）。但它有四个隐患：

| 隐患 | 说明 |
|---|---|
| 对齐 | x86/x64 硬件容忍未对齐访问；ARM（手机）上部分指令（如 LDRD、LDM、部分 NEON 指令）要求对齐，可能直接崩溃或变慢 |
| 结构体填充 | 实测 `sizeof(HeaderNatural)=12`，`#pragma pack(1)` 后为 8。两端编译器、平台、打包参数不同，布局就不同 |
| 字节序 | 网络字节序通常是大端，x86 和 ARM 一般都是小端，需要 `ntohl` 等转换 |
| 严格别名 | `unsigned char` 缓冲区通过结构体指针访问，严格来说不合法 |

推荐写法：**`memcpy` 到本地结构体**（实测结果相同，编译器会优化成几条普通 mov，没有额外开销），或者逐字段反序列化；更正规的做法是用 Protobuf / FlatBuffers 这类序列化库。

### 9.6 其他常见场景

| 场景 | 涉及的 cast | 注意 |
|---|---|---|
| Lua / 物理引擎 / UI 的 `void* userData` | `static_cast` 往返 | 必须转回**放进去时的确切类型**，多继承尤其注意 |
| `GetProcAddress` 加载 DLL 函数 | `reinterpret_cast` 函数指针 | 签名必须完全一致，否则调用约定错乱 |
| 图形 API 句柄（Vulkan 的 `uint64_t` 句柄、D3D 指针） | `reinterpret_cast` | 封装成强类型句柄类 |
| 对象池 / 内存分配器 | `reinterpret_cast<uintptr_t>` 做对齐 | 新对象要用 placement new，不能直接 `reinterpret_cast` 后使用（C++20 起对隐式生存期类型有放宽） |
| 枚举和网络/配置表的整数 | `static_cast` | 先校验范围 |
| 帧同步的定点数 | `static_cast` 浮点转定点 | 截断和舍入方式要两端一致，否则不同步 |

---

## 10. 面试注意点

### 10.1 易错说法对照

| ❌ 易错说法 | ✅ 正确说法 |
|---|---|
| `static_cast` 是类型安全的 | 它只在编译期检查转换是否「合理」，下行转换不检查实际类型，类型错误就是 UB |
| 指针转换只是换个类型，地址不变 | 多继承下 `static_cast` / 隐式转换会调整地址，`reinterpret_cast` 不调整 |
| `dynamic_cast` 失败会抛异常 | 指针失败返回 `nullptr`，**引用**失败才抛 `std::bad_cast` |
| `dynamic_cast` 能用于任何类 | 下行和交叉转换要求源类型是多态类型（有虚函数），否则编译失败 |
| `const_cast` 可以修改 const 变量 | 只有对象本身非 const 时修改才合法；修改真正的 const 对象是 UB（实测：常量折叠或只读页崩溃） |
| `reinterpret_cast` 可以把 float 转成 int | 不能做数值转换（编译报错）；看位模式用 `std::bit_cast` / `memcpy` |
| 用 `reinterpret_cast` 指针读 float 的位是安全的 | 违反严格别名，MSVC 上碰巧正常，GCC/Clang 优化后可能出错 |
| C 风格转换就是 `static_cast` | 它按顺序尝试五种组合，可能悄悄变成 `reinterpret_cast` 或 `const_cast`，还无视访问控制 |
| 关闭 RTTI 后 `dynamic_cast` 会编译报错 | MSVC 只给 C4541 警告，运行期才抛异常或崩溃；GCC/Clang 才是编译报错 |
| `typeid` 比较可以替代 `dynamic_cast` | `typeid ==` 只判断精确类型，不认派生类 |

### 10.2 坑位清单

1. `static_cast` 下行转换类型错了也不报错，数据悄悄错乱。
2. 多继承下用 `reinterpret_cast` 或经过 `void*` 转成别的基类，少了偏移，调错虚函数。
3. 带偏移的 `static_cast` 会对空指针特判，不是完全零开销。
4. `dynamic_cast` 在构造/析构函数里看到的是当前这一层的类型。
5. 关闭 RTTI 后 MSVC 只警告不报错。
6. 跨 DLL 的 `dynamic_cast` 可能失败或变慢。
7. 修改真正的 const 对象：常量折叠或只读页崩溃。
8. `reinterpret_cast` 违反严格别名，在不同编译器上表现不同。
9. `reinterpret_cast` 套网络包结构体：对齐、填充、字节序问题。
10. 从裸指针重新构造 `shared_ptr` 导致重复释放，应使用 `xxx_pointer_cast`。
11. 不完整类型下 C 风格转换悄悄变成 `reinterpret_cast`。
12. `static_cast` 转枚举不检查范围。
13. 浮点转整数是向 0 截断，超出范围是 UB。

### 10.3 自测题

**Q1：四种 cast 分别是什么，各用在什么场景？**
`static_cast` 做编译期的合理转换（数值、枚举、void*、上行/下行）；`dynamic_cast` 做运行期带检查的多态转换；`const_cast` 去掉或加上 cv 限定；`reinterpret_cast` 做指针、引用、整数之间的位重解释。

**Q2：`static_cast` 和 `dynamic_cast` 做下行转换有什么区别？**
`static_cast` 编译期按静态类型计算偏移，不检查实际类型，错了是 UB，开销为零；`dynamic_cast` 运行期查 RTTI，失败返回 `nullptr` 或抛 `bad_cast`，要求多态类型，有可观开销。

**Q3：`dynamic_cast` 是怎么实现的？为什么慢？**
通过虚表 `[-1]` 拿到类型信息（MSVC 是 CompleteObjectLocator，Itanium 是 `type_info`），先找到完整对象，再在类层次描述里逐个比较基类的 `type_info`，找到后加偏移。慢在不能内联的函数调用、遍历继承树、多继承/虚继承的额外路径，跨模块还可能比较字符串。

**Q4：多继承下 `static_cast` 和 `reinterpret_cast` 的区别？**
`static_cast` 会加上基类子对象的偏移（并对空指针特判），`reinterpret_cast` 地址不变。用后者得到的基类指针调虚函数会查错虚表。

**Q5：什么时候用 `const_cast` 是合法的？**
对象本身不是 const、只是通过 const 指针或引用访问时，去 const 后修改是合法的。典型用途：对接没写 const 的老接口；用 const 版本成员函数实现非 const 版本。

**Q6：如何正确地获取 float 的二进制表示？**
`std::bit_cast<uint32_t>(f)`（C++20）或 `memcpy`；用 `unsigned char*` 逐字节查看也合法。不能用 `*reinterpret_cast<uint32_t*>(&f)`，违反严格别名；C++ 中 union 双关也是 UB。

**Q7：C 风格转换有什么问题？**
按 const → static → static+const → reinterpret → reinterpret+const 的顺序尝试，意图不清、可能悄悄做位重解释或去 const、无视访问控制、遇到不完整类型时退化成 `reinterpret_cast`，也难以搜索。

**Q8：游戏引擎为什么常关闭 RTTI？不用 `dynamic_cast` 怎么做类型判断？**
为了减小包体、提升性能、避免跨模块问题，引擎本身也有反射系统。替代方案：UE 的 `Cast<T>`（ClassCastFlags 位掩码 + 父类链数组）、LLVM 风格的 Kind 区间 + `classof`、虚函数 `AsXxx()`、组件系统的类型 ID。都是「廉价判断 + `static_cast`」。

**Q9：`dynamic_cast<void*>(p)` 有什么用？**
返回 `p` 所指对象的最派生类型的首地址，可以判断两个不同接口指针是否指向同一个对象。

**Q10：为什么 `static_cast` 不能从虚基类下行，而 `dynamic_cast` 可以？**
虚基类子对象在派生对象中的位置不固定（取决于最终的完整类型），编译期算不出偏移，只能运行期从虚表里查。

**Q11：对 `shared_ptr` 做类型转换要注意什么？**
用 `std::static_pointer_cast` / `dynamic_pointer_cast` / `const_pointer_cast`，它们共享原控制块。用 `get()` 得到的裸指针新建 `shared_ptr` 会产生第二个控制块，导致重复释放。

**Q12：网络包解析时直接 `reinterpret_cast` 成结构体有什么风险？**
对齐（ARM 上可能崩溃）、结构体填充不一致、字节序、严格别名。推荐 `memcpy` 到本地结构体或逐字段反序列化。

### 10.4 面试表达建议

- 回答「四种 cast」时，先用一张表说清楚四者的分工，再按**风险从低到高**展开，最后落到工程实践：「我们项目关闭了 RTTI，用引擎的 Cast」。
- **多继承指针调整**是区分「背过」和「理解」的关键题，最好能在白板上画出对象布局。
- 能说出一个**实测数字**（如「MSVC 上 `dynamic_cast` 约 14 ns，是虚函数调用的十几倍」）比单纯说「它很慢」更有说服力。
- 提到 `reinterpret_cast` 时顺带说出**严格别名**和 **`std::bit_cast`**，是很好的加分点。

---

## 附：编译诊断速查（MSVC 14.44，详见 `compile_fail/compile_errors.txt`）

| 文件 | 写法 | 结果 |
|---|---|---|
| cf1 | `static_cast<ITickable*>(IRenderable*)`、`static_cast<int*>(float*)` | C2440 指向的类型不相关 |
| cf2 | `static_cast` 从虚基类下行 | C2635 暗含了从虚拟基类的转换 |
| cf3 | 对非多态类型 `dynamic_cast` 下行 | C2683 不是多态类型 |
| cf4 | `static_cast`/`reinterpret_cast` 去 const；`const_cast` 改类型 | C2440 转换丢失限定符 / 指向的类型不相关 |
| cf5 | `static_cast` 转到 private 基类 | C2243 转换存在，但无法访问 |
| cf6 | `reinterpret_cast<float>(int)`、`reinterpret_cast<int>(double)` | C2440，提示改用 static_cast |
| cf7 | `/GR-` 下 `dynamic_cast` | 仅 C4541 警告；运行期抛 `Access violation - no RTTI data!` |
| cf8 | `/GR-` 下 `typeid(*b)` | 仅 C4541 警告；运行崩溃 0xC0000005 |
