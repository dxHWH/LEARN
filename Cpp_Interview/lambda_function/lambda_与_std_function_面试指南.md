# C++ lambda 表达式与 `std::function` 完全指南（游戏客户端面试向）

> 配套代码：同目录 `lambda_function_demo.cpp`（已用 MSVC 14.44 / VS2022 x64 编译运行，文中“实测”均来自它，完整输出见 `run_output.txt`）
> 编译：`cl /std:c++latest /EHsc /O2 /utf-8 lambda_function_demo.cpp`，或 `g++ -std=c++23 -O2 lambda_function_demo.cpp`

---

## 提问记录

| 项 | 内容 |
|---|---|
| 时间 | 2026-10-09 |
| 提问 | 游戏客户端面试准备：详细地为我解析 C++ 中的 lambda 表达式、std::function，适当控制深度，增加可读性，并点出面试需要注意的问题。你需要详细回答我的问题，并做出深层次的技术解答，并将每一次提问的内容和回答的内容都落盘为 md 文档。 |
| 回答 | 即本文第 0～9 章 |

---

## 目录

0. 30 秒面试版答案（先背这个）
1. lambda 的本质：编译器替你写了一个类
2. 捕获：最容易出事的部分
3. 闭包类型的“性格”：唯一类型、大小、函数指针转换、泛型
4. `std::function`：类型擦除的可调用对象容器
5. 底层原理：手写一个 mini `std::function` + SBO 实测
6. 性能：模板 vs `std::function` vs 函数指针
7. 递归 lambda 的三种写法
8. 游戏客户端实战：事件回调、生命周期、热路径
9. 面试高频坑 + 自测题

---

## 0. 30 秒面试版答案（先背这个）

> **lambda** 是 C++11 引入的匿名函数语法。编译器遇到 lambda 时，会生成一个**独一无二的匿名类（闭包类型）**，把捕获的变量变成它的**成员变量**，把函数体变成它的 **`operator()`（默认是 `const` 的）**，然后在原地构造一个该类的对象（闭包对象）。所以 lambda 本质就是一个**编译器自动生成的仿函数**，调用可以被完全内联，**本身没有额外开销**。
>
> **`std::function<R(Args...)>`** 是 C++11 `<functional>` 里的**通用可调用对象包装器**。它用**类型擦除**把函数指针、lambda、仿函数、成员函数指针等“签名兼容但类型各异”的东西装进**同一种类型**里，方便存进容器、作为成员变量、跨模块传递回调。代价是：**一次间接调用（通常是虚调用），阻止内联；闭包超过小对象缓冲区时会有堆分配；对象本身也更大**（MSVC x64 实测 64 字节）。
>
> **一句话选型**：能用模板/`auto` 接住 lambda 就别用 `std::function`；需要“存起来、类型统一、运行时替换”时才用 `std::function`。
>
> **最常考的坑**：引用捕获悬垂、`[=]` 实际捕获的是 `this` 指针、`std::function` 要求可拷贝（装不下 `unique_ptr`）、调用空 `std::function` 抛 `bad_function_call`、回调里捕获 `shared_ptr` 造成循环引用。

后面每一章都是把这段话里的一句展开讲透。

---

## 1. lambda 的本质：编译器替你写了一个类

### 1.1 语法全貌

```cpp
[捕获列表] (参数列表) 说明符 -> 返回类型 { 函数体 }
//  ①          ②         ③        ④           ⑤
```

| 部分 | 说明 | 能否省略 |
|---|---|---|
| ① 捕获列表 `[]` | 决定闭包里装哪些外部变量 | **不能**，这是 lambda 的标志 |
| ② 参数列表 `()` | 和普通函数一样，C++14 起支持默认参数、`auto` 参数 | 无参数且无③④时可省 |
| ③ 说明符 | `mutable`、`constexpr`(C++17)、`consteval`(C++20)、`static`(C++23)、`noexcept` | 可省 |
| ④ 返回类型 | 不写则按 `auto` 规则从 `return` 推导 | 可省 |
| ⑤ 函数体 | | 不能 |

最短的合法 lambda：`[]{}`。

### 1.2 核心心智模型：手写等价类

```cpp
int base = 10;
int scale = 2;
auto f = [base, &scale](int x) { return base + x * scale; };
int r = f(5);
```

编译器生成的东西大致等价于：

```cpp
class __lambda_7_10 {                 // 名字由编译器随机生成，你写不出来
public:
    __lambda_7_10(int b, int& s) : base(b), scale(s) {}
    int operator()(int x) const {     // 注意：默认是 const 成员函数
        return base + x * scale;
    }
private:
    int  base;                        // 值捕获 -> 拷贝一份成员
    int& scale;                       // 引用捕获 -> 引用成员（实现上通常是指针）
};

__lambda_7_10 f{base, scale};         // 闭包对象（closure object）
int r = f.operator()(5);
```

两个术语别混：

- **闭包类型（closure type）**：编译器生成的那个类，每个 lambda 表达式一个，**独一无二**。
- **闭包对象（closure）**：那个类在运行时的实例，就是 `f`。

理解了这一点，下面所有规则都能**推出来**而不是背出来：

| 现象 | 用“它是个类”来解释 |
|---|---|
| 值捕获的变量在 lambda 里不能改 | `operator()` 是 `const` 的，不能改成员 |
| 加 `mutable` 就能改 | 去掉 `operator()` 的 `const` |
| 值捕获是“创建时的快照” | 成员在构造闭包对象时就拷贝好了 |
| 拷贝 lambda 会拷贝内部状态 | 就是拷贝一个对象 |
| `sizeof(lambda)` = 捕获成员之和 | 就是这个类的大小 |
| 两个一模一样的 lambda 类型不同 | 编译器生成了两个不同的类 |
| lambda 调用能被内联 | 调用的是一个已知类型的非虚成员函数 |

> 想亲眼看生成的类：把代码贴到 **cppinsights.io**，面试时提到这个工具会加分。

---

## 2. 捕获：最容易出事的部分

### 2.1 捕获方式一览

| 写法 | 含义 | 标准 |
|---|---|---|
| `[]` | 不捕获任何东西 | C++11 |
| `[x]` | 按值捕获 `x` | C++11 |
| `[&x]` | 按引用捕获 `x` | C++11 |
| `[=]` | 默认按值捕获**用到的**变量 | C++11 |
| `[&]` | 默认按引用捕获**用到的**变量 | C++11 |
| `[=, &x]` / `[&, x]` | 默认方式 + 个别例外 | C++11 |
| `[this]` | 捕获 `this` **指针** | C++11 |
| `[*this]` | 按值拷贝**整个当前对象** | C++17 |
| `[y = expr]` | **初始化捕获**：新建成员 `y`，用任意表达式初始化 | C++14 |
| `[p = std::move(ptr)]` | 初始化捕获 + 移动，常用于 `unique_ptr` | C++14 |
| `[&r = x]` | 引用版初始化捕获 | C++14 |
| `[...xs = std::move(args)]` | 参数包初始化捕获 | C++20 |

要点：

- `[=]`、`[&]` **只捕获函数体里真正用到的变量**，不是把整个作用域都塞进去。
- **全局变量、`static` 变量、`thread_local` 变量不需要也不能捕获**，lambda 里直接访问的就是那个变量本身（实测：无捕获 lambda 直接修改了 `static int s_hits`）。所以 `[=]` 看起来“值捕获了”一个静态变量，其实改的是全局那一份。
- `constexpr` 常量如果只读取值而不取地址，也不需要捕获。

### 2.2 值捕获 = 创建时快照；引用捕获 = 调用时读取

```cpp
int x = 10;
auto byVal = [x]  { return x; };
auto byRef = [&x] { return x; };
x = 20;
// 实测：byVal() = 10, byRef() = 20
```

**关键时间点**：值捕获发生在 **lambda 表达式求值（创建闭包对象）时**，而不是调用时。

### 2.3 `mutable` 与“带状态的 lambda”

```cpp
auto counter = [n = 0]() mutable { return ++n; };
counter(); counter();
counter();          // 3
auto copy = counter;
copy();             // 4
counter();          // 4   —— 两份独立状态
```

`mutable` 修改的是**闭包自己的那份拷贝**，不会影响外部变量。这一点也解释了一个常见坑：把带状态的 lambda **按值**传给算法（如 `std::for_each`、`std::generate`），算法内部改的是副本，你手里的那份不变。

### 2.4 引用捕获的悬垂：最高频的 bug

```cpp
std::function<int()> MakeBad() {
    int local = 42;
    return [&local] { return local; };   // ❌ local 函数返回后就销毁了
}
// 调用返回的函数 -> 读已销毁的栈内存 -> 未定义行为（可能“看起来正常”，最可怕）
```

判断标准只有一个：**闭包的生命周期会不会超过被引用对象的生命周期？**

| 场景 | 引用捕获安全吗 |
|---|---|
| 传给 `std::sort` / `std::find_if` 等**同步**算法立刻用掉 | ✅ 安全 |
| 作为返回值返回 | ❌ 危险 |
| 存进成员变量 / 事件系统 / 定时器 | ❌ 危险 |
| 丢给线程池、异步加载、网络回调 | ❌ 非常危险 |

### 2.5 `this` 捕获陷阱（面试必问）

在成员函数里写 `[=]`，**捕获的不是成员变量的拷贝，而是 `this` 指针**：

```cpp
struct Hero {
    int hp = 100;
    auto GetterByThis()  { return [this]  { return hp; }; } // 等价于 C++17 前的 [=]
    auto GetterByCopy()  { return [*this] { return hp; }; } // C++17：拷贝整个对象
    auto GetterByField() { return [hp = hp] { return hp; }; } // 只拷需要的字段（推荐）
};
// 实测：hp 改成 50 后  [this]=50  [*this]=100  [hp=hp]=100
// 实测：sizeof         [this]=8   [*this]=4    [hp=hp]=4
```

原因：成员 `hp` 在函数体里其实是 `this->hp`，lambda 要用它就得捕获 `this`。所以：

- **对象销毁后再调用 → 悬垂 `this`**，这是游戏里“异步回调回来时 Actor/GameObject 已经没了”崩溃的头号来源。
- C++20 起，`[=]` **隐式捕获 `this` 被废弃**，要求显式写 `[=, this]` 或 `[=, *this]`，就是为了让人意识到这里有个指针。
- `[*this]` 会拷贝整个对象，对象大时开销大，且回调里改的也只是副本。实战最推荐**初始化捕获只拷需要的字段**，或者配合 `weak_ptr`（见第 8 章）。

### 2.6 初始化捕获（C++14）：解决“只能拷贝或引用”的局限

```cpp
auto up = std::make_unique<int>(42);
auto task = [p = std::move(up)] { return *p; };   // 把所有权移进闭包
// 实测：task() = 42，原 up 为空；闭包 is_copy_constructible = 0
```

用途：移动只可移动的对象、捕获表达式结果（`[len = s.size()]`）、给捕获改名、避免拷贝大对象。

### 2.7 为什么不推荐默认捕获 `[=]` / `[&]`

《Effective Modern C++》条款 31 的结论：**避免默认捕获模式**。

- `[&]`：太容易悬垂，而且读代码时看不出捕获了什么。
- `[=]`：给人“自给自足、很安全”的错觉，但它会**隐式捕获 `this`**，而静态变量又根本没被拷贝。

工程里建议**显式列出每一个捕获**，代码审查时一眼就能看出生命周期风险。

---

## 3. 闭包类型的“性格”

### 3.1 每个 lambda 都是独一无二的类型

```cpp
auto f1 = []{};
auto f2 = []{};
std::is_same_v<decltype(f1), decltype(f2)>   // 实测：0（false）
```

后果：

- **不能**写 `std::vector<???> v{f1, f2};`，因为没有共同类型 → 这正是 `std::function` 存在的理由。
- 想接住 lambda，只能用 `auto`、模板参数，或者类型擦除容器（`std::function`）。

### 3.2 闭包对象的大小（实测，x64）

| lambda | `sizeof` | 解释 |
|---|---|---|
| `[]{}` | 1 | 空类最小 1 字节 |
| `[a]`（int） | 4 | 一个 int 成员 |
| `[&a]` | 8 | 实现为指针 |
| `[a, d]`（int + double） | 16 | 4 + 填充 4 + 8 |
| `[&]`（用到 a、d） | 16 | 两个指针 |
| `[s]`（std::string） | 32 | 拷贝了一个 `std::string` 对象 |

> 值捕获大对象会真的拷贝它，这在每帧都创建回调的地方是实打实的开销。

### 3.3 无捕获 lambda 可以转成函数指针

```cpp
int (*fp)(int) = [](int x) { return x * 2; };  // 隐式转换
auto fp2 = +[](int x) { return x + 1; };        // 一元 + 强制触发转换，fp2 的类型是 int(*)(int)
```

- **只有无捕获**的 lambda 能转（因为没有状态，`operator()` 不依赖 `this`）。
- 典型用途：对接 C 接口（`qsort`、Win32 回调、Lua/C 绑定、`pthread_create`）。
- 有捕获的 lambda 要传给 C 回调，只能借助 `void* userdata` 参数把闭包地址带过去。

### 3.4 C++20：无捕获 lambda 可默认构造、可赋值

```cpp
auto cmp = [](int x, int y) { return x > y; };
std::set<int, decltype(cmp)> desc{3, 1, 2};   // C++20 起不必再把 cmp 传给构造函数
// 实测遍历：3 2 1
```

C++20 之前，闭包类型**没有默认构造函数，拷贝赋值也被删除**，所以 `std::set<int, decltype(cmp)> s;` 会编译失败。有捕获的 lambda 至今仍不可默认构造。

### 3.5 泛型 lambda / 模板 lambda / constexpr lambda

```cpp
auto add = [](auto a, auto b) { return a + b; };          // C++14：operator() 是成员函数模板
auto bytes = []<class T>(const std::vector<T>& v) {       // C++20：显式模板参数
    return v.size() * sizeof(T);
};
constexpr auto sq = [](int v) { return v * v; };           // C++17：满足条件自动 constexpr
std::array<int, sq(3)> arr{};                              // 编译期使用
```

泛型 lambda 的 `auto` 参数本质是：

```cpp
struct __lambda {
    template <class T1, class T2>
    auto operator()(T1 a, T2 b) const { return a + b; }
};
```

模板 lambda 的价值：拿到 `T` 本身（例如上面的 `sizeof(T)`），或者约束“两个参数必须同类型”。

### 3.6 实用小技巧：IIFE 初始化 const 变量

```cpp
const auto config = [&] {
    Config c;
    if (isMobile) c.shadowQuality = 1;
    else          c.shadowQuality = 3;
    return c;
}();   // 立即调用
```

复杂的初始化逻辑依然能让变量保持 `const`。

---

## 4. `std::function`：类型擦除的可调用对象容器

### 4.1 它解决什么问题

lambda 每个类型都不同，但我们经常需要：

- 把一堆回调存进 `std::vector`（事件系统、命令队列）；
- 作为**类成员**保存回调（按钮点击、动画结束、加载完成）；
- 在**非模板**接口、跨编译单元/动态库边界传递回调；
- 运行时**替换**行为（策略、AI 状态切换）。

`std::function<R(Args...)>` 的承诺是：**只要能用 `Args...` 调用、返回值能转成 `R`，什么都能装。**

```cpp
std::function<int(int, int)> op;
op = [](int a, int b) { return a + b; };       // lambda
op = std::plus<int>{};                          // 仿函数
op = &SomeFreeFunction;                         // 函数指针
std::function<int(const Skill&, int)> cast = &Skill::Cast;  // 成员函数指针（第一个参数是对象）
// 实测：cast(Skill{3}, 5) = 305
```

### 4.2 必须知道的行为

| 行为 | 说明 |
|---|---|
| 空状态 | 默认构造或赋 `nullptr` 后为空，`if (fn)` 判断 |
| 调用空 function | **抛 `std::bad_function_call`**（实测 `what() = "bad function call"`），不是空指针崩溃 |
| 拷贝 | **深拷贝**内部的可调用对象（实测：拷贝一个持 128 字节闭包的 function → 1 次堆分配） |
| 移动 | 堆上的直接转移指针（实测 0 次分配）；SBO 里的需要移动构造闭包 |
| 比较 | **只能和 `nullptr` 比**，两个 `std::function` 之间不能 `==`（无法判断“是不是同一个回调”） |
| 取回原对象 | `target<T>()` 返回 `T*`（类型不对返回 `nullptr`），`target_type()` 返回 `typeid` |
| 对可调用对象的要求 | 必须**可拷贝构造**，即使你从不拷贝这个 function |

### 4.3 为什么装不下 `unique_ptr` 闭包

```cpp
auto task = [p = std::make_unique<int>(42)] { return *p; };
std::function<int()> f = std::move(task);       // ❌ 编译错误
std::move_only_function<int()> mf = std::move(task); // ✅ C++23（实测 mf() = 42）
```

原因在第 5 章会看到：类型擦除必须在**构造的那一刻**（还知道具体类型时）就生成好“如何拷贝”的代码，所以无论你后面拷不拷，构造时就要求它可拷贝。

替代方案：

| 方案 | 说明 |
|---|---|
| `std::move_only_function`（C++23） | 只可移动，还修正了 const 正确性问题 |
| 把 `unique_ptr` 换成 `shared_ptr` | 最省事，但语义变了 |
| UE 的 `TUniqueFunction` | 同理，见第 8 章 |

### 4.4 一个隐藏的 const 漏洞（深度加分点）

`std::function::operator()` 是 `const` 成员函数，但它可以调用一个 `mutable` lambda 并改变其状态：

```cpp
const std::function<int()> f = [n = 0]() mutable { return ++n; };
f(); f();   // 返回 1、2 —— 一个 const 对象的“状态”变了
```

这是 C++11 设计时的妥协。`std::move_only_function` 把 const 写进签名（`move_only_function<int() const>`），从类型上堵住了这个口子。

### 4.5 家族成员（版本演进）

| 类型 | 标准 | 所有权 | 可拷贝 | 典型用途 |
|---|---|---|---|---|
| 函数指针 | C | 无状态 | ✅ | 对接 C 接口 |
| `std::function` | C++11 | 拥有 | ✅ | 存储回调 |
| `std::move_only_function` | C++23 | 拥有 | ❌ 仅移动 | 存储持有独占资源的任务 |
| `std::copyable_function` | C++26 | 拥有 | ✅ | 修正版 `std::function` |
| `std::function_ref` | C++26 | **不拥有**（视图） | ✅ | 作为**函数参数**，零分配 |

`function_ref` 和 `std::string_view` 的关系类似：只借用、不管理生命周期，适合“同步调用完就不再用”的参数。

---

## 5. 底层原理：手写一个 mini `std::function` + SBO 实测

### 5.1 类型擦除的骨架

```cpp
template <class> class MyFunction;                // 主模板只声明

template <class R, class... Args>
class MyFunction<R(Args...)> {                    // 偏特化出“函数签名”语法
    struct ICallable {                            // ① 统一接口（擦掉具体类型）
        virtual ~ICallable() = default;
        virtual R Invoke(Args... args) = 0;
        virtual std::unique_ptr<ICallable> Clone() const = 0;
    };

    template <class F>
    struct Impl final : ICallable {               // ② 每种具体类型一份实现
        F f;
        explicit Impl(F fn) : f(std::move(fn)) {}
        R Invoke(Args... args) override {
            return std::invoke(f, std::forward<Args>(args)...);   // 统一支持成员函数指针
        }
        std::unique_ptr<ICallable> Clone() const override {
            return std::make_unique<Impl>(f);     // ③ 这里要求 F 可拷贝 —— 构造时就实例化了
        }
    };

    std::unique_ptr<ICallable> p_;

public:
    MyFunction() = default;

    template <class F>
        requires(!std::is_same_v<std::decay_t<F>, MyFunction>)   // 别劫持拷贝构造
    MyFunction(F f) : p_(std::make_unique<Impl<F>>(std::move(f))) {}

    MyFunction(const MyFunction& o) : p_(o.p_ ? o.p_->Clone() : nullptr) {}
    MyFunction(MyFunction&&) noexcept = default;

    R operator()(Args... args) const {
        if (!p_) throw std::bad_function_call{};
        return p_->Invoke(std::forward<Args>(args)...);   // ④ 虚调用 = 间接跳转，无法内联
    }
    explicit operator bool() const noexcept { return p_ != nullptr; }
};
```

这 40 行代码能解释 `std::function` 几乎所有特性：

| 特性 | 来自代码哪里 |
|---|---|
| 什么都能装 | ② 模板 `Impl<F>`，每种类型实例化一份 |
| 外部类型统一 | ① 只通过 `ICallable*` 访问 |
| 调用有开销、不能内联 | ④ 虚函数调用，编译器看不到 `F` |
| 要求可拷贝 | ③ `Clone` 在构造时随 `Impl<F>` 一起实例化 |
| 拷贝是深拷贝 | `Clone()` 复制了 `F` |
| 调用空对象抛异常 | `operator()` 里的检查 |
| 不能比较相等 | 擦掉类型后无从比较两个 `F` |

真实实现（MSVC 的 `_Func_base` / `_Func_impl_no_alloc`）思路一样，有的实现用手写函数指针表替代虚函数，但都是“一次间接调用”。

### 5.2 小对象优化（SBO）：避免每次都 new

上面的 mini 版每次构造都 `make_unique`，太慢。真实实现会在 `std::function` 对象内部预留一块缓冲区，闭包足够小就**原地构造**，放不下才上堆。

MSVC x64 实测（通过重载全局 `operator new` 计数）：

```
sizeof(std::function<void()>)       = 64
sizeof(std::function<int(int,int)>) = 64      // 和签名无关
  捕获   8 字节 -> 堆分配 0 次
  捕获  16 字节 -> 堆分配 0 次
  捕获  32 字节 -> 堆分配 0 次
  捕获  48 字节 -> 堆分配 0 次
  捕获  56 字节 -> 堆分配 1 次      ← 分界线
  捕获  64 字节 -> 堆分配 1 次
  捕获 128 字节 -> 堆分配 1 次
  存普通函数指针 -> 堆分配 0 次
```

**为什么分界线是 48 而不是 56？** MSVC 的内部缓冲区是 7 个指针 = 56 字节，再加 1 个指针指向当前实现对象，共 64 字节。但缓冲区里放的不是裸闭包，而是 `_Func_impl_no_alloc<F>` 对象——它**自带一个 8 字节虚表指针**，所以闭包本身最多 56 − 8 = **48 字节**。

内存布局示意：

```
std::function (64 B, MSVC x64)
┌──────────────────────────────── 56 B 内部缓冲区 ───────────────────────────────┬─────────┐
│ [vptr 8B][ 闭包成员 ≤ 48B ............................................... ]     │ _Impl*  │
└────────────────────────────────────────────────────────────────────────────────┴─────────┘
  闭包 ≤ 48B 且 移动构造为 noexcept  → 原地构造，_Impl* 指向自己的缓冲区
  否则                               → new 一块堆内存，_Impl* 指向堆
```

两个容易被忽略的条件：

1. **不同标准库差异很大**：GCC 的 libstdc++ 中 `sizeof(std::function)` 是 32 字节，内部只有 16 字节缓冲区，而且只有**可平凡拷贝**的小对象才放内部——捕获一个 `std::string` 在 GCC 上一定上堆。面试时说“SBO 阈值是实现定义的”即可，再举一个你实测过的数字。
2. **MSVC 要求闭包的移动构造是 `noexcept`** 才放进缓冲区，否则即使很小也上堆（为了保证 `std::function` 自己的移动不抛异常）。

---

## 6. 性能：模板 vs `std::function` vs 函数指针

### 6.1 实测（/O2，2 亿次调用 `i ^ k`）

```
模板参数(可内联)       0 ms   sum=19999999900000000
std::function       200 ms   sum=19999999900000000
函数指针             195 ms   sum=19999999900000000
```

怎么解读（面试要讲出这一层）：

- **模板接 lambda 不是“调用更快”，而是“根本没有调用”**。编译器看到了具体类型，把函数体内联进循环后，又做了向量化/代数化简，整段循环几乎被优化掉。这就是 lambda 的真正价值：**给优化器完整信息**。
- `std::function` 和函数指针差不多：**都是一次间接调用**，编译器看不见目标，内联、向量化、常量传播全部失效。`std::function` 多一点判空和虚表跳转，量级相同。
- 所以 `std::function` 的“慢”主要不是那一次跳转本身，而是**阻断了优化**；再加上可能的**堆分配**和**更大的对象**（缓存不友好）。

### 6.2 经典例子：`std::sort` vs `qsort`

`std::sort(v.begin(), v.end(), [](int a, int b){ return a < b; })` 通常比 C 的 `qsort` 快好几倍，原因就是比较函数被内联了，而 `qsort` 每次比较都要通过函数指针 + `void*` 转换。

### 6.3 选型表

| 场景 | 推荐 | 理由 |
|---|---|---|
| 算法参数、同步调用一次 | **模板 `F&&` / `auto`** | 零开销，可内联 |
| 函数参数但不想写模板（头文件不想暴露实现） | `std::function_ref`（C++26）/ 自写 `FunctionRef` / UE `TFunctionRef` | 不分配，只多一次间接调用 |
| 需要**存起来**的回调 | `std::function` / `move_only_function` | 统一类型 |
| 对接 C 接口 | 无捕获 lambda → 函数指针 | |
| 想“绑定部分参数” | **lambda**，别用 `std::bind` | 见下 |

### 6.4 为什么现代 C++ 不推荐 `std::bind`

《Effective Modern C++》条款 34：优先选用 lambda。

- 可读性差：`std::bind(&Skill::Cast, &sk, _1)` 不如 `[&sk](int lv){ return sk.Cast(lv); }`。
- 参数求值时机反直觉：`std::bind(f, now())` 的 `now()` 在**绑定时**求值。
- 重载函数无法直接 bind，需要强转。
- 编译器更难内联 bind 产生的对象。
- 默认按值存参数，想按引用要写 `std::ref`，容易忘。

---

## 7. 递归 lambda 的三种写法

lambda 在自己的函数体里看不到自己的名字（声明还没完成），所以不能直接递归。

```cpp
// 写法一：std::function + 引用捕获自己（C++11，最常见，但慢）
std::function<int(int)> fib1 = [&fib1](int n) {
    return n < 2 ? n : fib1(n - 1) + fib1(n - 2);
};

// 写法二：把自己当参数传进去（C++14 泛型 lambda）
auto fib2 = [](auto self, int n) -> int {
    return n < 2 ? n : self(self, n - 1) + self(self, n - 2);
};
fib2(fib2, 35);

// 写法三：C++23 deducing this（最优雅）
auto fib3 = [](this auto const& self, int n) -> int {
    return n < 2 ? n : self(n - 1) + self(n - 2);
};
fib3(35);
```

实测 `fib(35)`（约 3000 万次调用）：

```
std::function 递归     35 ms
auto self 递归          0 ms
C++23 deducing this     0 ms
```

后两种 0 ms，说明优化器看清了整个调用链，把递归改写甚至折叠掉了；`std::function` 版每层递归都是一次不透明的间接调用，什么也优化不了。另外写法一还有一个坑：**把 `fib1` 拷贝/返回出去后，它内部引用的还是原来那个 `fib1`**，原对象销毁就悬垂。

> 小知识：MSVC 14.44 在 `/std:c++latest` 下已支持 deducing this，但**没有定义** `__cpp_explicit_this_parameter` 特性宏，用宏做条件编译时要注意（demo 里额外判断了 `_MSVC_LANG`）。

---

## 8. 游戏客户端实战

### 8.1 事件系统：回调存储的标准姿势

```cpp
struct EventBus {
    using Handler = std::function<void(int /*dmg*/)>;
    std::vector<std::pair<uint32_t, Handler>> handlers;   // 配一个 ID
    uint32_t nextId = 1;

    uint32_t Subscribe(Handler h) { handlers.emplace_back(nextId, std::move(h)); return nextId++; }
    void Unsubscribe(uint32_t id) { /* 按 ID 删除 */ }
    void Broadcast(int dmg) { for (auto& [id, h] : handlers) h(dmg); }
};
```

为什么要 ID？因为**两个 `std::function` 不能比较相等**，你没法用“同一个 lambda”去退订。这也是为什么 UE 的委托返回 `FDelegateHandle`、很多引擎的事件系统返回订阅令牌。

### 8.2 生命周期一：对象死了，回调还在 → 用 `weak_ptr`

```cpp
auto boss = std::make_shared<Monster>("Boss");
bus.handlers.push_back([w = std::weak_ptr<Monster>(boss)](int dmg) {
    if (auto sp = w.lock()) sp->OnHit(dmg);      // 还活着才调用，且调用期间保活
    else std::printf("目标已销毁，回调安全跳过\n");
});
```

实测输出：

```
第一次广播（Boss 还活着）:  Boss 受到 30 点伤害
Boss 离开作用域:            ~Monster(Boss) 析构
第二次广播（Boss 已死）:    目标已销毁，回调安全跳过
```

对比三种捕获方式：

| 捕获 | 对象销毁后调用 | 是否延长寿命 |
|---|---|---|
| `[this]` / `[&obj]` | ❌ 悬垂，未定义行为 | 否 |
| `[sp = shared_from_this()]` | ✅ 安全 | **是**，可能导致对象“该死不死” |
| `[w = weak_from_this()]` + `lock()` | ✅ 安全并能感知死亡 | 否（推荐） |

### 8.3 生命周期二：回调强持有自己 → 循环引用泄漏

```cpp
struct Timer { std::function<void()> onTick; ~Timer(); };
auto t = std::make_shared<Timer>();
t->onTick = [t] { /* ... */ };   // Timer 持有回调，回调持有 Timer
```

实测：`use_count = 2`，离开作用域后 `expired = 0`（**没析构 = 泄漏**），手动 `onTick = nullptr` 打断后才析构。

规则：**谁持有这个回调，回调里就不要强引用谁**。改成 `weak_ptr`，或者在 `Stop()/Destroy()` 里显式清空回调。

### 8.4 生命周期三：广播时修改订阅列表

回调里经常会“打死怪物 → 怪物退订事件”或“触发新订阅”，如果在 `for` 遍历 `vector` 时 `erase/push_back`，迭代器失效直接崩溃。常见解法：

- 广播前**拷贝一份**列表再遍历（简单，有分配开销）；
- 遍历期间只**标记删除**，结束后统一清理；
- 新增的订阅先进 pending 队列，下一帧生效。

### 8.5 热路径注意事项

- **不要每帧构造 `std::function`**：闭包超过 SBO 就是一次 `new`，每帧几百个就是几百次分配，还会造成内存碎片。
- 每帧调用、数量巨大的逻辑（粒子更新、ECS system）用模板/虚函数/函数指针表或数据驱动，不要 `std::vector<std::function<...>>` 逐个调。
- 回调**只作为参数同步调用**时，用 `FunctionRef` 类，零分配。
- 值捕获大对象（容器、字符串）用 `[v = std::move(v)]` 或只捕获需要的字段。

### 8.6 引擎里的对应物（面试可以顺带提）

| 标准库 | Unreal Engine | 说明 |
|---|---|---|
| `std::function` | `TFunction` | 拥有、可拷贝 |
| `std::move_only_function` | `TUniqueFunction` | 只可移动，常用于异步任务 |
| `std::function_ref` | `TFunctionRef` | 不拥有，作参数 |
| 手写事件 + `weak_ptr` | `TDelegate` / 多播委托，`BindUObject`、`BindSP`、`BindWeakLambda` | 委托内部会检查绑定对象是否有效 |

如果项目是 **Unity（C#）**，概念可以一一对照：

- C# 的 lambda 也会生成一个编译器类（DisplayClass），但它捕获的是**变量本身**（类似引用捕获），并且把变量“提升”到堆上延长寿命，所以不会悬垂，但**会产生 GC 分配**——这就是 Unity 里 `Update` 中写 lambda 被要求避免的原因。
- 经典坑：`for (int i...) list.Add(() => Use(i));` 所有闭包共享同一个 `i`；C++ 用 `[i]` 值捕获则没有这个问题，这个对比很适合在面试中展示你对两种语言闭包模型的理解。

---

## 9. 面试高频坑 + 自测题

### 9.1 坑位清单（按出现频率排序）

1. **引用捕获悬垂**：闭包活得比被引用对象久（返回、存储、异步）。
2. **`[=]` 捕获的是 `this`**：不是成员的拷贝；C++20 已废弃隐式捕获 `this`。
3. **值捕获是创建时快照**，且默认不可修改；`mutable` 改的是副本。
4. **静态/全局变量不被捕获**，`[=]` 里改它就是改全局。
5. **`std::function` 不是零开销**：间接调用 + 阻断内联 + 可能堆分配 + 对象大。
6. **调用空 `std::function` 抛 `std::bad_function_call`**，调用前判空。
7. **`std::function` 要求可拷贝**，装不下捕获 `unique_ptr` 的 lambda → `move_only_function`。
8. **`std::function` 不能相互比较**，退订要靠 ID/句柄。
9. **回调捕获 `shared_ptr` 造成循环引用**，用 `weak_ptr`。
10. **每个 lambda 类型唯一**，装容器要用 `std::function` 或模板。
11. **广播回调中修改订阅列表**导致迭代器失效。
12. **带状态 lambda 按值传给算法**，外部状态不更新。
13. **`std::function` 递归 lambda** 慢，且拷贝出去后引用的还是原对象。
14. **`std::function::operator()` 是 const 却能改 mutable 闭包的状态**（设计缺陷，C++23 修正）。

### 9.2 自测题（先自己答，再看答案）

**Q1：lambda 的底层实现是什么？**
编译器生成一个唯一的匿名类，捕获变量成为成员，函数体成为 `const operator()`，lambda 表达式处构造该类的对象。

**Q2：值捕获的变量为什么在 lambda 里不能修改？怎么改？**
因为 `operator()` 默认是 `const` 成员函数。加 `mutable` 去掉 const，但改的只是闭包内部的副本。

**Q3：`[=]` 在成员函数里会捕获什么？有什么风险？**
捕获 `this` 指针，成员通过 `this->` 访问。对象销毁后调用就是悬垂指针。C++20 要求显式写 `[=, this]`；想要拷贝用 `[*this]`（C++17）或初始化捕获具体字段。

**Q4：什么样的 lambda 能转成函数指针？为什么？**
无捕获的。它没有状态，`operator()` 不依赖对象，编译器可以生成一个静态函数作为转换目标。

**Q5：`sizeof` 一个 lambda 是多少？**
取决于捕获：无捕获为 1（空类），值捕获就是各成员大小加对齐，引用捕获通常是指针大小。

**Q6：`std::function` 是怎么实现“什么都能装”的？**
类型擦除：用模板派生类 `Impl<F>` 记住具体类型，对外只暴露基类接口（虚函数或函数指针表），调用时间接转发。

**Q7：`std::function` 有哪些开销？**
一次间接调用、无法内联和进一步优化；闭包超过小对象缓冲区时堆分配（MSVC x64 阈值为闭包 48 字节，GCC 仅 16 字节且要求可平凡拷贝）；对象本身较大（MSVC 64 字节、GCC 32 字节）；拷贝是深拷贝。

**Q8：为什么 `std::function` 要求可拷贝？怎么存只可移动的闭包？**
构造时就必须生成“拷贝闭包”的代码（类型擦除后就不知道类型了），所以构造时就要求可拷贝。用 C++23 `std::move_only_function`，或 UE 的 `TUniqueFunction`。

**Q9：lambda 作为参数时，用模板还是 `std::function`？**
同步调用、追求性能 → 模板（`template<class F> void Foo(F&& f)`）。需要存储、需要统一类型、需要非模板接口 → `std::function`。只作参数又不想写模板 → `function_ref` 类。

**Q10：调用一个空的 `std::function` 会怎样？**
抛出 `std::bad_function_call` 异常。

**Q11：怎么写递归 lambda？**
`std::function` + 引用捕获自身；或泛型 lambda 把自己当参数传；或 C++23 deducing this `[](this auto const& self, ...)`。后两者能被优化，前者慢且有悬垂风险。

**Q12：lambda 和 `std::bind` 怎么选？**
优先 lambda：可读性好、求值时机直观、容易内联、能处理重载和移动捕获。

**Q13：C++14/17/20/23 给 lambda 加了什么？**
C++14：泛型 lambda、初始化捕获、返回类型推导；C++17：`constexpr` lambda、`[*this]`；C++20：模板 lambda、无捕获 lambda 可默认构造与赋值、可出现在不求值语境、捕获结构化绑定与参数包、废弃 `[=]` 隐式捕获 `this`；C++23：deducing this、`static operator()`。

**Q14：游戏里异步加载完成的回调，捕获 `this` 有什么问题？怎么解决？**
回调回来时对象可能已被销毁（切场景、角色死亡、界面关闭），访问 `this` 是悬垂指针。解决：捕获 `weak_ptr` 并 `lock()` 检查；或在析构时取消请求/退订；引擎层面用带有效性检查的委托（UE 的 `BindUObject`/`BindWeakLambda`）。

**Q15：事件系统为什么通常返回一个订阅 ID？**
`std::function` 不能比较相等，无法通过回调本身找到并删除它；ID/句柄让退订变成 O(1) 或简单查找。

### 9.3 面试表达建议

- 先给结论，再讲原理，最后讲工程实践：“lambda 本质是仿函数 → 所以零开销且能内联 → 但捕获有生命周期风险 → 我们项目里回调统一用 weak 引用/句柄退订”。
- 能说出一个**你自己实测过的数字**（如“MSVC 上 std::function 64 字节、闭包超过 48 字节会上堆”），比背概念更有说服力。
- 被问到性能，一定要强调“开销主要在**阻断内联**和**堆分配**，而不只是那一次间接跳转”。

---

## 附：demo 完整实测输出（MSVC 14.44，x64，/O2）

见同目录 `run_output.txt`。不同机器、不同编译器上的毫秒数会不同，`sizeof` 和 SBO 阈值也是**实现定义**的，但相对关系和结论是一致的。
