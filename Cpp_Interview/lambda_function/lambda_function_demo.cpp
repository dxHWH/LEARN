// lambda_function_demo.cpp
// 配套《C++ lambda 与 std::function（游戏客户端面试向）》的实测代码
// 编译：cl /std:c++latest /EHsc /O2 /utf-8 lambda_function_demo.cpp
//      g++ -std=c++23 -O2 lambda_function_demo.cpp
#include <algorithm>
#include <array>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <functional>
#include <memory>
#include <new>
#include <set>
#include <string>
#include <type_traits>
#include <vector>

// ---------- 全局 operator new 计数：用来观察 std::function 是否上堆 ----------
static size_t g_allocs = 0;
void* operator new(std::size_t n) {
    ++g_allocs;
    if (void* p = std::malloc(n ? n : 1)) return p;
    throw std::bad_alloc{};
}
void operator delete(void* p) noexcept { std::free(p); }
void operator delete(void* p, std::size_t) noexcept { std::free(p); }

static void Title(const char* t) { std::printf("\n===== %s =====\n", t); }

// ---------------------------------------------------------------------------
// 1. 闭包对象的大小 = 捕获成员的大小
// ---------------------------------------------------------------------------
static void Demo1_ClosureSize() {
    Title("1. 闭包对象大小");
    int a = 1;
    double d = 2.0;
    std::string s(100, 'x');
    auto l0 = [] { return 0; };
    auto l1 = [a] { return a; };
    auto l2 = [&a] { return a; };
    auto l3 = [a, d] { return a + d; };
    auto l4 = [&] { return a + d; };
    auto l5 = [s] { return s.size(); };
    std::printf("[]          sizeof = %zu\n", sizeof(l0));
    std::printf("[a]  (int)  sizeof = %zu\n", sizeof(l1));
    std::printf("[&a]        sizeof = %zu\n", sizeof(l2));
    std::printf("[a, d]      sizeof = %zu\n", sizeof(l3));
    std::printf("[&] (a,d)   sizeof = %zu\n", sizeof(l4));
    std::printf("[s] string  sizeof = %zu (sizeof(std::string)=%zu)\n", sizeof(l5), sizeof(std::string));
    (void)l0(); (void)l1(); (void)l2(); (void)l3(); (void)l4(); (void)l5();
}

// ---------------------------------------------------------------------------
// 2. 每个 lambda 都是独一无二的类型；无捕获 lambda 可转函数指针
// ---------------------------------------------------------------------------
static void Demo2_TypeAndFnPtr() {
    Title("2. 唯一类型 & 函数指针转换");
    auto f1 = [] {};
    auto f2 = [] {};
    std::printf("两个一模一样的 []{} 类型相同吗? %d\n", (int)std::is_same_v<decltype(f1), decltype(f2)>);

    int (*fp)(int) = [](int x) { return x * 2; };   // 隐式转换
    auto fp2 = +[](int x) { return x + 1; };         // 一元 + 强制转换
    static_assert(std::is_same_v<decltype(fp2), int (*)(int)>);
    std::printf("fp(21) = %d, fp2(41) = %d\n", fp(21), fp2(41));

    // C++20：无捕获 lambda 可默认构造 / 赋值 -> 可直接放进模板参数
    auto cmp = [](int x, int y) { return x > y; };
    std::set<int, decltype(cmp)> desc{3, 1, 2};      // 不需要再把 cmp 传进构造函数
    std::printf("std::set<int, decltype(cmp)> 遍历:");
    for (int v : desc) std::printf(" %d", v);
    std::printf("\n");
}

// ---------------------------------------------------------------------------
// 3. 值捕获是“创建时快照”；mutable；闭包拷贝会拷贝状态
// ---------------------------------------------------------------------------
static void Demo3_CaptureSemantics() {
    Title("3. 值捕获 / 引用捕获 / mutable");
    int x = 10;
    auto byVal = [x] { return x; };
    auto byRef = [&x] { return x; };
    x = 20;
    std::printf("x 改成 20 后: byVal() = %d, byRef() = %d\n", byVal(), byRef());

    auto counter = [n = 0]() mutable { return ++n; };
    counter();
    counter();
    std::printf("counter 调用第 3 次 = %d\n", counter());
    auto copy = counter;                 // 拷贝闭包 = 拷贝内部的 n
    std::printf("copy() = %d, counter() = %d  (两份独立状态)\n", copy(), counter());

    static int s_hits = 0;               // 静态/全局变量不需要也不能被“捕获”
    auto hit = [] { ++s_hits; };
    hit(); hit();
    std::printf("无捕获 lambda 修改静态变量 s_hits = %d\n", s_hits);
}

// ---------------------------------------------------------------------------
// 4. 成员函数里的 this 捕获陷阱
// ---------------------------------------------------------------------------
struct Hero {
    int hp = 100;
    auto GetterByThis()  { return [this]  { return hp; }; }  // 捕获的是指针 this
    auto GetterByCopy()  { return [*this] { return hp; }; }  // C++17：拷贝整个对象
    auto GetterByField() { return [hp = hp] { return hp; }; } // 只拷贝需要的字段（推荐）
};

static void Demo4_This() {
    Title("4. this 捕获");
    Hero h;
    auto g1 = h.GetterByThis();
    auto g2 = h.GetterByCopy();
    auto g3 = h.GetterByField();
    h.hp = 50;
    std::printf("hp 改成 50 后: [this]=%d  [*this]=%d  [hp=hp]=%d\n", g1(), g2(), g3());
    std::printf("sizeof: [this]=%zu  [*this]=%zu  [hp=hp]=%zu\n", sizeof(g1), sizeof(g2), sizeof(g3));
    // 若 h 先析构再调用 g1() -> 悬垂 this，未定义行为（本 demo 不触发）
}

// ---------------------------------------------------------------------------
// 5. std::function 的大小与小对象优化（SBO）
// ---------------------------------------------------------------------------
template <size_t N>
static void TestSbo() {
    std::array<char, N> buf{};
    size_t before = g_allocs;
    {
        std::function<int()> fn = [buf] { return (int)buf[0]; };
        (void)fn();
    }
    std::printf("  捕获 %3zu 字节 -> 堆分配 %zu 次\n", N, g_allocs - before);
}

static int FreeFunc() { return 7; }

static void Demo5_FunctionSbo() {
    Title("5. std::function 大小与 SBO");
    std::printf("sizeof(std::function<void()>)     = %zu\n", sizeof(std::function<void()>));
    std::printf("sizeof(std::function<int(int,int)>) = %zu\n", sizeof(std::function<int(int, int)>));
    std::printf("sizeof(void*)                      = %zu\n", sizeof(void*));
    TestSbo<8>();
    TestSbo<16>();
    TestSbo<32>();
    TestSbo<48>();
    TestSbo<56>();
    TestSbo<57>();
    TestSbo<64>();
    TestSbo<128>();

    size_t before = g_allocs;
    { std::function<int()> fn = FreeFunc; (void)fn(); }
    std::printf("  存普通函数指针 -> 堆分配 %zu 次\n", g_allocs - before);

    std::string s = "a long string that definitely exceeds SSO capacity";
    before = g_allocs;
    { std::function<size_t()> fn = [s] { return s.size(); }; (void)fn(); }
    std::printf("  捕获一个长 std::string -> 堆分配 %zu 次（string 自身拷贝 1 次）\n", g_allocs - before);

    std::array<char, 128> big{};
    std::function<int()> f1 = [big] { return (int)big[0]; };
    before = g_allocs;
    std::function<int()> f2 = f1;                // 拷贝 = 深拷贝闭包
    std::printf("  拷贝一个持大闭包的 std::function -> 堆分配 %zu 次\n", g_allocs - before);
    before = g_allocs;
    std::function<int()> f3 = std::move(f1);     // 移动 = 转移指针
    std::printf("  移动一个持大闭包的 std::function -> 堆分配 %zu 次\n", g_allocs - before);
    (void)f2; (void)f3;
}

// ---------------------------------------------------------------------------
// 6. 空 std::function、成员函数指针
// ---------------------------------------------------------------------------
struct Skill {
    int id;
    int Cast(int level) const { return id * 100 + level; }
};

static void Demo6_EmptyAndMember() {
    Title("6. 空 function / 成员函数");
    std::function<void()> empty;
    std::printf("empty 转 bool = %d\n", (int)static_cast<bool>(empty));
    try {
        empty();
    } catch (const std::bad_function_call& e) {
        std::printf("调用空 function 抛出 std::bad_function_call: %s\n", e.what());
    }
    std::function<int(const Skill&, int)> cast = &Skill::Cast;   // 内部走 std::invoke
    Skill sk{3};
    std::printf("std::function 包成员函数: cast(sk, 5) = %d\n", cast(sk, 5));
    auto bound = [&sk](int lv) { return sk.Cast(lv); };          // 比 std::bind 更直观
    std::printf("lambda 代替 bind: bound(9) = %d\n", bound(9));
}

// ---------------------------------------------------------------------------
// 7. 只可移动的捕获：init capture + move_only_function(C++23)
// ---------------------------------------------------------------------------
static void Demo7_MoveOnly() {
    Title("7. 只可移动的捕获");
    auto up = std::make_unique<int>(42);
    auto task = [p = std::move(up)] { return *p; };
    std::printf("task() = %d, 原 unique_ptr 是否为空 = %d\n", task(), (int)(up == nullptr));
    std::printf("闭包可拷贝构造吗? %d\n", (int)std::is_copy_constructible_v<decltype(task)>);
    // std::function<int()> f = std::move(task);   // 编译错误：std::function 要求可调用对象可拷贝
#if defined(__cpp_lib_move_only_function)
    std::move_only_function<int()> mf = std::move(task);
    std::printf("std::move_only_function 接住了它: mf() = %d\n", mf());
#else
    std::printf("当前标准库没有 std::move_only_function（需要 C++23）\n");
#endif
}

// ---------------------------------------------------------------------------
// 8. 递归 lambda 的三种写法 + 性能
// ---------------------------------------------------------------------------
static void Demo8_Recursive() {
    Title("8. 递归 lambda");
    using clk = std::chrono::steady_clock;
    const int N = 35;

    std::function<int(int)> fib1 = [&fib1](int n) { return n < 2 ? n : fib1(n - 1) + fib1(n - 2); };
    auto fib2 = [](auto self, int n) -> int { return n < 2 ? n : self(self, n - 1) + self(self, n - 2); };

    auto t0 = clk::now();
    int r1 = fib1(N);
    auto t1 = clk::now();
    int r2 = fib2(fib2, N);
    auto t2 = clk::now();
    std::printf("std::function 递归 fib(%d)=%d  %lld ms\n", N, r1,
                (long long)std::chrono::duration_cast<std::chrono::milliseconds>(t1 - t0).count());
    std::printf("auto self 递归     fib(%d)=%d  %lld ms\n", N, r2,
                (long long)std::chrono::duration_cast<std::chrono::milliseconds>(t2 - t1).count());
// MSVC 14.44 已支持 deducing this，但没有定义特性宏，所以额外判断 _MSVC_LANG
#if defined(__cpp_explicit_this_parameter) || (defined(_MSVC_LANG) && _MSVC_LANG > 202002L)
    auto fib3 = [](this auto const& self, int n) -> int { return n < 2 ? n : self(n - 1) + self(n - 2); };
    auto t3 = clk::now();
    int r3 = fib3(N);
    auto t4 = clk::now();
    std::printf("C++23 deducing this fib(%d)=%d  %lld ms\n", N, r3,
                (long long)std::chrono::duration_cast<std::chrono::milliseconds>(t4 - t3).count());
#endif
}

// ---------------------------------------------------------------------------
// 9. 模板参数 vs std::function vs 函数指针：调用开销
// ---------------------------------------------------------------------------
static int g_k = 0;
static int XorK(int i) { return i ^ g_k; }

template <class F>
__declspec(noinline) long long RunTemplate(F f, int n) {
    long long s = 0;
    for (int i = 0; i < n; ++i) s += f(i);
    return s;
}
__declspec(noinline) long long RunFunction(const std::function<int(int)>& f, int n) {
    long long s = 0;
    for (int i = 0; i < n; ++i) s += f(i);
    return s;
}
__declspec(noinline) long long RunFnPtr(int (*f)(int), int n) {
    long long s = 0;
    for (int i = 0; i < n; ++i) s += f(i);
    return s;
}

static void Demo9_Perf(int seed) {
    Title("9. 调用开销对比（/O2，2 亿次调用）");
    using clk = std::chrono::steady_clock;
    const int N = 200'000'000;
    int k = seed;
    g_k = seed;
    auto lam = [k](int i) { return i ^ k; };
    std::function<int(int)> fn = lam;

    auto ms = [](auto a, auto b) {
        return (long long)std::chrono::duration_cast<std::chrono::milliseconds>(b - a).count();
    };
    auto t0 = clk::now(); long long s1 = RunTemplate(lam, N);
    auto t1 = clk::now(); long long s2 = RunFunction(fn, N);
    auto t2 = clk::now(); long long s3 = RunFnPtr(&XorK, N);
    auto t3 = clk::now();
    std::printf("模板参数(可内联)   %5lld ms  sum=%lld\n", ms(t0, t1), s1);
    std::printf("std::function     %5lld ms  sum=%lld\n", ms(t1, t2), s2);
    std::printf("函数指针           %5lld ms  sum=%lld\n", ms(t2, t3), s3);
}

// ---------------------------------------------------------------------------
// 10. 游戏场景：事件回调与对象生命周期
// ---------------------------------------------------------------------------
struct Monster : std::enable_shared_from_this<Monster> {
    std::string name;
    explicit Monster(std::string n) : name(std::move(n)) {}
    ~Monster() { std::printf("  ~Monster(%s) 析构\n", name.c_str()); }
    void OnHit(int dmg) { std::printf("  %s 受到 %d 点伤害\n", name.c_str(), dmg); }
};

struct EventBus {
    std::vector<std::function<void(int)>> handlers;
    void Broadcast(int dmg) { for (auto& h : handlers) h(dmg); }
};

struct Timer {   // 自己持有回调，回调又强引用自己 -> 循环引用
    std::function<void()> onTick;
    ~Timer() { std::printf("  ~Timer 析构\n"); }
};

static void Demo10_GameEvents() {
    Title("10. 事件回调与生命周期");
    EventBus bus;
    {
        auto boss = std::make_shared<Monster>("Boss");
        bus.handlers.push_back([w = std::weak_ptr<Monster>(boss)](int dmg) {
            if (auto sp = w.lock()) sp->OnHit(dmg);
            else std::printf("  目标已销毁，回调安全跳过\n");
        });
        std::printf("第一次广播（Boss 还活着）:\n");
        bus.Broadcast(30);
        std::printf("Boss 离开作用域:\n");
    }
    std::printf("第二次广播（Boss 已死）:\n");
    bus.Broadcast(30);

    std::printf("循环引用演示:\n");
    std::weak_ptr<Timer> watch;
    {
        auto t = std::make_shared<Timer>();
        t->onTick = [t] { (void)t; };            // 回调里强持有 t -> 引用计数永远 >= 1
        watch = t;
        std::printf("  use_count = %ld\n", t.use_count());
    }
    std::printf("  离开作用域后 expired = %d  (没析构 = 泄漏)\n", (int)watch.expired());
    if (auto t = watch.lock()) t->onTick = nullptr;  // 手动打断环，Timer 随即析构
    std::printf("  打断后 expired = %d\n", (int)watch.expired());
}

// ---------------------------------------------------------------------------
// 11. 泛型 lambda / 模板 lambda / constexpr lambda
// ---------------------------------------------------------------------------
static void Demo11_Generic() {
    Title("11. 泛型 / 模板 / constexpr lambda");
    auto add = [](auto a, auto b) { return a + b; };                  // C++14
    std::printf("add(1,2)=%d  add(1.5,2.25)=%.2f\n", add(1, 2), add(1.5, 2.25));
    auto sizeOf = []<class T>(const std::vector<T>& v) { return v.size() * sizeof(T); }; // C++20
    std::printf("vector<double>{1,2,3} 字节数 = %zu\n", sizeOf(std::vector<double>{1, 2, 3}));
    constexpr auto sq = [](int v) { return v * v; };                   // C++17 起 lambda 默认可 constexpr
    static_assert(sq(9) == 81);
    std::array<int, sq(3)> arr{};
    std::printf("std::array<int, sq(3)>::size() = %zu\n", arr.size());
}

int main(int argc, char**) {
    Demo1_ClosureSize();
    Demo2_TypeAndFnPtr();
    Demo3_CaptureSemantics();
    Demo4_This();
    Demo5_FunctionSbo();
    Demo6_EmptyAndMember();
    Demo7_MoveOnly();
    Demo8_Recursive();
    Demo9_Perf(argc + 6);
    Demo10_GameEvents();
    Demo11_Generic();
    return 0;
}
