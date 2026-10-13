// pool_design_verify.cpp —— 第 11B 章「对象池落地设计」实测
// 把书中给出的新设计（带 generation 的 ObjectPool + World.SpawnActor/DestroyActor + 池化生命周期）
// 与仓库原有实现（ObjectPool.cs + GameObjectWrapper.cs + SurvivalGameMode 接入代码）分别移植为 C++，
// 在同样的操作序列下对比行为。Unity 生命周期按以下规则建模（书中标注【模型假设】）：
//   U1 Start 每个组件一生只调用一次；SetActive(false) 不会触发 OnDestroy
//   U2 原 World 只在 AActor.Start 时注册、在 OnDestroy 时注销
//   U3 World.Update 倒序遍历 _actors；C# 的 List 越界访问抛 ArgumentOutOfRangeException，本帧 Update 中止
//
// 用法：pool_design_verify.exe [H1 H2 ...]
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <functional>
#include <memory>
#include <random>
#include <stack>
#include <string>
#include <vector>

static void title(const char* s) { std::printf("\n==================== %s ====================\n", s); }

// ============================================================ 新设计 L0：带 generation 的对象池
struct PoolHandle {
    int Index = 0, Generation = 0;                       // default = (0,0)，generation 从 1 开始 → 永远无效
    bool operator==(const PoolHandle& o) const { return Index == o.Index && Generation == o.Generation; }
};
template <class T>
class GenPool {
    struct Slot { T* item = nullptr; int gen = 0; bool inUse = false; };
    std::vector<Slot> slots; int count = 0; std::vector<int> freeStack; int idleItems = 0;
    std::function<T*()> create; std::function<void(T*)> onGet, onRelease, onDestroy; int maxIdle;
public:
    int created = 0, destroyed = 0;
    GenPool(std::function<T*()> c, std::function<void(T*)> g, std::function<void(T*)> r, std::function<void(T*)> d, int maxIdle_ = 1 << 30)
        : create(c), onGet(g), onRelease(r), onDestroy(d), maxIdle(maxIdle_) {}
    PoolHandle Acquire(T*& item) {
        int i;
        if (!freeStack.empty()) { i = freeStack.back(); freeStack.pop_back(); }   // LIFO（与原实现一致）
        else { if (count == (int)slots.size()) slots.resize(std::max<size_t>(4, slots.size() * 2)); i = count++; slots[i].gen = 1; }
        Slot& s = slots[i];
        if (s.item) idleItems--; else { s.item = create(); created++; }
        s.inUse = true; item = s.item;
        if (onGet) onGet(item);
        return {i, s.gen};
    }
    bool IsValid(PoolHandle h) const { return (unsigned)h.Index < (unsigned)count && slots[h.Index].inUse && slots[h.Index].gen == h.Generation; }
    bool Release(PoolHandle h) {
        if (!IsValid(h)) return false;
        Slot& s = slots[h.Index];
        s.inUse = false; s.gen++;                          // 先失效，再回调 → 回调里重入 Release 同一借条会被拒绝
        if (onRelease) onRelease(s.item);
        if (idleItems >= maxIdle) { if (onDestroy) onDestroy(s.item); s.item = nullptr; destroyed++; }
        else idleItems++;
        freeStack.push_back(h.Index);
        return true;
    }
    int IdleItems() const { return idleItems; }
};

// ============================================================ 原实现：ObjectPool.cs 逐行移植
struct OldPoolable { int id; int recycled = 0; bool inUse = false; std::function<void()> onRecycleHook;
    void OnSpawn() { inUse = true; } void OnRecycle() { inUse = false; recycled++; if (onRecycleHook) onRecycleHook(); } };
struct OldPoolObject { OldPoolable* elem = nullptr; int index = 0; };
class OldPool {
    struct Elem { OldPoolable* elem; bool IsAvailable; };
    int _capacity; std::vector<Elem> _pool; std::stack<int> _avail; std::vector<std::unique_ptr<OldPoolable>> own;
public:
    explicit OldPool(int cap) : _capacity(cap), _pool(cap) {
        for (int i = 0; i < cap; i++) { own.emplace_back(new OldPoolable{i}); _pool[i] = {own.back().get(), true}; _avail.push(i); }
    }
    bool Acquire(OldPoolObject& o) {
        if (_avail.empty()) return false;
        int i = _avail.top(); _avail.pop(); o = {_pool[i].elem, i}; _pool[i].IsAvailable = false; o.elem->OnSpawn(); return true;
    }
    // 返回值 -1 表示“若在 C# 中会对 null 调用 OnRecycle → NullReferenceException”
    int Recycle(OldPoolObject& o) {
        if (o.index < 0 || o.index >= _capacity) return 0;
        if (_pool[o.index].IsAvailable) return 0;
        if (!o.elem) return -1;
        o.elem->OnRecycle(); o.elem = nullptr;
        _pool[o.index].IsAvailable = true; _avail.push(o.index); return 1;
    }
    size_t AvailCount() const { return _avail.size(); }
};

struct Item { int id; int released = 0; bool inUse = false; };

static void H1_Handles() {
    title("H1 借条安全：原 ObjectPool vs 带 generation 的新池（同一操作序列）");
    // a) 陈旧拷贝
    {
        OldPool op(4); OldPoolObject a, b; op.Acquire(a); OldPoolObject aCopy = a; op.Recycle(a); op.Acquire(b);
        int r = op.Recycle(aCopy);
        int nid = 0; GenPool<Item> np([&] { return new Item{nid++}; }, [](Item* i) { i->inUse = true; }, [](Item* i) { i->inUse = false; i->released++; }, nullptr);
        Item *ia, *ib; PoolHandle ha = np.Acquire(ia); PoolHandle haCopy = ha; np.Release(ha); PoolHandle hb = np.Acquire(ib);
        bool nr = np.Release(haCopy);
        std::printf("a) 陈旧拷贝归还：原池 Recycle=%s（b 的对象 inUse=%s，被别人回收了）｜新池 Release=%s（b 仍在用=%s，句柄 a=(%d,%d) b=(%d,%d)）\n",
                    r == 1 ? "成功" : "拒绝", b.elem->inUse ? "true" : "false", nr ? "成功" : "拒绝", ib->inUse ? "true" : "false",
                    ha.Index, ha.Generation, hb.Index, hb.Generation);
    }
    // b) default 借条
    {
        OldPool op(4); OldPoolObject o[4]; for (auto& x : o) op.Acquire(x);    // 槽 0 也在使用
        OldPoolObject def{}; int r = op.Recycle(def);
        GenPool<Item> np([] { return new Item{}; }, nullptr, nullptr, nullptr); Item* it; np.Acquire(it);
        std::printf("b) default 借条：原池 %s｜新池 IsValid(default)=%s\n",
                    r == -1 ? "通过两项检查后对 null 调 OnRecycle（C# 中 NullReferenceException）" : "被拒绝",
                    np.IsValid(PoolHandle{}) ? "true" : "false（generation 从 1 开始）");
    }
    // c) 回调里重入归还同一借条（池化后 EndPlay → UnPossess → 回调链可能再次回收自己）
    {
        OldPool op(4); OldPoolObject a; op.Acquire(a); OldPoolObject inner = a; bool once = false;
        a.elem->onRecycleHook = [&] { if (!once) { once = true; op.Recycle(inner); } };
        op.Recycle(a);
        OldPoolObject x, y; op.Acquire(x); op.Acquire(y);
        GenPool<Item>* pnp = nullptr; PoolHandle hin; bool innerOk = true;
        GenPool<Item> np([] { return new Item{}; }, nullptr, [&](Item*) { innerOk = pnp->Release(hin); }, nullptr);
        pnp = &np; Item* ia; hin = np.Acquire(ia); np.Release(hin);
        Item *nx, *ny; np.Acquire(nx); np.Acquire(ny);
        std::printf("c) OnRecycle 里再回收自己：原池 同一槽位被压栈两次 → 接下来两次借出拿到同一对象=%s｜新池 内层 Release=%s，两次借出是同一对象=%s\n",
                    x.elem == y.elem ? "true" : "false", innerOk ? "成功" : "拒绝", nx == ny ? "true" : "false");
    }
    // d) 重复归还
    {
        int nid = 0; GenPool<Item> np([&] { return new Item{nid++}; }, nullptr, nullptr, nullptr); Item* it; PoolHandle h = np.Acquire(it);
        bool r1 = np.Release(h), r2 = np.Release(h);
        std::printf("d) 同一借条归还两次：新池 第一次=%s 第二次=%s（原池同样能拦截，靠 IsAvailable）\n", r1 ? "成功" : "拒绝", r2 ? "成功" : "拒绝");
    }
}

// ============================================================ H2：World Tick 中回收 —— 立即回收 vs 延迟到帧末
struct MActor { int id; int ticks = 0; bool pendingKill = false; std::function<void()> onTick; };
struct MiniWorld {
    std::vector<MActor*> actors; std::vector<MActor*> pendingKill; bool deferred; bool aborted = false;
    bool Has(MActor* a) const { return std::find(actors.begin(), actors.end(), a) != actors.end(); }
    void DestroyActor(MActor* a) {
        if (deferred) { if (a->pendingKill) return; a->pendingKill = true; pendingKill.push_back(a); }
        else { auto it = std::find(actors.begin(), actors.end(), a); if (it != actors.end()) actors.erase(it); }   // 原 Unregister：List.Remove
    }
    void Update() {
        for (int i = (int)actors.size() - 1; i >= 0; i--) {
            if (i >= (int)actors.size()) { aborted = true; break; }       // C#: ArgumentOutOfRangeException，本帧中止
            MActor* a = actors[i];
            if (a->pendingKill) continue;
            a->ticks++; if (a->onTick) a->onTick();
        }
    }
    void LateUpdate() {                                                  // 新设计：帧末统一处理
        for (size_t k = 0; k < pendingKill.size(); k++) { auto it = std::find(actors.begin(), actors.end(), pendingKill[k]); if (it != actors.end()) actors.erase(it); }
        pendingKill.clear();
    }
};
static void H2_DeferredReturn() {
    title("H2 在 World Tick 里回收 Actor：立即回收 vs 标记 PendingKill、帧末统一回收");
    struct Case { const char* name; std::function<void(MiniWorld&, std::vector<MActor>&)> setup; };
    std::vector<Case> cases = {
        {"A3 的 Tick 回收 A1（玩家夺舍 → 踢出的 AI 灵魂被回收）", [](MiniWorld& w, std::vector<MActor>& a) { a[3].onTick = [&w, &a] { if (w.Has(&a[1])) w.DestroyActor(&a[1]); }; }},
        {"A4 的 Tick 回收 A0、A1、A2（一次结算回收多个）", [](MiniWorld& w, std::vector<MActor>& a) { a[4].onTick = [&w, &a] { for (int k = 0; k < 3; k++) if (w.Has(&a[k])) w.DestroyActor(&a[k]); }; }},
        {"A2 的 Tick 回收自己", [](MiniWorld& w, std::vector<MActor>& a) { a[2].onTick = [&w, &a] { if (w.Has(&a[2])) w.DestroyActor(&a[2]); }; }},
    };
    for (auto& c : cases) {
        std::printf("%s\n", c.name);
        for (int mode = 0; mode < 2; mode++) {
            std::vector<MActor> a(5); for (int i = 0; i < 5; i++) a[i].id = i;
            MiniWorld w; w.deferred = mode == 1; for (auto& x : a) w.actors.push_back(&x);
            c.setup(w, a);
            w.Update(); if (w.deferred) w.LateUpdate();
            std::printf("   %-12s Tick 次数：", w.deferred ? "延迟回收" : "立即回收");
            for (auto& x : a) std::printf("A%d=%d ", x.id, x.ticks);
            std::printf("｜本帧%s｜帧末名单 %zu 个\n", w.aborted ? "越界中止" : "正常结束", w.actors.size());
        }
    }
}

// ============================================================ H3：延迟回收的 ABA（尸体 3 秒后回收）
struct Body { std::string name; bool active = false; PoolHandle self; };
static void H3_DelayedABA() {
    title("H3 延迟回收：尸体被提前回收并被复用后，3 秒前安排的回收计时器到期");
    for (int guarded = 0; guarded < 2; guarded++) {
        GenPool<Body> pool([] { return new Body{}; }, [](Body* b) { b->active = true; }, [](Body* b) { b->active = false; }, nullptr);
        struct T { float at; std::function<void()> fn; }; std::vector<T> timers;
        auto Spawn = [&](const char* name) { Body* b; PoolHandle h = pool.Acquire(b); b->self = h; b->name = name; return b; };
        auto ReturnNow = [&](Body* b) { pool.Release(b->self); };                 // DestroyActor(b) 立即版
        std::string log;
        Body* x = Spawn("玩家肉体X");
        log += "t=0.0 生成 X(" + std::to_string(x->self.Index) + "," + std::to_string(x->self.Generation) + ")  ";
        // t=0.5 玩家夺舍别的肉体 → DestroyActor(X, 3s)
        if (guarded) { PoolHandle h = x->self; timers.push_back({3.5f, [&, h] { if (pool.IsValid(h)) { Body* b = x; pool.Release(h); log += "t=3.5 计时器回收了 " + b->name + "  "; } else log += "t=3.5 计时器：借条已失效，忽略  "; }}); }
        else { Body* b = x; timers.push_back({3.5f, [&, b] { if (b->active) { log += "t=3.5 计时器回收了“" + b->name + "”  "; ReturnNow(b); } else log += "t=3.5 计时器：对象不活跃，忽略  "; }}); }
        log += "t=0.5 安排 3 秒后回收 X  ";
        ReturnNow(x); log += "t=1.0 X 被提前回收（清场 / 被碾碎）  ";
        Body* y = Spawn("红队 AI 肉体Y");
        log += "t=1.5 刷怪：拿到" + std::string(y == x ? "同一个对象" : "另一个对象") + " Y(" + std::to_string(y->self.Index) + "," + std::to_string(y->self.Generation) + ")  ";
        for (auto& t : timers) t.fn();
        std::printf("%s\n   %s\n   → Y 仍在场上=%s\n", guarded ? "【新设计】计时器捕获的是安排时的 PoolHandle" : "【朴素做法】计时器捕获对象引用，到期时只检查“对象是否活跃”",
                    log.c_str(), y->active ? "true" : "false（一只活着的 AI 被凭空回收）");
    }
}

// ============================================================ H4：池化生命周期 —— 原 GameObjectWrapper 方案 vs 新设计
struct PawnModel {
    bool startCalled = false, hasBegunPlay = false, isDying = false, colliderEnabled = true, inWorld = false, active = false;
    std::string name = "LightPawn"; int beginPlays = 0;
};
static void H4_Lifecycle() {
    title("H4 同一具肉体被复用 3 次：BeginPlay、World 名单与状态复位");
    for (int mode = 0; mode < 2; mode++) {
        PawnModel p; std::printf("%s\n", mode == 0 ? "【原方案】GameObjectWrapper：SetActive + IsTickEnabled 开关（不注销、不复位）"
                                                    : "【新设计】SpawnActor / DestroyActor：EndPlay + 注销 + OnReturnToPool 复位，出池重新注册");
        for (int cycle = 1; cycle <= 3; cycle++) {
            // ---- 出池
            p.active = true;
            if (mode == 0) {
                p.name = "LightPawn";                                       // GetOrCreatePawn 只恢复了名字
                if (!p.startCalled) { p.startCalled = true; if (!p.inWorld) p.inWorld = true; }   // Start → RegisterAActor（只有第一次）
                if (p.inWorld && !p.hasBegunPlay) { p.hasBegunPlay = true; p.beginPlays++; }       // pending → BeginPlay
            } else {
                p.inWorld = true;                                           // SpawnActor → RegisterAActor
                if (!p.hasBegunPlay) { p.hasBegunPlay = true; p.beginPlays++; }
            }
            bool dyingAtSpawn = p.isDying, colAtSpawn = p.colliderEnabled;
            bool willScheduleCorpseCleanup = !p.isDying;                     // OnUnPossess：if(!isDying) 延迟回收尸体
            // ---- 这一轮里被碾碎死亡：Die
            p.isDying = true; p.colliderEnabled = false; p.name = "Dead Body";
            // ---- 回收
            if (mode == 0) { p.active = false; }                            // OnRecycle：关 Tick + SetActive(false)，仍在 World 名单
            else { p.hasBegunPlay = false; p.inWorld = false; p.isDying = false; p.colliderEnabled = true; p.name = "LightPawn"; p.active = false; }
            std::printf("   第 %d 轮：出池时 isDying=%-5s 碰撞体=%-3s 被玩家抛弃会延迟回收=%-5s｜累计 BeginPlay=%d｜闲置时仍在 World 名单=%s\n",
                        cycle, dyingAtSpawn ? "true" : "false", colAtSpawn ? "开" : "关", willScheduleCorpseCleanup ? "true" : "false",
                        p.beginPlays, p.inWorld ? "true" : "false");
        }
    }
}

// ============================================================ H5：一局 10 分钟的 SoulDecay，创建 / 销毁次数
static void H5_Session() {
    title("H5 模拟一局 10 分钟 SoulDecay：GameObject 创建 / 销毁次数（无池 vs 有池）");
    // 规则：每 3 s 刷一只 AI（50% 重型）；AI 肉体存活 U[8,60] s 后被碾碎（肉体与 AI 灵魂一起回收）；
    //       玩家每 4 s 夺舍一具 AI 肉体：被踢出的 AI 灵魂立即回收，玩家旧肉体 3 s 后回收
    enum { Light, Heavy, Ctrl, K };
    const char* names[K] = {"轻型肉体", "重型肉体", "AI 灵魂"};
    std::mt19937 rng(20261013); std::uniform_real_distribution<float> life(8.f, 60.f), u01(0.f, 1.f);
    struct Ai { int kind; float death; bool alive; };
    std::vector<Ai> ais; int playerKind = Light;
    std::vector<std::pair<float, int>> delayed;                  // (回收时刻, 种类)
    long spawn[K] = {0}, ret[K] = {0}; int live[K] = {0}, peak[K] = {0};
    // 有池（prewarm 0，maxIdle 32）
    int idle[K] = {0}; long created[K] = {0}, overflow[K] = {0}; const int maxIdle = 32;
    auto take = [&](int k) { spawn[k]++; live[k]++; peak[k] = std::max(peak[k], live[k]); if (idle[k] > 0) idle[k]--; else created[k]++; };
    auto give = [&](int k) { ret[k]++; live[k]--; if (idle[k] >= maxIdle) overflow[k]++; else idle[k]++; };
    take(Light);                                                  // 玩家初始肉体
    const float dt = 1.f / 60.f; int nextSpawn = 1, nextPossess = 1;
    for (int step = 0; step <= 600 * 60; step++) {
        float t = step * dt;
        if (t >= nextSpawn * 3.f) { nextSpawn++; int k = u01(rng) < 0.5f ? Heavy : Light; take(k); take(Ctrl); ais.push_back({k, t + life(rng), true}); }
        for (auto& a : ais) if (a.alive && t >= a.death) { a.alive = false; give(a.kind); give(Ctrl); }
        if (t >= nextPossess * 4.f) {
            nextPossess++;
            std::vector<int> cand; for (int i = 0; i < (int)ais.size(); i++) if (ais[i].alive) cand.push_back(i);
            if (!cand.empty()) {
                Ai& a = ais[cand[rng() % cand.size()]];
                a.alive = false; give(Ctrl);                       // 踢出 AI 灵魂
                delayed.push_back({t + 3.f, playerKind});          // 旧肉体 3 秒后回收
                playerKind = a.kind;
            }
        }
        for (auto it = delayed.begin(); it != delayed.end();) { if (t >= it->first) { give(it->second); it = delayed.erase(it); } else ++it; }
    }
    long noPoolCreate = 0, noPoolDestroy = 0, poolCreate = 0, poolDestroy = 0;
    std::printf("%-8s | %8s | %8s | %8s | %10s | %12s\n", "种类", "借出次数", "归还次数", "峰值同时", "有池·创建", "有池·溢出销毁");
    for (int k = 0; k < K; k++) {
        std::printf("%-8s | %8ld | %8ld | %8d | %10ld | %12ld\n", names[k], spawn[k], ret[k], peak[k], created[k], overflow[k]);
        noPoolCreate += spawn[k]; noPoolDestroy += ret[k]; poolCreate += created[k]; poolDestroy += overflow[k];
    }
    std::printf("无池：Instantiate/AddComponent 共 %ld 次，Destroy 共 %ld 次\n", noPoolCreate, noPoolDestroy);
    std::printf("有池（prewarm=0, maxIdle=32）：创建 %ld 次（= 各池峰值之和），销毁 %ld 次；若 prewarm 取峰值，局内创建 0 次\n", poolCreate, poolDestroy);
}

int main(int argc, char** argv) {
    setvbuf(stdout, nullptr, _IONBF, 0);
    struct { const char* id; void (*fn)(); } all[] = {{"H1", H1_Handles}, {"H2", H2_DeferredReturn}, {"H3", H3_DelayedABA}, {"H4", H4_Lifecycle}, {"H5", H5_Session}};
    for (auto& e : all) {
        bool run = argc == 1;
        for (int i = 1; i < argc; i++) if (std::strcmp(argv[i], e.id) == 0) run = true;
        if (run) e.fn();
    }
    return 0;
}
