// template_spec_demo.cpp —— 模板全特化 / 偏特化 / 函数模板重载 实测
// 编译：cl /std:c++20 /EHsc /utf-8 /W4 template_spec_demo.cpp
//       g++ -std=c++20 template_spec_demo.cpp
#include <concepts>
#include <cstdio>
#include <string>
#include <type_traits>
#include <unordered_map>
#include <vector>

#ifdef _WIN32
#include <windows.h>
#endif

#define SECTION(s) std::printf("\n==== %s ====\n", s)

// =====================================================================
// 1. 类模板全特化：特化版本和主模板是两个“毫无关系”的类
// =====================================================================
template <typename T>
class Printer {
public:
    void Print(const T& v) { std::printf("  [主模板]  Print(%d)\n", static_cast<int>(v)); }
};

template <>
class Printer<bool> {          // 全特化：成员可以完全不同，甚至没有 Print
public:
    void Print(bool v);         // 只声明
    void PrintUpper(bool v) { std::printf("  [全特化 bool] %s\n", v ? "TRUE" : "FALSE"); }
};
// 全特化类的成员在类外定义时 —— 不要再写 template<>（它已经是普通类了）
void Printer<bool>::Print(bool v) { std::printf("  [全特化 bool] %s\n", v ? "true" : "false"); }

// 只特化类模板的某一个成员函数（不用整类重写）
template <typename T>
class Logger {
public:
    void Log() { std::printf("  Logger<T>::Log      通用版本\n"); }
    void Other() { std::printf("  Logger<T>::Other    通用版本（特化 Log 不影响它）\n"); }
};
template <>
void Logger<std::string>::Log() { std::printf("  Logger<string>::Log 成员函数全特化\n"); }

// =====================================================================
// 2. 类模板偏特化：各种“形状”
// =====================================================================
template <typename T> struct Traits             { static constexpr const char* name = "主模板 T"; };
template <typename T> struct Traits<T*>         { static constexpr const char* name = "偏特化 T*"; };
template <typename T> struct Traits<const T>    { static constexpr const char* name = "偏特化 const T"; };
template <typename T> struct Traits<T&>         { static constexpr const char* name = "偏特化 T&"; };
template <typename T, size_t N> struct Traits<T[N]> { static constexpr const char* name = "偏特化 T[N]"; };
template <typename T> struct Traits<std::vector<T>> { static constexpr const char* name = "偏特化 vector<T>"; };
template <typename R, typename... A> struct Traits<R(*)(A...)> { static constexpr const char* name = "偏特化 函数指针 R(*)(A...)"; };
template <typename T, typename C> struct Traits<T C::*> { static constexpr const char* name = "偏特化 成员指针 T C::*"; };
template <> struct Traits<int*> { static constexpr const char* name = "全特化 int*"; };

struct Hero { int hp; };
int Add(int a, int b) { return a + b; }

// 多参数：固定其中一部分参数
template <typename K, typename V> struct Pair             { static constexpr const char* name = "主模板 <K,V>"; };
template <typename K>             struct Pair<K, int>     { static constexpr const char* name = "偏特化 <K,int>"; };
template <typename T>             struct Pair<T, T>       { static constexpr const char* name = "偏特化 <T,T>"; };
template <typename K, typename V> struct Pair<K*, V*>     { static constexpr const char* name = "偏特化 <K*,V*>"; };
// Pair<int,int> 同时匹配 <K,int> 和 <T,T>，二者谁也不比谁更特殊 → 歧义。给它一个全特化消歧：
template <>                       struct Pair<int, int>   { static constexpr const char* name = "全特化 <int,int>（消歧）"; };

// 非类型参数的偏特化
template <typename T, int N> struct Buffer      { static constexpr const char* name = "主模板 Buffer<T,N>"; };
template <typename T>        struct Buffer<T, 0>{ static constexpr const char* name = "偏特化 Buffer<T,0>（空缓冲）"; };

// =====================================================================
// 3. 偏特化的核心用途：在编译期“拆解类型” —— 手写 type_traits
// =====================================================================
template <typename T> struct MyRemovePtr     { using type = T; };
template <typename T> struct MyRemovePtr<T*> { using type = T; };

template <typename T> struct MyIsPointer     : std::false_type {};
template <typename T> struct MyIsPointer<T*> : std::true_type  {};

template <typename T> struct ElemOf;                                   // 只声明主模板：不支持的类型直接编译报错
template <typename T> struct ElemOf<std::vector<T>> { using type = T; };
template <typename T, size_t N> struct ElemOf<T[N]> { using type = T; };

// 编译期递归的终止条件：全特化
template <unsigned N> struct Factorial    { static constexpr unsigned long long value = N * Factorial<N - 1>::value; };
template <>           struct Factorial<0> { static constexpr unsigned long long value = 1; };

// =====================================================================
// 4. 函数模板：只能全特化，不能偏特化；全特化不参与重载决议
// =====================================================================
// 4.1 Dimov/Abrahams 例子 —— 声明顺序不同，结果不同
namespace order1 {
template <typename T> const char* f(T)  { return "(a) f(T)"; }
template <typename T> const char* f(T*) { return "(b) f(T*)"; }
template <> const char* f<>(int*)       { return "(c) f<int*> 全特化"; }   // 此时 (b) 更特殊 → c 特化的是 (b)
}
namespace order2 {
template <typename T> const char* f(T)  { return "(a) f(T)"; }
template <> const char* f<>(int*)       { return "(c) f<int*> 全特化"; }   // 此时只有 (a) → c 特化的是 (a)，T = int*
template <typename T> const char* f(T*) { return "(b) f(T*)"; }
}

// 4.2 用重载代替“函数偏特化”；非模板函数在同等匹配时优先
template <typename T> const char* g(T)   { return "模板 g(T)"; }
template <typename T> const char* g(T*)  { return "模板重载 g(T*)（相当于偏特化）"; }
const char* g(int)                       { return "非模板 g(int)"; }
template <> const char* g<double>(double){ return "全特化 g<double>"; }

// 4.3 想要真正的“函数偏特化”：转发给类模板的静态成员
template <typename T> struct SerializeImpl {
    static std::string Do(const T&) { return "通用序列化"; }
};
template <typename T> struct SerializeImpl<std::vector<T>> {
    static std::string Do(const std::vector<T>& v) { return "vector 序列化, size=" + std::to_string(v.size()); }
};
template <typename T> std::string Serialize(const T& v) { return SerializeImpl<T>::Do(v); }

// 4.4 现代替代：if constexpr（C++17）
template <typename T> std::string Describe(const T&) {
    if constexpr (std::is_pointer_v<T>)        return "if constexpr: 指针";
    else if constexpr (std::is_integral_v<T>)  return "if constexpr: 整数";
    else                                       return "if constexpr: 其他";
}

// =====================================================================
// 5. C++20 concepts：带约束的偏特化，约束越强越“特殊”
// =====================================================================
template <typename T> struct Codec { static constexpr const char* name = "Codec 主模板"; };
template <std::integral T> struct Codec<T> { static constexpr const char* name = "Codec<integral>"; };
template <typename T> requires std::integral<T> && (sizeof(T) == 1)
struct Codec<T> { static constexpr const char* name = "Codec<integral && 1字节>（约束更强，胜出）"; };

// =====================================================================
// 6. 变量模板（C++14）也能全特化 / 偏特化
// =====================================================================
template <typename T> constexpr const char* kTypeName     = "unknown";
template <typename T> constexpr const char* kTypeName<T*> = "pointer";   // 偏特化
template <>           constexpr const char* kTypeName<int> = "int";      // 全特化（注意 >= 会被当成一个记号，必须加空格）

// =====================================================================
// 7. 为自定义类型特化标准库模板：std::hash（合法且常用）
// =====================================================================
struct GridPos { int x, y; bool operator==(const GridPos&) const = default; };
template <>
struct std::hash<GridPos> {
    size_t operator()(const GridPos& p) const noexcept {
        return std::hash<long long>{}((static_cast<long long>(p.x) << 32) ^ static_cast<unsigned>(p.y));
    }
};

int main() {
#ifdef _WIN32
    SetConsoleOutputCP(CP_UTF8);
#endif
    SECTION("1. 类模板全特化");
    Printer<int>{}.Print(42);
    Printer<bool>{}.Print(true);
    Printer<bool>{}.PrintUpper(false);
    Logger<int>{}.Log();
    Logger<std::string>{}.Log();
    Logger<std::string>{}.Other();

    SECTION("2. 类模板偏特化的各种形状");
    std::printf("  Traits<double>            -> %s\n", Traits<double>::name);
    std::printf("  Traits<double*>           -> %s\n", Traits<double*>::name);
    std::printf("  Traits<int*>              -> %s\n", Traits<int*>::name);
    std::printf("  Traits<const double>      -> %s\n", Traits<const double>::name);
    std::printf("  Traits<const double*>     -> %s   （指向 const 的指针，顶层不是 const）\n", Traits<const double*>::name);
    std::printf("  Traits<double* const>     -> %s   （顶层 const）\n", Traits<double* const>::name);
    std::printf("  Traits<float&>            -> %s\n", Traits<float&>::name);
    std::printf("  Traits<char[16]>          -> %s\n", Traits<char[16]>::name);
    std::printf("  Traits<vector<Hero>>      -> %s\n", Traits<std::vector<Hero>>::name);
    std::printf("  Traits<decltype(&Add)>    -> %s\n", Traits<decltype(&Add)>::name);
    std::printf("  Traits<decltype(&Hero::hp)> -> %s\n", Traits<decltype(&Hero::hp)>::name);
    std::printf("  Pair<float,double>        -> %s\n", Pair<float, double>::name);
    std::printf("  Pair<float,int>           -> %s\n", Pair<float, int>::name);
    std::printf("  Pair<float,float>         -> %s\n", Pair<float, float>::name);
    std::printf("  Pair<int*,float*>         -> %s\n", Pair<int*, float*>::name);
    std::printf("  Pair<int,int>             -> %s\n", Pair<int, int>::name);
    std::printf("  Buffer<int,8>             -> %s\n", Buffer<int, 8>::name);
    std::printf("  Buffer<int,0>             -> %s\n", Buffer<int, 0>::name);

    SECTION("3. 偏特化拆解类型 / 全特化终止递归");
    static_assert(std::is_same_v<MyRemovePtr<Hero*>::type, Hero>);
    static_assert(MyIsPointer<int*>::value && !MyIsPointer<int>::value);
    static_assert(std::is_same_v<ElemOf<std::vector<float>>::type, float>);
    static_assert(std::is_same_v<ElemOf<int[3]>::type, int>);
    std::printf("  MyRemovePtr / MyIsPointer / ElemOf 全部 static_assert 通过\n");
    std::printf("  Factorial<20> = %llu（编译期计算）\n", Factorial<20>::value);

    SECTION("4.1 函数模板全特化不参与重载决议（Dimov/Abrahams）");
    int x = 0;
    std::printf("  顺序 a,b,c：f(&x) -> %s\n", order1::f(&x));
    std::printf("  顺序 a,c,b：f(&x) -> %s   <-- 同样的代码换个顺序，特化被“绕过”了\n", order2::f(&x));

    SECTION("4.2 重载决议：非模板 > 更特殊的模板 > 通用模板");
    std::printf("  g(1)      -> %s\n", g(1));
    std::printf("  g<>(1)    -> %s   （显式写 <> 排除非模板函数）\n", g<>(1));
    std::printf("  g(1L)     -> %s\n", g(1L));
    std::printf("  g(&x)     -> %s\n", g(&x));
    std::printf("  g(3.14)   -> %s\n", g(3.14));

    SECTION("4.3 / 4.4 函数“偏特化”的两种替代");
    std::printf("  Serialize(42)         -> %s\n", Serialize(42).c_str());
    std::printf("  Serialize(vector{1,2})-> %s\n", Serialize(std::vector<int>{1, 2}).c_str());
    std::printf("  Describe(&x)          -> %s\n", Describe(&x).c_str());
    std::printf("  Describe(7)           -> %s\n", Describe(7).c_str());
    std::printf("  Describe(1.5)         -> %s\n", Describe(1.5).c_str());

    SECTION("5. C++20 concepts 约束偏特化");
    std::printf("  Codec<double> -> %s\n", Codec<double>::name);
    std::printf("  Codec<int>    -> %s\n", Codec<int>::name);
    std::printf("  Codec<char>   -> %s\n", Codec<char>::name);

    SECTION("6. 变量模板特化");
    std::printf("  kTypeName<float>=%s  kTypeName<float*>=%s  kTypeName<int>=%s\n",
                kTypeName<float>, kTypeName<float*>, kTypeName<int>);

    SECTION("7. 特化 std::hash");
    std::unordered_map<GridPos, const char*> grid{{{1, 2}, "防御塔"}, {{3, 4}, "野怪"}};
    std::printf("  grid[{3,4}] = %s\n", grid[{3, 4}]);
    return 0;
}
