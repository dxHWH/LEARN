// infra_verify.cpp —— 第三批（第 7、8、11、12、13 章）实测
// 把 MyGamePlayArchitecture@427d7c1 的 EventSystem.cs / RingBuffer.cs / ThreadSafeRingBuffer.cs /
// ObjectPool.cs / Log.cs 的控制流逐行移植为 C++，验证纯逻辑结论；并补充若干【扩展知识】基准
// （取模 vs 位与、锁 vs 无锁、false sharing、逐行写文件 vs 缓冲写），基准只看数量级与趋势。
//
// 用法：infra_verify.exe            全部实验
//       infra_verify.exe G3 G6      只跑指定实验（计时类实验全量重跑数字会变，书中引用以本文件输出为准）
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <functional>
#include <memory>
#include <mutex>
#include <stack>
#include <stdexcept>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

static void title(const char* s) { std::printf("\n==================== %s ====================\n", s); }
using Clock = std::chrono::steady_clock;
static double msSince(Clock::time_point t0) { return std::chrono::duration<double, std::milli>(Clock::now() - t0).count(); }

// ------------------------------------------------------------------ RingBuffer.cs 移植（逐行）
// isValueType 参数模拟 C# 的 typeof(T).IsValueType：为 true 时出队不清槽位
template <class T, bool isValueType>
class RingBuffer {
    std::vector<T> _buffer; int _capacity, _head = 0, _tail = 0, _size = 0;
public:
    explicit RingBuffer(int capacity) : _buffer(capacity), _capacity(capacity) {
        if (capacity <= 0) throw std::invalid_argument("Capacity must be greater than 0");
    }
    bool TryEnqueue(const T& e) {
        if (_size >= _capacity) return false;
        _buffer[_tail++] = e; _tail %= _capacity; _size++; return true;
    }
    bool TryDequeue(T& e) {
        if (_size == 0) { e = T(); return false; }
        e = _buffer[_head];
        if (!isValueType) _buffer[_head] = T();
        _head = (_head + 1) % _capacity; --_size; return true;
    }
    int Count() const { return _size; }
    int Head() const { return _head; }
    int Tail() const { return _tail; }
};

// ------------------------------------------------------------------ EventSystem.cs 移植（按优先级的双缓冲）
enum Priority { Logic = 0, Appearance = 1, Max = 2 };
struct Evt { int type; Priority pri; int payload; };
using Listener = std::function<void(const Evt&)>;
struct Multicast { std::vector<std::pair<int, Listener>> list; };   // Delegate.Combine 产生的不可变调用列表
using MulticastPtr = std::shared_ptr<const Multicast>;

class EventSystem {
public:
    static constexpr int LEVELS = (int)Max + 1;
    std::vector<RingBuffer<std::function<void()>, false>> front, back;
    std::unordered_map<int, MulticastPtr> dic;
    int dropped = 0;
    explicit EventSystem(int cap = 2000) {
        for (int i = 0; i < LEVELS; i++) { front.emplace_back(cap); back.emplace_back(cap); }
    }
    void Register(int type, int id, Listener l) {
        Multicast m = dic.count(type) ? *dic[type] : Multicast{};
        m.list.push_back({id, l});
        dic[type] = std::make_shared<const Multicast>(m);
    }
    void UnRegister(int type, int id) {
        if (!dic.count(type)) return;
        Multicast m = *dic[type];
        m.list.erase(std::remove_if(m.list.begin(), m.list.end(), [&](auto& p) { return p.first == id; }), m.list.end());
        if (m.list.empty()) dic.erase(type); else dic[type] = std::make_shared<const Multicast>(m);
    }
    void Trigger(const Evt& e) {
        auto it = dic.find(e.type);
        if (it == dic.end()) return;
        MulticastPtr cb = it->second;
        std::function<void()> task = [cb, e] { for (auto& p : cb->list) p.second(e); };   // 多播：按注册顺序，异常即中断
        if (!back[e.pri].TryEnqueue(task)) dropped++;
    }
    void Update() {
        std::swap(front, back);
        for (int i = 0; i < LEVELS; ++i) { std::function<void()> t; while (front[i].TryDequeue(t)) t(); }
    }
};

// G1：多播委托中某个监听者抛异常 —— 同一事件的后续监听者也收不到
static void G1_MulticastException() {
    title("G1 多播委托：一个监听者抛异常，同一事件后面的监听者也收不到");
    EventSystem es; std::string seq;
    es.Register(1, 1, [&](const Evt&) { seq += "UI分数 "; });
    es.Register(1, 2, [&](const Evt&) { seq += "成就系统(抛异常) "; throw std::runtime_error("x"); });
    es.Register(1, 3, [&](const Evt&) { seq += "音效 "; });
    es.Register(2, 1, [&](const Evt&) { seq += "[另一事件的监听者] "; });
    es.Trigger({1, Logic, 0}); es.Trigger({2, Logic, 0});
    try { es.Update(); } catch (...) { seq += "→ 异常逃出 Update "; }
    std::printf("第 1 帧：%s\n", seq.c_str());
    for (int f = 2; f <= 3; f++) {
        seq.clear(); es.Update();
        std::printf("第 %d 帧：%s\n", f, seq.empty() ? "（无派发：被打断时剩下的事件还留在旧 front，这一帧 swap 后它变成了 back）" : seq.c_str());
    }
    std::printf("→ “音效”永远收不到这一次事件；同一帧排在后面的另一个事件晚了 2 帧\n");
}

// G2：事件链延迟 —— 与 World.Update 的相对顺序决定 0 还是 1 帧
static void G2_Latency() {
    title("G2 事件延迟：Trigger 在 World.Update 中发出，A→B→C 三级事件链各在第几帧被处理");
    for (int order = 0; order < 2; order++) {
        EventSystem es; int frame = 0; int fa = -1, fb = -1, fc = -1;
        es.Register(1, 1, [&](const Evt&) { fa = frame; es.Trigger({2, Logic, 0}); });
        es.Register(2, 1, [&](const Evt&) { fb = frame; es.Trigger({3, Appearance, 0}); });
        es.Register(3, 1, [&](const Evt&) { fc = frame; });
        for (frame = 0; frame < 6; frame++) {
            if (order == 0) { if (frame == 0) es.Trigger({1, Logic, 0}); es.Update(); }      // World 先于 EventSystem
            else { es.Update(); if (frame == 0) es.Trigger({1, Logic, 0}); }                 // EventSystem 先于 World
        }
        std::printf("%s：A 在第 %d 帧，B 在第 %d 帧，C 在第 %d 帧（Trigger 发生在第 0 帧）\n",
                    order == 0 ? "World.Update 先执行      " : "EventSystem.Update 先执行", fa, fb, fc);
    }
    std::printf("→ 每多一级“回调里再发事件”，就多 1 帧；起点延迟 0 或 1 帧取决于未定义的脚本执行顺序\n");
}

// G3：优先级 —— 同批次内 Logic 先于 Appearance；不会饿死；各级容量独立
static void G3_Priority() {
    title("G3 优先级：Logic 洪水下 Appearance 是否被饿死、各级容量是否独立");
    EventSystem es; int logic = 0, app = 0; std::vector<char> order;
    es.Register(1, 1, [&](const Evt&) { logic++; if (order.size() < 4) order.push_back('L'); });
    es.Register(2, 1, [&](const Evt&) { app++; if (order.size() < 4) order.push_back('A'); });
    for (int k = 0; k < 2500; k++) es.Trigger({1, Logic, 0});
    for (int k = 0; k < 10; k++) es.Trigger({2, Appearance, 0});
    es.Update();
    std::printf("单帧 Trigger Logic×2500 + Appearance×10：派发 Logic %d、Appearance %d，丢弃 %d（只丢 Logic 级的溢出）\n",
                logic, app, es.dropped);
    std::printf("派发顺序前 4 个：%c %c %c %c；Max 级队列长度 %d（分配了 2×2000 槽但永远为空）\n",
                order[0], order[1], order[2], order[3], es.front[Max].Count());
}

// G4：RingBuffer 正确性 —— 回绕、满/空、head==tail 的歧义
static void G4_RingBasics() {
    title("G4 RingBuffer：回绕与满/空判断（capacity=3）");
    RingBuffer<int, true> rb(3); int v;
    auto show = [&](const char* op) { std::printf("%-22s head=%d tail=%d size=%d\n", op, rb.Head(), rb.Tail(), rb.Count()); };
    show("初始");
    rb.TryEnqueue(1); rb.TryEnqueue(2); rb.TryEnqueue(3); show("入队 1,2,3（满）");
    bool ok = rb.TryEnqueue(4); std::printf("再入队 4 → %s\n", ok ? "true" : "false（满）");
    rb.TryDequeue(v); show("出队 → 1");
    rb.TryEnqueue(4); show("入队 4（tail 回绕到 0）");
    std::string s; while (rb.TryDequeue(v)) s += std::to_string(v) + " "; show(("全部出队 → " + s).c_str());
    std::printf("→ “满”和“空”时都是 head==tail，靠 size 区分；若不存 size，就要浪费一个槽或用单调递增计数\n");
}

// G5：值类型里含引用 —— IsValueType 为 true 时不清槽，被包含的对象无法释放
struct EventEnvelope { int typeId; std::shared_ptr<int> payload; };
static void G5_RetainInStruct() {
    title("G5 出队不清槽：T 是“含引用的结构体”时，payload 被缓冲区钉住");
    {
        RingBuffer<EventEnvelope, true> rb(8);
        auto obj = std::make_shared<int>(7); std::weak_ptr<int> w = obj;
        rb.TryEnqueue({1, obj}); obj.reset();
        EventEnvelope e; rb.TryDequeue(e); e = {};
        std::printf("T=struct{int, ref}，按源码 IsValueType=true 不清槽：出队并丢弃后 payload 仍存活=%s\n", w.expired() ? "false" : "true");
    }
    {
        RingBuffer<EventEnvelope, false> rb(8);
        auto obj = std::make_shared<int>(7); std::weak_ptr<int> w = obj;
        rb.TryEnqueue({1, obj}); obj.reset();
        EventEnvelope e; rb.TryDequeue(e); e = {};
        std::printf("若改为“含引用就清槽”：出队并丢弃后 payload 仍存活=%s\n", w.expired() ? "false" : "true");
    }
}

// G6：取模 vs 位与（容量为 2 的幂）
static volatile int g_sink;
static void G6_ModVsMask() {
    title("G6 下标回绕：% capacity vs & (capacity-1)（每次入队+出队，ns/次）");
    const int N = 50'000'000;
    for (int cap : {2000, 2048}) {
        std::vector<int> buf(cap); int head = 0, tail = 0; volatile int capv = cap;
        int c = capv;
        auto t0 = Clock::now();
        for (int i = 0; i < N; i++) { buf[tail] = i; tail = (tail + 1) % c; g_sink = buf[head]; head = (head + 1) % c; }
        double mod = msSince(t0) * 1e6 / N;
        double mask = -1;
        if ((cap & (cap - 1)) == 0) {
            int m = c - 1; head = tail = 0;
            t0 = Clock::now();
            for (int i = 0; i < N; i++) { buf[tail] = i; tail = (tail + 1) & m; g_sink = buf[head]; head = (head + 1) & m; }
            mask = msSince(t0) * 1e6 / N;
        }
        if (mask > 0) std::printf("capacity=%d  取模 %.2f ns  位与 %.2f ns\n", cap, mod, mask);
        else std::printf("capacity=%d  取模 %.2f ns  （不是 2 的幂，不能用位与）\n", cap, mod);
    }
    std::printf("→ 容量是运行期变量时编译器无法把 %% 优化掉；差距在纳秒级，对每帧几十个事件无意义\n");
}

// ------------------------------------------------------------------ ThreadSafeRingBuffer.cs 移植 + 无锁 SPSC
template <class T>
class LockedRing {
    std::vector<T> _buffer; int _capacity, _head = 0, _tail = 0, _size = 0; std::mutex _lock;
public:
    explicit LockedRing(int cap) : _buffer(cap), _capacity(cap) {}
    bool TryEnqueue(const T& e) { std::lock_guard<std::mutex> g(_lock); if (_size >= _capacity) return false; _buffer[_tail++] = e; _tail %= _capacity; _size++; return true; }
    bool TryDequeue(T& e) { std::lock_guard<std::mutex> g(_lock); if (_size == 0) return false; e = _buffer[_head]; _head = (_head + 1) % _capacity; --_size; return true; }
};
template <class T>
class SpscRing {                      // 单生产者单消费者：head 只由消费者写，tail 只由生产者写
    std::vector<T> buf; size_t mask;
    alignas(64) std::atomic<size_t> head{0};
    alignas(64) std::atomic<size_t> tail{0};
public:
    explicit SpscRing(size_t capPow2) : buf(capPow2), mask(capPow2 - 1) {}
    bool TryEnqueue(const T& e) {
        size_t t = tail.load(std::memory_order_relaxed);
        if (t - head.load(std::memory_order_acquire) == buf.size()) return false;
        buf[t & mask] = e; tail.store(t + 1, std::memory_order_release); return true;
    }
    bool TryDequeue(T& e) {
        size_t h = head.load(std::memory_order_relaxed);
        if (h == tail.load(std::memory_order_acquire)) return false;
        e = buf[h & mask]; head.store(h + 1, std::memory_order_release); return true;
    }
};

template <class Q>
static double runQueue(Q& q, int producers, int perProducer, bool& fifoOk, long long& sum) {
    std::atomic<bool> go{false};
    std::vector<std::thread> ps;
    auto t0 = Clock::now();
    for (int p = 0; p < producers; p++)
        ps.emplace_back([&, p] {
            while (!go.load()) {}
            for (int i = 0; i < perProducer; i++) {
                int v = p * perProducer + i;
                while (!q.TryEnqueue(v)) std::this_thread::yield();     // 源码测试里是 Thread.Sleep(0)
            }
        });
    std::vector<int> last(producers, -1); fifoOk = true; sum = 0;
    std::thread consumer([&] {
        while (!go.load()) {}
        int got = 0, total = producers * perProducer, v;
        while (got < total) {
            if (q.TryDequeue(v)) {
                int p = v / perProducer, i = v % perProducer;
                if (i <= last[p]) fifoOk = false;                       // 同一生产者内必须递增
                last[p] = i; sum += v; got++;
            } else std::this_thread::yield();
        }
    });
    t0 = Clock::now(); go = true;
    for (auto& t : ps) t.join();
    consumer.join();
    return msSince(t0);
}

static void G7_Locks() {
    title("G7 线程安全队列：lock 版（源码移植）vs 无锁 SPSC，传递 400 万个 int（容量 1024）");
    const int TOTAL = 4'000'000;
    long long expect = (long long)TOTAL * (TOTAL - 1) / 2;
    for (int P : {1, 4}) {
        LockedRing<int> q(1024); bool fifo; long long sum;
        double ms = runQueue(q, P, TOTAL / P, fifo, sum);
        std::printf("lock 版  %d 生产者 1 消费者：%7.1f ms（%5.1f M/s） 每个生产者内 FIFO=%s 校验和=%s\n",
                    P, ms, TOTAL / ms / 1000, fifo ? "OK" : "FAIL", sum == expect ? "OK" : "FAIL");
    }
    {
        SpscRing<int> q(1024); bool fifo; long long sum;
        double ms = runQueue(q, 1, TOTAL, fifo, sum);
        std::printf("无锁 SPSC 1 生产者 1 消费者：%7.1f ms（%5.1f M/s） FIFO=%s 校验和=%s\n",
                    ms, TOTAL / ms / 1000, fifo ? "OK" : "FAIL", sum == expect ? "OK" : "FAIL");
    }
    std::printf("→ 源码 RingBufferStressTest 只计数且入队值恒为 1，验证不了 FIFO；这里给每个值编号后逐个校验\n");
}

// G8：false sharing
struct Packed { std::atomic<long long> a{0}; std::atomic<long long> b{0}; };
struct Padded { alignas(64) std::atomic<long long> a{0}; alignas(64) std::atomic<long long> b{0}; };
template <class S> static double runFS() {
    S s; const int N = 50'000'000;
    auto t0 = Clock::now();
    std::thread t1([&] { for (int i = 0; i < N; i++) s.a.fetch_add(1, std::memory_order_relaxed); });
    std::thread t2([&] { for (int i = 0; i < N; i++) s.b.fetch_add(1, std::memory_order_relaxed); });
    t1.join(); t2.join();
    return msSince(t0);
}
static void G8_FalseSharing() {
    title("G8 false sharing：两个线程各自累加自己的计数器 5000 万次");
    double p = runFS<Packed>(), q = runFS<Padded>();
    std::printf("两个计数器相邻（同一 cache line）：%.0f ms\n", p);
    std::printf("各自对齐到 64 字节：            %.0f ms  → 慢 %.1f 倍只因为共享了 cache line\n", q, p / q);
}

// ------------------------------------------------------------------ ObjectPool.cs 移植（含 ReAllocate）
struct Poolable { int id; bool inUse = false; void OnSpawn() { inUse = true; } void OnRecycle() { inUse = false; } };
struct PoolObject { Poolable* elem = nullptr; int index = 0; };
class ObjectPool {
    struct Elem { Poolable* elem; bool IsAvailable; };
    int _capacity; std::vector<Elem> _pool; std::stack<int> _avail; std::vector<std::unique_ptr<Poolable>> own; int nextId = 0;
    Poolable* make() { own.emplace_back(new Poolable{nextId++}); return own.back().get(); }
public:
    int created() const { return nextId; }
    explicit ObjectPool(int cap) : _capacity(cap), _pool(cap) { for (int i = 0; i < cap; i++) { _pool[i] = {make(), true}; _avail.push(i); } }
    bool ReAllocate(int newCapacity) {
        std::vector<Elem> np(newCapacity);
        for (int i = 0; i < _capacity; i++) np[i] = _pool[i];
        for (int i = _capacity; i < newCapacity; i++) { np[i] = {make(), true}; _avail.push(i); }
        _capacity = newCapacity; _pool = np; return true;
    }
    bool Acquire(PoolObject& obj) {
        if (!_avail.empty()) { int i = _avail.top(); _avail.pop(); obj = {_pool[i].elem, i}; _pool[i].IsAvailable = false; obj.elem->OnSpawn(); return true; }
        if (ReAllocate(_capacity * 2) && !_avail.empty()) { int i = _avail.top(); _avail.pop(); obj = {_pool[i].elem, i}; _pool[i].IsAvailable = false; obj.elem->OnSpawn(); return true; }
        obj = {}; return false;
    }
    bool Recycle(PoolObject& obj) {
        if (obj.index < 0 || obj.index >= _capacity) return false;
        if (_pool[obj.index].IsAvailable) return false;
        obj.elem->OnRecycle(); obj.elem = nullptr; _pool[obj.index].IsAvailable = true; _avail.push(obj.index); return true;
    }
    Poolable* at(int i) { return _pool[i].elem; }
};

static void G9_Pool() {
    title("G9 ObjectPool：LIFO 复用、扩容、容量 0、错配回收");
    {
        ObjectPool pool(4); PoolObject a, b, c;
        pool.Acquire(a); pool.Acquire(b); int ai = a.index; pool.Recycle(a); a.index = ai; pool.Acquire(c);
        std::printf("容量 4：先取 a=%d b=%d，归还 a 后再取 c=%d → Stack 让刚归还的槽立刻被复用（LIFO）\n", a.index, b.index, c.index);
    }
    {
        ObjectPool pool(2); PoolObject o[5]; std::string s;
        for (int i = 0; i < 5; i++) { pool.Acquire(o[i]); s += std::to_string(o[i].index) + " "; }
        std::printf("容量 2 连取 5 个：槽位依次 %s，共 new 了 %d 个对象（2→4→8 两次扩容，每次预先 new 满新容量）\n", s.c_str(), pool.created());
    }
    {
        ObjectPool pool(0); PoolObject o; bool ok = pool.Acquire(o);
        std::printf("容量 0：Acquire=%s（0×2=0，ReAllocate 返回 true 却什么也没分配，永远取不到）\n", ok ? "true" : "false");
    }
    {
        ObjectPool pool(4); PoolObject a, b; pool.Acquire(a); pool.Acquire(b);
        PoolObject forged{a.elem, b.index};                       // elem 和 index 来自不同的租借
        bool r = pool.Recycle(forged);
        std::printf("用 {a 的对象, b 的槽位} 回收：Recycle=%s → a 的对象被 OnRecycle（inUse=%s），b 的槽位被标记可用但 b 仍在用（inUse=%s）\n",
                    r ? "true" : "false", a.elem->inUse ? "true" : "false", b.elem->inUse ? "true" : "false");
        PoolObject c; pool.Acquire(c);
        std::printf("    再 Acquire 一次拿到槽位 %d，它的对象就是 b 正在用的那个（同一对象被两人持有=%s）\n", c.index, c.elem == b.elem ? "true" : "false");
    }
}

// ------------------------------------------------------------------ Log：逐行写文件 vs 缓冲写
static void G10_LogIO() {
    title("G10 日志落盘（Windows 11 + NTFS，含杀毒实时扫描，数字只看量级）：每行打开-追加-关闭 vs 攒满 2048 字符再写（10000 行，每行约 60 字符）");
    const char* f1 = "_log_line.txt"; const char* f2 = "_log_buf.txt";
    std::remove(f1); std::remove(f2);
    char line[128];
    auto t0 = Clock::now();
    for (int i = 0; i < 10000; i++) {
        std::snprintf(line, sizeof line, "[12:00:00.000][T:1][Debug] Thread[3] Log Index:%05d .......\n", i);
        FILE* f = std::fopen(f1, "ab"); std::fputs(line, f); std::fclose(f);      // File.AppendAllText 每次打开关闭
    }
    double perLine = msSince(t0);
    std::string buffer; int flushes = 0; size_t maxLoss = 0;
    t0 = Clock::now();
    for (int i = 0; i < 10000; i++) {
        std::snprintf(line, sizeof line, "[12:00:00.000][T:1][Debug] Thread[3] Log Index:%05d .......\n", i);
        buffer += line;
        if (buffer.size() >= 2048) { FILE* f = std::fopen(f2, "ab"); std::fputs(buffer.c_str(), f); std::fclose(f); flushes++; maxLoss = std::max(maxLoss, buffer.size()); buffer.clear(); }
    }
    double buffered = msSince(t0);
    size_t lineLen = std::strlen(line);
    std::printf("逐行：%.1f ms（每行 %.1f µs）  缓冲：%.1f ms，共写盘 %d 次\n", perLine, perLine * 1000 / 10000, buffered, flushes);
    std::printf("一行约 %zu 字节 → 崩溃时最多丢失缓冲区里约 %zu 行（Log.E 也不会触发立即写盘）\n", lineLen, 2048 / lineLen);
    std::remove(f1); std::remove(f2);
}

int main(int argc, char** argv) {
    setvbuf(stdout, nullptr, _IONBF, 0);
    struct { const char* id; void (*fn)(); } all[] = {
        {"G1", G1_MulticastException}, {"G2", G2_Latency}, {"G3", G3_Priority}, {"G4", G4_RingBasics}, {"G5", G5_RetainInStruct},
        {"G6", G6_ModVsMask}, {"G7", G7_Locks}, {"G8", G8_FalseSharing}, {"G9", G9_Pool}, {"G10", G10_LogIO}};
    for (auto& e : all) {
        bool run = argc == 1;
        for (int i = 1; i < argc; i++) if (std::strcmp(argv[i], e.id) == 0) run = true;
        if (run) e.fn();
    }
    return 0;
}
