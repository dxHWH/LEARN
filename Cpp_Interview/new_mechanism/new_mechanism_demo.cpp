// new_mechanism_demo.cpp —— C++ new 机制全景实测（游戏客户端面试向）
// 编译：cl /nologo /std:c++latest /EHsc /O2 /utf-8 new_mechanism_demo.cpp
// 所有 demo 都只用标准 C++ + MSVC 行为观察；“数组 cookie”等属于实现细节，仅供理解 ABI。
#include <cstdio>
#include <cstdlib>
#include <cstdint>
#include <cstddef>
#include <new>
#include <memory>
#include <memory_resource>
#include <vector>
#include <string>
#include <chrono>
#include <algorithm>
#include <stdexcept>
#include <type_traits>
#include <malloc.h>   // _aligned_malloc / _aligned_free

// ============================================================================
// 全局替换 operator new / delete：计数 + 可开关追踪
//   （标准允许程序替换这些“可替换分配函数”，链接时用户版本优先于 CRT 版本）
// ============================================================================
static std::size_t g_allocCount = 0;
static std::size_t g_allocBytes = 0;
static bool        g_trace      = false;

void* operator new(std::size_t sz) {
    ++g_allocCount; g_allocBytes += sz;
    if (g_trace) std::printf("    [全局 operator new] size=%zu\n", sz);
    if (sz == 0) sz = 1;                       // 标准要求 0 字节也要返回唯一非空指针
    for (;;) {                                  // 标准规定的 new_handler 循环
        if (void* p = std::malloc(sz)) return p;
        std::new_handler h = std::get_new_handler();
        if (!h) throw std::bad_alloc();
        h();                                    // 让 handler 想办法（释放内存/换 handler/抛异常/终止）
    }
}
void operator delete(void* p) noexcept {
    if (g_trace && p) std::printf("    [全局 operator delete] %p\n", p);
    std::free(p);
}
void operator delete(void* p, std::size_t sz) noexcept {          // C++14 sized delete
    if (g_trace && p) std::printf("    [全局 operator delete(sized)] %p size=%zu\n", p, sz);
    std::free(p);
}
// C++17 对齐版本：alignof(T) > __STDCPP_DEFAULT_NEW_ALIGNMENT__ 时编译器改调这一族
void* operator new(std::size_t sz, std::align_val_t al) {
    ++g_allocCount; g_allocBytes += sz;
    if (g_trace) std::printf("    [全局 operator new(align_val_t)] size=%zu align=%zu\n", sz, (std::size_t)al);
    if (void* p = _aligned_malloc(sz ? sz : 1, (std::size_t)al)) return p;
    throw std::bad_alloc();
}
void operator delete(void* p, std::align_val_t) noexcept { _aligned_free(p); }
void operator delete(void* p, std::size_t, std::align_val_t) noexcept { _aligned_free(p); }

static void Title(const char* s) { std::printf("\n==================== %s ====================\n", s); }

// ============================================================================
// D1  new 表达式 = operator new（分配）+ 构造；delete = 析构 + operator delete（释放）
// ============================================================================
struct Tracer {
    int id;
    explicit Tracer(int i) : id(i) {
        std::printf("    Tracer(%d) 构造, this=%p\n", id, (void*)this);
        if (i < 0) throw std::runtime_error("构造失败");
    }
    ~Tracer() { std::printf("    ~Tracer(%d) 析构\n", id); }

    // 类专属 operator new/delete：只影响 new Tracer / delete Tracer
    static void* operator new(std::size_t sz) {
        void* p = std::malloc(sz);
        std::printf("    Tracer::operator new(size=%zu) -> %p\n", sz, p);
        if (!p) throw std::bad_alloc();
        return p;
    }
    static void operator delete(void* p) noexcept {
        std::printf("    Tracer::operator delete(%p)\n", p);
        std::free(p);
    }
};

static void D1_NewExpression() {
    Title("D1 new 表达式拆解");
    std::printf("[1] Tracer* t = new Tracer(1); delete t;\n");
    Tracer* t = new Tracer(1);
    delete t;

    std::printf("[2] 构造函数抛异常：编译器自动调用『匹配的』operator delete 回收内存，不调用析构\n");
    try { Tracer* bad = new Tracer(-1); (void)bad; }
    catch (const std::exception& e) { std::printf("    捕获异常: %s\n", e.what()); }

    std::printf("[3] 手动两步走（等价于 new 表达式），注意必须写 ::new，类内 operator new 会隐藏全局 placement new\n");
    void*   raw = Tracer::operator new(sizeof(Tracer));
    Tracer* t2  = ::new (raw) Tracer(2);         // placement new：只构造不分配
    t2->~Tracer();                               // 手动析构
    Tracer::operator delete(raw);                // 手动释放

    std::printf("[4] ::new / ::delete 强制走全局版本，跳过类专属版本\n");
    g_trace = true;
    Tracer* t3 = ::new Tracer(3);
    ::delete t3;
    g_trace = false;
}

// ============================================================================
// D2  new[] / delete[] 与数组 cookie
// ============================================================================
static void* g_lastRaw = nullptr;
#define ARRAY_NEW(Name)                                                                   \
    static void* operator new[](std::size_t sz) {                                         \
        void* p = std::malloc(sz); g_lastRaw = p;                                         \
        std::printf("    " Name "::operator new[](size=%zu) 原始块=%p\n", sz, p);           \
        return p;                                                                         \
    }

struct PodItem   { int v; ARRAY_NEW("PodItem")
    static void operator delete[](void* p) noexcept { std::printf("    PodItem::operator delete[](%p)\n", p); std::free(p); } };
struct DtorItem  { int v; ~DtorItem() {} ARRAY_NEW("DtorItem")       // 用户提供析构 → 非平凡析构
    static void operator delete[](void* p) noexcept { std::printf("    DtorItem::operator delete[](%p)\n", p); std::free(p); } };
struct SizedItem { int v; ARRAY_NEW("SizedItem")                      // 平凡析构，但释放函数要 size
    static void operator delete[](void* p, std::size_t sz) noexcept { std::printf("    SizedItem::operator delete[](%p, size=%zu)\n", p, sz); std::free(p); } };

template <class T>
static void ArrayCase(const char* name, std::size_t n) {
    std::printf("[new %s[%zu]]  sizeof(%s)=%zu, 平凡析构=%s\n", name, n, name, sizeof(T),
                std::is_trivially_destructible_v<T> ? "是" : "否");
    T* arr = new T[n];
    std::ptrdiff_t off = (char*)arr - (char*)g_lastRaw;
    std::printf("    表达式返回=%p, 与原始块偏移=%td 字节", (void*)arr, off);
    if (off >= (std::ptrdiff_t)sizeof(std::size_t)) std::printf("  → cookie 里存的元素个数=%zu", *(std::size_t*)g_lastRaw);
    std::printf("\n");
    delete[] arr;
}

static void D2_ArrayCookie() {
    Title("D2 new[] 与数组 cookie");
    ArrayCase<PodItem>("PodItem", 5);
    ArrayCase<DtorItem>("DtorItem", 5);
    ArrayCase<SizedItem>("SizedItem", 5);

    g_trace = true;
    std::printf("[new int[0]] 合法，返回非空且唯一的指针\n");
    int* z = new int[0];
    std::printf("    返回 %p (非空=%s)\n", (void*)z, z ? "是" : "否");
    delete[] z;
    g_trace = false;
}

// ============================================================================
// D3  分配失败：new_handler / bad_alloc / nothrow / 数组长度溢出
// ============================================================================
static int   g_handlerCalls = 0;
static char* g_reserve      = nullptr;     // 游戏里常见的“应急内存”

static void MyNewHandler() {
    ++g_handlerCalls;
    std::printf("    new_handler 第 %d 次被调用\n", g_handlerCalls);
    if (g_reserve) {                        // 第一次：释放应急内存，让 operator new 重试
        std::printf("    → 释放 64MB 应急内存后重试（真实项目里还会触发资源卸载/GC）\n");
        delete[] g_reserve; g_reserve = nullptr;
        return;
    }
    std::printf("    → 已无可释放内存，卸载 handler，下次失败将抛 bad_alloc\n");
    std::set_new_handler(nullptr);
}

struct Thrower { Thrower() { throw std::runtime_error("Thrower 构造失败"); } };

static void D3_Failure() {
    Title("D3 分配失败的处理路径");
    volatile std::size_t huge = std::size_t(1) << 50;   // 1 PB，必然失败；volatile 防止编译期优化

    std::printf("[1] 普通 new：失败 → 循环调用 new_handler → 最终抛 std::bad_alloc\n");
    g_reserve = new char[64 << 20];
    std::set_new_handler(MyNewHandler);
    try { char* p = new char[huge]; (void)p; }
    catch (const std::bad_alloc& e) { std::printf("    捕获 %s\n", e.what()); }

    std::printf("[2] new (std::nothrow)：分配失败返回 nullptr，不抛异常\n");
    char* q = new (std::nothrow) char[huge];
    std::printf("    返回 %p\n", (void*)q);

    std::printf("[3] new (std::nothrow) 只管『分配』不管『构造』：构造函数的异常照样抛出\n");
    g_trace = true;
    try { Thrower* t = new (std::nothrow) Thrower; (void)t; }
    catch (const std::exception& e) { std::printf("    捕获 %s（内存已被自动释放，见上一行 delete）\n", e.what()); }
    g_trace = false;

    std::printf("[4] 元素个数 * sizeof 溢出 / 负数长度 → std::bad_array_new_length（bad_alloc 的派生类）\n");
    volatile std::size_t n1 = SIZE_MAX / 2;
    try { int* p = new int[n1]; (void)p; }
    catch (const std::bad_array_new_length& e) { std::printf("    捕获 bad_array_new_length: %s\n", e.what()); }
    catch (const std::bad_alloc& e)            { std::printf("    捕获 bad_alloc: %s\n", e.what()); }
    volatile int n2 = -1;
    try { int* p = new int[n2]; (void)p; }
    catch (const std::bad_array_new_length& e) { std::printf("    捕获 bad_array_new_length: %s\n", e.what()); }
    catch (const std::bad_alloc& e)            { std::printf("    捕获 bad_alloc: %s\n", e.what()); }
}

// ============================================================================
// D4  过对齐类型：C++17 起 new 会自动走 align_val_t 版本
// ============================================================================
struct alignas(64) CacheLineData { float v[16]; };   // 例：按缓存行对齐的 SIMD/多线程数据

static void D4_Alignment() {
    Title("D4 过对齐类型与 align_val_t");
    std::printf("__STDCPP_DEFAULT_NEW_ALIGNMENT__ = %zu, alignof(CacheLineData) = %zu\n",
                (std::size_t)__STDCPP_DEFAULT_NEW_ALIGNMENT__, alignof(CacheLineData));
    g_trace = true;
    for (int i = 0; i < 3; ++i) {
        CacheLineData* p = new CacheLineData;
        std::printf("    地址 %p, 地址 %% 64 = %zu\n", (void*)p, (std::size_t)((std::uintptr_t)p % 64));
        delete p;
    }
    std::printf("[vector<CacheLineData>] std::allocator 也会自动走对齐版本:\n");
    std::vector<CacheLineData> vec(4);
    std::printf("    data()=%p, %% 64 = %zu\n", (void*)vec.data(), (std::size_t)((std::uintptr_t)vec.data() % 64));
    g_trace = false;
}

// ============================================================================
// D5  sized delete + 虚析构；C++20 destroying delete
// ============================================================================
struct Base {
    int a = 0;
    virtual ~Base() = default;
    static void operator delete(void* p, std::size_t sz) noexcept {
        std::printf("    Base::operator delete(%p, size=%zu)\n", p, sz);
        ::operator delete(p);
    }
};
struct Derived : Base { char payload[100]{}; };

// destroying delete：operator delete 接管“析构 + 释放”，可用于把对象还给池子
struct PooledEnemy {
    int hp = 100;
    static void operator delete(PooledEnemy* p, std::destroying_delete_t) {
        std::printf("    destroying delete：手动析构 hp=%d 的对象并还给池子（这里简单 free）\n", p->hp);
        p->~PooledEnemy();
        ::operator delete(p);
    }
};

static void D5_SizedAndDestroying() {
    Title("D5 sized delete / destroying delete");
    std::printf("sizeof(Base)=%zu, sizeof(Derived)=%zu\n", sizeof(Base), sizeof(Derived));
    Base* b = new Derived;
    std::printf("[delete Base* 指向 Derived，有虚析构] 虚『删除析构函数』传入真实大小:\n");
    delete b;
    std::printf("[delete PooledEnemy*]\n");
    PooledEnemy* e = new PooledEnemy;
    delete e;
}

// ============================================================================
// D6  常见写法的隐藏分配次数（性能排查最常用的视角）
// ============================================================================
struct Counter {
    std::size_t c0, b0;
    Counter() : c0(g_allocCount), b0(g_allocBytes) {}
    void Report(const char* what) const {
        std::printf("    %-46s 分配 %3zu 次, %6zu 字节\n", what, g_allocCount - c0, g_allocBytes - b0);
    }
};
struct Monster { int hp = 100; float pos[3]{}; };

static void D6_HiddenAllocations() {
    Title("D6 隐藏的堆分配");
    { Counter c; std::string s = "short_name";                         c.Report("std::string 10 字符（SSO）"); }
    { Counter c; std::string s = "a_very_long_monster_resource_path";  c.Report("std::string 33 字符"); }
    { Counter c; std::vector<int> v; for (int i = 0; i < 1000; ++i) v.push_back(i); c.Report("vector push_back x1000（无 reserve）"); }
    { Counter c; std::vector<int> v; v.reserve(1000); for (int i = 0; i < 1000; ++i) v.push_back(i); c.Report("vector push_back x1000（先 reserve）"); }
    { Counter c; std::shared_ptr<Monster> p(new Monster);              c.Report("shared_ptr<Monster>(new Monster)"); }
    { Counter c; auto p = std::make_shared<Monster>();                 c.Report("make_shared<Monster>()"); }
    { Counter c; auto p = std::make_unique<Monster>();                 c.Report("make_unique<Monster>()"); }
}

// ============================================================================
// D7  性能：逐个 new/delete vs 对象池(placement new) vs 帧分配器 vs pmr
// ============================================================================
struct Bullet {
    float pos[3], vel[3]; float life; int owner; std::uint64_t pad[4];
    explicit Bullet(float x) : pos{x, 0, 0}, vel{1, 0, 0}, life(1.f), owner(0), pad{} {}
};
static_assert(sizeof(Bullet) == 64);

template <class T>
class ObjectPool {                                  // 定长空闲链表池
    union Slot { Slot* next; alignas(T) unsigned char storage[sizeof(T)]; };
    std::unique_ptr<Slot[]> slots_;
    Slot* free_ = nullptr;
public:
    explicit ObjectPool(std::size_t n) : slots_(new Slot[n]) {
        for (std::size_t i = n; i-- > 0;) { slots_[i].next = free_; free_ = &slots_[i]; }
    }
    template <class... A> T* Create(A&&... a) {
        Slot* s = free_; if (!s) return nullptr;
        free_ = s->next;
        return std::construct_at(reinterpret_cast<T*>(s->storage), std::forward<A>(a)...);  // = placement new
    }
    void Destroy(T* p) {
        std::destroy_at(p);
        Slot* s = reinterpret_cast<Slot*>(p);
        s->next = free_; free_ = s;
    }
};

class FrameArena {                                  // 线性/帧分配器：只 bump，不单独释放
    std::unique_ptr<std::byte[]> buf_; std::size_t cap_, off_ = 0;
public:
    explicit FrameArena(std::size_t cap) : buf_(new std::byte[cap]), cap_(cap) {}
    void* Alloc(std::size_t sz, std::size_t al) {
        std::size_t p = (off_ + al - 1) & ~(al - 1);
        if (p + sz > cap_) return nullptr;
        off_ = p + sz; return buf_.get() + p;
    }
    template <class T, class... A> T* Create(A&&... a) {
        return std::construct_at(static_cast<T*>(Alloc(sizeof(T), alignof(T))), std::forward<A>(a)...);
    }
    void Reset() { off_ = 0; }                      // 一帧结束整体丢弃（要求对象平凡析构或已手动析构）
};

using Clock = std::chrono::steady_clock;
static double Ms(Clock::duration d) { return std::chrono::duration<double, std::milli>(d).count(); }

static void D7_Perf() {
    Title("D7 性能对比：100 万个 64B 子弹，分配→写入→释放");
    constexpr int N = 1'000'000, REP = 5;
    std::vector<Bullet*> ptrs(N);
    volatile float sink = 0;
    double best[4] = {1e9, 1e9, 1e9, 1e9};

    ObjectPool<Bullet> pool(N);
    FrameArena arena(std::size_t(N) * sizeof(Bullet) + 64);
    std::vector<std::byte> pmrBuf(std::size_t(N) * sizeof(Bullet) + 64);

    for (int r = 0; r < REP; ++r) {
        // 1) 逐个 new / delete
        auto t0 = Clock::now();
        for (int i = 0; i < N; ++i) ptrs[i] = new Bullet(float(i));
        float s = 0; for (int i = 0; i < N; ++i) s += ptrs[i]->pos[0];
        for (int i = 0; i < N; ++i) delete ptrs[i];
        best[0] = std::min(best[0], Ms(Clock::now() - t0)); sink = sink + s;

        // 2) 对象池 + placement new
        t0 = Clock::now();
        for (int i = 0; i < N; ++i) ptrs[i] = pool.Create(float(i));
        s = 0; for (int i = 0; i < N; ++i) s += ptrs[i]->pos[0];
        for (int i = 0; i < N; ++i) pool.Destroy(ptrs[i]);
        best[1] = std::min(best[1], Ms(Clock::now() - t0)); sink = sink + s;

        // 3) 帧分配器
        t0 = Clock::now();
        for (int i = 0; i < N; ++i) ptrs[i] = arena.Create<Bullet>(float(i));
        s = 0; for (int i = 0; i < N; ++i) s += ptrs[i]->pos[0];
        arena.Reset();
        best[2] = std::min(best[2], Ms(Clock::now() - t0)); sink = sink + s;

        // 4) std::pmr::monotonic_buffer_resource
        t0 = Clock::now();
        {
            std::pmr::monotonic_buffer_resource mr(pmrBuf.data(), pmrBuf.size(), std::pmr::null_memory_resource());
            std::pmr::polymorphic_allocator<Bullet> alloc(&mr);
            for (int i = 0; i < N; ++i) ptrs[i] = alloc.new_object<Bullet>(float(i));
            s = 0; for (int i = 0; i < N; ++i) s += ptrs[i]->pos[0];
        }   // mr 析构时整体释放
        best[3] = std::min(best[3], Ms(Clock::now() - t0)); sink = sink + s;
    }
    const char* names[4] = {"逐个 new/delete（malloc → HeapAlloc）", "ObjectPool + placement new",
                            "FrameArena（bump 指针 + Reset）", "pmr::monotonic_buffer_resource"};
    for (int k = 0; k < 4; ++k)
        std::printf("    %-40s %8.2f ms  (%5.1f ns/个, %5.1fx)\n", names[k], best[k], best[k] * 1e6 / N, best[0] / best[k]);
    std::printf("    (sink=%g)\n", (double)sink);
}

int main() {
    D1_NewExpression();
    D2_ArrayCookie();
    D3_Failure();
    D4_Alignment();
    D5_SizedAndDestroying();
    D6_HiddenAllocations();
    D7_Perf();
    return 0;
}
