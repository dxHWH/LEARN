// ue_gc_sim_demo.cpp —— 用 ~300 行 C++ 模拟 UE GC 的核心机制（不是 UE 源码，是“同构的玩具”）
//
//   GUObjectArray      -> World::items_ （每个对象一个条目：指针 + 标志位 + 簇根 + 序列号）
//   UPROPERTY/TObjectPtr -> Obj::props   （GC 能看见的引用）
//   裸 UObject* 成员    -> Obj::raw      （GC 看不见的引用）
//   TWeakObjectPtr     -> WeakPtr{index, serial}
//   AddToRoot/RootSet  -> F_RootSet
//   MarkAsGarbage      -> F_Garbage（GC 时把指向它的 UPROPERTY 置空）
//   GC Cluster         -> Cluster（整簇同生共死，只扫描簇的外部引用）
//   IncrementalPurge   -> 按帧时间预算分批释放
//   增量可达性分析+写屏障 -> BeginMark/Step + SetRef 里的 barrier
//
// 编译：cl /nologo /std:c++latest /EHsc /O2 /utf-8 ue_gc_sim_demo.cpp
#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <random>
#include <vector>

using Clock = std::chrono::steady_clock;
static double MsSince(Clock::time_point t0) {
    return std::chrono::duration<double, std::milli>(Clock::now() - t0).count();
}

enum : uint32_t { F_RootSet = 1u << 0, F_Unreachable = 1u << 1, F_Garbage = 1u << 2 };

struct Obj {
    char name[24]{};
    std::vector<Obj*> props;   // 模拟 UPROPERTY()：GC 会沿着它走
    Obj* raw = nullptr;        // 模拟没加 UPROPERTY 的裸指针：GC 不知道它存在
    std::vector<char> payload; // 模拟对象持有的资源，让“析构”有真实成本
    int32_t index = -1;        // 在对象数组里的下标（UObjectBase::InternalIndex）
};

struct ObjItem {               // 模拟 FUObjectItem
    Obj* obj = nullptr;
    uint32_t flags = 0;
    int32_t clusterRoot = -1;  // 我是某个簇的成员 -> 簇根下标
    int32_t clusterId = -1;    // 我是簇根 -> 簇编号
    uint32_t serial = 0;       // 槽位每复用一次 +1，弱指针靠它识别“换人了”
};

struct WeakPtr { int32_t index = -1; uint32_t serial = 0; };
struct Cluster { int32_t root = -1; std::vector<int32_t> members, externals; };
struct GCStats { size_t scanned = 0, refs = 0, nulled = 0, freed = 0; double markMs = 0, purgeMs = 0; };

class World {
public:
    bool useClusters = true;
    bool barrier = true;
    GCStats stats;

    explicit World(size_t cap) : pool_(new Obj[cap]), items_(cap) {
        free_.reserve(cap);
        for (size_t i = cap; i-- > 0;) free_.push_back((int32_t)i);
    }
    Obj* NewObject(const char* name, size_t payloadBytes = 0) {
        if (free_.empty()) { std::puts("pool exhausted"); std::exit(1); }
        int32_t idx = free_.back(); free_.pop_back();   // LIFO：刚释放的槽位最先被复用
        Obj* o = &pool_[idx];
        std::snprintf(o->name, sizeof(o->name), "%s", name);
        o->props.clear(); o->raw = nullptr; o->index = idx;
        if (payloadBytes) o->payload.assign(payloadBytes, 1);
        ObjItem& it = items_[idx];
        it.obj = o; it.flags = 0; it.clusterRoot = -1; it.clusterId = -1; ++it.serial;
        ++live_;
        return o;
    }
    void AddToRoot(Obj* o) { items_[o->index].flags |= F_RootSet; }
    void MarkAsGarbage(Obj* o) { items_[o->index].flags |= F_Garbage; }
    bool IsValid(const Obj* o) const { return o && items_[o->index].obj == o && !(items_[o->index].flags & F_Garbage); }
    WeakPtr MakeWeak(Obj* o) const { return {o->index, items_[o->index].serial}; }
    Obj* Get(WeakPtr w) const {
        if (w.index < 0) return nullptr;
        const ObjItem& it = items_[w.index];
        if (!it.obj || it.serial != w.serial || (it.flags & F_Garbage)) return nullptr;
        return it.obj;
    }
    size_t Live() const { return live_; }

    void CreateCluster(Obj* root, const std::vector<Obj*>& members) {
        Cluster c; c.root = root->index;
        items_[root->index].clusterId = (int32_t)clusters_.size();
        for (Obj* m : members) { items_[m->index].clusterRoot = root->index; c.members.push_back(m->index); }
        auto inCluster = [&](Obj* p) { return p->index == root->index || items_[p->index].clusterRoot == root->index; };
        auto collect = [&](Obj* o) { for (Obj* p : o->props) if (p && !inCluster(p)) c.externals.push_back(p->index); };
        collect(root);
        for (Obj* m : members) collect(m);
        std::sort(c.externals.begin(), c.externals.end());
        c.externals.erase(std::unique(c.externals.begin(), c.externals.end()), c.externals.end());
        clusters_.push_back(std::move(c));
    }

    // ---------- 可达性分析 ----------
    void BeginMark() {   // 阶段 1：除根集外全部标成 Unreachable，根入队
        stats = {};
        gray_.clear();
        for (size_t i = 0; i < items_.size(); ++i) {
            ObjItem& it = items_[i];
            if (!it.obj) continue;
            if (useClusters && it.clusterRoot >= 0) continue;   // 簇成员不单独标记，跟随簇根
            if (it.flags & F_RootSet) { it.flags &= ~F_Unreachable; gray_.push_back((int32_t)i); }
            else it.flags |= F_Unreachable;
        }
        marking_ = true;
    }
    size_t Step(size_t budget) {   // 阶段 2：从灰色队列取对象扫描其引用（增量时每次只扫 budget 个）
        for (size_t n = 0; n < budget && !gray_.empty(); ++n) {
            int32_t i = gray_.back(); gray_.pop_back();
            Scan(i);
        }
        return gray_.size();
    }
    std::vector<int32_t> GatherUnreachable() {   // 阶段 3：仍带 Unreachable 的就是垃圾
        marking_ = false;
        std::vector<int32_t> out;
        for (size_t i = 0; i < items_.size(); ++i) {
            const ObjItem& it = items_[i];
            if (!it.obj) continue;
            bool dead = (useClusters && it.clusterRoot >= 0) ? (items_[it.clusterRoot].flags & F_Unreachable)
                                                             : (it.flags & F_Unreachable);
            if (dead) out.push_back((int32_t)i);
        }
        return out;
    }
    void Free(int32_t idx) {   // 模拟 BeginDestroy/FinishDestroy/析构 + 内存归还
        Obj* o = items_[idx].obj;
        std::vector<char>().swap(o->payload);
        std::vector<Obj*>().swap(o->props);
        o->raw = nullptr;
        std::snprintf(o->name, sizeof(o->name), "<freed>");
        ObjItem& it = items_[idx];
        it.obj = nullptr; it.flags = 0; it.clusterRoot = -1; it.clusterId = -1;
        free_.push_back(idx); --live_; ++stats.freed;
    }
    GCStats CollectGarbage() {   // 非增量：一口气 mark + 全部 purge
        BeginMark();
        auto t0 = Clock::now();
        Step(SIZE_MAX);
        double mark = MsSince(t0);
        auto dead = GatherUnreachable();
        stats.markMs = mark;
        t0 = Clock::now();
        for (int32_t i : dead) Free(i);
        stats.purgeMs = MsSince(t0);
        return stats;
    }
    // 修改 UPROPERTY 引用的唯一入口（相当于 TObjectPtr 的赋值运算符）
    void SetRef(Obj* owner, size_t slot, Obj* value) {
        owner->props[slot] = value;
        if (marking_ && barrier && value) Reach(value->index);   // 写屏障：新指向的对象直接置为可达
    }
    const char* NameAt(int32_t idx) const { return pool_[idx].name; }

private:
    void Reach(int32_t idx) {
        if (useClusters && items_[idx].clusterRoot >= 0) idx = items_[idx].clusterRoot;   // 碰到簇成员 = 碰到簇根
        ObjItem& it = items_[idx];
        if (it.flags & F_Unreachable) { it.flags &= ~F_Unreachable; gray_.push_back(idx); }
    }
    void Scan(int32_t idx) {
        ++stats.scanned;
        ObjItem& it = items_[idx];
        if (useClusters && it.clusterId >= 0) {   // 簇根：只看预先算好的外部引用，簇内部一概不扫
            for (int32_t e : clusters_[it.clusterId].externals) {
                ++stats.refs;
                if (items_[e].obj && !(items_[e].flags & F_Garbage)) Reach(e);
            }
            return;
        }
        for (Obj*& r : it.obj->props) {
            if (!r) continue;
            ++stats.refs;
            if (items_[r->index].flags & F_Garbage) { r = nullptr; ++stats.nulled; continue; }   // Garbage Elimination
            Reach(r->index);
        }
    }
    std::unique_ptr<Obj[]> pool_;
    std::vector<ObjItem> items_;
    std::vector<int32_t> free_, gray_;
    std::vector<Cluster> clusters_;
    size_t live_ = 0;
    bool marking_ = false;
};

// ============================================================================
static void Demo1_MarkCostScaling() {
    std::puts("\n[D1] 标记阶段耗时 vs 存活对象数（单线程，全部可达，每对象约 2 条引用）");
    std::puts("     N        mark(ms)   ns/对象   扫描对象   访问引用");
    for (size_t N : {10'000u, 50'000u, 200'000u, 500'000u}) {
        World w(N);
        std::mt19937 rng(42);
        std::vector<Obj*> objs(N);
        for (size_t i = 0; i < N; ++i) objs[i] = w.NewObject("o");
        w.AddToRoot(objs[0]);
        for (size_t i = 1; i < N; ++i) objs[(i - 1) / 2]->props.push_back(objs[i]);       // 树：保证全部可达
        for (size_t i = 0; i < N; ++i) objs[i]->props.push_back(objs[rng() % N]);       // 再加一条随机引用（制造 cache miss）
        double best = 1e9; GCStats s;
        for (int k = 0; k < 3; ++k) { s = w.CollectGarbage(); best = std::min(best, s.markMs); }
        std::printf("  %7zu   %8.3f   %7.1f   %8zu   %8zu\n", N, best, best * 1e6 / N, s.scanned, s.refs);
    }
}

static void Demo2_RawPtrWeakPtrGarbage() {
    std::puts("\n[D2] 裸指针 / 弱指针 / MarkAsGarbage 的行为差异");
    World w(64);
    w.useClusters = false;
    Obj* gm = w.NewObject("GameMode");  w.AddToRoot(gm);
    Obj* hud = w.NewObject("HUD");      gm->props.push_back(hud);

    // 场景 A：只被裸指针持有的对象
    Obj* cache = w.NewObject("PathCache");
    hud->raw = cache;   // 没有 UPROPERTY
    auto s = w.CollectGarbage();
    std::printf("  A1 GC 后释放 %zu 个对象；HUD->raw 仍非空，指向的内容 name=\"%s\"（已被回收）\n", s.freed, hud->raw->name);
    Obj* ally = w.NewObject("Ally_B");
    std::printf("  A2 新建 Ally_B 复用了同一槽位：HUD->raw==Ally_B ? %s，HUD->raw->name=\"%s\"  <- 野指针“打中别人”\n",
                hud->raw == ally ? "是" : "否", hud->raw->name);
    gm->props.push_back(ally);

    // 场景 B：Actor::Destroy() -> MarkAsGarbage
    Obj* enemy = w.NewObject("Enemy_A");
    gm->props.push_back(enemy);          // UPROPERTY Target
    size_t slot = gm->props.size() - 1;
    WeakPtr weak = w.MakeWeak(enemy);    // TWeakObjectPtr
    w.MarkAsGarbage(enemy);
    std::printf("  B1 Destroy 后、GC 前：Target 指针=%s，IsValid(Target)=%s，Weak.Get()=%s\n",
                gm->props[slot] ? "非空" : "nullptr", w.IsValid(gm->props[slot]) ? "true" : "false",
                w.Get(weak) ? "有效" : "nullptr");
    s = w.CollectGarbage();
    std::printf("  B2 GC 后：Target 指针=%s（被 GC 自动置空 %zu 处），释放 %zu 个，Weak.Get()=%s\n",
                gm->props[slot] ? "非空" : "nullptr", s.nulled, s.freed, w.Get(weak) ? "有效" : "nullptr");
    Obj* again = w.NewObject("Enemy_C");
    std::printf("  B3 Enemy_C 复用槽位后：Weak.Get()=%s（序列号 %u != %u，弱指针不会认错人）\n",
                w.Get(weak) ? "有效" : "nullptr", w.MakeWeak(again).serial, weak.serial);
}

struct AssetScene { Obj* manager; Obj* leafOfAsset0; };
static AssetScene BuildAssets(World& w, int K, int S, bool clusters) {
    Obj* mgr = w.NewObject("AssetManager"); w.AddToRoot(mgr);
    std::vector<Obj*> tex;
    for (int t = 0; t < 8; ++t) { tex.push_back(w.NewObject("SharedTex")); mgr->props.push_back(tex.back()); }
    Obj* leaf0 = nullptr;
    for (int k = 0; k < K; ++k) {
        Obj* root = w.NewObject("AssetRoot");
        mgr->props.push_back(root);
        std::vector<Obj*> subs(S);
        for (int j = 0; j < S; ++j) subs[j] = w.NewObject("SubObj");
        root->props.push_back(subs[0]);
        for (int j = 0; j < S; ++j) {
            if (2 * j + 1 < S) subs[j]->props.push_back(subs[2 * j + 1]);
            if (2 * j + 2 < S) subs[j]->props.push_back(subs[2 * j + 2]);
            if (j % 25 == 0) subs[j]->props.push_back(tex[(k + j) % 8]);   // 少量外部引用
        }
        if (k == 0) leaf0 = subs[S - 1];
        if (clusters) w.CreateCluster(root, subs);
    }
    return {mgr, leaf0};
}

static void Demo3_Cluster() {
    const int K = 2000, S = 100;
    std::printf("\n[D3] GC 簇：%d 个资产 x (1 根 + %d 子对象)，共 %d 个对象\n", K, S, K * (S + 1) + 9);
    for (bool cl : {false, true}) {
        World w(K * (S + 1) + 16);
        w.useClusters = cl;
        AssetScene sc = BuildAssets(w, K, S, cl);
        double best = 1e9; GCStats s;
        for (int r = 0; r < 3; ++r) { s = w.CollectGarbage(); best = std::min(best, s.markMs); }
        std::printf("  %-6s mark=%7.3f ms  扫描对象=%7zu  访问引用=%7zu\n", cl ? "有簇" : "无簇", best, s.scanned, s.refs);
        // 卸载所有资产，但某个系统还抓着资产 0 的一个叶子子对象
        sc.manager->props.clear();
        sc.manager->props.push_back(sc.leafOfAsset0);
        size_t before = w.Live();
        s = w.CollectGarbage();
        std::printf("         卸载后只引用资产0的1个叶子：释放 %zu，存活 %zu（原 %zu）\n", s.freed, w.Live(), before);
    }
}

static void Demo4_IncrementalPurge() {
    const size_t N = 200'000, Payload = 512;
    const double BudgetMs = 2.0;
    std::printf("\n[D4] 关卡卸载：%zu 个对象（各带 %zuB 资源）同时变成垃圾\n", N, Payload);
    for (bool incremental : {false, true}) {
        World w(N);
        w.useClusters = false;
        for (size_t i = 0; i < N; ++i) w.NewObject("LevelObj", Payload);
        w.BeginMark(); w.Step(SIZE_MAX);
        auto dead = w.GatherUnreachable();
        if (!incremental) {
            auto t0 = Clock::now();
            for (int32_t i : dead) w.Free(i);
            std::printf("  一次性 purge：单帧耗时 %.2f ms\n", MsSince(t0));
        } else {
            size_t pos = 0; int frames = 0; double worst = 0, total = 0;
            while (pos < dead.size()) {
                auto t0 = Clock::now();
                while (pos < dead.size()) {
                    w.Free(dead[pos++]);
                    if ((pos & 63) == 0 && MsSince(t0) >= BudgetMs) break;   // 每 64 个对象看一次表
                }
                double ms = MsSince(t0); worst = std::max(worst, ms); total += ms; ++frames;
            }
            std::printf("  增量 purge（预算 %.1fms/帧）：分 %d 帧完成，单帧最长 %.2f ms，总计 %.2f ms\n",
                        BudgetMs, frames, worst, total);
        }
    }
}

static void Demo5_IncrementalMarkBarrier() {
    std::puts("\n[D5] 增量标记 + 写屏障（三色不变式）");
    for (bool barrier : {false, true}) {
        World w(16);
        w.useClusters = false; w.barrier = barrier;
        Obj* root = w.NewObject("Root"); w.AddToRoot(root);
        Obj* A = w.NewObject("A"); Obj* B = w.NewObject("B"); Obj* C = w.NewObject("C");
        root->props = {B, A};      // 栈式灰队列：A 先被扫描
        A->props = {nullptr};
        B->props = {C};
        int32_t cIdx = C->index;
        w.BeginMark();
        w.Step(1);                 // 扫 Root -> A、B 变灰
        w.Step(1);                 // 扫 A -> A 变黑（A 当时没有引用）
        w.SetRef(A, 0, C);         // 游戏逻辑在两次 GC 步之间改了引用：黑色 A 指向白色 C
        w.SetRef(B, 0, nullptr);   //                                  并断开了灰色 B -> C
        w.Step(SIZE_MAX);
        auto dead = w.GatherUnreachable();
        bool cFreed = std::find(dead.begin(), dead.end(), cIdx) != dead.end();
        for (int32_t i : dead) w.Free(i);
        std::printf("  写屏障%s：C 被回收=%s，A->props[0]->name=\"%s\"%s\n", barrier ? "开" : "关",
                    cFreed ? "是" : "否", A->props[0]->name, cFreed ? "  <- A 持有已回收对象（悬空）" : "");
    }
}

int main() {
    std::printf("ue_gc_sim_demo  _MSC_VER=%d  sizeof(ObjItem)=%zu\n", _MSC_VER, sizeof(ObjItem));
    Demo1_MarkCostScaling();
    Demo2_RawPtrWeakPtrGarbage();
    Demo3_Cluster();
    Demo4_IncrementalPurge();
    Demo5_IncrementalMarkBarrier();
}
