// timer_bench.cpp —— 回答面试追问「数组一定比链表/对象列表快吗？」「Swap-and-Pop 比 RemoveAt 快多少？」
// 这是 C++ 的【扩展知识】实验，不代表 C#/Mono 的绝对数值；只看数量级与趋势。
// B1：每帧 Tick 遍历的单元素耗时：dense 结构体数组 vs 按序分配的指针数组 vs 打乱分配顺序的指针数组（pointer chasing）
// B2：从中间删除：保序的 erase(=List.RemoveAt) vs swap-and-pop
#include <cstdio>
#include <vector>
#include <memory>
#include <random>
#include <chrono>
#include <algorithm>
#include <numeric>

struct TimerData {                // 与 C# TimerData 字段布局对应：4 个 float + 2 个引用 + 4 个 bool + int
    float duration, timeElapsed, interval, intervalElapsed;
    void* onTrigger; void* onComplete;
    bool isLoop, isPaused, isUnScaled, isChase;
    int timerId;
};

using Clock = std::chrono::steady_clock;
static volatile int g_sink;

template <class F> static double best_ns(F&& f, int reps) {
    double best = 1e300;
    for (int r = 0; r < reps; r++) {
        auto t0 = Clock::now(); f(); auto t1 = Clock::now();
        best = std::min(best, std::chrono::duration<double, std::nano>(t1 - t0).count());
    }
    return best;
}

static int tick(TimerData& t, float dt) {
    if (t.isPaused) return 0;
    t.timeElapsed += dt;
    return t.timeElapsed >= t.duration;
}

static void B1(size_t N) {
    std::mt19937 rng(42);
    std::vector<TimerData> dense(N);
    for (size_t i = 0; i < N; i++) dense[i] = {1e9f, 0, -1, 0, nullptr, nullptr, false, false, false, true, 0};
    // 按序分配：堆上节点大致连续
    std::vector<std::unique_ptr<TimerData>> seq(N);
    for (size_t i = 0; i < N; i++) seq[i] = std::make_unique<TimerData>(dense[i]);
    // 打乱：先分配一批再随机打乱访问顺序，模拟对象散落在堆里（GC 堆 / 长期运行后的碎片化）
    std::vector<std::unique_ptr<TimerData>> pool(N);
    for (size_t i = 0; i < N; i++) pool[i] = std::make_unique<TimerData>(dense[i]);
    std::vector<TimerData*> shuffled(N);
    for (size_t i = 0; i < N; i++) shuffled[i] = pool[i].get();
    std::shuffle(shuffled.begin(), shuffled.end(), rng);

    int reps = N >= 1000000 ? 7 : 31;
    int inner = (int)std::max<size_t>(1, 2000000 / N);
    double a = best_ns([&] { int s = 0; for (int k = 0; k < inner; k++) for (auto& t : dense) s += tick(t, 0.016f); g_sink = s; }, reps) / (double(N) * inner);
    double b = best_ns([&] { int s = 0; for (int k = 0; k < inner; k++) for (auto& p : seq) s += tick(*p, 0.016f); g_sink = s; }, reps) / (double(N) * inner);
    double c = best_ns([&] { int s = 0; for (int k = 0; k < inner; k++) for (auto* p : shuffled) s += tick(*p, 0.016f); g_sink = s; }, reps) / (double(N) * inner);
    double kb = N * sizeof(TimerData) / 1024.0;
    std::printf("N=%8zu (dense %8.0f KB)  dense=%6.2f ns  指针·按序=%6.2f ns  指针·打乱=%6.2f ns  打乱/dense=%5.1fx\n",
                N, kb, a, b, c, c / a);
}

static void B2(size_t N) {
    std::mt19937 rng(7);
    const int K = 1000;     // 每轮删除 K 个随机位置的元素再补回，保持规模 N
    std::vector<TimerData> v(N);
    double erase_ns = best_ns([&] {
        for (int k = 0; k < K; k++) {
            size_t i = rng() % v.size();
            v.erase(v.begin() + i);              // 保序：后面元素整体前移（List<T>.RemoveAt 同理）
            v.push_back(TimerData{});
        }
    }, 5) / K;
    std::vector<TimerData> w(N);
    double swap_ns = best_ns([&] {
        for (int k = 0; k < K; k++) {
            size_t i = rng() % w.size();
            w[i] = w.back(); w.pop_back();       // 不保序：末尾元素填洞
            w.push_back(TimerData{});
        }
    }, 5) / K;
    std::printf("N=%8zu  erase(保序)=%10.1f ns/次  swap-and-pop=%6.1f ns/次  比值=%8.0fx\n", N, erase_ns, swap_ns, erase_ns / swap_ns);
}

int main() {
    setvbuf(stdout, nullptr, _IONBF, 0);
    std::printf("sizeof(TimerData)=%zu 字节（C# 版本字段相同，含 2 个对象引用）\n", sizeof(TimerData));
    std::printf("\n== B1 每帧遍历 Tick，单元素耗时（取多轮最小值） ==\n");
    for (size_t N : {100, 1000, 10000, 100000, 1000000}) B1(N);
    std::printf("\n== B2 随机位置删除一个计时器的耗时 ==\n");
    for (size_t N : {100, 1000, 10000, 100000}) B2(N);
    return 0;
}
