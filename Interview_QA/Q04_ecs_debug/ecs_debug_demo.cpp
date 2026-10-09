// ecs_debug_demo.cpp —— 「ECS 怎么 debug」配套演示
// 一个 ~400 行的稀疏集（sparse set）迷你 ECS，内置 6 种调试手段：
//   Part1 代际句柄（generation）：抓「实体已销毁、槽位被复用」的悬空引用
//   Part2 迭代保护（iteration guard）+ CommandBuffer：抓「遍历中做结构变更」导致的漏处理
//   Part3 组件 watchpoint：回答「到底是哪个 System 在哪一帧改了这个值」
//   Part4 System 执行顺序追踪 + 分 System 性能统计
//   Part5 按 NetId 的帧状态哈希 + 组件级 diff：定位帧同步不同步（desync）的第一帧和字段
//   Part6 调试功能本身的开销（为什么 Shipping 要编译期剔除）
// 编译：cl /nologo /std:c++latest /EHsc /O2 /utf-8 ecs_debug_demo.cpp
#include <cstdio>
#include <cstdarg>
#include <cstdint>
#include <cstring>
#include <vector>
#include <string>
#include <tuple>
#include <memory>
#include <functional>
#include <algorithm>
#include <chrono>
#include <map>

using clk = std::chrono::steady_clock;
constexpr uint32_t kInvalid = 0xFFFFFFFFu;

// ============================================================ 组件
struct Entity   { uint32_t index = kInvalid; uint32_t gen = 0; };
struct Position { float x = 0, y = 0; };
struct Velocity { float vx = 0, vy = 0; };
struct Health   { int hp = 100; };
struct Poison   { int dmg = 0; };
struct Dead     { uint8_t _ = 0; };              // 标签组件
struct NetId    { uint32_t id = 0; };            // 网络/逻辑稳定 ID（跨端一致）
struct Name     { char s[16]{}; };               // 调试用名字（POD，方便 memcmp/哈希）
struct Target   { Entity e; };                   // 引用另一个实体（导弹的目标）

inline Name mkName(const char* s) { Name n; strncpy_s(n.s, s, 15); return n; }

static std::string fmt(const char* f, ...) {
    char b[160]; va_list ap; va_start(ap, f); vsnprintf(b, sizeof b, f, ap); va_end(ap); return b;
}
std::string toStr(const Position& p) { return fmt("Position{%.1f,%.1f}", p.x, p.y); }
std::string toStr(const Velocity& v) { return fmt("Velocity{%.1f,%.1f}", v.vx, v.vy); }
std::string toStr(const Health& h)   { return fmt("Health{%d}", h.hp); }
std::string toStr(const Poison& p)   { return fmt("Poison{%d}", p.dmg); }
std::string toStr(const Dead&)       { return "Dead"; }
std::string toStr(const NetId& n)    { return fmt("NetId{%u}", n.id); }
std::string toStr(const Name& n)     { return fmt("Name{%s}", n.s); }
std::string toStr(const Target& t)   { return fmt("Target{E%uv%u}", t.e.index, t.e.gen); }

inline uint64_t fnv(const void* p, size_t n, uint64_t h = 1469598103934665603ull) {
    auto b = static_cast<const uint8_t*>(p);
    for (size_t i = 0; i < n; ++i) { h ^= b[i]; h *= 1099511628211ull; }
    return h;
}

// ============================================================ 调试上下文
thread_local const char* g_curSystem = "<main>";   // 当前正在执行的 System 名
int g_frame = 0;
struct ScopedSystem {
    const char* prev;
    explicit ScopedSystem(const char* n) : prev(g_curSystem) { g_curSystem = n; }
    ~ScopedSystem() { g_curSystem = prev; }
};

// ============================================================ 存储：稀疏集
struct IStorage {
    virtual ~IStorage() = default;
    virtual bool has(uint32_t idx) const = 0;
    virtual void remove(uint32_t idx) = 0;
    virtual std::string dump(uint32_t idx) const = 0;
    virtual uint64_t hash(uint32_t idx, uint64_t h) const = 0;
    const char* nm = "";
};
template <class T> struct Storage final : IStorage {
    std::vector<uint32_t> sparse;   // 实体索引 -> dense 位置
    std::vector<uint32_t> dense;    // dense 位置 -> 实体索引
    std::vector<T> data;
    explicit Storage(const char* n) { nm = n; }
    bool has(uint32_t i) const override { return i < sparse.size() && sparse[i] != kInvalid; }
    T& add(uint32_t i, const T& v) {
        if (i >= sparse.size()) sparse.resize(i + 1, kInvalid);
        if (sparse[i] != kInvalid) return data[sparse[i]] = v;
        sparse[i] = (uint32_t)dense.size(); dense.push_back(i); data.push_back(v);
        return data.back();
    }
    void remove(uint32_t i) override {           // swap-and-pop：O(1) 但会打乱顺序
        if (!has(i)) return;
        uint32_t pos = sparse[i], last = (uint32_t)dense.size() - 1;
        if (pos != last) { dense[pos] = dense[last]; data[pos] = data[last]; sparse[dense[pos]] = pos; }
        dense.pop_back(); data.pop_back(); sparse[i] = kInvalid;
    }
    T* get(uint32_t i) { return has(i) ? &data[sparse[i]] : nullptr; }
    std::string dump(uint32_t i) const override { return toStr(data[sparse[i]]); }
    uint64_t hash(uint32_t i, uint64_t h) const override { return fnv(&data[sparse[i]], sizeof(T), h); }
};

inline int g_typeCounter = 0;
template <class T> int typeId() { static int id = g_typeCounter++; return id; }

// ============================================================ World
struct World {
    std::vector<uint32_t> gens;
    std::vector<uint8_t>  alive, watched;
    std::vector<uint32_t> freeList;
    std::vector<std::unique_ptr<IStorage>> stores;
    std::vector<IStorage*> storeList;
    int  iterDepth = 0;          // >0 表示正在遍历
    int  violations = 0;         // 迭代中结构变更次数
    int  staleHits = 0;          // 失效句柄访问次数
    bool watchPrint = true;
    size_t changeCount = 0;
    uint32_t itemOwner = 0;      // 「单例组件」：道具归属（NetId）

    template <class T> void reg(const char* n) {
        int id = typeId<T>();
        if ((int)stores.size() <= id) stores.resize(id + 1);
        stores[id] = std::make_unique<Storage<T>>(n);
        storeList.push_back(stores[id].get());
    }
    template <class T> Storage<T>& store() { return *static_cast<Storage<T>*>(stores[typeId<T>()].get()); }

    Entity create() {
        uint32_t idx;
        if (!freeList.empty()) { idx = freeList.back(); freeList.pop_back(); alive[idx] = 1; }
        else { idx = (uint32_t)gens.size(); gens.push_back(0); alive.push_back(1); watched.push_back(0); }
        return {idx, gens[idx]};
    }
    bool valid(Entity e) const { return e.index < gens.size() && alive[e.index] && gens[e.index] == e.gen; }

    void structural(const char* op, const char* comp, uint32_t idx) {
        if (iterDepth > 0) {
            ++violations;
            printf("  [ECS-ASSERT] 遍历中发生结构变更：%s<%s>(E%u)  system=%s frame=%d\n", op, comp, idx, g_curSystem, g_frame);
        }
        if (idx < watched.size() && watched[idx] && watchPrint)
            printf("    [watch] f%-2d %-20s E%u %s<%s>\n", g_frame, g_curSystem, idx, op, comp);
    }
    void destroy(Entity e) {
        if (!valid(e)) { ++staleHits; printf("  [ECS-CHECK] destroy 了失效句柄 E%uv%u\n", e.index, e.gen); return; }
        structural("destroy", "*", e.index);
        for (auto* s : storeList) s->remove(e.index);
        ++gens[e.index]; alive[e.index] = 0; watched[e.index] = 0; freeList.push_back(e.index);
    }
    template <class T> T& add(Entity e, const T& v) { structural("add", store<T>().nm, e.index); return store<T>().add(e.index, v); }
    template <class T> void remove(Entity e) { structural("remove", store<T>().nm, e.index); store<T>().remove(e.index); }
    template <class T> bool has(Entity e) { return valid(e) && store<T>().has(e.index); }

    // 带代际校验的访问：句柄失效时返回 nullptr 并报告
    template <class T> T* get(Entity e) {
        if (!valid(e)) {
            ++staleHits;
            bool reused = e.index < gens.size() && alive[e.index];
            printf("  [ECS-CHECK] 失效句柄 E%uv%u（槽位当前 v%u，%s） system=%s\n", e.index, e.gen,
                   e.index < gens.size() ? gens[e.index] : 0, reused ? "已被新实体复用" : "已销毁", g_curSystem);
            return nullptr;
        }
        return store<T>().get(e.index);
    }
    template <class T> T* getByIndexUnsafe(uint32_t idx) { return store<T>().get(idx); }   // 反例：只认索引

    void watch(Entity e) { watched[e.index] = 1; }

    template <class F, class... Cs> void watchedCall(Entity e, F& f, Cs&... cs) {
        std::tuple<Cs...> before{cs...};          // 只对被 watch 的实体做快照
        int v0 = violations;
        f(e, cs...);
        if (violations != v0) return;             // 回调里发生结构变更，引用可能已失效
        auto cmp = [&](const auto& old, const auto& now) {
            if (memcmp(&old, &now, sizeof(old)) != 0) {
                ++changeCount;
                if (watchPrint) printf("    [watch] f%-2d %-20s E%u %s -> %s\n", g_frame, g_curSystem, e.index,
                                       toStr(old).c_str(), toStr(now).c_str());
            }
        };
        std::apply([&](const auto&... olds) { (cmp(olds, cs), ...); }, before);
    }

    template <bool Dbg, class T0, class... Ts, class F> size_t eachImpl(F&& f) {
        Storage<T0>& s0 = store<T0>();
        std::tuple<Storage<Ts>&...> os{store<Ts>()...};
        ++iterDepth; size_t n = 0;
        for (size_t i = 0; i < s0.dense.size(); ++i) {   // 每轮重读 size：结构变更会让它变化
            uint32_t idx = s0.dense[i];
            if (!(true && ... && std::get<Storage<Ts>&>(os).has(idx))) continue;
            Entity e{idx, gens[idx]};
            ++n;
            if constexpr (Dbg) {
                if (watched[idx]) { watchedCall(e, f, s0.data[i], *std::get<Storage<Ts>&>(os).get(idx)...); continue; }
            }
            f(e, s0.data[i], *std::get<Storage<Ts>&>(os).get(idx)...);
        }
        --iterDepth; return n;
    }
    template <class... Ts, class F> size_t each(F&& f)        { return eachImpl<true,  Ts...>(std::forward<F>(f)); }
    template <class... Ts, class F> size_t eachNoDbg(F&& f)   { return eachImpl<false, Ts...>(std::forward<F>(f)); }

    std::string dumpEntity(Entity e) {
        std::string s = fmt("E%uv%u%s {", e.index, e.gen, valid(e) ? "" : "(失效)");
        if (valid(e)) for (auto* st : storeList) if (st->has(e.index)) s += " " + st->dump(e.index);
        return s + " }";
    }
    // 反例哈希：把实体索引也算进去（本地分配细节，跨端天然不一致）
    uint64_t hashNaive() {
        uint64_t h = fnv(&itemOwner, 4);
        for (uint32_t i = 0; i < gens.size(); ++i) if (alive[i]) {
            h = fnv(&i, 4, h);
            for (auto* s : storeList) if (s->has(i)) h = s->hash(i, h);
        }
        return h;
    }
    // 正确哈希：只对有 NetId 的逻辑实体，按 NetId 排序后哈希
    std::vector<std::pair<uint32_t, uint32_t>> byNetId() {
        auto& ns = store<NetId>();
        std::vector<std::pair<uint32_t, uint32_t>> v;
        for (size_t i = 0; i < ns.dense.size(); ++i) v.push_back({ns.data[i].id, ns.dense[i]});
        std::sort(v.begin(), v.end());
        return v;
    }
    uint64_t hashByNetId() {
        uint64_t h = fnv(&itemOwner, 4);
        for (auto [id, idx] : byNetId()) {
            h = fnv(&id, 4, h);
            for (auto* s : storeList) if (s->has(idx)) h = s->hash(idx, h);
        }
        return h;
    }
};

void registerAll(World& w) {
    w.reg<Name>("Name"); w.reg<NetId>("NetId"); w.reg<Position>("Position"); w.reg<Velocity>("Velocity");
    w.reg<Health>("Health"); w.reg<Poison>("Poison"); w.reg<Dead>("Dead"); w.reg<Target>("Target");
}

// ============================================================ CommandBuffer：延迟结构变更
struct CommandBuffer {
    struct Cmd { const char* who; std::function<void(World&)> fn; };
    std::vector<Cmd> cmds;
    void destroy(Entity e) { cmds.push_back({g_curSystem, [e](World& w) { w.destroy(e); }}); }
    template <class T> void add(Entity e, T v) {
        cmds.push_back({g_curSystem, [e, v](World& w) {
            if (w.valid(e)) w.add<T>(e, v); else printf("  [ECS-CHECK] 延迟命令目标已失效 E%uv%u\n", e.index, e.gen); }});
    }
    template <class T> void remove(Entity e) {
        cmds.push_back({g_curSystem, [e](World& w) { if (w.valid(e)) w.remove<T>(e); }});
    }
    void playback(World& w) {
        for (auto& c : cmds) { std::string tag = std::string(c.who) + "(延迟)"; ScopedSystem s(tag.c_str()); c.fn(w); }
        cmds.clear();
    }
};

// ============================================================ Scheduler：顺序追踪 + 分 System 计时
struct Scheduler {
    struct Sys { const char* name; std::function<size_t(World&, CommandBuffer&)> fn; double us = 0, maxUs = 0; size_t ent = 0; };
    std::vector<Sys> sys; CommandBuffer cb; int frame = 0; bool trace = false;
    void add(const char* n, std::function<size_t(World&, CommandBuffer&)> f) { sys.push_back({n, std::move(f)}); }
    void tick(World& w) {
        g_frame = ++frame; int k = 0;
        for (auto& s : sys) {
            ScopedSystem sc(s.name);
            auto t0 = clk::now();
            size_t n = s.fn(w, cb);
            double us = std::chrono::duration<double, std::micro>(clk::now() - t0).count();
            s.us += us; s.maxUs = std::max(s.maxUs, us); s.ent += n;
            if (trace) printf("  [trace] f%d #%d %-14s 匹配实体=%-6zu 待执行命令=%zu\n", frame, ++k, s.name, n, cb.cmds.size());
        }
        if (trace) printf("  [trace] f%d 同步点：回放 %zu 条延迟命令\n", frame, cb.cmds.size());
        cb.playback(w);
    }
    void report() {
        double total = 0; for (auto& s : sys) total += s.us;
        printf("  %-14s %10s %10s %10s %7s\n", "System", "平均us/帧", "最大us", "平均实体", "占比");
        for (auto& s : sys)
            printf("  %-14s %10.1f %10.1f %10zu %6.1f%%\n", s.name, s.us / frame, s.maxUs, s.ent / frame, 100.0 * s.us / total);
    }
};

// ============================================================ Part 1
void part1() {
    printf("\n===== Part1 代际句柄：悬空实体引用 =====\n");
    World w; registerAll(w);
    Entity enemy = w.create(); w.add(enemy, mkName("enemy")); w.add(enemy, Health{100});
    Entity missile = w.create(); w.add(missile, mkName("missile")); w.add(missile, Target{enemy});
    printf("  导弹锁定：%s\n", w.dumpEntity(missile).c_str());
    w.destroy(enemy);                                   // 敌人死亡
    Entity ally = w.create(); w.add(ally, mkName("ally_hero")); w.add(ally, Health{100});   // 复用槽位
    printf("  敌人销毁后新建友军：%s\n", w.dumpEntity(ally).c_str());

    Target* t = w.get<Target>(missile);
    if (Health* h = w.getByIndexUnsafe<Health>(t->e.index)) {   // 反例
        h->hp -= 50;
        printf("  [反例·只存索引] 导弹命中槽位 %u -> %s  ← 打中了友军，且没有任何报错\n", t->e.index, w.dumpEntity(ally).c_str());
    }
    w.get<Health>(ally)->hp = 100;   // 复原，继续演示正例
    printf("  [正例·索引+代际] ");
    Health* h2 = w.get<Health>(t->e);
    printf("  结果=%s，导弹应改为「目标丢失」逻辑\n", h2 ? "命中" : "nullptr");
}

// ============================================================ Part 2
void part2() {
    printf("\n===== Part2 遍历中结构变更：swap-and-pop 导致漏处理 =====\n");
    const int hps[6] = {0, 30, 0, 60, 0, 0};
    for (int mode = 0; mode < 2; ++mode) {
        World w; registerAll(w);
        for (int i = 0; i < 6; ++i) { Entity e = w.create(); w.add(e, Health{hps[i]}); }
        printf(" %s\n", mode == 0 ? "[反例] CleanupSystem 在 each 里直接 destroy：" : "[正例] 用 CommandBuffer 延迟到同步点：");
        CommandBuffer cb; ScopedSystem sc("CleanupSystem");
        w.each<Health>([&](Entity e, Health& h) {
            if (h.hp <= 0) { if (mode == 0) w.destroy(e); else cb.destroy(e); }
        });
        cb.playback(w);
        std::string rest; int zombies = 0;
        for (size_t i = 0; i < w.store<Health>().dense.size(); ++i) {
            int hp = w.store<Health>().data[i].hp; zombies += hp <= 0;
            rest += fmt(" E%u(hp=%d)", w.store<Health>().dense[i], hp);
        }
        printf("  期望存活 2 个(E1,E3)；实际存活 %zu 个:%s  僵尸=%d  violations=%d\n",
               w.store<Health>().dense.size(), rest.c_str(), zombies, w.violations);
    }
}

// ============================================================ Part 3 + 4
void part3and4() {
    printf("\n===== Part3 组件 watchpoint：谁改了 Boss 的血？ =====\n");
    World w; registerAll(w);
    Entity boss = w.create(); w.add(boss, mkName("boss")); w.add(boss, Health{100}); w.add(boss, Poison{35});
    w.add(boss, Position{}); w.add(boss, Velocity{1, 0});
    for (int i = 0; i < 20000; ++i) { Entity m = w.create(); w.add(m, Position{(float)i, 0}); w.add(m, Velocity{1, 1}); w.add(m, Health{100}); }
    w.watch(boss);

    Scheduler s;
    s.add("MoveSystem",   [](World& w, CommandBuffer&) { return w.each<Position, Velocity>([](Entity, Position& p, Velocity& v) { p.x += v.vx; p.y += v.vy; }); });
    s.add("PoisonSystem", [](World& w, CommandBuffer&) { return w.each<Health, Poison>([](Entity, Health& h, Poison& p) { h.hp -= p.dmg; }); });
    s.add("RegenSystem",  [](World& w, CommandBuffer&) {   // BUG：没有排除 Dead
        return w.each<Health>([](Entity, Health& h) { if (h.hp < 100) h.hp = std::min(100, h.hp + 5); }); });
    s.add("DeathSystem",  [](World& w, CommandBuffer& cb) {
        return w.each<Health>([&](Entity e, Health& h) {
            if (h.hp <= 0 && !w.has<Dead>(e)) { cb.add(e, Dead{}); cb.remove<Poison>(e); } }); });

    for (int f = 0; f < 6; ++f) s.tick(w);
    printf("  6 帧后：%s\n", w.dumpEntity(boss).c_str());

    printf("\n===== Part4 执行顺序追踪 + 分 System 性能 =====\n");
    s.trace = true; s.tick(w); s.trace = false;
    w.watchPrint = false;
    for (int f = 0; f < 200; ++f) s.tick(w);
    printf("  共 %d 帧统计：\n", s.frame);
    s.report();
}

// ============================================================ Part 5
void buildDet(World& w, bool withLocalVfx) {
    auto unit = [&](uint32_t net, const char* nm, float x) {
        Entity e = w.create(); w.add(e, NetId{net}); w.add(e, mkName(nm)); w.add(e, Position{x, 0}); w.add(e, Velocity{1, 0}); return e; };
    unit(100, "u0", 0); unit(101, "u1", 2);
    Entity vfx;
    if (withLocalVfx) { vfx = w.create(); w.add(vfx, mkName("vfx_local")); w.add(vfx, Position{3, 0}); }  // 只在本端存在的表现实体
    unit(102, "u2", 5); unit(103, "u3", 5);
    if (withLocalVfx) w.destroy(vfx);                 // swap-and-pop：u3 被换到 u2 前面
}
void addDetSystems(Scheduler& s, bool sortedPickup) {
    s.add("MoveSystem", [](World& w, CommandBuffer&) { return w.each<Position, Velocity>([](Entity, Position& p, Velocity& v) { p.x += v.vx; }); });
    if (!sortedPickup)
        s.add("PickupSystem", [](World& w, CommandBuffer&) {   // BUG：结果依赖 dense 遍历顺序
            return w.each<Position, NetId>([&](Entity e, Position& p, NetId& n) {
                if (w.itemOwner == 0 && p.x >= 10.f) { w.itemOwner = n.id; w.get<Velocity>(e)->vx *= 2; } }); });
    else
        s.add("PickupSystem", [](World& w, CommandBuffer&) {   // 修复：候选按 NetId 决胜
            uint32_t best = 0; Entity be;
            size_t n = w.each<Position, NetId>([&](Entity e, Position& p, NetId& id) {
                if (w.itemOwner == 0 && p.x >= 10.f && (best == 0 || id.id < best)) { best = id.id; be = e; } });
            if (best) { w.itemOwner = best; w.get<Velocity>(be)->vx *= 2; }
            return n; });
}
void diffByNetId(World& a, World& b) {
    auto va = a.byNetId(), vb = b.byNetId();
    std::map<uint32_t, std::pair<uint32_t, uint32_t>> m;
    for (auto [id, i] : va) m[id].first = i;
    for (auto [id, i] : vb) m[id].second = i;
    if (a.itemOwner != b.itemOwner) printf("    单例 itemOwner : A=%u  B=%u\n", a.itemOwner, b.itemOwner);
    for (auto& [id, p] : m)
        for (size_t k = 0; k < a.storeList.size(); ++k) {
            auto *sa = a.storeList[k], *sb = b.storeList[k];
            std::string da = sa->has(p.first) ? sa->dump(p.first) : "-", db = sb->has(p.second) ? sb->dump(p.second) : "-";
            if (da != db) printf("    NetId %u (%s) %-8s: A=%s  B=%s\n", id, a.store<Name>().get(p.first)->s, sa->nm, da.c_str(), db.c_str());
        }
}
void part5() {
    printf("\n===== Part5 帧同步 desync：帧哈希定位第一帧 + 组件 diff =====\n");
    for (int fix = 0; fix < 2; ++fix) {
        World A, B; registerAll(A); registerAll(B);
        buildDet(A, false); buildDet(B, true);
        printf(" %s  A 的 Position 遍历顺序:", fix ? "[修复后]" : "[有 bug]");
        for (auto i : A.store<Position>().dense) printf(" %s", A.store<Name>().get(i)->s);
        printf(" | B:");
        for (auto i : B.store<Position>().dense) printf(" %s", B.store<Name>().get(i)->s);
        printf("\n");
        Scheduler sa, sb; addDetSystems(sa, fix); addDetSystems(sb, fix);
        int firstNaive = -1, firstNet = -1;
        printf("  帧  A.netHash  B.netHash  一致?  | 含实体索引的哈希一致?\n");
        for (int f = 1; f <= 8; ++f) {
            sa.tick(A); sb.tick(B);
            uint64_t ha = A.hashByNetId(), hb = B.hashByNetId();
            bool okNet = ha == hb, okNaive = A.hashNaive() == B.hashNaive();
            if (!okNet && firstNet < 0) firstNet = f;
            if (!okNaive && firstNaive < 0) firstNaive = f;
            printf("  %2d   %08X   %08X   %s   | %s\n", f, (uint32_t)ha, (uint32_t)hb, okNet ? "✓" : "✗", okNaive ? "✓" : "✗");
            if (f == firstNet) { printf("  >>> 第一次不一致在第 %d 帧，组件级 diff：\n", f); diffByNetId(A, B); }
        }
        printf("  结论：NetId 哈希首次分歧帧=%d；含实体索引的哈希首次分歧帧=%d%s\n", firstNet, firstNaive,
               firstNaive == 1 ? "（第 1 帧就报，属于误报）" : "");
    }
}

// ============================================================ Part 6
void part6() {
    printf("\n===== Part6 调试功能的运行时开销（100 万实体 Move）=====\n");
    World w; registerAll(w);
    const int N = 1000000;
    std::vector<Entity> es; es.reserve(N);
    for (int i = 0; i < N; ++i) { Entity e = w.create(); w.add(e, Position{(float)i, 0}); w.add(e, Velocity{1, 2}); es.push_back(e); }
    auto bench = [&](const char* name, auto&& fn) {
        double best = 1e30;
        for (int r = 0; r < 15; ++r) { auto t0 = clk::now(); fn(); best = std::min(best, std::chrono::duration<double, std::milli>(clk::now() - t0).count()); }
        printf("  %-34s %7.2f ms  (%.2f ns/实体)\n", name, best, best * 1e6 / N);
    };
    auto mv = [](Entity, Position& p, Velocity& v) { p.x += v.vx; p.y += v.vy; };
    bench("eachNoDbg（调试代码编译期剔除）", [&] { w.eachNoDbg<Position, Velocity>(mv); });
    bench("each（带 watch 检查，0 个被 watch）", [&] { w.each<Position, Velocity>(mv); });
    w.watchPrint = false;
    for (auto e : es) w.watch(e);
    bench("each（全部被 watch：快照+memcmp）", [&] { w.each<Position, Velocity>(mv); });
    printf("  （记录到的变更次数=%zu，防止被优化）\n", w.changeCount);
}

int main() {
    printf("ecs_debug_demo  _MSC_VER=%d  %s\n", _MSC_VER,
#ifdef _DEBUG
           "Debug"
#else
           "Release(/O2)"
#endif
    );
    part1(); part2(); part3and4(); part5(); part6();
}
