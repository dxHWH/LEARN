// forward_demo.cpp —— std::forward 教学配套示例（C++20）
// 编译（MSVC）：cl /std:c++20 /EHsc /utf-8 /nologo forward_demo.cpp
// 编译（GCC） ：g++ -std=c++20 forward_demo.cpp -o forward_demo
#include <iostream>
#include <memory>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

// ---------------------------------------------------------------
// 0. 一个会打印“拷贝/移动”的探针类型
// ---------------------------------------------------------------
struct Tracker {
    std::string tag;
    explicit Tracker(std::string t) : tag(std::move(t)) {}
    Tracker(const Tracker& o) : tag(o.tag) { std::cout << "    [Tracker copy ctor]\n"; }
    Tracker(Tracker&& o) noexcept : tag(std::move(o.tag)) { std::cout << "    [Tracker move ctor]\n"; }
};

// 按值类别重载的“下游函数”
void sink(const Tracker&) { std::cout << "    -> sink(const Tracker&)   lvalue overload\n"; }
void sink(Tracker&&)      { std::cout << "    -> sink(Tracker&&)        rvalue overload\n"; }

// ---------------------------------------------------------------
// 1. 三种转发写法对比
// ---------------------------------------------------------------
template <class T> void wrap_plain(T&& x)   { sink(x); }                  // 直接传：x 有名字 => 永远是左值
template <class T> void wrap_move(T&& x)    { sink(std::move(x)); }       // 无条件 move：左值也被当右值（危险）
template <class T> void wrap_forward(T&& x) { sink(std::forward<T>(x)); } // 完美转发：保持原值类别

// ---------------------------------------------------------------
// 2. 观察万能引用推导出的 T
// ---------------------------------------------------------------
template <class T> void show_T(T&&) {
    if constexpr (std::is_lvalue_reference_v<T>)
        std::cout << "    T = Tracker&  (lvalue passed, T&& collapses to Tracker&)\n";
    else
        std::cout << "    T = Tracker   (rvalue passed, T&& is Tracker&&)\n";
}

// ---------------------------------------------------------------
// 3. 引用折叠规则：编译期验证
// ---------------------------------------------------------------
using LR = int&;
using RR = int&&;
static_assert(std::is_same_v<LR&,  int&>);   // &  + &  -> &
static_assert(std::is_same_v<LR&&, int&>);   // &  + && -> &
static_assert(std::is_same_v<RR&,  int&>);   // && + &  -> &
static_assert(std::is_same_v<RR&&, int&&>);  // && + && -> &&  （只有全是 && 才得到 &&）

// ---------------------------------------------------------------
// 4. 手写一个 std::forward（与标准库实现等价）
// ---------------------------------------------------------------
template <class T>
constexpr T&& my_forward(std::remove_reference_t<T>& t) noexcept {   // 接收左值
    return static_cast<T&&>(t);
}
template <class T>
constexpr T&& my_forward(std::remove_reference_t<T>&& t) noexcept {  // 接收右值
    static_assert(!std::is_lvalue_reference_v<T>, "cannot forward an rvalue as an lvalue");
    return static_cast<T&&>(t);
}
template <class T> void wrap_my_forward(T&& x) { sink(my_forward<T>(x)); }

// ---------------------------------------------------------------
// 5. 典型应用：工厂函数（make_unique 的简化版）+ 可变参数包
// ---------------------------------------------------------------
struct Widget {
    Widget(const Tracker&, int) { std::cout << "    -> Widget(const Tracker&, int)\n"; }
    Widget(Tracker&&, int)      { std::cout << "    -> Widget(Tracker&&, int)\n"; }
};
template <class T, class... Args>
std::unique_ptr<T> my_make_unique(Args&&... args) {
    return std::unique_ptr<T>(new T(std::forward<Args>(args)...));  // 每个参数各自保持值类别
}

// ---------------------------------------------------------------
// 6. 陷阱：同一个参数 forward 两次
// ---------------------------------------------------------------
template <class T> void forward_twice(T&& x) {
    Tracker a(std::forward<T>(x));
    Tracker b(std::forward<T>(x));  // 若 x 原本是右值，这里拿到的是“被掏空”的对象
    std::cout << "    a.tag=\"" << a.tag << "\"  b.tag=\"" << b.tag << "\"\n";
}

// ---------------------------------------------------------------
// 7. 陷阱：转发构造函数“劫持”拷贝构造 —— 用 C++20 约束修复
// ---------------------------------------------------------------
struct Person {
    std::string name;
    template <class T>
        requires(!std::is_same_v<std::remove_cvref_t<T>, Person> &&
                 std::is_constructible_v<std::string, T>)
    explicit Person(T&& n) : name(std::forward<T>(n)) { std::cout << "    [Person forwarding ctor]\n"; }
    Person(const Person& o) : name(o.name) { std::cout << "    [Person copy ctor]\n"; }
};

int main() {
    Tracker a{"a"};

    std::cout << "== 1. plain / move / forward ==\n";
    std::cout << "  wrap_plain(a):\n";               wrap_plain(a);
    std::cout << "  wrap_plain(Tracker{tmp}):\n";    wrap_plain(Tracker{"tmp"});
    std::cout << "  wrap_move(a):\n";                wrap_move(a);
    std::cout << "  wrap_move(Tracker{tmp}):\n";     wrap_move(Tracker{"tmp"});
    std::cout << "  wrap_forward(a):\n";             wrap_forward(a);
    std::cout << "  wrap_forward(Tracker{tmp}):\n";  wrap_forward(Tracker{"tmp"});

    std::cout << "\n== 2. deduced T ==\n";
    std::cout << "  show_T(a):\n";                   show_T(a);
    std::cout << "  show_T(Tracker{tmp}):\n";        show_T(Tracker{"tmp"});

    std::cout << "\n== 4. hand-written my_forward ==\n";
    std::cout << "  wrap_my_forward(a):\n";          wrap_my_forward(a);
    std::cout << "  wrap_my_forward(std::move(a)):\n"; wrap_my_forward(std::move(a));

    std::cout << "\n== 5. factory with variadic forwarding ==\n";
    Tracker t{"t"};
    std::cout << "  my_make_unique<Widget>(t, 1):\n";              auto w1 = my_make_unique<Widget>(t, 1);
    std::cout << "  my_make_unique<Widget>(Tracker{tmp}, 2):\n";   auto w2 = my_make_unique<Widget>(Tracker{"tmp"}, 2);

    std::cout << "\n== 6. pitfall: forward the same arg twice ==\n";
    Tracker src{"src"};
    std::cout << "  forward_twice(src)  (lvalue):\n";            forward_twice(src);
    std::cout << "  forward_twice(Tracker{tmp})  (rvalue):\n";   forward_twice(Tracker{"tmp"});

    std::cout << "\n== 7. pitfall: forwarding ctor vs copy ctor (constrained) ==\n";
    std::cout << "  Person p1(\"Alice\"):\n";  Person p1("Alice");
    std::cout << "  Person p2(p1):\n";         Person p2(p1);  // 无约束时会错误匹配模板构造

    std::cout << "\n== 8. auto&& in generic lambda ==\n";
    auto lam = [](auto&& x) { sink(std::forward<decltype(x)>(x)); };
    std::cout << "  lam(a):\n";               lam(a);
    std::cout << "  lam(Tracker{tmp}):\n";    lam(Tracker{"tmp"});

    std::cout << "\n== 9. push_back vs emplace_back ==\n";
    std::vector<Tracker> v;
    v.reserve(4);
    std::cout << "  v.push_back(Tracker{x}):\n";  v.push_back(Tracker{"x"});
    std::cout << "  v.emplace_back(\"y\"):\n";    v.emplace_back("y");
    std::cout << "    (no copy / move: constructed in place)\n";
    return 0;
}
