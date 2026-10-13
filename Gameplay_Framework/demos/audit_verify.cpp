// audit_verify.cpp
// 目的：把仓库 dxHWH/MyGamePlayArchitecture (commit 427d7c1) 中
//   TimerSystem.cs / EventSystem.cs + RingBuffer.cs / ObjectPool.cs / World.cs 的核心逻辑
// 逐行移植为 C++（保持控制流与数据结构语义一致），用来实测 Phase 1 审计里的结论。
// 说明：C# 的 Array.Resize 不会让旧引用失效，而 std::vector 会；
//       因此回调前先拷贝 std::function（语义等价于 C# 先读出委托引用再 Invoke）。
#include <cstdio>
#include <cstdint>
#include <vector>
#include <deque>
#include <stack>
#include <functional>
#include <unordered_map>
#include <memory>
#include <string>
#include <stdexcept>
#include <algorithm>

// ============================== TimerSystem 移植 ==============================
struct TimerHandle {
    int Index = 0, Generation = 0;               // C#: default(TimerHandle) == (0,0)
    bool operator==(const TimerHandle& o) const { return Index == o.Index && Generation == o.Generation; }
    bool operator!=(const TimerHandle& o) const { return !(*this == o); }
};
static const TimerHandle Invalid{-1, -1};

struct TimerData {
    float duration = 0, timeElapsed = 0, interval = 0, intervalElapsed = 0;
    std::function<void()> onTrigger, onComplete;
    bool isLoop = false, isPaused = false, isUnScaled = false, isChase = false;
    int timerId = 0;
};

class TimerSystem {
public:
    static constexpr int MAX_CHASE_COUNT = 5;
    std::vector<TimerData> _timers = std::vector<TimerData>(128);
    int _timerCount = 0;
    std::vector<int> _handleToIndex, _indexToHandle, _generations;
    std::deque<int> _freeIndices;                       // C#: Queue<int>  (FIFO)
    std::unordered_map<int, TimerHandle> _idToHandle;
    std::vector<TimerHandle> _expiredTimersCache, _intervalTimersCache;

    void EnsureCapacity(int minCapacity) {
        if ((int)_timers.size() < minCapacity)
            _timers.resize(std::max((int)_timers.size() * 2, minCapacity));
    }

    TimerHandle CreateTimer(float duration, std::function<void()> onComplete,
                            float interval = -1.f, std::function<void()> onTrigger = nullptr,
                            int timerId = 0, bool isLoop = false, bool isUnScaled = false, bool isChase = true) {
        if (timerId != 0) {
            auto it = _idToHandle.find(timerId);
            if (it != _idToHandle.end()) {
                TimerHandle old = it->second;
                if (IsHandleValid(old)) StopTimer(old);
                _idToHandle.erase(timerId);
            }
        }
        TimerData data;
        data.duration = duration; data.interval = interval;
        data.onTrigger = onTrigger; data.onComplete = onComplete;
        data.isLoop = isLoop; data.isUnScaled = isUnScaled; data.isChase = isChase; data.timerId = timerId;

        int handleIndex;
        if (!_freeIndices.empty()) { handleIndex = _freeIndices.front(); _freeIndices.pop_front(); }
        else {
            handleIndex = (int)_handleToIndex.size();
            _handleToIndex.push_back(-1); _indexToHandle.push_back(-1); _generations.push_back(0);
        }
        TimerHandle handle{handleIndex, _generations[handleIndex]};
        EnsureCapacity(_timerCount + 1);
        _timers[_timerCount] = data;
        int realIndex = _timerCount++;
        _handleToIndex[handleIndex] = realIndex;
        if (realIndex >= (int)_indexToHandle.size()) _indexToHandle.push_back(handleIndex);
        else _indexToHandle[realIndex] = handleIndex;
        if (timerId != 0) _idToHandle[timerId] = handle;
        return handle;
    }

    void StopTimer(TimerHandle handle) {
        if (!IsHandleValid(handle)) return;
        int handleIndex = handle.Index;
        int realIndex = _handleToIndex[handleIndex];
        int timerId = _timers[realIndex].timerId;
        if (timerId != 0) {
            auto it = _idToHandle.find(timerId);
            if (it != _idToHandle.end() && it->second == handle) _idToHandle.erase(it);
        }
        int lastRealIndex = _timerCount - 1;
        if (realIndex != lastRealIndex) {
            _timers[realIndex] = _timers[lastRealIndex];
            int lastHandleIndex = _indexToHandle[lastRealIndex];
            _indexToHandle[realIndex] = lastHandleIndex;
            _handleToIndex[lastHandleIndex] = realIndex;
        }
        _timerCount--;
        _generations[handleIndex]++;
        _freeIndices.push_back(handleIndex);
    }

    bool IsHandleValid(TimerHandle h) const {
        if (h.Index < 0 || h.Index >= (int)_generations.size()) return false;
        return _generations[h.Index] == h.Generation;
    }

    float GetTimeRemaining(TimerHandle h) const {
        if (!IsHandleValid(h)) return 0.f;
        const TimerData& d = _timers[_handleToIndex[h.Index]];
        return std::max(0.f, d.duration - d.timeElapsed);
    }

    const TimerData* Peek(TimerHandle h) const { return IsHandleValid(h) ? &_timers[_handleToIndex[h.Index]] : nullptr; }

    void Tick(float scaledDt, float unscaledDt) {
        _expiredTimersCache.clear();
        _intervalTimersCache.clear();
        for (int i = 0; i < _timerCount; i++) {
            TimerData& timer = _timers[i];
            if (timer.isPaused) continue;
            float dt = timer.isUnScaled ? unscaledDt : scaledDt;
            timer.timeElapsed += dt;
            if (timer.interval > 0.f) {
                timer.intervalElapsed += dt;
                int handleIndex = _indexToHandle[i];
                int generation = _generations[handleIndex];
                if (timer.isChase) {
                    int chaseCount = 0;
                    while (timer.intervalElapsed >= timer.interval && chaseCount < MAX_CHASE_COUNT) {
                        _intervalTimersCache.push_back({handleIndex, generation});
                        timer.intervalElapsed -= timer.interval;
                        chaseCount++;
                    }
                } else {
                    if (timer.intervalElapsed >= timer.interval) {
                        _intervalTimersCache.push_back({handleIndex, generation});
                        timer.intervalElapsed -= timer.interval;
                    }
                }
                if (timer.intervalElapsed >= timer.interval) timer.intervalElapsed = 0.f;
            }
            if (timer.timeElapsed >= timer.duration) {
                int handleIndex = _indexToHandle[i];
                _expiredTimersCache.push_back({handleIndex, _generations[handleIndex]});
                if (timer.isLoop) timer.timeElapsed -= timer.duration;
            }
        }
        for (size_t i = 0; i < _intervalTimersCache.size(); i++) {
            TimerHandle h = _intervalTimersCache[i];
            if (!IsHandleValid(h)) continue;
            auto cb = _timers[_handleToIndex[h.Index]].onTrigger;
            if (cb) cb();
        }
        for (size_t i = 0; i < _expiredTimersCache.size(); i++) {
            TimerHandle h = _expiredTimersCache[i];
            if (!IsHandleValid(h)) continue;
            TimerData data = _timers[_handleToIndex[h.Index]];   // C#: var data = _timers[realIndex]; (拷贝)
            if (data.onComplete) data.onComplete();
            if (!data.isLoop && IsHandleValid(h)) StopTimer(h);
        }
    }
};

static void title(const char* s) { std::printf("\n==================== %s ====================\n", s); }

// E1：default(TimerHandle) == (0,0) 会别名到"第一个被创建的计时器"
static void E1_DefaultHandleAlias() {
    title("E1 default(TimerHandle) 别名问题");
    {
        TimerSystem ts;
        // SurvivalGameMode.BeginPlay 的真实顺序：玩家附身 → CombatPawn.OnPossess 创建寿命计时器 → 再创建 AISpawner
        TimerHandle playerDecay = ts.CreateTimer(5.f, [] {});
        TimerHandle spawner = ts.CreateTimer(3.f, [] {}, -1.f, nullptr, /*id*/ 1001, /*loop*/ true);
        std::printf("[A] 当前 Demo 顺序：playerDecay=(%d,%d) spawner=(%d,%d)\n",
                    playerDecay.Index, playerDecay.Generation, spawner.Index, spawner.Generation);
        TimerHandle aiPawnDecay{};  // AI 肉体从未创建计时器：private TimerHandle _decayTimer; → (0,0)
        std::printf("    AI 肉体的 _decayTimer=(%d,%d)，!= Invalid ? %s，IsHandleValid ? %s\n",
                    aiPawnDecay.Index, aiPawnDecay.Generation, aiPawnDecay != Invalid ? "true" : "false",
                    ts.IsHandleValid(aiPawnDecay) ? "true(危险)" : "false");
        // AI 肉体被碾碎：Die() → UnPossess → OnUnPossess → StopTimer(_decayTimer)
        if (aiPawnDecay != Invalid) ts.StopTimer(aiPawnDecay);
        std::printf("    AI 死亡后：玩家寿命计时器 IsHandleValid=%s，剩余=%.2f s → UI 显示 DIE，且玩家永不爆炸\n",
                    ts.IsHandleValid(playerDecay) ? "true" : "false", ts.GetTimeRemaining(playerDecay));
    }
    {
        TimerSystem ts;
        TimerHandle spawner = ts.CreateTimer(3.f, [] {}, -1.f, nullptr, 1001, true);   // 若把刷怪器先创建
        TimerHandle playerDecay = ts.CreateTimer(5.f, [] {});
        TimerHandle aiPawnDecay{};
        if (aiPawnDecay != Invalid) ts.StopTimer(aiPawnDecay);
        std::printf("[B] 若刷怪器先创建：spawner=(%d,%d) 在任意 AI 死亡后 IsHandleValid=%s → 刷怪停止\n",
                    spawner.Index, spawner.Generation, ts.IsHandleValid(spawner) ? "true" : "false");
        (void)playerDecay;
    }
    {
        TimerSystem ts;
        TimerHandle t0 = ts.CreateTimer(5.f, [] {});
        ts.StopTimer(t0);                         // gen[0] → 1
        TimerHandle t1 = ts.CreateTimer(5.f, [] {});
        TimerHandle aiPawnDecay{};
        if (aiPawnDecay != Invalid) ts.StopTimer(aiPawnDecay);
        std::printf("[C] slot0 被复用过一次之后：t1=(%d,%d)，default 句柄已失效，t1 存活=%s（所以当前 Demo 能侥幸跑通）\n",
                    t1.Index, t1.Generation, ts.IsHandleValid(t1) ? "true" : "false");
    }
}

// E2：IsHandleValid 只比对 generation、不检查槽位是否在用 + _handleToIndex 残留 → 伪造/越权句柄能删掉别人的计时器
static void E2_ForgedHandle() {
    title("E2 空闲槽 + 残留映射 + 伪造句柄");
    TimerSystem ts;
    TimerHandle A = ts.CreateTimer(10.f, [] {});
    TimerHandle B = ts.CreateTimer(20.f, [] {});
    TimerHandle C = ts.CreateTimer(30.f, [] {});
    ts.StopTimer(B);   // slot1 gen→1，dense[1] ← C，_handleToIndex[1] 仍残留为 1
    std::printf("Stop(B) 后：_handleToIndex[1]=%d（残留，未置 -1），_generations[1]=%d，slot1 位于 free list\n",
                ts._handleToIndex[1], ts._generations[1]);
    TimerHandle forged{1, ts._generations[1]};
    std::printf("伪造句柄 (1,%d) IsHandleValid=%s，GetTimeRemaining=%.1f（读到的是 C 的数据）\n",
                forged.Generation, ts.IsHandleValid(forged) ? "true" : "false", ts.GetTimeRemaining(forged));
    ts.StopTimer(forged);
    std::printf("StopTimer(伪造) 后：A 有效=%s C 有效=%s，但 C 的 dense 下标=%d ≥ _timerCount=%d → C 成了僵尸句柄（不再 Tick）\n",
                ts.IsHandleValid(A) ? "true" : "false", ts.IsHandleValid(C) ? "true" : "false",
                ts._handleToIndex[C.Index], ts._timerCount);
    std::printf("    同时 slot1 被第二次压入 free list（长度=%zu，含重复下标）\n", ts._freeIndices.size());
    TimerHandle D = ts.CreateTimer(99.f, [] {});
    std::printf("    新建 D=(%d,%d) 落在 dense[%d] → 用 C 的句柄查询剩余时间=%.1f（读到的是 D）\n",
                D.Index, D.Generation, ts._handleToIndex[D.Index], ts.GetTimeRemaining(C));
}

// E3：追赶模式 / 普通模式 / 循环计时器在掉帧时的真实触发次数
static void E3_Chase() {
    title("E3 interval 追赶语义 (interval=0.1s, 一帧 dt=0.6s)");
    for (int chase = 1; chase >= 0; --chase) {
        TimerSystem ts;
        int cnt = 0;
        TimerHandle h = ts.CreateTimer(100.f, [] {}, 0.1f, [&] { cnt++; }, 0, false, false, chase == 1);
        ts.Tick(0.6f, 0.6f);
        std::printf("isChase=%-5s 卡顿帧触发 %d 次，intervalElapsed=%.9g", chase ? "true" : "false", cnt,
                    ts.Peek(h)->intervalElapsed);
        int before = cnt;
        ts.Tick(0.016f, 0.016f);
        std::printf("；下一帧(16ms)再触发 %d 次\n", cnt - before);
    }
    {
        TimerSystem ts;
        int cnt = 0;
        TimerHandle h = ts.CreateTimer(100.f, [] {}, 0.1f, [&] { cnt++; }, 0, false, false, true);
        ts.Tick(0.35f, 0.35f);
        std::printf("isChase=true  dt=0.35s 触发 %d 次，余量 intervalElapsed=%.9g（相位被保留）\n", cnt,
                    ts.Peek(h)->intervalElapsed);
    }
    {
        // 理想 6 次的边界：浮点误差决定"防爆清零"是否触发
        TimerSystem ts;
        int cnt = 0;
        TimerHandle h = ts.CreateTimer(100.f, [] {}, 0.1f, [&] { cnt++; }, 0, false, false, true);
        ts.Tick(0.5f, 0.5f);
        std::printf("isChase=true  dt=0.5s 触发 %d 次，余量=%.9g（float 0.5-5*0.1 不精确为 0）\n", cnt,
                    ts.Peek(h)->intervalElapsed);
    }
    {
        TimerSystem ts;
        int done = 0;
        ts.CreateTimer(0.1f, [&] { done++; }, -1.f, nullptr, 0, /*loop*/ true);
        float dts[] = {0.35f, 0.016f, 0.016f, 0.016f, 0.016f, 0.016f};
        std::printf("Loop(duration=0.1) 卡顿 0.35s 后各帧 onComplete 次数：");
        for (float dt : dts) { int b = done; ts.Tick(dt, dt); std::printf("%d ", done - b); }
        std::printf("→ 每帧至多 1 次但不丢弃，欠账分摊到后续帧（与 interval 的封顶+清零语义不一致）\n");
    }
    {
        TimerSystem ts;
        int done = 0;
        ts.CreateTimer(0.f, [&] { done++; }, -1.f, nullptr, 0, true);
        for (int f = 0; f < 5; f++) ts.Tick(0.016f, 0.016f);
        std::printf("Loop(duration=0) 5 帧触发 %d 次 → 每帧必触发，没有参数校验\n", done);
    }
}

// E4：回调重入 —— 回调中停止自己 / 停止同帧到期的另一个 / 停止后立刻新建复用槽位
static void E4_Reentrancy() {
    title("E4 回调重入安全性");
    TimerSystem ts;
    std::vector<std::string> log;
    TimerHandle A, B, C;
    A = ts.CreateTimer(0.1f, [&] {
        log.push_back("A.complete");
        ts.StopTimer(A);                                   // 停自己
        ts.StopTimer(B);                                   // 停同帧已进入 expired 缓存的 B
        C = ts.CreateTimer(1.f, [&] { log.push_back("C.complete"); });   // FIFO free list：复用最早释放的槽
        log.push_back("C=(" + std::to_string(C.Index) + "," + std::to_string(C.Generation) + ")");
    });
    B = ts.CreateTimer(0.1f, [&] { log.push_back("B.complete(不应出现)"); });
    ts.Tick(0.2f, 0.2f);
    std::printf("A=(%d,%d) B=(%d,%d) 回调序列：", A.Index, A.Generation, B.Index, B.Generation);
    for (auto& s : log) std::printf("%s ", s.c_str());
    std::printf("\n→ B 的旧句柄被 generation 拦截；C 复用 slot%d 但代数为 %d，旧句柄无法访问它\n", C.Index, C.Generation);

    // 同帧到期回调顺序 ≠ 创建顺序（Swap-and-Pop 破坏稳定顺序）
    TimerSystem t2;
    std::vector<int> order;
    TimerHandle hs[4];
    for (int i = 0; i < 4; i++) hs[i] = t2.CreateTimer(1.f, [&order, i] { order.push_back(i); });
    TimerHandle tmp = t2.CreateTimer(5.f, [] {});
    t2.StopTimer(hs[0]);                 // dense: [tmp?]... 末尾元素搬到 0 号位
    hs[0] = t2.CreateTimer(1.f, [&order] { order.push_back(0); });
    t2.StopTimer(tmp);
    t2.Tick(1.5f, 1.5f);
    std::printf("创建顺序 1,2,3,0(重建) 的同帧到期回调顺序：");
    for (int v : order) std::printf("%d ", v);
    std::printf("\n");
}

// E5：仓库 Test_GenerationIncrement / Test_IndexReuse 依赖 free list 的残留状态
static void E5_TestOrderDependence() {
    title("E5 测试用例的顺序依赖 (FIFO free list)");
    for (int leftovers = 0; leftovers <= 2; leftovers++) {
        TimerSystem ts;
        std::vector<TimerHandle> tmp;
        for (int i = 0; i < leftovers; i++) tmp.push_back(ts.CreateTimer(1.f, [] {}));
        for (auto& h : tmp) ts.StopTimer(h);           // 先前用例留下 leftovers 个空闲槽
        TimerHandle h1 = ts.CreateTimer(10.f, [] {}, -1.f, nullptr, 77);
        ts.StopTimer(h1);
        TimerHandle h2 = ts.CreateTimer(10.f, [] {}, -1.f, nullptr, 77);
        bool genPass = (h2.Generation == h1.Generation + 1);
        bool idxPass = (h1.Index == h2.Index);
        std::printf("free list 预留 %d 个槽：h1=(%d,%d) h2=(%d,%d) → GenerationIncrement %s, IndexReuse %s\n",
                    leftovers, h1.Index, h1.Generation, h2.Index, h2.Generation,
                    genPass ? "PASS" : "FAIL", idxPass ? "PASS" : "FAIL");
    }
}

// E6：generation 溢出估算（C# int 默认 unchecked，++ 到 int.MaxValue 后回绕为 int.MinValue）
static void E6_GenerationOverflow() {
    title("E6 generation 溢出估算");
    const double wrap = 4294967296.0;   // 2^32 次自增后回到原值
    double perSec[] = {1, 60, 1000};
    for (double r : perSec)
        std::printf("同一 slot 每秒复用 %6.0f 次 → %.3g 天后 generation 回到同一值\n", r, wrap / r / 86400.0);
    int32_t g = INT32_MAX;
    g = (int32_t)((uint32_t)g + 1u);
    std::printf("int.MaxValue + 1 (unchecked) = %d\n", g);
}

// ============================== EventSystem 移植 ==============================
template <class T>
class RingBuffer {
    std::vector<T> _buffer; int _capacity, _head = 0, _tail = 0, _size = 0;
public:
    explicit RingBuffer(int cap) : _buffer(cap), _capacity(cap) {}
    bool TryEnqueue(T e) { if (_size >= _capacity) return false; _buffer[_tail++] = std::move(e); _tail %= _capacity; _size++; return true; }
    bool TryDequeue(T& e) { if (_size == 0) return false; e = std::move(_buffer[_head]); _buffer[_head] = T(); _head = (_head + 1) % _capacity; --_size; return true; }
    int Count() const { return _size; }
};

enum Priority { Logic = 0, Appearance = 1, Max = 2 };
struct Evt { int type; Priority pri; std::string payload; };
using Listener = std::function<void(const Evt&)>;
using Multicast = std::shared_ptr<const std::vector<std::pair<int, Listener>>>;   // 模拟不可变的多播委托

class EventSystem {
public:
    static constexpr int LEVELS = (int)Max + 1;   // 与源码相同：3 级，其中 Max 级永远为空
    int cap;
    std::vector<RingBuffer<std::function<void()>>> front, back;
    std::unordered_map<int, Multicast> dic;
    int dropped = 0;
    explicit EventSystem(int capacity = 2000) : cap(capacity) {
        for (int i = 0; i < LEVELS; i++) { front.emplace_back(cap); back.emplace_back(cap); }
    }
    void Register(int type, int id, Listener l) {
        auto v = dic.count(type) ? *dic[type] : std::vector<std::pair<int, Listener>>{};
        v.push_back({id, l});
        dic[type] = std::make_shared<const std::vector<std::pair<int, Listener>>>(v);   // Delegate.Combine → 新对象
    }
    void UnRegister(int type, int id) {
        if (!dic.count(type)) return;
        auto v = *dic[type];
        v.erase(std::remove_if(v.begin(), v.end(), [&](auto& p) { return p.first == id; }), v.end());
        if (v.empty()) dic.erase(type); else dic[type] = std::make_shared<const std::vector<std::pair<int, Listener>>>(v);
    }
    void Trigger(const Evt& e) {
        auto it = dic.find(e.type);
        if (it == dic.end()) return;
        Multicast callback = it->second;          // 在 Trigger 时刻抓取委托快照（闭包捕获）
        std::function<void()> task = [callback, e] { for (auto& p : *callback) p.second(e); };
        if (!back[e.pri].TryEnqueue(task)) dropped++;   // 源码忽略返回值 → 静默丢弃
    }
    void Update() {                               // 源码没有 try/catch
        std::swap(front, back);
        for (int i = 0; i < LEVELS; ++i) {
            std::function<void()> task;
            while (front[i].TryDequeue(task)) task();
        }
    }
};

static void E7_EventSystem() {
    title("E7 EventSystem 双缓冲 / 溢出 / 快照 / 异常");
    {   // (a) A 回调里触发 B，B 回调里再触发 A：不会递归
        EventSystem es; int depth = 0, maxDepth = 0; std::string seq;
        es.Register(1, 1, [&](const Evt&) { depth++; maxDepth = std::max(maxDepth, depth); seq += "A "; es.Trigger({2, Logic, ""}); depth--; });
        es.Register(2, 1, [&](const Evt&) { depth++; maxDepth = std::max(maxDepth, depth); seq += "B "; es.Trigger({1, Logic, ""}); depth--; });
        es.Trigger({1, Logic, ""});
        for (int f = 1; f <= 5; f++) { seq += "| "; es.Update(); }
        std::printf("(a) A↔B 互相触发，5 帧派发序列：%s 最大调用深度=%d\n", seq.c_str(), maxDepth);
    }
    {   // (b) 单帧 2001 个同优先级事件
        EventSystem es; int got = 0;
        es.Register(3, 1, [&](const Evt&) { got++; });
        for (int i = 0; i < 2001; i++) es.Trigger({3, Logic, ""});
        es.Update();
        std::printf("(b) 单帧 Trigger 2001 次 → 派发 %d 次，静默丢弃 %d 次（无日志、无返回值）\n", got, es.dropped);
    }
    {   // (c) Trigger 之后、派发之前注销监听者
        EventSystem es; int got = 0;
        es.Register(4, 1, [&](const Evt&) { got++; });
        es.Trigger({4, Logic, ""});
        es.UnRegister(4, 1);                       // 例如 UI 在同一帧 OnDisable
        es.Update();
        std::printf("(c) 已注销的监听者在下一帧仍被调用 %d 次（快照语义；若监听者是已销毁的 UI → MissingReferenceException）\n", got);
    }
    {   // (d) 回调抛异常
        EventSystem es; std::string seq;
        es.Register(5, 1, [&](const Evt& e) { seq += e.payload + " "; if (e.payload == "E1") throw std::runtime_error("boom"); });
        es.Trigger({5, Logic, "E1"}); es.Trigger({5, Logic, "E2"}); es.Trigger({5, Logic, "E3"});
        for (int f = 1; f <= 3; f++) {
            if (f == 2) es.Trigger({5, Logic, "E4"});
            seq += "|F" + std::to_string(f) + ": ";
            try { es.Update(); } catch (const std::exception&) { seq += "(异常逃出 Update) "; }
        }
        std::printf("(d) 派发序列 %s→ E2/E3 晚 2 帧且排到 E4 之后（乱序）\n", seq.c_str());
    }
    {   // (e) 优先级：Logic 回调里触发的 Appearance 事件进入下一帧
        EventSystem es; std::string seq;
        es.Register(6, 1, [&](const Evt&) { seq += "Logic "; es.Trigger({7, Appearance, ""}); });
        es.Register(7, 1, [&](const Evt&) { seq += "Appearance "; });
        es.Trigger({7, Appearance, ""});
        es.Trigger({6, Logic, ""});
        for (int f = 1; f <= 2; f++) { seq += "|F" + std::to_string(f) + ": "; es.Update(); }
        std::printf("(e) 先 Trigger Appearance 后 Trigger Logic：%s\n", seq.c_str());
    }
}

// ============================== ObjectPool 移植 ==============================
struct Poolable { int id = 0; bool inUse = false; void OnSpawn() { inUse = true; } void OnRecycle() { inUse = false; } };
struct PoolObject { Poolable* elem = nullptr; int index = 0; };
class ObjectPool {
    struct Elem { Poolable* elem; bool IsAvailable; };
    int _capacity; std::vector<Elem> _pool; std::stack<int> _avail; std::vector<std::unique_ptr<Poolable>> own;
public:
    explicit ObjectPool(int cap) : _capacity(cap), _pool(cap) {
        for (int i = 0; i < cap; i++) { own.emplace_back(new Poolable{i}); _pool[i] = {own.back().get(), true}; _avail.push(i); }
    }
    bool Acquire(PoolObject& obj) {
        if (_avail.empty()) return false;          // 省略扩容分支（与本实验无关）
        int index = _avail.top(); _avail.pop();
        obj = {_pool[index].elem, index}; _pool[index].IsAvailable = false; obj.elem->OnSpawn(); return true;
    }
    bool Recycle(PoolObject& obj) {
        if (obj.index < 0 || obj.index >= _capacity) return false;
        if (_pool[obj.index].IsAvailable) return false;
        obj.elem->OnRecycle(); obj.elem = nullptr;
        _pool[obj.index].IsAvailable = true; _avail.push(obj.index); return true;
    }
};

static void E8_ObjectPool() {
    title("E8 ObjectPool 陈旧 PoolObject 拷贝");
    ObjectPool pool(4);
    PoolObject a; pool.Acquire(a);
    PoolObject aCopy = a;                         // struct 拷贝：例如存进 Dictionary<GameObject, PoolObject>
    bool r1 = pool.Recycle(a);
    PoolObject b; pool.Acquire(b);                // Stack(LIFO) → 立刻拿回同一个槽
    bool r2 = pool.Recycle(aCopy);                // 用旧拷贝再回收一次
    std::printf("a.index=%d b.index=%d；Recycle(a)=%s，随后 Recycle(旧拷贝)=%s → b 手里的对象 inUse=%s（被别人回收了）\n",
                aCopy.index, b.index, r1 ? "true" : "false", r2 ? "true" : "false", b.elem->inUse ? "true" : "false");
    PoolObject def{};                             // default(PoolObject)：index=0, elem=null
    std::printf("default(PoolObject).index=%d → 若 slot0 正在使用，Recycle(default) 会解引用 null elem（C# 中为 NullReferenceException）\n", def.index);
}

// ============================== World 倒序遍历移植 ==============================
static void E9_WorldReverseTick() {
    title("E9 World 倒序遍历 + Tick 中注销更低下标的 Actor");
    std::vector<int> actors = {0, 1, 2, 3, 4};
    std::vector<int> tickCount(5, 0);
    for (int i = (int)actors.size() - 1; i >= 0; i--) {
        int a = actors[i];
        tickCount[a]++;
        auto it = std::find(actors.begin(), actors.end(), 1);
        if (a == 3 && it != actors.end()) {       // Actor3 在 Tick 中 DestroyImmediate(Actor1) → OnDestroy → List.Remove
            actors.erase(it);
        }
    }
    std::printf("各 Actor 本帧 Tick 次数：");
    for (int k = 0; k < 5; k++) std::printf("A%d=%d ", k, tickCount[k]);
    std::printf("\n→ 删除自己/已遍历元素安全；删除更低下标元素会让当前 Actor 被 Tick 两次\n");
}


// E10：句柄不带"属于哪个 TimerSystem"的信息 → 跨场景（跨实例）别名
static void E10_CrossInstance() {
    title("E10 跨场景：旧场景的句柄在新场景的 TimerSystem 上仍然有效");
    TimerSystem sceneA;
    TimerHandle kept = sceneA.CreateTimer(10.f, [] {});        // 被 DontDestroyOnLoad 对象保存下来的句柄
    TimerSystem sceneB;                                        // 切场景：新的 TimerSystem（懒创建）
    TimerHandle fresh = sceneB.CreateTimer(60.f, [] {});
    std::printf("旧句柄=(%d,%d) 新场景第一个计时器=(%d,%d) → sceneB.IsHandleValid(旧)=%s，剩余=%.1f s（读到的是新计时器）\n",
                kept.Index, kept.Generation, fresh.Index, fresh.Generation,
                sceneB.IsHandleValid(kept) ? "true" : "false", sceneB.GetTimeRemaining(kept));
    sceneB.StopTimer(kept);
    std::printf("sceneB.StopTimer(旧句柄) 后新计时器存活=%s\n", sceneB.IsHandleValid(fresh) ? "true" : "false");
}

// E11：Time.timeScale = 0（暂停菜单）时 scaled dt = 0
static void E11_Paused() {
    title("E11 timeScale=0 时（scaled dt=0）");
    TimerSystem ts;
    int loop0 = 0, normal = 0, iv = 0;
    ts.CreateTimer(0.f, [&] { loop0++; }, -1.f, nullptr, 0, true);
    TimerHandle n = ts.CreateTimer(5.f, [&] { normal++; });
    ts.CreateTimer(100.f, [] {}, 0.1f, [&] { iv++; });
    for (int f = 0; f < 10; f++) ts.Tick(0.f, 0.016f);
    std::printf("暂停 10 帧：duration=0 的循环计时器触发 %d 次；5s 计时器剩余 %.2f s；interval 计时器触发 %d 次\n",
                loop0, ts.GetTimeRemaining(n), iv);
}

// E12：Unity 的 Time.deltaTime 被 Time.maximumDeltaTime(默认 0.3333333) 截断，unscaledDeltaTime 不截断
static void E12_MaxDeltaTime() {
    title("E12 Unity 帧时长上限对 interval / loop 的影响");
    const float maxDt = 0.3333333f;
    float hitches[] = {0.6f, 2.0f};
    for (float real : hitches) {
        for (int unscaled = 0; unscaled <= 1; unscaled++) {
            TimerSystem ts; int cnt = 0;
            TimerHandle h = ts.CreateTimer(1000.f, [] {}, 0.1f, [&] { cnt++; }, 0, false, unscaled == 1, true);
            float scaledDt = std::min(real, maxDt);
            ts.Tick(scaledDt, real);
            std::printf("真实卡顿 %.1fs，%s 计时器(interval=0.1, chase)：本帧 dt=%.7g 触发 %d 次，余量=%.7g\n",
                        real, unscaled ? "unscaled" : "scaled  ", unscaled ? real : scaledDt, cnt, ts.Peek(h)->intervalElapsed);
        }
    }
    TimerSystem ts; int done = 0;
    ts.CreateTimer(0.1f, [&] { done++; }, -1.f, nullptr, 0, true, /*unscaled*/ true);
    std::printf("unscaled Loop(duration=0.1)：卡顿帧(2s) + 之后 30 帧(16ms) 每帧触发次数：");
    double tsum = 0; int b = done;
    ts.Tick(0.016f, 2.0f); tsum += 2.0; std::printf("[%d] ", done - b);
    int lag = 0;
    for (int f = 0; f < 30; f++) { b = done; ts.Tick(0.016f, 0.016f); tsum += 0.016; std::printf("%d", done - b); if (done - b) lag = f + 1; }
    std::printf("\n→ 总时长 %.3fs，理想触发 %d 次，实际 %d 次；卡顿后连续约 %d 帧每帧补 1 次（每帧至多 1 次，欠账分摊）\n",
                tsum, (int)(tsum / 0.1), done, lag);
}


// E13：StopTimer 只做 _timerCount--，不清空 dense 尾部 → 已停止计时器的委托（及其捕获对象）仍被数组引用
static void E13_TailRetention() {
    title("E13 dense 尾部残留引用（托管层的'泄漏'）");
    TimerSystem ts;
    auto pawn = std::make_shared<int>(42);                    // 代表被回调捕获的 CombatPawn / 闭包对象
    std::weak_ptr<int> alive = pawn;
    TimerHandle a = ts.CreateTimer(5.f, [] {});
    TimerHandle b = ts.CreateTimer(5.f, [p = pawn] { (void)p; });   // 回调捕获 pawn
    pawn.reset();                                              // 业务侧已经不再持有
    ts.StopTimer(b);                                           // b 在末尾：只 _timerCount--
    std::printf("StopTimer(b) 后：_timerCount=%d，被捕获对象仍存活=%s（引用残留在 _timers[1]）\n",
                ts._timerCount, alive.expired() ? "false" : "true");
    TimerHandle c = ts.CreateTimer(5.f, [] {});                // 新计时器覆盖 _timers[1]
    std::printf("新建一个计时器覆盖该槽后：被捕获对象仍存活=%s\n", alive.expired() ? "false" : "true");
    (void)a; (void)c;
}


// E14：timeElapsed 是 float 累加 → 超长计时器（数天）在大数值处精度崩溃
static void E14_FloatAccumulation() {
    title("E14 float 累加 timeElapsed：已走时间很大时，每帧 +0.016 会被舍入");
    float starts[] = {3600.f, 86400.f, 262144.f, 524288.f};
    for (float t0 : starts) {
        float t = t0;
        for (int k = 0; k < 600; k++) t += 0.016f;           // 600 帧 ≈ 9.6 s
        std::printf("已走 %8.0f s（%.2f 天）后再走 600 帧：理想 +9.600 s，实际 +%.4f s\n", t0, t0 / 86400.f, t - t0);
    }
}

int main() {
    setvbuf(stdout, nullptr, _IONBF, 0);
    E1_DefaultHandleAlias();
    E2_ForgedHandle();
    E3_Chase();
    E4_Reentrancy();
    E5_TestOrderDependence();
    E6_GenerationOverflow();
    E7_EventSystem();
    E8_ObjectPool();
    E9_WorldReverseTick();
    E10_CrossInstance();
    E11_Paused();
    E12_MaxDeltaTime();
    E13_TailRetention();
    E14_FloatAccumulation();
    return 0;
}
