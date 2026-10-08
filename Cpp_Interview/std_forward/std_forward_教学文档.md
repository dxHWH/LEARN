# C++ `std::forward` 完全指南（秋招八股向）

> 配套代码：同目录 `forward_demo.cpp`（C++20，已用 MSVC 14.44 / VS2022 编译运行验证，文中“实测输出”均来自它）
> 编译：`cl /std:c++20 /EHsc /utf-8 forward_demo.cpp` 或 `g++ -std=c++20 forward_demo.cpp`

---

## 0. 30 秒面试版答案（先背这个）

> `std::forward` 是 C++11 引入的、定义在 `<utility>` 里的函数模板，用来实现**完美转发**：在函数模板里，把通过**转发引用（万能引用）`T&&`** 接收到的参数，**按调用方传进来时的值类别**（左值还是右值）原样传给下一个函数。
>
> 为什么需要它：函数参数一旦有了名字，它本身就是**左值**，即使它的类型是右值引用。所以直接把参数往下传，右值会“退化”成左值，移动语义就丢了；而无条件 `std::move` 又会把调用方的左值也当右值，导致对象被偷走。
>
> 它的原理：模板推导时，传左值则 `T` 推导为 `X&`，传右值则 `T` 推导为 `X`；`std::forward<T>(x)` 本质是 `static_cast<T&&>(x)`，再结合**引用折叠**，左值得到 `X&`，右值得到 `X&&`。所以它是**有条件的 move**，只在编译期起作用，没有运行时开销。
>
> 典型场景：`std::make_unique / make_shared`、容器的 `emplace_back`、`std::thread` / `std::invoke` / `std::bind` 这类包装函数、通用工厂和装饰器。

后面的章节就是把这段话里的每个概念讲透。

---

## 1. 前置知识：值类别（Value Category）

### 1.1 三种基本值类别

| 类别 | 英文 | 特点 | 例子 |
|---|---|---|---|
| 左值 | lvalue | 有身份（能取地址），不能被移动 | 变量名 `a`、`*p`、`arr[0]`、返回 `T&` 的函数调用 |
| 将亡值 | xvalue | 有身份，**可以被移动** | `std::move(a)`、返回 `T&&` 的函数调用 |
| 纯右值 | prvalue | 没有身份，是临时值 | `42`、`a + b`、`Tracker{"tmp"}`、按值返回的函数调用 |

- **glvalue（泛左值）** = lvalue + xvalue（“有身份的”）
- **rvalue（右值）** = xvalue + prvalue（“可以被移动的”）

### 1.2 最关键的一句话（`std::forward` 存在的根本原因）

> **值类别是表达式的属性，类型是变量的属性，两者不是一回事。**
> **一个有名字的右值引用变量，作为表达式使用时是左值。**

```cpp
void f(Tracker&& x) {   // x 的【类型】是 Tracker&&（右值引用）
    sink(x);            // 但表达式 x 的【值类别】是左值，调用 sink(const Tracker&)
    sink(std::move(x)); // 想把它当右值用，必须显式转换
}
```

为什么这样设计？因为有名字就可能被使用多次。如果 `x` 自动当右值，第一次使用就可能把它掏空，后面再用就是 bug。所以标准规定：**有名字 = 左值**，想移动必须显式表达出来。

---

## 2. 问题引入：“转发”到底难在哪

### 2.1 场景

我们想写一个包装函数 `wrapper`，把参数**原封不动**地交给 `target`：

```cpp
template <class T>
void wrapper(??? arg) { target(arg); }
```

“原封不动”指三件事都保持不变：**类型**、**const 属性**、**值类别（左值/右值）**。

### 2.2 C++98 的困境

| 写法 | 问题 |
|---|---|
| `wrapper(T arg)` | 按值传，多一次拷贝；拷贝不了的类型（如 `unique_ptr`）直接编译失败 |
| `wrapper(T& arg)` | 不能接收右值：`wrapper(42)` 编译失败 |
| `wrapper(const T& arg)` | 能接左值也能接右值，但 const 去不掉，**右值信息丢了**，下游只能拷贝、不能移动 |
| `T&` 和 `const T&` 都重载一份 | 1 个参数要 2 个重载，**N 个参数要 2ᴺ 个**，组合爆炸；而且仍然分不清右值 |

C++11 的解决方案由三部分组成：

```
转发引用 T&&（接住任何东西，同时记住它是什么）
      + 引用折叠（让 T&& 在不同情况下变成 X& 或 X&&）
      + std::forward<T>（根据记住的信息恢复值类别）
      = 完美转发
```

---

## 3. 转发引用（万能引用）

### 3.1 定义

`T&&` **当且仅当**满足下面两个条件时，是转发引用（Scott Meyers 叫它 universal reference，标准术语是 forwarding reference）：

1. **`T` 是函数模板自己的模板参数，并且正在进行类型推导**；
2. **形式严格是 `T&&`**，不能带 `const`，也不能套在别的类型里。

`auto&&` 同样是转发引用，因为 `auto` 也走模板推导规则。

### 3.2 判断练习（面试常考）

```cpp
template <class T> void f1(T&& x);              // ✅ 转发引用
auto&& r = expr;                                 // ✅ 转发引用
for (auto&& e : range) {}                        // ✅ 转发引用
[](auto&& x) {};                                 // ✅ 转发引用（泛型 lambda）

void f2(Widget&& x);                             // ❌ 右值引用：没有推导
template <class T> void f3(std::vector<T>&& x);  // ❌ 右值引用：形式不是 T&&
template <class T> void f4(const T&& x);         // ❌ 右值引用：带了 const

template <class T>
class vector {
    void push_back(T&& x);                       // ❌ 右值引用：T 在类实例化时已经确定，这里不发生推导
    template <class... Args>
    void emplace_back(Args&&... args);           // ✅ 转发引用：Args 是成员函数模板自己推导的
};
```

### 3.3 推导规则：`T` 会“记住”值类别

这是整个机制的核心。对 `template<class T> void f(T&& x)`：

| 调用方传入 | `T` 被推导为 | `T&&` 展开后 | 经引用折叠，`x` 的最终类型 |
|---|---|---|---|
| 左值 `Tracker a; f(a);` | `Tracker&` | `Tracker& &&` | `Tracker&` |
| const 左值 `const Tracker ca; f(ca);` | `const Tracker&` | `const Tracker& &&` | `const Tracker&` |
| 右值 `f(Tracker{})` / `f(std::move(a))` | `Tracker`（不带引用） | `Tracker&&` | `Tracker&&` |

**实测输出（`forward_demo.cpp` 第 2 节）：**

```
show_T(a):
  T = Tracker&  (lvalue passed, T&& collapses to Tracker&)
show_T(Tracker{tmp}):
  T = Tracker   (rvalue passed, T&& is Tracker&&)
```

> 结论：**原来是左值还是右值的信息，被编码进了模板参数 `T` 里**。但在函数体内，`x` 有名字，所以总是左值。要把这份信息“取出来用”，就需要 `std::forward<T>`。

---

## 4. 引用折叠（Reference Collapsing）

C++ 不允许用户直接写“引用的引用”（`int& && r;` 编译错误），但在**模板实例化、`typedef`/`using`、`decltype`** 这几种语境中会间接产生，这时编译器按下面规则折叠：

| 组合 | 结果 |
|---|---|
| `X&  &` | `X&` |
| `X&  &&` | `X&` |
| `X&& &` | `X&` |
| `X&& &&` | `X&&` |

**口诀：只要有一个是左值引用，结果就是左值引用；只有两个都是右值引用，结果才是右值引用。**

配套代码用 `static_assert` 在编译期验证了这四条：

```cpp
using LR = int&;  using RR = int&&;
static_assert(std::is_same_v<LR&,  int&>);
static_assert(std::is_same_v<LR&&, int&>);
static_assert(std::is_same_v<RR&,  int&>);
static_assert(std::is_same_v<RR&&, int&&>);
```

---

## 5. `std::forward` 的实现与原理

### 5.1 标准库实现（libstdc++ / MSVC STL 本质相同）

```cpp
// 重载 1：接收左值（最常用）
template <class T>
constexpr T&& forward(std::remove_reference_t<T>& t) noexcept {
    return static_cast<T&&>(t);
}

// 重载 2：接收右值
template <class T>
constexpr T&& forward(std::remove_reference_t<T>&& t) noexcept {
    static_assert(!std::is_lvalue_reference_v<T>,
                  "cannot forward an rvalue as an lvalue");
    return static_cast<T&&>(t);
}
```

核心只有一句：**`static_cast<T&&>(t)`**。

### 5.2 逐步推演

以 `template<class T> void wrap(T&& x) { sink(std::forward<T>(x)); }` 为例：

**情况 A：传左值 `wrap(a)`**

1. `T = Tracker&`
2. `std::forward<Tracker&>(x)` → 返回类型 `Tracker& &&` → 折叠为 **`Tracker&`**
3. 函数返回左值引用，调用表达式是**左值** → 调用 `sink(const Tracker&)` ✅

**情况 B：传右值 `wrap(Tracker{})`**

1. `T = Tracker`
2. `std::forward<Tracker>(x)` → 返回类型 **`Tracker&&`**
3. 函数返回右值引用，调用表达式是**将亡值（右值）** → 调用 `sink(Tracker&&)` ✅

```
            调用方                 wrap 内部                  forward<T>(x)             下游
  ┌───────────────────┐    ┌──────────────────────┐    ┌──────────────────────┐   ┌───────────────┐
  │ 左值 a            │ ─► │ T=Tracker&  x 是左值 │ ─► │ static_cast<Tracker&>│─► │ sink(const&)  │
  └───────────────────┘    └──────────────────────┘    └──────────────────────┘   └───────────────┘
  ┌───────────────────┐    ┌──────────────────────┐    ┌──────────────────────┐   ┌───────────────┐
  │ 右值 Tracker{}    │ ─► │ T=Tracker   x 是左值 │ ─► │static_cast<Tracker&&>│─► │ sink(&&)      │
  └───────────────────┘    └──────────────────────┘    └──────────────────────┘   └───────────────┘
                                    ▲ 注意：两种情况 x 本身都是左值，区别只记录在 T 里
```

### 5.3 三个设计细节（面试追问点）

**① 为什么必须显式写 `std::forward<T>`，不能让编译器推导？**

形参类型是 `std::remove_reference_t<T>&`，`T` 出现在 `::type` 的左边，属于**不可推导语境（non-deduced context）**，编译器无法推导 `T`，必须显式指定。这是**故意设计**的：如果让它推导，`x` 永远是左值，推出来的结果永远是“左值”，原始信息就丢了。原始信息只存在于外层的 `T` 中，所以必须由程序员手动把 `T` 传进去。

**② 为什么还要右值重载？**

用于转发一个**本身就是右值的表达式**，比如转发某个函数调用的结果或者成员：`std::forward<T>(get())`、`std::forward<U>(std::forward<T>(obj).member)`。

**③ 右值重载里的 `static_assert` 防什么？**

防止 `std::forward<X&>(右值)`，也就是把临时对象“转发成左值引用”，这样会得到一个指向临时对象的悬垂引用。

### 5.4 零开销

`std::forward` 就是一个 `constexpr noexcept` 的 `static_cast`，只在编译期改变表达式的值类别，**不生成任何运行时指令，也不移动任何东西**。真正的移动发生在下游函数（比如移动构造函数）里。

---

## 6. `std::forward` vs `std::move`

```cpp
template <class T>
constexpr std::remove_reference_t<T>&& move(T&& t) noexcept {
    return static_cast<std::remove_reference_t<T>&&>(t);   // 先去引用，再强转为 &&：无条件右值
}
```

| 维度 | `std::move` | `std::forward<T>` |
|---|---|---|
| 语义 | **无条件**转成右值 | **有条件**转成右值（原来是右值才转） |
| 模板参数 | 自动推导 | **必须显式指定** |
| 实现 | `static_cast<remove_reference_t<T>&&>` | `static_cast<T&&>` + 引用折叠 |
| 搭配对象 | 右值引用 `X&&`、你确定不再使用的对象 | 转发引用 `T&&` / `auto&&` |
| 是否真的移动 | 否，只是转换 | 否，只是转换 |
| 记忆 | “我不要它了，拿走吧” | “它原本是什么，就还给你什么” |

**实测输出（第 1 节：三种写法对比）：**

```
wrap_plain(a):                  -> sink(const Tracker&)   ✅ 左值→左值
wrap_plain(Tracker{tmp}):       -> sink(const Tracker&)   ❌ 右值退化成左值，失去移动机会
wrap_move(a):                   -> sink(Tracker&&)        ❌ 左值被当成右值，调用方的 a 可能被偷走
wrap_move(Tracker{tmp}):        -> sink(Tracker&&)        ✅
wrap_forward(a):                -> sink(const Tracker&)   ✅
wrap_forward(Tracker{tmp}):     -> sink(Tracker&&)        ✅ 两种情况都对
```

> Effective Modern C++ Item 25：**对右值引用用 `std::move`，对转发引用用 `std::forward`。**
> 理论上 `std::forward<X>(x)` 可以替代 `std::move(x)`，但可读性差，也容易写错，不推荐。

---

## 7. `std::forward` 能实现什么：典型应用

### 7.1 工厂函数：`make_unique` / `make_shared`

```cpp
template <class T, class... Args>
std::unique_ptr<T> my_make_unique(Args&&... args) {
    return std::unique_ptr<T>(new T(std::forward<Args>(args)...));
}
```

`std::forward<Args>(args)...` 是**包展开**：对每个参数分别用它自己的 `Args_i` 进行转发，每个参数各自保持值类别。

**实测：**

```
my_make_unique<Widget>(t, 1):              -> Widget(const Tracker&, int)
my_make_unique<Widget>(Tracker{tmp}, 2):   -> Widget(Tracker&&, int)
```

### 7.2 容器原地构造：`emplace_back`

```cpp
v.push_back(Tracker{"x"});  // 先构造临时对象，再移动进容器：1 次构造 + 1 次移动
v.emplace_back("y");        // 把 "y" 完美转发给 Tracker 的构造函数，直接在容器内存上构造：0 次拷贝/移动
```

**实测：** `push_back` 打印了 `[Tracker move ctor]`，`emplace_back` 什么都没打印。

### 7.3 通用调用包装 / 装饰器（计时、日志、加锁）

```cpp
struct ScopeTimer {                       // RAII：析构时打印耗时
    std::chrono::steady_clock::time_point t0 = std::chrono::steady_clock::now();
    ~ScopeTimer() { log(std::chrono::steady_clock::now() - t0); }
};

template <class F, class... Args>
decltype(auto) timed_call(F&& f, Args&&... args) {
    ScopeTimer timer;
    // f 本身也要转发：可调用对象的 operator() 可能带有 & / && 限定
    return std::invoke(std::forward<F>(f), std::forward<Args>(args)...);
}
```

- **参数转发**：`std::forward<Args>(args)...` 保证每个参数的值类别不变。
- **返回值转发**：返回类型写 `decltype(auto)`，`f` 返回引用时就返回引用，返回值时就返回值，返回 `void` 时也能编译；写成 `auto` 会让引用退化成值拷贝。
- `std::thread`、`std::async`、`std::bind`、`std::invoke`、`std::function` 的构造等都使用了这个模式。

### 7.4 泛型 lambda 中的完美转发

lambda 里没有名字 `T`，可以用 `decltype(x)` 代替：

```cpp
auto lam = [](auto&& x) { sink(std::forward<decltype(x)>(x)); };
```

- 传左值：`decltype(x)` = `Tracker&` → `forward<Tracker&>` → 左值 ✅
- 传右值：`decltype(x)` = `Tracker&&` → `forward<Tracker&&>` → 返回类型 `Tracker&& &&` 折叠成 `Tracker&&` → 右值 ✅

C++20 也可以写显式模板 lambda：`[]<class T>(T&& x) { sink(std::forward<T>(x)); }`。

### 7.5 完美转发构造函数（setter / 构造的“一份代码两种效率”）

```cpp
class Person {
    std::string name;
public:
    template <class T>
    explicit Person(T&& n) : name(std::forward<T>(n)) {}
};
```

传左值 `std::string` 时拷贝，传右值时移动，传 `const char*` 时直接构造 `std::string`，**连临时对象都不会产生**。不过这种写法有一个大坑，见 8.3。

---

## 8. 常见陷阱（面试加分项）

### 8.1 同一个参数 forward 两次

```cpp
template <class T> void forward_twice(T&& x) {
    Tracker a(std::forward<T>(x));
    Tracker b(std::forward<T>(x));   // 如果 x 原来是右值，它已经被 a 掏空了
}
```

**实测：**

```
forward_twice(src)  (lvalue):         a.tag="src"  b.tag="src"   ← 左值：拷贝两次，没问题
forward_twice(Tracker{tmp}) (rvalue): a.tag="tmp"  b.tag=""      ← 右值：b 拿到了被移动过的空对象！
```

**原则：只在参数最后一次使用时 forward**，前面的使用都把它当左值。

### 8.2 对非转发引用使用 forward

```cpp
void f(Widget&& w) { g(std::forward<Widget>(w)); }  // 能工作，但应该写 std::move(w)
template <class T> void h(const T& x) { g(std::forward<T>(x)); } // 毫无意义，x 不是转发引用
```

### 8.3 转发构造函数“劫持”拷贝构造函数（EMC++ Item 26）

```cpp
class Person {
public:
    template <class T> explicit Person(T&& n);   // 转发构造
    Person(const Person&);                         // 拷贝构造
};
Person p1("Alice");
Person p2(p1);   // ❗ p1 是非 const 左值：模板推导出 T=Person&，形参是 Person&，
                 //    比 const Person& 更匹配 → 调用了转发构造，然后用 Person 去初始化 std::string → 编译错误
```

**修复**：约束模板，排除 `Person` 自身（C++20 写 `requires`，C++11/17 用 `std::enable_if_t`）：

```cpp
template <class T>
    requires (!std::is_same_v<std::remove_cvref_t<T>, Person> &&
              std::is_constructible_v<std::string, T>)
explicit Person(T&& n) : name(std::forward<T>(n)) {}
```

**实测：** `Person p2(p1);` 正确打印 `[Person copy ctor]`。如果有派生类，可以把 `is_same` 改成 `std::is_base_of_v<Person, std::remove_cvref_t<T>>`。

### 8.4 完美转发失败的五种情况（EMC++ Item 30，高频）

“完美”转发并不总是完美的：在下面这些情况下，`f(expr)` 能编译，`wrapper(expr)` 却编译不过或者行为不一样。

| # | 情况 | 原因 | 绕过方法 |
|---|---|---|---|
| 1 | **花括号初始化列表** `wrapper({1,2,3})` | 模板无法从 `{...}` 推导 `T` | `auto il = {1,2,3}; wrapper(il);` |
| 2 | **`0` / `NULL` 当空指针** | 推导成 `int` / `long`，不是指针 | 使用 `nullptr` |
| 3 | **只声明未定义的 `static const` 整型成员** | 绑定引用会 ODR-use，导致链接错误 | 在类外提供定义，或者 C++17 用 `inline static constexpr` |
| 4 | **重载函数名 / 函数模板名** `wrapper(process)` | 没有目标类型，无法确定选哪个重载 | `wrapper(static_cast<int(*)(int)>(process))` |
| 5 | **位域** `wrapper(h.flags)` | 非 const 引用不能绑定位域 | 先拷贝到局部变量：`auto v = h.flags; wrapper(v);` |

### 8.5 万能引用重载导致匹配“过于贪婪”

```cpp
template <class T> void log(T&& x);  // 几乎能精确匹配任何东西
void log(int x);
short s = 1;
log(s);  // 调用的是模板（T=short& 是精确匹配），而不是 int 版本（需要整型提升）
```

结论：**尽量不要对转发引用做重载**，改用标签分派、`if constexpr` 或者约束。

---

## 9. 手写实现（面试“手撕”）

```cpp
template <class T>
constexpr T&& my_forward(std::remove_reference_t<T>& t) noexcept {
    return static_cast<T&&>(t);
}
template <class T>
constexpr T&& my_forward(std::remove_reference_t<T>&& t) noexcept {
    static_assert(!std::is_lvalue_reference_v<T>, "cannot forward an rvalue as an lvalue");
    return static_cast<T&&>(t);
}

template <class T>
constexpr std::remove_reference_t<T>&& my_move(T&& t) noexcept {
    return static_cast<std::remove_reference_t<T>&&>(t);
}
```

**实测：** `wrap_my_forward(a)` 调用左值重载，`wrap_my_forward(std::move(a))` 调用右值重载，行为和标准库一致。

手写要点：① 形参用 `remove_reference_t` 制造不可推导语境；② 返回类型 `T&&` 依靠引用折叠；③ `constexpr noexcept`；④ 右值重载加 `static_assert`。

---

## 10. 高频面试题 Q&A

**Q1：`std::move` 和 `std::forward` 有什么区别？**
两者都只是 `static_cast`，都不会真正移动任何东西。`move` 无条件转成右值；`forward<T>` 根据 `T` 有条件地转换：`T` 是左值引用类型就返回左值，否则返回右值。`move` 用于右值引用，`forward` 用于转发引用。

**Q2：什么是完美转发？**
函数模板把参数传给另一个函数时，保持参数的类型、cv 限定和值类别都不变。实现方式是：转发引用 `T&&` + 引用折叠 + `std::forward<T>`。

**Q3：为什么函数内部的右值引用参数是左值？**
值类别是表达式的属性。有名字的变量都是左值，因为它可能被多次使用，自动当右值会导致它在第一次使用时就被掏空。

**Q4：怎么判断 `T&&` 是不是万能引用？**
必须同时满足：发生类型推导，且形式严格是 `T&&`（或 `auto&&`）。`const T&&`、`vector<T>&&`、类模板成员函数里的 `T&&`（例如 `push_back(T&&)`）都不是。

**Q5：说一下引用折叠规则？**
有一个 `&` 就是 `&`，全部是 `&&` 才是 `&&`。只在模板实例化、类型别名、`decltype` 等语境中发生，用户不能直接写引用的引用。

**Q6：`std::forward` 为什么必须显式指定模板参数？**
形参是 `remove_reference_t<T>&`，属于不可推导语境。另外，值类别信息只存在于外层的 `T` 中，而函数体内的 `x` 永远是左值，即使让编译器推导也推不出原始信息。

**Q7：不用 `forward` 直接传，或者用 `move` 传，分别会怎样？**
直接传：右值退化成左值，本来能移动的变成拷贝（性能问题），只能移动的类型（`unique_ptr`）会编译失败。用 `move`：调用方传入的左值被当成右值，可能被下游偷走资源（正确性问题）。

**Q8：完美转发在哪些情况下会失败？**
花括号初始化列表、`0`/`NULL` 作空指针、只声明未定义的 `static const` 成员、重载函数名/模板名、位域。

**Q9：`emplace_back` 比 `push_back` 好在哪？一定更好吗？**
`emplace_back` 把参数完美转发给元素的构造函数，原地构造，省掉临时对象和一次移动。但它不一定更好：① 它会调用 `explicit` 构造函数，可能绕过类型检查（如 `vector<unique_ptr<T>>` 中 `emplace_back(new T)`，如果扩容时抛异常，裸指针会泄漏）；② 对已经构造好的对象，两者效果一样。

**Q10：`std::forward` 有运行时开销吗？**
没有。它是 `constexpr noexcept` 的 `static_cast`，开启优化后完全消失。

**Q11：同一个参数能 forward 多次吗？**
如果原来是右值，第一次 forward 后可能已经被移走。应该只在最后一次使用时 forward。

**Q12：lambda 里怎么完美转发？**
`[](auto&& x){ g(std::forward<decltype(x)>(x)); }`。右值时 `decltype(x)` 是 `X&&`，折叠后仍然是 `X&&`，结果正确。C++20 可以写 `[]<class T>(T&& x){ g(std::forward<T>(x)); }`。

**Q13：`std::forward` 为什么有两个重载？**
左值重载处理最常见的情况（转发具名参数）；右值重载用于转发右值表达式（函数返回值、成员访问结果），并用 `static_assert` 禁止把右值转发成左值引用，防止悬垂引用。

---

## 11. 速记卡

```
┌──────────────────────────── std::forward 速记 ────────────────────────────┐
│ 本质     static_cast<T&&>(x)，编译期“有条件的 move”，零开销                │
│ 前提     只用于转发引用：template<class T> f(T&& x) / auto&& x             │
│ 推导     传左值 T=X&  →  forward 返回 X&   （左值）                        │
│          传右值 T=X   →  forward 返回 X&&  （右值）                        │
│ 折叠     有 & 即 &，全 && 才 &&                                            │
│ 显式<T>  remove_reference_t<T>& 是不可推导语境；信息只存在 T 中            │
│ vs move  move 无条件转右值（用于 X&&）；forward 原样还原（用于 T&&）       │
│ 场景     make_unique/make_shared、emplace_back、thread/invoke/bind、装饰器 │
│ 陷阱     ① 只在最后一次使用时 forward ② 转发构造劫持拷贝构造 → 加约束      │
│          ③ 失败五种：{}、0/NULL、static const 未定义、重载函数名、位域     │
│ 参考     Effective Modern C++ Item 23 / 24 / 25 / 26 / 28 / 30             │
└───────────────────────────────────────────────────────────────────────────┘
```

---

## 附：配套代码完整实测输出（MSVC 14.44，/std:c++20）

```
== 1. plain / move / forward ==
  wrap_plain(a):                 -> sink(const Tracker&)   lvalue overload
  wrap_plain(Tracker{tmp}):      -> sink(const Tracker&)   lvalue overload
  wrap_move(a):                  -> sink(Tracker&&)        rvalue overload
  wrap_move(Tracker{tmp}):       -> sink(Tracker&&)        rvalue overload
  wrap_forward(a):               -> sink(const Tracker&)   lvalue overload
  wrap_forward(Tracker{tmp}):    -> sink(Tracker&&)        rvalue overload
== 2. deduced T ==
  show_T(a):             T = Tracker&
  show_T(Tracker{tmp}):  T = Tracker
== 4. hand-written my_forward ==
  wrap_my_forward(a):             -> sink(const Tracker&)
  wrap_my_forward(std::move(a)):  -> sink(Tracker&&)
== 5. factory with variadic forwarding ==
  my_make_unique<Widget>(t, 1):             -> Widget(const Tracker&, int)
  my_make_unique<Widget>(Tracker{tmp}, 2):  -> Widget(Tracker&&, int)
== 6. pitfall: forward the same arg twice ==
  forward_twice(src)  (lvalue):         [copy][copy]  a.tag="src"  b.tag="src"
  forward_twice(Tracker{tmp}) (rvalue): [move][move]  a.tag="tmp"  b.tag=""
== 7. pitfall: forwarding ctor vs copy ctor (constrained) ==
  Person p1("Alice"):  [Person forwarding ctor]
  Person p2(p1):       [Person copy ctor]
== 8. auto&& in generic lambda ==
  lam(a):            -> sink(const Tracker&)
  lam(Tracker{tmp}): -> sink(Tracker&&)
== 9. push_back vs emplace_back ==
  v.push_back(Tracker{x}):  [Tracker move ctor]
  v.emplace_back("y"):      (no copy / move: constructed in place)
```
