// new_init_demo.cpp —— new T / new T() / new T{} 的初始化差异实测
//
// 观察方法：先把一块缓冲区刷成 0xAB，再在上面 placement new。
//           如果某个字段读出来仍是 0xABABABAB，说明这次初始化“根本没碰它”。
//           （读取未初始化值在标准里是未定义行为，这里通过 memcpy 读对象字节，仅用于观察）
//
// 编译（Release）：cl /std:c++20 /EHsc /utf-8 /O2 /W4 new_init_demo.cpp
// 编译（Debug）  ：cl /std:c++20 /EHsc /utf-8 /Od /MDd /W4 new_init_demo.cpp /Fe:new_init_demo_debug.exe
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <memory>
#include <new>
#include <string>
#include <vector>

#ifdef _WIN32
#include <windows.h>
#endif

alignas(64) static unsigned char g_buf[256];

__declspec(noinline) uint32_t Word(const void* p, int i) {
    uint32_t v;
    std::memcpy(&v, static_cast<const unsigned char*>(p) + i * 4, 4);
    return v;
}

template <class T>
void Dump(const char* label, const T* p, int nWords) {
    std::printf("  %-26s", label);
    for (int i = 0; i < nWords; ++i) {
        uint32_t w = Word(p, i);
        if (w == 0xABABABABu) std::printf("  [%d]=未初始化", i);
        else                  std::printf("  [%d]=0x%08X", i, w);
    }
    std::printf("\n");
}

// T 后面的 INIT 可以为空（default-init）、()（value-init）、{}（list-init）
#define CASE(T, INIT, N)                                              \
    do {                                                              \
        std::memset(g_buf, 0xAB, sizeof g_buf);                       \
        T* p_ = ::new (static_cast<void*>(g_buf)) T INIT;             \
        Dump("new " #T " " #INIT, p_, N);                             \
        std::destroy_at(p_);                                          \
    } while (0)

// ---------------------------------------------------------------------
// 各种“默认构造函数”形态
// ---------------------------------------------------------------------
struct Pod          { int a; int b; };                                 // 没有任何构造函数（平凡）
struct UserCtor     { int a; int b; UserCtor() : a(1) {} };           // 用户提供，只初始化了 a
struct DefaultedIn  { int a; int b; DefaultedIn() = default; };       // 首次声明处 =default：不算“用户提供”
struct DefaultedOut { int a; int b; DefaultedOut(); };
DefaultedOut::DefaultedOut() = default;                                // 类外 =default：算“用户提供”！
struct WithString   { int a; int b; std::string s; };                 // 隐式构造函数，但非平凡（要构造 string）
struct WithNsdmi    { int a = 5; int b; };                             // 有默认成员初始化器 → 构造函数非平凡
struct Vec3         { float x, y, z; Vec3() {} };                     // 模仿 UE 的 FVector：为了性能故意不初始化
struct Base         { int hp; };
struct Derived : Base { int mp; Derived() {} };                       // 用户提供的空构造函数，基类成员也不管

int main() {
#ifdef _WIN32
    SetConsoleOutputCP(CP_UTF8);
#endif
    std::printf("编译配置：%s\n", 
#ifdef _DEBUG
        "Debug（/MDd，调试 CRT）"
#else
        "Release（/O2）"
#endif
    );

    std::printf("\n==== 1. 标量 ====\n");
    CASE(int, , 1);
    CASE(int, (), 1);
    CASE(int, {}, 1);
    CASE(int, (7), 1);
    CASE(double, , 2);
    CASE(double, (), 2);

    std::printf("\n==== 2. 类类型：关键看默认构造函数是不是“用户提供的” ====\n");
    CASE(Pod, , 2);
    CASE(Pod, (), 2);
    CASE(Pod, {}, 2);
    CASE(UserCtor, , 2);
    CASE(UserCtor, (), 2);
    CASE(DefaultedIn, , 2);
    CASE(DefaultedIn, (), 2);
    CASE(DefaultedOut, , 2);
    CASE(DefaultedOut, (), 2);
    CASE(WithString, , 2);
    CASE(WithString, (), 2);
    CASE(WithNsdmi, , 2);
    CASE(WithNsdmi, (), 2);
    CASE(Vec3, , 3);
    CASE(Vec3, (), 3);
    CASE(Vec3, {}, 3);
    CASE(Derived, , 2);
    CASE(Derived, (), 2);

    std::printf("\n==== 3. 真实堆上的 new int / new int[4] ====\n");
    {
        int* old = new int[4]{0x11111111, 0x22222222, 0x33333333, 0x44444444};
        void* oldAddr = old;
        delete[] old;
        int* p = new int[4];             // default-init
        std::printf("  new int[4]    地址%s复用：", p == oldAddr ? "" : "未");
        for (int i = 0; i < 4; ++i) std::printf(" 0x%08X", Word(p, i));
        std::printf("\n");
        delete[] p;
        int* q = new int[4]();           // value-init
        std::printf("  new int[4]()  ：");
        for (int i = 0; i < 4; ++i) std::printf(" 0x%08X", Word(q, i));
        std::printf("\n");
        delete[] q;
        int* r = new int[4]{9};          // 剩余元素 value-init
        std::printf("  new int[4]{9} ：");
        for (int i = 0; i < 4; ++i) std::printf(" 0x%08X", Word(r, i));
        std::printf("\n");
        delete[] r;
    }

    std::printf("\n==== 4. 大块内存：零初始化的代价 ====\n");
    {
        using Clock = std::chrono::steady_clock;
        constexpr size_t N = 256ull << 20;   // 256 MB
        auto ms = [](Clock::time_point a, Clock::time_point b) {
            return std::chrono::duration<double, std::milli>(b - a).count();
        };
        auto t0 = Clock::now();
        char* a = new char[N];                       // 不初始化
        auto t1 = Clock::now();
        std::printf("  new char[256MB]            : %8.3f ms   首字节=0x%02X（新页由系统清零，“看起来是 0”不代表初始化了）\n",
                    ms(t0, t1), static_cast<unsigned char>(Word(a, 0) & 0xFF));
        delete[] a;

        t0 = Clock::now();
        char* b = new char[N]();                     // 零初始化：memset 触碰每一页
        t1 = Clock::now();
        std::printf("  new char[256MB]()          : %8.3f ms\n", ms(t0, t1));
        delete[] b;

        t0 = Clock::now();
        char* c = new char[N];
        std::memset(c, 1, N);                        // 马上要整体覆盖写：只写一次
        t1 = Clock::now();
        std::printf("  new char[N]   + 覆盖写一遍 : %8.3f ms\n", ms(t0, t1));
        delete[] c;

        t0 = Clock::now();
        char* d = new char[N]();
        std::memset(d, 1, N);                        // 先清零再覆盖：写了两遍
        t1 = Clock::now();
        std::printf("  new char[N]() + 覆盖写一遍 : %8.3f ms\n", ms(t0, t1));
        delete[] d;

        t0 = Clock::now();
        auto e = std::make_unique<char[]>(N);        // 值初始化（清零）
        t1 = Clock::now();
        std::printf("  make_unique<char[]>(N)     : %8.3f ms\n", ms(t0, t1));
        e.reset();

        t0 = Clock::now();
        auto f = std::make_unique_for_overwrite<char[]>(N);  // C++20：默认初始化（不清零）
        t1 = Clock::now();
        std::printf("  make_unique_for_overwrite  : %8.3f ms\n", ms(t0, t1));
        f.reset();

        t0 = Clock::now();
        std::vector<char> v(N);                      // vector 也是值初始化
        t1 = Clock::now();
        std::printf("  std::vector<char>(N)       : %8.3f ms\n", ms(t0, t1));
    }
    return 0;
}
