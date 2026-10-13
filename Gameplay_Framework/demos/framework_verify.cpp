// framework_verify.cpp —— 第二批（第 2~6 章）实测
// 把 MyGamePlayArchitecture@427d7c1 中 World.cs / AActor.cs / AController.cs / APawn.cs /
// AGameModeBase.cs / AGameMode.cs / AGameState(Base).cs 的控制流逐行移植为 C++，验证纯逻辑结论。
//
// Unity 运行时行为无法在本机实测，这里按官方文档与社区实测的规则「建模」（书中标注为【模型假设】）：
//   R1 AddComponent / Instantiate 时 Awake、OnEnable 立即执行（对象处于激活状态时）
//   R2 Start 不会在创建调用内部执行；在 Update 阶段创建的对象，Start 在本帧 Update 阶段之后执行，
//      第一次 Update 在下一帧（Unity Discussions / StackOverflow 实测结论）
//   R3 Destroy() 延迟到本帧 Update 循环之后才真正销毁并调用 OnDestroy；DestroyImmediate() 立即执行
//   R4 已销毁的 UnityEngine.Object 用 == null 判断为 true（"fake null"），但 C# 对象本身仍然存在
//
// 用法：framework_verify.exe            全部实验
//       framework_verify.exe F2 F3      只跑指定实验（追加实验时不要全量重跑，计时数字会变）
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <unordered_set>
#include <vector>

static void title(const char* s) { std::printf("\n==================== %s ====================\n", s); }

// =====================================================================================
// 迷你 Unity 帧模型 + World / AActor 移植
// =====================================================================================
struct Engine;
struct Actor {
    std::string name;
    bool awake = false, started = false, destroyed = false, pendingDestroy = false;
    bool goActive = true, enabled = true;                 // GameObject.activeSelf / Behaviour.enabled（World 不读它们）
    bool hasBegunPlay = false, isTickEnabled = true;      // AActor 自己的两个标志
    bool callBaseOnDestroy = true;                        // 子类 override OnDestroy 是否调用了 base
    int beginPlayFrame = -1, beginPlays = 0;
    std::map<int, int> ticks;                             // frame -> 本帧 Tick 次数
    std::function<void(Actor&)> onTick, onBeginPlay;
};
static bool IsNull(const Actor* a) { return a == nullptr || a->destroyed; }   // R4：Unity 重载的 ==

struct World {
    std::vector<Actor*> actors, pending;
    std::vector<std::string> log;
    Engine* eng = nullptr;
    static bool Contains(const std::vector<Actor*>& v, const Actor* a) { return std::find(v.begin(), v.end(), a) != v.end(); }
    void RegisterAActor(Actor* a) {                       // World.cs:92-102
        if (!Contains(actors, a) && !Contains(pending, a)) pending.push_back(a);
    }
    void UnregisterAActor(Actor* a) {                     // World.cs:118-130（List.Remove 只删第一个匹配）
        auto it = std::find(actors.begin(), actors.end(), a);
        if (it != actors.end()) actors.erase(it);
        auto jt = std::find(pending.begin(), pending.end(), a);
        if (jt != pending.end()) pending.erase(jt);
    }
    void Update(int frame);                               // World.cs:51-88
};

struct Engine {
    int frame = 0;
    World world;
    std::vector<std::unique_ptr<Actor>> all;
    std::vector<Actor*> startQueue, destroyQueue;
    Engine() { world.eng = this; }
    Actor* Spawn(const std::string& n) {                  // R1：Awake 立即执行；R2：Start 排队
        all.push_back(std::make_unique<Actor>());
        Actor* a = all.back().get();
        a->name = n; a->awake = true;
        startQueue.push_back(a);
        return a;
    }
    void OnDestroy(Actor* a) {                            // AActor.OnDestroy：if (World.HasInstance) World.UnregisterAActor(this)
        if (a->callBaseOnDestroy) world.UnregisterAActor(a);
    }
    void Destroy(Actor* a) {                              // R3：延迟到帧末
        if (a->destroyed || a->pendingDestroy) return;
        a->pendingDestroy = true;
        destroyQueue.push_back(a);
    }
    void DestroyImmediate(Actor* a) {
        if (a->destroyed) return;
        a->destroyed = true;
        if (a->awake) OnDestroy(a);
    }
    void RunStarts() {                                    // AActor.Start：World.RegisterAActor(this)
        auto q = startQueue; startQueue.clear();
        for (Actor* a : q) {
            if (a->destroyed) continue;
            if (!a->goActive || !a->enabled) { startQueue.push_back(a); continue; }
            a->started = true;
            world.RegisterAActor(a);
        }
    }
    void Frame() {
        ++frame;
        RunStarts();                                      // 场景对象 / 上一帧之后创建的对象
        world.Update(frame);
        RunStarts();                                      // R2：本帧 Update 中创建的对象，Start 在 Update 阶段之后
        auto q = destroyQueue; destroyQueue.clear();      // R3：帧末真正销毁
        for (Actor* a : q) { a->destroyed = true; if (a->awake) OnDestroy(a); }
    }
};

void World::Update(int frame) {
    if (!pending.empty()) {
        auto newActors = pending;                         // 拷贝一份防止遍历时修改
        pending.clear();
        for (Actor* a : newActors) {
            if (!Contains(actors, a)) {
                actors.push_back(a);
                a->hasBegunPlay = true;                   // AActor.BeginPlay 基类
                a->beginPlays++;
                if (a->beginPlayFrame < 0) a->beginPlayFrame = frame;
                if (a->onBeginPlay) a->onBeginPlay(*a);
            }
        }
    }
    for (int i = (int)actors.size() - 1; i >= 0; i--) {   // 倒序遍历
        if (i >= (int)actors.size()) {                    // C#：_actors[i] 抛 ArgumentOutOfRangeException，Update 中止
            log.push_back("第 " + std::to_string(frame) + " 帧：下标 " + std::to_string(i) + " 越界（List 只剩 " +
                          std::to_string(actors.size()) + " 个）→ C# 抛 ArgumentOutOfRangeException，本帧剩余 Actor 不再 Tick");
            return;
        }
        if (IsNull(actors[i])) { actors.erase(actors.begin() + i); continue; }
        Actor* a = actors[i];
        if (a->hasBegunPlay && a->isTickEnabled) {
            a->ticks[frame]++;
            if (a->onTick) a->onTick(*a);
        }
    }
}

// =====================================================================================
// F1：注册 / 注销 / Tick 的复杂度（List.Contains 是线性查找）
// =====================================================================================
struct BenchActor { virtual ~BenchActor() = default; virtual void Tick(float dt) { acc += dt; } float acc = 0; bool begun = false; };

static double msSince(std::chrono::steady_clock::time_point t0) {
    return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
}

static void F1_Complexity() {
    title("F1 World 注册 / 首帧 BeginPlay / Tick / 场景卸载 的耗时（List.Contains 线性查找，C++ 移植）");
    std::printf("%7s | %11s | %13s | %10s | %12s | %16s\n", "N", "Register(ms)", "首帧pending(ms)", "Tick(ms)", "全部注销(ms)", "哈希集合注册(ms)");
    for (int N : {100, 1000, 5000, 20000}) {
        std::vector<std::unique_ptr<BenchActor>> owners;
        for (int i = 0; i < N; i++) owners.push_back(std::make_unique<BenchActor>());
        std::vector<BenchActor*> actors, pending;
        auto has = [](const std::vector<BenchActor*>& v, BenchActor* a) { return std::find(v.begin(), v.end(), a) != v.end(); };
        auto t0 = std::chrono::steady_clock::now();
        for (auto& o : owners) if (!has(actors, o.get()) && !has(pending, o.get())) pending.push_back(o.get());
        double reg = msSince(t0);
        t0 = std::chrono::steady_clock::now();
        auto copy = pending; pending.clear();
        for (auto* a : copy) if (!has(actors, a)) { actors.push_back(a); a->begun = true; }
        double first = msSince(t0);
        t0 = std::chrono::steady_clock::now();
        for (int i = (int)actors.size() - 1; i >= 0; i--) if (actors[i]->begun) actors[i]->Tick(0.016f);
        double tick = msSince(t0);
        t0 = std::chrono::steady_clock::now();
        for (auto& o : owners) {                                    // 按创建顺序逐个 OnDestroy → Remove
            auto it = std::find(actors.begin(), actors.end(), o.get());
            if (it != actors.end()) actors.erase(it);
        }
        double unreg = msSince(t0);
        std::unordered_set<BenchActor*> set1, set2;
        t0 = std::chrono::steady_clock::now();
        for (auto& o : owners) if (!set1.count(o.get()) && !set2.count(o.get())) set2.insert(o.get());
        double hreg = msSince(t0);
        std::printf("%7d | %11.3f | %13.3f | %10.4f | %12.3f | %16.3f\n", N, reg, first, tick, unreg, hreg);
    }
    std::printf("→ 注册与首帧处理都是 O(N^2)：每次 Contains 扫描整个 List；注销同理（Remove 先查找再整体搬移）\n");
}

// =====================================================================================
// F2：Tick 中修改 Actor 列表的 10 种情况
// =====================================================================================
static std::string TickRow(Engine& e, int frames) {
    std::string s;
    for (auto& up : e.all) {
        Actor* a = up.get();
        char buf[256];
        std::string per;
        for (int f = 1; f <= frames; f++) { per += std::to_string(a->ticks.count(f) ? a->ticks[f] : 0); if (f < frames) per += "/"; }
        std::snprintf(buf, sizeof buf, "%s[BeginPlay@%s Tick=%s]  ", a->name.c_str(),
                      a->beginPlayFrame < 0 ? "-" : std::to_string(a->beginPlayFrame).c_str(), per.c_str());
        s += buf;
    }
    return s;
}

static void F2_TickMutation() {
    title("F2 World.Update 中修改 Actor 列表（倒序遍历的真实安全边界）");
    std::printf("说明：场景里预置 A0..A4（第 1 帧 Start→pending，第 1 帧 World.Update 中 BeginPlay 并 Tick），在第 2 帧触发动作，跑 3 帧。\n");
    std::printf("      Tick=x/y/z 表示第 1/2/3 帧被 Tick 的次数；_actors 列表顺序 = 注册顺序 A0..A4，倒序遍历先 Tick A4。\n");
    struct Case { const char* name; std::function<void(Engine&, std::vector<Actor*>&)> setup; };
    std::vector<Case> cases = {
        {"C1 A2 在 Tick 中 Destroy(自己)（Unity 默认，延迟到帧末）", [](Engine& e, std::vector<Actor*>& A) {
            A[2]->onTick = [&e](Actor& me) { if (e.frame == 2) e.Destroy(&me); }; }},
        {"C2 A2 在 Tick 中 DestroyImmediate(自己)", [](Engine& e, std::vector<Actor*>& A) {
            A[2]->onTick = [&e](Actor& me) { if (e.frame == 2) e.DestroyImmediate(&me); }; }},
        {"C3 A2 在 Tick 中 DestroyImmediate(A3)（下标更高 = 本帧已 Tick 过）", [](Engine& e, std::vector<Actor*>& A) {
            Actor* v = A[3]; A[2]->onTick = [&e, v](Actor&) { if (e.frame == 2) e.DestroyImmediate(v); }; }},
        {"C4 A3 在 Tick 中 DestroyImmediate(A1)（下标更低 = 本帧还没 Tick）", [](Engine& e, std::vector<Actor*>& A) {
            Actor* v = A[1]; A[3]->onTick = [&e, v](Actor&) { if (e.frame == 2) e.DestroyImmediate(v); }; }},
        {"C5 A3 在 Tick 中 Destroy(A1)（延迟销毁：被“打死”的 A1 本帧仍会 Tick）", [](Engine& e, std::vector<Actor*>& A) {
            Actor* v = A[1]; A[3]->onTick = [&e, v](Actor&) { if (e.frame == 2) e.Destroy(v); }; }},
        {"C6 A4 在 Tick 中 DestroyImmediate(A0,A1,A2)（批量清场）", [](Engine& e, std::vector<Actor*>& A) {
            Actor *a = A[0], *b = A[1], *c = A[2];
            A[4]->onTick = [&e, a, b, c](Actor&) { if (e.frame == 2) { e.DestroyImmediate(a); e.DestroyImmediate(b); e.DestroyImmediate(c); } }; }},
        {"C7 A2 在 Tick 中 Spawn 新 Actor N（新 Actor 何时 BeginPlay / 第一次 Tick）", [](Engine& e, std::vector<Actor*>& A) {
            A[2]->onTick = [&e](Actor&) { if (e.frame == 2) e.Spawn("N"); }; }},
        {"C8 A4 在 Tick 中把 A1 的 GameObject SetActive(false)、A2 的组件 enabled=false", [](Engine& e, std::vector<Actor*>& A) {
            Actor *b = A[1], *c = A[2]; A[4]->onTick = [&e, b, c](Actor&) { if (e.frame == 2) { b->goActive = false; c->enabled = false; } }; }},
        {"C9 同一批 pending：P.BeginPlay 里 DestroyImmediate(Q)，Q 排在 P 后面", [](Engine& e, std::vector<Actor*>& A) {
            A[2]->onTick = [&e](Actor&) {
                if (e.frame != 2) return;
                Actor* p = e.Spawn("P"); Actor* q = e.Spawn("Q");
                p->onBeginPlay = [&e, q](Actor&) { e.DestroyImmediate(q); };
            }; }},
        {"C10 A2 的子类 override OnDestroy 没调 base，然后在 Tick 中 Destroy(自己)", [](Engine& e, std::vector<Actor*>& A) {
            A[2]->callBaseOnDestroy = false;
            A[2]->onTick = [&e](Actor& me) { if (e.frame == 2) e.Destroy(&me); }; }},
    };
    for (auto& c : cases) {
        Engine e;
        std::vector<Actor*> A;
        for (int i = 0; i < 5; i++) A.push_back(e.Spawn("A" + std::to_string(i)));
        c.setup(e, A);
        for (int f = 0; f < 3; f++) e.Frame();
        std::printf("\n%s\n  %s\n", c.name, TickRow(e, 3).c_str());
        std::string extra;
        for (auto& up : e.all) {
            Actor* a = up.get();
            if (a->destroyed && a->beginPlays > 0 && (a->name == "Q")) extra += "  Q 已被 DestroyImmediate，但仍执行了 BeginPlay、被加入 _actors；";
        }
        size_t nulls = 0; for (Actor* a : e.world.actors) if (IsNull(a)) nulls++;
        std::printf("  第 3 帧末 _actors.Count=%zu（其中已销毁=%zu）%s\n", e.world.actors.size(), nulls, extra.c_str());
        for (auto& l : e.world.log) std::printf("  %s\n", l.c_str());
    }
}

// =====================================================================================
// F3：Possess / UnPossess 的不变式（AController.cs / APawn.cs 逐行移植）
// =====================================================================================
struct Ctl;
struct Pwn {
    std::string name; Ctl* controller = nullptr; bool destroyed = false;
    int onPossessCalls = 0, onUnPossessCalls = 0;
    std::function<void(Pwn&, Ctl*)> onPossess; std::function<void(Pwn&)> onUnPossess;
    void PossessedBy(Ctl* c);
    void UnPossessed();
};
struct Ctl {
    std::string name; Pwn* pawn = nullptr; bool destroyed = false;
    int onPossessCalls = 0, onUnPossessCalls = 0;
    std::function<void(Ctl&, Pwn*)> onUnPossessHook;
    void Possess(Pwn* p);
    void UnPossess();
};
static std::vector<std::string> g_trace;
static bool Alive(const Pwn* p) { return p && !p->destroyed; }   // Unity 重载 ==
static bool Alive(const Ctl* c) { return c && !c->destroyed; }
void Pwn::PossessedBy(Ctl* c) {
    controller = c;
    g_trace.push_back(name + ".PossessedBy(" + c->name + ") → " + name + ".Controller=" + c->name);
    onPossessCalls++;
    g_trace.push_back("  " + name + ".OnPossess(" + c->name + ")");
    if (onPossess) onPossess(*this, c);
}
void Pwn::UnPossessed() {
    onUnPossessCalls++;
    g_trace.push_back("  " + name + ".OnUnPossess()（此时 Controller 仍=" + (controller ? controller->name : "null") + "）");
    if (onUnPossess) onUnPossess(*this);
    controller = nullptr;
    g_trace.push_back(name + ".Controller=null");
}
void Ctl::Possess(Pwn* p) {
    if (!Alive(p)) return;
    if (pawn == p) return;
    g_trace.push_back(name + ".Possess(" + p->name + ") 开始");
    if (Alive(pawn)) UnPossess();                                         // ① 先抛弃自己的
    if (Alive(p->controller)) p->controller->UnPossess();                 // ② 踢掉目标原来的控制者
    p->PossessedBy(this);                                                 // ③ Pawn 侧赋值 + Pawn 回调
    pawn = p;                                                             // ④ Controller 侧赋值
    g_trace.push_back(name + ".ControlledPawn=" + p->name);
    onPossessCalls++;
    g_trace.push_back("  " + name + ".OnPossess(" + p->name + ")");       // ⑤ Controller 回调
}
void Ctl::UnPossess() {
    if (!Alive(pawn)) { g_trace.push_back(name + ".UnPossess()：不存在控制的对象 → return"); return; }
    g_trace.push_back(name + ".UnPossess(" + pawn->name + ") 开始");
    pawn->UnPossessed();                                                  // ① Pawn 侧
    onUnPossessCalls++;
    g_trace.push_back("  " + name + ".OnUnPossess(" + (pawn ? pawn->name : "null") + ")（重新读取 ControlledPawn 字段）");
    if (onUnPossessHook) onUnPossessHook(*this, pawn);
    pawn = nullptr;                                                       // ③ 断开
    g_trace.push_back(name + ".ControlledPawn=null");
}

static std::string Check(const std::vector<Ctl*>& cs, const std::vector<Pwn*>& ps) {
    std::string bad;
    for (Ctl* c : cs) for (Pwn* p : ps) {
        bool a = (c->pawn == p), b = (p->controller == c);
        if (a != b) bad += "  ✗ " + c->name + ".ControlledPawn" + (a ? "==" : "!=") + p->name + " 但 " + p->name + ".Controller" + (b ? "==" : "!=") + c->name + "\n";
    }
    std::string st = "  状态：";
    for (Ctl* c : cs) st += c->name + "→" + (c->pawn ? c->pawn->name : "null") + "  ";
    for (Pwn* p : ps) st += p->name + "←" + (p->controller ? p->controller->name : "null") + "  ";
    return st + "\n" + (bad.empty() ? "  ✓ 双向引用一致\n" : bad);
}
static void Dump(const char* t, const std::vector<Ctl*>& cs, const std::vector<Pwn*>& ps) {
    std::printf("\n%s\n", t);
    for (auto& s : g_trace) std::printf("    %s\n", s.c_str());
    g_trace.clear();
    std::printf("%s", Check(cs, ps).c_str());
}

static void F3_Possess() {
    title("F3 Possess / UnPossess：调用顺序与双向引用不变式");
    {   // P1 正常切换
        Ctl c{"C"}; Pwn p1{"P1"}, p2{"P2"};
        c.Possess(&p1); g_trace.clear();
        c.Possess(&p2);
        Dump("P1 C 已控制 P1，再 Possess(P2)：回调顺序", {&c}, {&p1, &p2});
    }
    {   // P2 抢夺
        Ctl c1{"C1"}, c2{"C2"}; Pwn p{"P"};
        c1.Possess(&p); g_trace.clear();
        c2.Possess(&p);
        Dump("P2 C1 控制 P，C2.Possess(P)（夺舍：踢掉原灵魂）", {&c1, &c2}, {&p});
    }
    {   // P3 OnPossess 抛异常（例：字段在 BeginPlay 才初始化，Possess 早于 BeginPlay）
        Ctl c{"C"}; Pwn p{"P"};
        bool begun = false;
        p.onPossess = [&begun](Pwn&, Ctl*) { if (!begun) throw std::runtime_error("NullReferenceException：_renderer 在 BeginPlay 里才赋值"); };
        try { c.Possess(&p); } catch (const std::exception& ex) { g_trace.push_back(std::string("!! 异常冒出 Possess：") + ex.what()); }
        Dump("P3 Pawn.OnPossess 抛异常（Possess 早于 BeginPlay，像 MyHeroPawn 那样在 BeginPlay 里取 Renderer）", {&c}, {&p});
        begun = true;
        c.Possess(&p);
        Dump("P3' 下一帧（BeginPlay 之后）重试 C.Possess(P)", {&c}, {&p});
    }
    {   // P4 OnPossess 里再 Possess（重入）
        Ctl c{"C"}; Pwn seat{"Seat"}, car{"Car"};
        seat.onPossess = [&car](Pwn&, Ctl* who) { who->Possess(&car); };   // 坐上座位 → 立即转控载具
        c.Possess(&seat);
        Dump("P4【构造反例】Seat.OnPossess 里调用 C.Possess(Car)（坐上座位立即转控载具）", {&c}, {&seat, &car});
    }
    {   // P5 OnUnPossess 里再 Possess（重入）
        Ctl c{"C"}; Pwn body{"Body"}, home{"Home"};
        c.Possess(&body); g_trace.clear();
        bool once = false;
        body.onUnPossess = [&](Pwn&) { if (!once) { once = true; c.Possess(&home); } };  // 离魂时把灵魂送回大本营（只做一次）
        c.UnPossess();
        Dump("P5【构造反例】Body.OnUnPossess 里调用 C.Possess(Home)（离魂时送回大本营）", {&c}, {&body, &home});
        std::printf("  Controller.OnUnPossess 被调用 %d 次（期望 1 次）\n", c.onUnPossessCalls);
    }
    {   // P6 Pawn 被直接 Destroy（没走 UnPossess）
        Ctl c{"C"}; Pwn old{"Old"}, nw{"New"};
        c.Possess(&old); g_trace.clear();
        int before = old.onUnPossessCalls;
        old.destroyed = true;                                             // Destroy(gameObject) 而不是 Die()
        c.Possess(&nw);
        Dump("P6 Old 被直接 Destroy（没有先 UnPossess），之后 C.Possess(New)", {&c}, {&old, &nw});
        std::printf("  Old.OnUnPossess 执行次数 = %d（它的清理逻辑——停寿命计时器、改名、关碰撞——永远不会执行）\n", old.onUnPossessCalls - before);
    }
}

// =====================================================================================
// F4：场景里有两个 SurvivalGameMode（AGameModeBase / AGameMode / World 注册逻辑移植）
// =====================================================================================
struct GS { std::string name; int matchState = 0; bool pendingDestroy = false, destroyed = false; };
static const char* kState[] = {"EnteringMap", "WaitingToStart", "InProgress", "WaitingPostMatch"};
struct GM;
struct W4 {
    GM* authority = nullptr; GS* gameState = nullptr;
    std::vector<std::unique_ptr<GS>> states;
    std::map<std::string, GM*> namedTimers;               // timerName → 回调属于哪个 GameMode
    int triggers = 0, players = 0, controllers = 0;
    std::vector<std::string> log;
};
struct GM {
    std::string name; W4* w; GS* gameState = nullptr;
    void SetMatchState(int s) {                           // AGameState.SetMatchState
        if (!gameState || gameState->matchState == s) return;
        w->log.push_back("  " + gameState->name + "：" + kState[gameState->matchState] + " → " + kState[s] +
                         (gameState->pendingDestroy ? "（这个 GameState 已被 World 拒绝并 Destroy，帧末才消失）" : "") + " → EventSystem.Trigger");
        gameState->matchState = s; w->triggers++;
    }
    void BeginPlay() {                                    // SurvivalGameMode.BeginPlay → base.BeginPlay（AGameModeBase）
        w->log.push_back(name + ".BeginPlay");
        if (w->authority) w->log.push_back("  World.RegisterGameMode：已有裁判 → Log.E 并 return（但 BeginPlay 继续执行）");
        else { w->authority = this; w->log.push_back("  World.RegisterGameMode：成为 AuthorityGameMode"); }
        w->states.push_back(std::make_unique<GS>()); gameState = w->states.back().get();   // AGameMode.InitGameState
        gameState->name = "GameState_" + name;
        if (w->authority != this) {                       // World.RegisterGameState
            gameState->pendingDestroy = true;
            w->log.push_back("  World.RegisterGameState：越权 → Destroy(" + gameState->name + ")（延迟），return");
        } else { w->gameState = gameState; w->log.push_back("  World.RegisterGameState：注册 " + gameState->name); }
        SetMatchState(1);                                 // AGameMode.StartPlay
        SetMatchState(2);                                 // SurvivalGameMode.StartMatch
        w->players++; w->controllers++;                   // CreatePawn + new SoulPlayerController + Possess
        w->log.push_back("  生成玩家肉体 + SoulPlayerController 并 Possess");
        if (w->namedTimers.count("AISpawner")) w->log.push_back("  CreateTimer(\"AISpawner\")：同名计时器仍有效 → 先 Stop 旧的（属于 " + w->namedTimers["AISpawner"]->name + "）再新建");
        else w->log.push_back("  CreateTimer(\"AISpawner\")：新建");
        w->namedTimers["AISpawner"] = this;
    }
};

static void F4_TwoGameModes() {
    title("F4 场景里放了两个 SurvivalGameMode（“将忽略后注册的裁判”是否成立）");
    W4 w;
    GM a{"GM_A", &w}, b{"GM_B", &w};
    a.BeginPlay(); b.BeginPlay();
    for (auto& s : w.log) std::printf("%s\n", s.c_str());
    for (auto& s : w.states) if (s->pendingDestroy) s->destroyed = true;   // 帧末
    std::printf("\n帧末汇总：AuthorityGameMode=%s，World.GameState=%s\n", w.authority->name.c_str(), w.gameState->name.c_str());
    std::printf("  MatchStateChanged 的 Trigger 次数 = %d（其中来自被拒绝的 GameState = %d）\n", w.triggers, 2);
    std::printf("  玩家肉体 = %d，SoulPlayerController = %d（两个 Controller 读同一份输入 → 两具肉体同步移动）\n", w.players, w.controllers);
    std::printf("  \"AISpawner\" 计时器 = 1 个，回调属于 %s（不是裁判）\n", w.namedTimers["AISpawner"]->name.c_str());
    std::printf("  %s.GameState 帧末已销毁 → SpawnRandomAI 里 base.GameState != null 为 false → “比赛结束就不刷怪”的检查对它永远不成立\n", b.name.c_str());
}

// =====================================================================================
// F5：AGameStateBase.ServerWorldTimeSeconds —— 名字说的是“服务器世界时间”，实际是本地 scaled dt 的 float 累加
// =====================================================================================
static void F5_WorldTime() {
    title("F5 ServerWorldTimeSeconds：scaled deltaTime 的 float 累加");
    // 120 s 真实时间、60 fps；t=30 s 卡顿一帧 0.6 s，t=60~70 s 暂停（timeScale=0），t=90 s 再卡 0.6 s
    const float maxDt = 0.33333334f;                      // Time.maximumDeltaTime 默认 1/3
    double real = 0; float sw = 0; bool begun = false; int frame = 0;
    bool hitch1 = false, hitch2 = false;
    while (real < 120.0) {
        double dt = 1.0 / 60.0;
        if (!hitch1 && real >= 30.0) { dt = 0.6; hitch1 = true; }
        if (!hitch2 && real >= 90.0) { dt = 0.6; hitch2 = true; }
        float timeScale = (real >= 60.0 && real < 70.0) ? 0.0f : 1.0f;
        real += dt; frame++;
        if (frame == 2) begun = true;                     // GameState 在第 2 帧才 BeginPlay（第 2 章）
        float scaled = std::min((float)dt, maxDt) * timeScale;
        if (begun) sw += scaled;                          // AGameStateBase.Tick
    }
    std::printf("真实经过 %.3f s → ServerWorldTimeSeconds = %.3f s（差 %.3f s：暂停 10 s + 两次卡顿各被截断 0.267 s + 少算第 1 帧）\n", real, sw, real - sw);
    for (double hours : {1.0, 10.0, 100.0}) {
        float f = 0; double d = 0; long long n = (long long)(hours * 3600 * 60);
        for (long long i = 0; i < n; i++) { f += 1.0f / 60.0f; d += 1.0 / 60.0; }
        std::printf("连续 %5.0f 小时 60fps：float 累加 = %.2f s，double = %.2f s，误差 %.2f s（%.3f%%）\n", hours, f, d, d - f, (d - f) / d * 100);
    }
}

int main(int argc, char** argv) {
    setvbuf(stdout, nullptr, _IONBF, 0);
    struct { const char* id; void (*fn)(); } all[] = {
        {"F1", F1_Complexity}, {"F2", F2_TickMutation}, {"F3", F3_Possess}, {"F4", F4_TwoGameModes}, {"F5", F5_WorldTime}};
    for (auto& e : all) {
        bool run = argc == 1;
        for (int i = 1; i < argc; i++) if (std::strcmp(argv[i], e.id) == 0) run = true;
        if (run) e.fn();
    }
    return 0;
}
