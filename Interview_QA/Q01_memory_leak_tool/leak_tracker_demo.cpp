// leak_tracker_demo.cpp
// 一个“麻雀虽小五脏俱全”的内存泄漏检测器（Windows / MSVC）
//
//  采集层：替换全局 operator new / delete（8 个重载）
//  记录层：外部哈希表 ptr -> {size, seq, stackId, tag}；调用栈只存 PC，哈希去重
//  检测层：① 程序结束时仍存活  ② 序号区间（关卡/对局）快照 diff  ③ 尾部哨兵越界检测
//          ④ 重复释放/野指针释放  ⑤ new[] / delete 不匹配
//  展示层：按调用点聚合、按字节排序；DbgHelp 符号化（函数名 + 文件:行号）；按内存标签汇总
//
// 编译：cl /std:c++20 /EHsc /utf-8 /Zi /Od leak_tracker_demo.cpp /link dbghelp.lib
//       （/Zi 生成 PDB 才能符号化；/Od 避免内联让栈更直观）

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <dbghelp.h>

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <new>
#include <thread>
#include <unordered_map>
#include <vector>

#pragma comment(lib, "dbghelp.lib")

namespace mt {

// ------------------------------------------------------------------
// 0. 追踪器自身的容器必须绕开 operator new，否则会无限递归
// ------------------------------------------------------------------
template <class T>
struct RawAlloc {
    using value_type = T;
    RawAlloc() = default;
    template <class U> RawAlloc(const RawAlloc<U>&) {}
    T* allocate(size_t n) {
        void* p = std::malloc(n * sizeof(T));
        if (!p) throw std::bad_alloc();
        return static_cast<T*>(p);
    }
    void deallocate(T* p, size_t) { std::free(p); }
    template <class U> bool operator==(const RawAlloc<U>&) const { return true; }
    template <class U> bool operator!=(const RawAlloc<U>&) const { return false; }
};
template <class T> using RawVec = std::vector<T, RawAlloc<T>>;
template <class K, class V>
using RawMap = std::unordered_map<K, V, std::hash<K>, std::equal_to<K>, RawAlloc<std::pair<const K, V>>>;

constexpr int      kMaxFrames = 16;
constexpr uint32_t kCanary    = 0xFDFDFDFD;   // 和 MSVC Debug CRT 的 “no man's land” 同一个值

struct CallStack {
    void*    frames[kMaxFrames];
    uint16_t depth;
};

struct AllocInfo {
    size_t      size;
    uint64_t    seq;      // 全局单调递增的分配序号，用于“区间快照”
    uint32_t    stackId;  // 去重后的调用栈 ID
    const char* tag;      // 内存标签（类似 UE 的 LLM_SCOPE）
    bool        isArray;
};

struct Tracker {
    SRWLOCK                   lock = SRWLOCK_INIT;
    RawMap<void*, AllocInfo>  live;        // 当前存活的分配
    RawMap<uint64_t, uint32_t> stackIndex; // 栈哈希 -> stackId
    RawVec<CallStack>         stacks;      // stackId -> 栈
    uint64_t nextSeq     = 1;
    size_t   liveBytes   = 0;
    size_t   peakBytes   = 0;
    uint64_t totalAllocs = 0;
    uint64_t totalFrees  = 0;
    uint32_t badFrees    = 0;
    volatile LONG overflows = 0;
};

// 放在静态缓冲区里、永不析构：保证 ① 第一次 new 之前已构造 ② 全局析构阶段仍可用
Tracker& T() {
    alignas(Tracker) static unsigned char buf[sizeof(Tracker)];
    static Tracker* t = ::new (static_cast<void*>(buf)) Tracker();
    return *t;
}

thread_local bool        t_inHook = false;   // 重入保护
thread_local const char* t_tag    = nullptr; // 当前线程的内存标签

struct MemTagScope {
    const char* prev;
    explicit MemTagScope(const char* tag) : prev(t_tag) { t_tag = tag; }
    ~MemTagScope() { t_tag = prev; }
};

static uint64_t HashStack(const CallStack& cs) {
    uint64_t h = 1469598103934665603ull;               // FNV-1a
    for (int i = 0; i < cs.depth; ++i) {
        uint64_t v = reinterpret_cast<uint64_t>(cs.frames[i]);
        for (int b = 0; b < 8; ++b) { h ^= (v >> (b * 8)) & 0xff; h *= 1099511628211ull; }
    }
    return h;
}

// ------------------------------------------------------------------
// 1. 符号化（只在出报告时做，运行时只存 PC）
// ------------------------------------------------------------------
static void EnsureSym() {
    static bool inited = false;
    if (!inited) {
        SymSetOptions(SYMOPT_UNDNAME | SYMOPT_DEFERRED_LOADS | SYMOPT_LOAD_LINES);
        SymInitialize(GetCurrentProcess(), nullptr, TRUE);
        inited = true;
    }
}

static const char* ShortFile(const char* path) {
    const char* s = std::strrchr(path, '\\');
    return s ? s + 1 : path;
}

static bool IsNoiseFrame(const char* name) {
    static const char* kPrefixes[] = {"std::", "operator new", "operator delete", "mt::",
                                      "BaseThreadInitThunk", "RtlUserThreadStart", "thread_start",
                                      "invoke_main", "__scrt", "mainCRTStartup"};
    for (const char* p : kPrefixes)
        if (std::strncmp(name, p, std::strlen(p)) == 0) return true;
    return false;
}

static void PrintStack(const CallStack& cs, int maxLines) {
    EnsureSym();
    HANDLE proc = GetCurrentProcess();
    alignas(SYMBOL_INFO) char buf[sizeof(SYMBOL_INFO) + 256];
    int printed = 0;
    for (int i = 0; i < cs.depth && printed < maxLines; ++i) {
        auto* sym = reinterpret_cast<SYMBOL_INFO*>(buf);
        std::memset(buf, 0, sizeof(buf));
        sym->SizeOfStruct = sizeof(SYMBOL_INFO);
        sym->MaxNameLen   = 255;
        DWORD64 addr = reinterpret_cast<DWORD64>(cs.frames[i]);
        DWORD64 disp = 0;
        const char* name = SymFromAddr(proc, addr, &disp, sym) ? sym->Name : "???";
        if (IsNoiseFrame(name)) continue;      // 过滤 STL / CRT 帧，让报告直指业务代码
        IMAGEHLP_LINE64 line{};
        line.SizeOfStruct = sizeof(line);
        DWORD lineDisp = 0;
        // 栈里存的是“返回地址”，指向 call 的下一条指令，减 1 才能落到调用所在行
        if (SymGetLineFromAddr64(proc, addr - 1, &lineDisp, &line))
            std::printf("        at %s  (%s:%lu)\n", name, ShortFile(line.FileName), line.LineNumber);
        else
            std::printf("        at %s\n", name);
        ++printed;
        if (std::strcmp(name, "main") == 0) break;
    }
}

// ------------------------------------------------------------------
// 2. 采集：分配 / 释放
// ------------------------------------------------------------------
void* Alloc(size_t size, bool isArray) {
    if (size == 0) size = 1;
    auto* p = static_cast<unsigned char*>(std::malloc(size + sizeof(kCanary)));
    if (!p) return nullptr;
    std::memcpy(p + size, &kCanary, sizeof(kCanary));    // 尾部哨兵
    if (t_inHook) return p;                                // 追踪器内部分配：不记录

    t_inHook = true;
    CallStack cs{};
    cs.depth = CaptureStackBackTrace(2, kMaxFrames, cs.frames, nullptr); // 跳过 Alloc + operator new
    uint64_t h = HashStack(cs);

    Tracker& t = T();
    AcquireSRWLockExclusive(&t.lock);
    uint32_t sid;
    auto it = t.stackIndex.find(h);
    if (it == t.stackIndex.end()) {
        sid = static_cast<uint32_t>(t.stacks.size());
        t.stacks.push_back(cs);
        t.stackIndex.emplace(h, sid);
    } else {
        sid = it->second;
    }
    t.live[p] = AllocInfo{size, t.nextSeq++, sid, t_tag, isArray};
    t.liveBytes += size;
    t.peakBytes = std::max(t.peakBytes, t.liveBytes);
    ++t.totalAllocs;
    ReleaseSRWLockExclusive(&t.lock);
    t_inHook = false;
    return p;
}

void Free(void* p, bool isArray) {
    if (!p) return;
    if (t_inHook) { std::free(p); return; }
    t_inHook = true;

    Tracker&  t = T();
    AllocInfo info{};
    CallStack allocStack{};
    bool found = false;
    AcquireSRWLockExclusive(&t.lock);
    auto it = t.live.find(p);
    if (it != t.live.end()) {
        found      = true;
        info       = it->second;
        allocStack = t.stacks[info.stackId];
        t.live.erase(it);
        t.liveBytes -= info.size;
        ++t.totalFrees;
    } else {
        ++t.badFrees;
    }
    ReleaseSRWLockExclusive(&t.lock);

    if (!found) {
        CallStack cs{};
        cs.depth = CaptureStackBackTrace(2, kMaxFrames, cs.frames, nullptr);
        std::printf("\n[MemTracker][错误] 释放了未追踪/已释放的指针 %p（重复释放或野指针），已拦截\n      释放位置:\n", p);
        PrintStack(cs, 2);
        t_inHook = false;
        return;  // 不真正 free，避免把堆结构搞坏
    }
    if (std::memcmp(static_cast<unsigned char*>(p) + info.size, &kCanary, sizeof(kCanary)) != 0) {
        InterlockedIncrement(&t.overflows);
        std::printf("\n[MemTracker][错误] 堆越界写：块 %p（%zu 字节）尾部哨兵被破坏\n      分配位置:\n", p, info.size);
        PrintStack(allocStack, 2);
    }
    if (info.isArray != isArray) {
        std::printf("\n[MemTracker][错误] 分配用 new%s，释放用 delete%s，不匹配\n      分配位置:\n",
                    info.isArray ? "[]" : "", isArray ? "[]" : "");
        PrintStack(allocStack, 2);
    }
    std::free(p);
    t_inHook = false;
}

// ------------------------------------------------------------------
// 3. 查询 / 报告
// ------------------------------------------------------------------
uint64_t Mark() {
    Tracker& t = T();
    AcquireSRWLockShared(&t.lock);
    uint64_t s = t.nextSeq;
    ReleaseSRWLockShared(&t.lock);
    return s;
}

size_t LiveBytes() {
    Tracker& t = T();
    AcquireSRWLockShared(&t.lock);
    size_t b = t.liveBytes;
    ReleaseSRWLockShared(&t.lock);
    return b;
}

// 报告 seq >= sinceSeq 且仍存活的分配，按调用点聚合
void Report(const char* title, uint64_t sinceSeq, size_t topN = 10) {
    struct Site { CallStack stack; size_t bytes; uint32_t count; const char* tag; };
    bool prev = t_inHook;
    t_inHook  = true;
    Tracker& t = T();
    RawVec<Site> sites;
    size_t totalBytes = 0, totalBlocks = 0;
    {
        RawMap<uint32_t, size_t> idx;
        AcquireSRWLockShared(&t.lock);           // 持锁只做拷贝聚合，符号化放到锁外
        for (auto& [ptr, info] : t.live) {
            if (info.seq < sinceSeq) continue;
            auto it = idx.find(info.stackId);
            if (it == idx.end()) {
                idx.emplace(info.stackId, sites.size());
                sites.push_back(Site{t.stacks[info.stackId], info.size, 1, info.tag});
            } else {
                sites[it->second].bytes += info.size;
                sites[it->second].count++;
            }
            totalBytes += info.size;
            ++totalBlocks;
        }
        ReleaseSRWLockShared(&t.lock);
    }
    std::sort(sites.begin(), sites.end(), [](const Site& a, const Site& b) { return a.bytes > b.bytes; });

    std::printf("\n==== %s ====\n", title);
    std::printf("  存活 %zu 块 / %zu 字节，来自 %zu 个不同调用点\n", totalBlocks, totalBytes, sites.size());
    for (size_t i = 0; i < sites.size() && i < topN; ++i) {
        std::printf("  [%zu] %zu 字节 / %u 块   tag=%s\n", i + 1, sites[i].bytes, sites[i].count,
                    sites[i].tag ? sites[i].tag : "-");
        PrintStack(sites[i].stack, 3);
    }
    t_inHook = prev;
}

void ReportTags(const char* title, uint64_t sinceSeq) {
    struct TagAgg { const char* tag; size_t bytes; uint32_t count; };
    bool prev = t_inHook;
    t_inHook  = true;
    Tracker& t = T();
    RawVec<TagAgg> v;
    AcquireSRWLockShared(&t.lock);
    for (auto& [ptr, info] : t.live) {
        if (info.seq < sinceSeq) continue;
        const char* tag = info.tag ? info.tag : "(untagged)";
        auto it = std::find_if(v.begin(), v.end(), [&](const TagAgg& a) { return std::strcmp(a.tag, tag) == 0; });
        if (it == v.end()) v.push_back(TagAgg{tag, info.size, 1});
        else { it->bytes += info.size; it->count++; }
    }
    ReleaseSRWLockShared(&t.lock);
    std::sort(v.begin(), v.end(), [](const TagAgg& a, const TagAgg& b) { return a.bytes > b.bytes; });
    std::printf("\n==== %s ====\n", title);
    for (auto& a : v) std::printf("  %-14s %8zu 字节 / %u 块\n", a.tag, a.bytes, a.count);
    t_inHook = prev;
}

void PrintStats() {
    Tracker& t = T();
    AcquireSRWLockShared(&t.lock);
    std::printf("\n==== 追踪器统计 ====\n");
    std::printf("  累计分配 %llu 次 / 释放 %llu 次\n", t.totalAllocs, t.totalFrees);
    std::printf("  当前存活 %zu 块 / %zu 字节，峰值 %zu 字节\n", t.live.size(), t.liveBytes, t.peakBytes);
    std::printf("  唯一调用栈 %zu 个（每个 %zu 字节，去重后只存一份）\n", t.stacks.size(), sizeof(CallStack));
    std::printf("  非法/重复释放 %u 次，越界写 %ld 次\n", t.badFrees, t.overflows);
    ReleaseSRWLockShared(&t.lock);
}

}  // namespace mt

// ------------------------------------------------------------------
// 4. 替换全局 operator new / delete（链接期替换，整个程序生效）
// ------------------------------------------------------------------
void* operator new(size_t n) {
    void* p = mt::Alloc(n, false);
    if (!p) throw std::bad_alloc();
    return p;
}
void* operator new[](size_t n) {
    void* p = mt::Alloc(n, true);
    if (!p) throw std::bad_alloc();
    return p;
}
void* operator new(size_t n, const std::nothrow_t&) noexcept { return mt::Alloc(n, false); }
void* operator new[](size_t n, const std::nothrow_t&) noexcept { return mt::Alloc(n, true); }
void operator delete(void* p) noexcept { mt::Free(p, false); }
void operator delete[](void* p) noexcept { mt::Free(p, true); }
void operator delete(void* p, size_t) noexcept { mt::Free(p, false); }    // C++14 sized delete
void operator delete[](void* p, size_t) noexcept { mt::Free(p, true); }
void operator delete(void* p, const std::nothrow_t&) noexcept { mt::Free(p, false); }
void operator delete[](void* p, const std::nothrow_t&) noexcept { mt::Free(p, true); }

// ==================================================================
// 业务代码：模拟游戏客户端里最常见的几类泄漏
// ==================================================================
struct Bullet { float pos[3]; float vel[3]; int owner; };

void SpawnBullets(int n) {
    for (int i = 0; i < n; ++i) {
        Bullet* b = new Bullet{};
        if (i % 10 == 0) continue;   // BUG：某个分支提前 continue，忘了 delete
        delete b;
    }
}

struct Monster { int hp = 100; std::shared_ptr<Monster> target; };

void CreateMonsterPair() {
    auto a = std::make_shared<Monster>();
    auto b = std::make_shared<Monster>();
    a->target = b;
    b->target = a;                   // BUG：互相持有强引用 -> 循环引用，离开作用域计数仍为 1
}

struct Texture {
    char name[32];
    std::vector<unsigned char> pixels;
    Texture(const char* n, size_t bytes) : pixels(bytes) { strncpy_s(name, n, _TRUNCATE); }
};
std::vector<std::shared_ptr<Texture>> g_TextureCache;   // 全局纹理缓存：“为了下次进图更快”

struct Level {
    std::vector<std::shared_ptr<Texture>> textures;
    void Load() {
        for (int i = 0; i < 3; ++i) {
            auto tex = std::make_shared<Texture>("forest_tex", 64 * 1024);
            textures.push_back(tex);
            if (i == 0) g_TextureCache.push_back(tex);   // BUG：塞进全局缓存，卸载时没移除
        }
    }
    void Unload() { textures.clear(); textures.shrink_to_fit(); }
};

void PlayForestLevel() {
    mt::MemTagScope tag("Level_Forest");
    Level* lv = new Level;
    lv->Load();
    lv->Unload();
    delete lv;
}

void WorkerJob(int id) {
    mt::MemTagScope tag("Worker");
    for (int i = 0; i < 20000; ++i) { int* p = new int(i); delete p; }
    if (id == 2) { char* leak = new char[256]; (void)leak; }  // BUG：只有 2 号线程漏了一块
}

static double NowNs() {
    static LARGE_INTEGER f = [] { LARGE_INTEGER x; QueryPerformanceFrequency(&x); return x; }();
    LARGE_INTEGER c;
    QueryPerformanceCounter(&c);
    return c.QuadPart * 1e9 / f.QuadPart;
}

int main() {
    SetConsoleOutputCP(CP_UTF8);

    // 场景 0：正常使用，不应出现在任何报告里
    { std::vector<int> v(1000); auto u = std::make_unique<Bullet>(); }

    // 场景 1 + 2
    uint64_t m0 = mt::Mark();
    SpawnBullets(100);
    CreateMonsterPair();
    mt::Report("场景 1+2：裸指针泄漏 & shared_ptr 循环引用", m0);

    // 场景 3：关卡反复进出 —— 游戏里最有价值的检测方式
    std::printf("\n== 场景 3：反复进出森林关卡 ==\n");
    uint64_t lvMark = mt::Mark();
    for (int round = 1; round <= 3; ++round) {
        size_t before = mt::LiveBytes();
        PlayForestLevel();
        std::printf("  第 %d 次进出后，存活内存变化 %+lld 字节\n", round,
                    static_cast<long long>(mt::LiveBytes()) - static_cast<long long>(before));
    }
    mt::Report("场景 3：关卡期间分配、卸载后仍存活", lvMark);

    // 场景 4：多线程
    uint64_t thMark = mt::Mark();
    {
        std::vector<std::thread> ths;
        for (int i = 0; i < 4; ++i) ths.emplace_back(WorkerJob, i);
        for (auto& th : ths) th.join();
    }
    mt::Report("场景 4：4 个工作线程各 2 万次 new/delete 后", thMark);

    // 场景 5：顺带能抓的内存错误
    std::printf("\n== 场景 5：越界写 / new[]-delete 不匹配 / 重复释放 ==\n");
    { char* buf = new char[8]; std::memcpy(buf, "ABCDEFGHIJ", 10); delete[] buf; }
    { int* arr = new int[4]; delete arr; }
    { int* x = new int(42); delete x; delete x; }

    // 性能开销
    constexpr int N = 200000;
    double t0 = NowNs();
    for (int i = 0; i < N; ++i) { void* volatile p = std::malloc(32); std::free(p); }
    double t1 = NowNs();
    for (int i = 0; i < N; ++i) { char* volatile p = new char[32]; delete[] p; }
    double t2 = NowNs();
    std::printf("\n==== 性能 ====\n  裸 malloc/free: %.0f ns/次；带追踪 new/delete: %.0f ns/次（约 %.0f 倍）\n",
                (t1 - t0) / N, (t2 - t1) / N, (t2 - t1) / (t1 - t0));

    mt::ReportTags("按内存标签汇总（全部存活分配）", 0);
    mt::PrintStats();
    mt::Report("main 结束前仍存活（含全局对象，注意误报）", 0, 6);
    return 0;
}
