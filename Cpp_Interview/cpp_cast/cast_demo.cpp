// cast_demo.cpp
// 配套《C++ 四种 cast（游戏客户端面试向）》的实测代码
// 编译：cl /nologo /std:c++latest /EHsc /O2 /utf-8 cast_demo.cpp
// 说明：标注 [UB] 的片段是未定义行为，只为观察 MSVC 下的实际表现，工程中禁止这样写。
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

#include <bit>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <memory>
#include <string>
#include <typeinfo>
#include <utility>
#include <vector>

static void Title(const char* t) { std::printf("\n===== %s =====\n", t); }

// ---------------------------------------------------------------------------
// 1. static_cast：数值 / 枚举 / explicit / void* / 右值
// ---------------------------------------------------------------------------
enum class ESkillSlot : uint8_t { Q = 0, W = 1, E = 2, R = 3 };

struct Meter {
    double v;
    explicit Meter(double x) : v(x) {}
    explicit operator double() const { return v; }
};

static void Demo1_StaticBasic() {
    Title("1. static_cast：数值 / 枚举 / explicit / void* / 右值");
    volatile double d1 = 3.99, d2 = -3.99;
    volatile int neg = -1, big = 300, f24 = 16777217;
    std::printf("static_cast<int>(3.99)          = %d   (向 0 截断，不是四舍五入)\n", static_cast<int>(d1));
    std::printf("static_cast<int>(-3.99)         = %d\n", static_cast<int>(d2));
    std::printf("static_cast<unsigned>(-1)       = %u\n", static_cast<unsigned>(neg));
    std::printf("static_cast<uint8_t>(300)       = %u   (按模 256 截断)\n", (unsigned)static_cast<uint8_t>(big));
    std::printf("static_cast<float>(16777217)    = %.1f   (float 只有 24 位尾数)\n", static_cast<float>(f24));

    ESkillSlot s = static_cast<ESkillSlot>(3);
    std::printf("static_cast<ESkillSlot>(3) 回转 = %d\n", static_cast<int>(s));
    volatile int seven = 7;
    ESkillSlot bad = static_cast<ESkillSlot>(seven);   // 编译通过、运行不报错：不做范围检查
    std::printf("static_cast<ESkillSlot>(7) 也照转 = %d\n", static_cast<int>(bad));

    Meter m = static_cast<Meter>(2.5);                 // 调用 explicit 构造
    std::printf("explicit 构造 + explicit operator double = %.1f\n", static_cast<double>(m));

    int hp = 100;
    void* vp = &hp;
    int* ip = static_cast<int*>(vp);
    std::printf("int* -> void* -> int* 往返       = %d\n", *ip);

    std::string s1 = "ArthurTheKnight-very-long-name";
    std::string s2 = static_cast<std::string&&>(s1);   // std::move 的本体
    std::printf("static_cast<string&&> 后 s1.size()=%zu  s2=%s\n", s1.size(), s2.c_str());
}

// ---------------------------------------------------------------------------
// 2. 类层次：上行 / 下行；static_cast 不检查，dynamic_cast 检查
// ---------------------------------------------------------------------------
struct Unit    { virtual ~Unit() = default; int hp = 100; };
struct Warrior : Unit { int rage = 50; };
struct Mage    : Unit { int mana = 300; };

static void Demo2_Hierarchy() {
    Title("2. 类层次：static_cast vs dynamic_cast");
    Warrior w;
    Unit* u = &w;                                          // 上行：隐式即可
    Warrior* pw = static_cast<Warrior*>(u);                // 下行：类型对，没问题
    Mage* pmS = static_cast<Mage*>(u);                     // [UB] 类型错，照样给你一个指针
    Mage* pmD = dynamic_cast<Mage*>(u);                    // 运行期检查 -> nullptr
    std::printf("static_cast<Warrior*>  = %s\n", pw ? "非空(正确)" : "nullptr");
    std::printf("static_cast<Mage*>     = %s   <- 错误类型也不报错\n", pmS ? "非空" : "nullptr");
    std::printf("dynamic_cast<Mage*>    = %s\n", pmD ? "非空" : "nullptr");
    std::printf("[UB] 通过错误指针读 mana = %d   (实际读到的是 Warrior::rage)\n", pmS->mana);
    try {
        Mage& rm = dynamic_cast<Mage&>(*u);
        (void)rm;
    } catch (const std::bad_cast& e) {
        std::printf("dynamic_cast<Mage&> 失败抛 std::bad_cast: %s\n", e.what());
    }
    std::printf("typeid(*u).name() = %s\n", typeid(*u).name());
    std::printf("typeid(u).name()  = %s\n", typeid(u).name());
}

// ---------------------------------------------------------------------------
// 3. 多继承：static_cast 会调整指针，reinterpret_cast 不会
// ---------------------------------------------------------------------------
struct IRenderable { virtual ~IRenderable() = default; virtual const char* Name() const { return "IRenderable::Name"; } int layer = 1; };
struct ITickable   { virtual ~ITickable()   = default; virtual const char* Tick() const { return "ITickable::Tick"; }   int tickGroup = 2; };
struct Sprite : IRenderable, ITickable { int frame = 7; };

static void Demo3_MultipleInheritance() {
    Title("3. 多继承下的指针调整");
    Sprite sp;
    ITickable* tS = static_cast<ITickable*>(&sp);
    ITickable* tR = reinterpret_cast<ITickable*>(&sp);     // [UB] 不调整地址
    std::printf("&sp                                = %p\n", (void*)&sp);
    std::printf("static_cast<ITickable*>(&sp)       = %p   (偏移 +%td)\n", (void*)tS, (char*)tS - (char*)&sp);
    std::printf("reinterpret_cast<ITickable*>(&sp)  = %p   (偏移 +%td)\n", (void*)tR, (char*)tR - (char*)&sp);
    std::printf("tS->tickGroup = %d,  tS->Tick() = %s\n", tS->tickGroup, tS->Tick());
    std::printf("[UB] tR->tickGroup = %d (读到 layer),  tR->Tick() = %s  <- 调错了虚函数\n", tR->tickGroup, tR->Tick());

    std::printf("static_cast<Sprite*>(tS) == &sp    : %d (下行时反向调整)\n", (int)(static_cast<Sprite*>(tS) == &sp));
    std::printf("dynamic_cast<void*>(tS) == &sp     : %d (取最派生对象首地址)\n", (int)(dynamic_cast<void*>(tS) == &sp));
    Sprite* nul = nullptr;
    std::printf("static_cast<ITickable*>(空 Sprite*) = %p (编译器插入判空，空指针不加偏移)\n", (void*)static_cast<ITickable*>(nul));

    IRenderable* r = &sp;
    ITickable* cross = dynamic_cast<ITickable*>(r);        // 交叉转换（侧向转换）
    std::printf("dynamic_cast IRenderable*->ITickable* (cross-cast) == tS : %d\n", (int)(cross == tS));
    // static_cast<ITickable*>(r);   // 编译错误：两者无继承关系，见 compile_fail/cf1
}

// ---------------------------------------------------------------------------
// 4. 虚继承 & 构造/析构期间的 dynamic_cast
// ---------------------------------------------------------------------------
struct VBase   { virtual ~VBase() = default; };
struct VDerived : virtual VBase { int x = 42; };

struct BaseC;
static void Probe(BaseC* p, const char* when);
struct BaseC   { BaseC() { Probe(this, "BaseC 构造函数中  "); } virtual ~BaseC() { Probe(this, "BaseC 析构函数中  "); } };
struct DerivedC : BaseC { DerivedC() { Probe(this, "DerivedC 构造函数中"); } };
static void Probe(BaseC* p, const char* when) {
    std::printf("  %s typeid=%-15s dynamic_cast<DerivedC*>=%s\n", when, typeid(*p).name(),
                dynamic_cast<DerivedC*>(p) ? "非空" : "nullptr");
}

static void Demo4_VirtualAndCtor() {
    Title("4. 虚继承 / 构造析构中的动态类型");
    VDerived vd;
    VBase* vb = &vd;
    // VDerived* bad = static_cast<VDerived*>(vb);  // 编译错误：从虚基类下行，见 compile_fail/cf2
    VDerived* ok = dynamic_cast<VDerived*>(vb);
    std::printf("从虚基类下行只能用 dynamic_cast: x = %d\n", ok->x);
    std::printf("构造/析构过程中动态类型的变化:\n");
    { DerivedC dc; }
}

// ---------------------------------------------------------------------------
// 5. 智能指针的 cast
// ---------------------------------------------------------------------------
static void Demo5_SmartPtr() {
    Title("5. shared_ptr 的 cast");
    std::shared_ptr<Unit> u = std::make_shared<Warrior>();
    auto w = std::dynamic_pointer_cast<Warrior>(u);
    auto m = std::dynamic_pointer_cast<Mage>(u);
    std::printf("dynamic_pointer_cast<Warrior>: %s, use_count = %ld (共享同一控制块)\n", w ? "非空" : "空", u.use_count());
    std::printf("dynamic_pointer_cast<Mage>   : %s\n", m ? "非空" : "空");
    auto s = std::static_pointer_cast<Warrior>(u);
    std::printf("static_pointer_cast<Warrior> 后 use_count = %ld\n", u.use_count());
    // 错误写法：std::shared_ptr<Warrior> bad(static_cast<Warrior*>(u.get()));
    //           -> 新建第二个控制块，两个控制块各 delete 一次 = 重复释放
}

// ---------------------------------------------------------------------------
// 6. const_cast
// ---------------------------------------------------------------------------
static size_t LegacyStrLen(char* s) { return std::strlen(s); }   // 老 C 接口忘写 const，但并不修改

class Inventory {
    std::vector<int> items_{101, 102, 103};
public:
    const int& At(size_t i) const { /* 假设这里有一大段越界检查、日志 */ return items_.at(i); }
    int& At(size_t i) { return const_cast<int&>(std::as_const(*this).At(i)); }   // 复用 const 版本
};

static const int kMaxLevel = 15;   // 全局 const：放在只读段 .rdata

static unsigned long TryWriteConstGlobal() {   // 用 SEH 捕获访问冲突（函数内不能有带析构的对象）
    __try {
        volatile int* p = const_cast<volatile int*>(&kMaxLevel);
        *p = 99;
        return 0;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return GetExceptionCode();
    }
}

static void Demo6_ConstCast() {
    Title("6. const_cast");
    const std::string name = "Arthur";
    std::printf("对接老接口 LegacyStrLen(const_cast<char*>(name.c_str())) = %zu\n",
                LegacyStrLen(const_cast<char*>(name.c_str())));

    Inventory inv;
    inv.At(1) = 999;
    std::printf("非 const At() 复用 const 版本实现, 修改后 At(1) = %d\n", std::as_const(inv).At(1));

    int real = 1;
    const int& cref = real;
    const_cast<int&>(cref) = 2;              // 合法：对象本身不是 const
    std::printf("对象本身非 const, 经 const 引用去 const 后修改: real = %d (合法)\n", real);

    const int local = 10;
    int* p = const_cast<int*>(&local);
    *p = 20;                                 // [UB] 对象本身是 const
    std::printf("[UB] const int local=10 被改写后: local = %d, *p = %d  (编译器常量折叠了 local)\n", local, *p);

    unsigned long code = TryWriteConstGlobal();
    std::printf("[UB] 改写全局 const kMaxLevel -> 异常码 0x%08lX %s\n", code,
                code == EXCEPTION_ACCESS_VIOLATION ? "(EXCEPTION_ACCESS_VIOLATION，只读页，直接崩溃)" : "");
}

// ---------------------------------------------------------------------------
// 7. reinterpret_cast / bit_cast
// ---------------------------------------------------------------------------
static float Q_rsqrt(float number) {        // 《雷神之锤 III》快速平方根倒数，现代安全写法
    float x2 = number * 0.5f;
    float y = number;
    uint32_t i = std::bit_cast<uint32_t>(y); // 原版：i = *(long*)&y;  -> 违反严格别名，UB
    i = 0x5f3759df - (i >> 1);
    y = std::bit_cast<float>(i);
    y = y * (1.5f - (x2 * y * y));
    return y;
}

__declspec(noinline) static int AliasTest(int* i, float* f) {
    *i = 1;
    *f = 0.0f;
    return *i;   // 按严格别名规则，编译器可以假定 f 不指向 i，直接返回 1
}

static void Demo7_Reinterpret() {
    Title("7. reinterpret_cast / bit_cast");
    Unit u;
    uintptr_t addr = reinterpret_cast<uintptr_t>(&u);
    Unit* back = reinterpret_cast<Unit*>(addr);
    std::printf("指针 -> uintptr_t -> 指针 往返相等: %d  (addr=0x%llx, 低 3 位=%llu，可借来存标记)\n",
                (int)(back == &u), (unsigned long long)addr, (unsigned long long)(addr & 7));

    float f = 1.0f;
    const unsigned char* bytes = reinterpret_cast<const unsigned char*>(&f);  // char/unsigned char/std::byte 可合法别名任何对象
    std::printf("1.0f 的内存字节: %02X %02X %02X %02X (小端)\n", bytes[0], bytes[1], bytes[2], bytes[3]);
    uint32_t viaMemcpy;
    std::memcpy(&viaMemcpy, &f, sizeof f);
    std::printf("memcpy 取位模式 = 0x%08X, std::bit_cast = 0x%08X\n", viaMemcpy, std::bit_cast<uint32_t>(f));
    std::printf("Q_rsqrt(4.0f) = %.6f, 1/sqrtf(4) = %.6f\n", Q_rsqrt(4.0f), 1.0f / std::sqrt(4.0f));

    int x = 0;
    int r = AliasTest(&x, reinterpret_cast<float*>(&x));     // [UB] 违反严格别名
    std::printf("[UB] 严格别名测试 AliasTest 返回 %d (MSVC 不做基于类型的别名优化；GCC/Clang -O2 通常返回 1)\n", r);
}

// ---------------------------------------------------------------------------
// 8. 网络包解析：reinterpret_cast 套结构体 vs memcpy
// ---------------------------------------------------------------------------
struct HeaderNatural { uint16_t msgId; uint32_t seq; uint16_t len; };
#pragma pack(push, 1)
struct HeaderPacked  { uint16_t msgId; uint32_t seq; uint16_t len; };
#pragma pack(pop)

static void Demo8_Packet() {
    Title("8. 网络包解析");
    std::printf("sizeof(HeaderNatural)=%zu alignof=%zu | sizeof(HeaderPacked)=%zu alignof=%zu\n",
                sizeof(HeaderNatural), alignof(HeaderNatural), sizeof(HeaderPacked), alignof(HeaderPacked));
    alignas(8) unsigned char buf[32] = {};
    // 收包缓冲区：第 0 字节是包类型，头从第 1 字节开始（故意未对齐）
    unsigned char wire[] = {0xA5, 0x34, 0x12, 0x78, 0x56, 0x34, 0x12, 0x10, 0x00};
    std::memcpy(buf, wire, sizeof wire);
    const unsigned char* p = buf + 1;
    std::printf("头部地址 %% 4 = %llu (未对齐)\n", (unsigned long long)(reinterpret_cast<uintptr_t>(p) % 4));

    auto* h1 = reinterpret_cast<const HeaderPacked*>(p);    // 常见写法：x64 能跑，但有对齐/别名/字节序隐患
    std::printf("reinterpret_cast 套结构体: msgId=0x%04X seq=0x%08X len=%u\n", h1->msgId, h1->seq, h1->len);

    HeaderPacked h2;
    std::memcpy(&h2, p, sizeof h2);                          // 推荐写法：编译器会优化成普通 mov
    std::printf("memcpy 拷到本地结构体  : msgId=0x%04X seq=0x%08X len=%u\n", h2.msgId, h2.seq, h2.len);
}

// ---------------------------------------------------------------------------
// 9. C 风格转换能做什么
// ---------------------------------------------------------------------------
struct Secret : private Unit { int code = 7; };

static void Demo9_CStyle() {
    Title("9. C 风格转换");
    Secret sec;
    Unit* pu = (Unit*)&sec;                    // 无视 private 继承（static_cast 编译不过，见 cf5）
    std::printf("(Unit*)&sec 绕过 private 继承: hp = %d\n", pu->hp);
    const int ci = 5;
    int* pi = (int*)&ci;                       // 悄悄做了 const_cast
    std::printf("(int*)&ci 悄悄去掉了 const: pi 非空 = %d\n", (int)(pi != nullptr));
    float fl = 3.7f;
    int asInt = (int)fl;                       // static_cast：数值转换
    int* asPtr = (int*)&fl;                    // reinterpret_cast：位模式重解释
    std::printf("(int)3.7f = %d, *(int*)&3.7f = 0x%08X  <- 同样的括号，语义天差地别\n", asInt, (unsigned)*asPtr);
}

// ---------------------------------------------------------------------------
// 10. 性能：dynamic_cast vs typeid vs 虚函数 vs 类型标志位
// ---------------------------------------------------------------------------
enum : uint32_t { CF_Actor = 1, CF_Pawn = 2, CF_Character = 4, CF_Hero = 8, CF_Prop = 16, CF_Damageable = 32 };
struct Hero;
struct IDamageable { virtual ~IDamageable() = default; int armor = 5; };
struct Entity      { virtual ~Entity() = default; uint32_t castFlags = 0; virtual Hero* AsHero() { return nullptr; } };
struct Actor       : Entity { Actor()     { castFlags |= CF_Actor; } };
struct Pawn        : Actor  { Pawn()      { castFlags |= CF_Pawn; } };
struct Character   : Pawn   { Character() { castFlags |= CF_Character; } };
struct Hero final  : Character, IDamageable { Hero() { castFlags |= CF_Hero | CF_Damageable; } Hero* AsHero() override { return this; } };
struct Prop        : Actor  { Prop()      { castFlags |= CF_Prop; } };

template <class F>
__declspec(noinline) size_t RunCast(Entity* const* arr, size_t n, int iters, F f) {
    size_t hit = 0;
    for (int it = 0; it < iters; ++it)
        for (size_t i = 0; i < n; ++i)
            if (f(arr[i])) ++hit;
    return hit;
}

static void Demo10_Perf() {
    Title("10. 类型判断开销（/O2，1024 个对象 × 10000 轮 = 1024 万次，Hero/Prop 各半）");
    std::vector<std::unique_ptr<Entity>> owners;
    std::vector<Entity*> arr;
    for (int i = 0; i < 1024; ++i) {
        if ((i * 7919) % 2) owners.push_back(std::make_unique<Hero>());
        else owners.push_back(std::make_unique<Prop>());
        arr.push_back(owners.back().get());
    }
    const int iters = 10000;
    using clk = std::chrono::steady_clock;
    auto bench = [&](const char* label, auto f) {
        auto t0 = clk::now();
        size_t hit = RunCast(arr.data(), arr.size(), iters, f);
        auto t1 = clk::now();
        double ms = std::chrono::duration<double, std::milli>(t1 - t0).count();
        std::printf("%-44s %8.1f ms  %6.2f ns/次  命中=%zu\n", label, ms, ms * 1e6 / (double(arr.size()) * iters), hit);
    };
    bench("static_cast<Hero*>（不检查，仅基线）", [](Entity* e) { return static_cast<Hero*>(e) != nullptr; });
    bench("castFlags & CF_Hero（位标志，UE FastCast 思路）", [](Entity* e) { return (e->castFlags & CF_Hero) != 0; });
    bench("e->AsHero()（虚函数）", [](Entity* e) { return e->AsHero() != nullptr; });
    bench("typeid(*e) == typeid(Hero)（只能精确匹配）", [](Entity* e) { return typeid(*e) == typeid(Hero); });
    bench("dynamic_cast<Actor*>（目标是浅层基类）", [](Entity* e) { return dynamic_cast<Actor*>(e) != nullptr; });
    bench("dynamic_cast<Hero*>（下行 4 层）", [](Entity* e) { return dynamic_cast<Hero*>(e) != nullptr; });
    bench("dynamic_cast<IDamageable*>（交叉转换）", [](Entity* e) { return dynamic_cast<IDamageable*>(e) != nullptr; });
}

int main() {
    Demo1_StaticBasic();
    Demo2_Hierarchy();
    Demo3_MultipleInheritance();
    Demo4_VirtualAndCtor();
    Demo5_SmartPtr();
    Demo6_ConstCast();
    Demo7_Reinterpret();
    Demo8_Packet();
    Demo9_CStyle();
    Demo10_Perf();
    return 0;
}
