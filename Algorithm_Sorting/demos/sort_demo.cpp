// sort_demo.cpp —— 《排序算法（游戏客户端面试向）》配套实测
// 编译（MSVC）：cl /nologo /std:c++latest /EHsc /O2 /utf-8 sort_demo.cpp
// 编译（GCC） ：g++ -std=c++20 -O2 sort_demo.cpp   （E4 栈溢出捕获使用 Windows SEH，非 Windows 下自动跳过）
//
// 实验清单
//   E1 正确性 + 稳定性校验（每个算法 × 5 种数据分布）
//   E2 操作计数：N=1000 时比较次数 / 写次数，对照理论值
//   E3 O(n^2) 组计时：N=20000
//   E4 朴素快排在有序输入上的退化：递归深度 + 64KB 小栈线程栈溢出
//   E5 O(n log n) 组 + 非比较排序计时：N=1,000,000
//   E6 稳定性的可见后果：同层 UI 节点被 std::sort 打乱
//   E7 比较器不满足严格弱序（用 <=）：libstdc++ 式无哨兵插入越界写 / MSVC std::sort 表现
//   E8 游戏实战 A：渲染队列 64 位 SortKey —— 排大结构体 vs 排 (key,idx) vs 基数排序；帧间增量排序
//   E9 游戏实战 B：排行榜 Top-K —— 全排序 / partial_sort / nth_element / 小顶堆
//   E10 游戏实战 C：帧同步确定性 —— 平局顺序依赖算法实现，加 tie-break 后全部一致
#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <numeric>
#include <queue>
#include <random>
#include <string>
#include <vector>
#ifdef _WIN32
#define NOMINMAX
#include <windows.h>
#endif

using u32 = uint32_t;
using u64 = uint64_t;

// ---------------------------------------------------------------- 计数器
struct NoCnt { void c() {} void w(u64 = 1) {} };
struct Cnt { u64 cmp = 0, wr = 0; void c() { ++cmp; } void w(u64 k = 1) { wr += k; } };

static int g_depth = 0, g_maxDepth = 0;

// ================================================================ 1. 冒泡（带提前退出）
template <class T, class L, class C>
void bubble_sort(T* a, int n, L less, C& st) {
    for (int end = n - 1; end > 0; --end) {
        bool swapped = false;
        for (int j = 0; j < end; ++j) {
            st.c();
            if (less(a[j + 1], a[j])) { std::swap(a[j], a[j + 1]); st.w(2); swapped = true; }
        }
        if (!swapped) break;  // 一趟没交换 => 已有序，最好 O(n)
    }
}

// ================================================================ 2. 选择
template <class T, class L, class C>
void selection_sort(T* a, int n, L less, C& st) {
    for (int i = 0; i < n - 1; ++i) {
        int m = i;
        for (int j = i + 1; j < n; ++j) { st.c(); if (less(a[j], a[m])) m = j; }
        if (m != i) { std::swap(a[i], a[m]); st.w(2); }  // 远距离交换 => 不稳定
    }
}

// ================================================================ 3. 插入（移位版）
template <class T, class L, class C>
void insertion_sort(T* a, int n, L less, C& st) {
    for (int i = 1; i < n; ++i) {
        T v = std::move(a[i]);
        int j = i - 1;
        while (j >= 0) {
            st.c();
            if (!less(v, a[j])) break;  // 相等不越过 => 稳定
            a[j + 1] = std::move(a[j]); st.w();
            --j;
        }
        a[j + 1] = std::move(v); st.w();
    }
}

// ================================================================ 4. 希尔（Ciura 增量序列）
template <class T, class L, class C>
void shell_sort(T* a, int n, L less, C& st) {
    std::vector<int> gaps = {1, 4, 10, 23, 57, 132, 301, 701, 1750};
    while (gaps.back() < n / 2) gaps.push_back(int(gaps.back() * 2.25));
    for (int gi = int(gaps.size()) - 1; gi >= 0; --gi) {
        int gap = gaps[gi];
        if (gap >= n) continue;
        for (int i = gap; i < n; ++i) {  // 对每个 gap 做一次“步长为 gap 的插入排序”
            T v = std::move(a[i]);
            int j = i;
            while (j >= gap) {
                st.c();
                if (!less(v, a[j - gap])) break;
                a[j] = std::move(a[j - gap]); st.w();
                j -= gap;
            }
            a[j] = std::move(v); st.w();
        }
    }
}

// ================================================================ 5. 归并（自顶向下，一次性分配缓冲）
template <class T, class L, class C>
void merge_rec(T* a, T* buf, int lo, int hi, L less, C& st) {  // [lo, hi)
    if (hi - lo < 2) return;
    int mid = lo + (hi - lo) / 2;
    merge_rec(a, buf, lo, mid, less, st);
    merge_rec(a, buf, mid, hi, less, st);
    st.c();
    if (!less(a[mid], a[mid - 1])) return;  // 两半已衔接有序，跳过合并 => 有序输入 O(n)
    int i = lo, j = mid, k = lo;
    while (i < mid && j < hi) {
        st.c();
        if (less(a[j], a[i])) buf[k++] = std::move(a[j++]);  // 严格小于才取右边 => 稳定
        else                  buf[k++] = std::move(a[i++]);
        st.w();
    }
    while (i < mid) { buf[k++] = std::move(a[i++]); st.w(); }
    while (j < hi)  { buf[k++] = std::move(a[j++]); st.w(); }
    for (k = lo; k < hi; ++k) { a[k] = std::move(buf[k]); st.w(); }
}
template <class T, class L, class C>
void merge_sort(T* a, int n, L less, C& st) {
    std::vector<T> buf(n);
    merge_rec(a, buf.data(), 0, n, less, st);
}

// ================================================================ 6a. 朴素快排：Lomuto + 末元素作 pivot
template <class T, class L, class C>
void quick_naive_rec(T* a, int lo, int hi, L less, C& st, int depth) {  // [lo, hi]
    if (lo >= hi) return;
    if (depth > g_maxDepth) g_maxDepth = depth;
    T& p = a[hi];
    int i = lo;
    for (int j = lo; j < hi; ++j) {
        st.c();
        if (less(a[j], p)) { std::swap(a[i], a[j]); st.w(2); ++i; }
    }
    std::swap(a[i], a[hi]); st.w(2);
    quick_naive_rec(a, lo, i - 1, less, st, depth + 1);
    quick_naive_rec(a, i + 1, hi, less, st, depth + 1);
}
template <class T, class L, class C>
void quick_naive(T* a, int n, L less, C& st) { quick_naive_rec(a, 0, n - 1, less, st, 1); }

// ================================================================ 6b. 工程化快排：三数取中 + Hoare + 小区间插排 + 只递归短边
template <class T, class L, class C>
void quick_good_rec(T* a, int lo, int hi, L less, C& st, int depth) {  // [lo, hi]
    while (hi - lo > 16) {
        if (depth > g_maxDepth) g_maxDepth = depth;
        int mid = lo + (hi - lo) / 2;
        st.c(); if (less(a[mid], a[lo])) { std::swap(a[mid], a[lo]); st.w(2); }
        st.c(); if (less(a[hi], a[lo]))  { std::swap(a[hi], a[lo]);  st.w(2); }
        st.c(); if (less(a[hi], a[mid])) { std::swap(a[hi], a[mid]); st.w(2); }
        T p = a[mid];
        int i = lo - 1, j = hi + 1;
        for (;;) {
            do { ++i; st.c(); } while (less(a[i], p));
            do { --j; st.c(); } while (less(p, a[j]));
            if (i >= j) break;
            std::swap(a[i], a[j]); st.w(2);
        }
        if (j - lo < hi - j) { quick_good_rec(a, lo, j, less, st, depth + 1); lo = j + 1; }
        else                 { quick_good_rec(a, j + 1, hi, less, st, depth + 1); hi = j; }
    }
    insertion_sort(a + lo, hi - lo + 1, less, st);
}
template <class T, class L, class C>
void quick_good(T* a, int n, L less, C& st) { if (n > 1) quick_good_rec(a, 0, n - 1, less, st, 1); }

// ================================================================ 6c. 三路快排（Dijkstra 荷兰国旗）+ 随机 pivot
template <class T, class L, class C>
void quick3_rec(T* a, int lo, int hi, L less, C& st, std::mt19937& rng) {
    while (hi - lo > 16) {
        int r = lo + int(rng() % u32(hi - lo + 1));
        std::swap(a[lo], a[r]); st.w(2);
        T p = a[lo];
        int lt = lo, i = lo + 1, gt = hi;
        while (i <= gt) {
            st.c();
            if (less(a[i], p)) { std::swap(a[lt++], a[i++]); st.w(2); continue; }
            st.c();
            if (less(p, a[i])) { std::swap(a[i], a[gt--]); st.w(2); }
            else ++i;
        }  // [lo,lt) < p  ==  [lt,gt]  < (gt,hi]
        if (lt - lo < hi - gt) { quick3_rec(a, lo, lt - 1, less, st, rng); lo = gt + 1; }
        else                   { quick3_rec(a, gt + 1, hi, less, st, rng); hi = lt - 1; }
    }
    insertion_sort(a + lo, hi - lo + 1, less, st);
}
template <class T, class L, class C>
void quick3(T* a, int n, L less, C& st) { std::mt19937 rng(12345); if (n > 1) quick3_rec(a, 0, n - 1, less, st, rng); }

// ================================================================ 7. 堆排序
template <class T, class L, class C>
void sift_down(T* a, int i, int n, L less, C& st) {
    T v = std::move(a[i]);
    for (;;) {
        int c = 2 * i + 1;
        if (c >= n) break;
        if (c + 1 < n) { st.c(); if (less(a[c], a[c + 1])) ++c; }
        st.c();
        if (!less(v, a[c])) break;
        a[i] = std::move(a[c]); st.w();
        i = c;
    }
    a[i] = std::move(v); st.w();
}
template <class T, class L, class C>
void heap_sort(T* a, int n, L less, C& st) {
    for (int i = n / 2 - 1; i >= 0; --i) sift_down(a, i, n, less, st);  // 自底向上建堆 O(n)
    for (int end = n - 1; end > 0; --end) {
        std::swap(a[0], a[end]); st.w(2);
        sift_down(a, 0, end, less, st);
    }
}

// ================================================================ 8. 计数排序（稳定，键域 [0, range)）
template <class T, class K, class C>
void counting_sort(T* a, int n, int range, K key, C& st) {
    std::vector<int> cnt(range + 1, 0);
    for (int i = 0; i < n; ++i) ++cnt[key(a[i]) + 1];
    for (int k = 0; k < range; ++k) cnt[k + 1] += cnt[k];  // 前缀和 => 每个键的起始下标
    std::vector<T> out(n);
    for (int i = 0; i < n; ++i) { out[cnt[key(a[i])]++] = a[i]; st.w(); }  // 正序扫描 => 稳定
    for (int i = 0; i < n; ++i) { a[i] = out[i]; st.w(); }
}

// ================================================================ 9. LSD 基数排序（8 bit 一趟，跳过无效趟）
template <class T, class K, class C>
void radix_sort(T* a, int n, int bytes, K key, C& st, T* buf) {
    T* src = a; T* dst = buf;
    for (int pass = 0; pass < bytes; ++pass) {
        int sh = pass * 8;
        u32 cnt[257] = {};
        for (int i = 0; i < n; ++i) ++cnt[((key(src[i]) >> sh) & 255) + 1];
        bool trivial = false;
        for (int b = 0; b < 256; ++b) if (cnt[b + 1] == u32(n)) { trivial = true; break; }
        if (trivial) continue;  // 这一字节全相同，整趟可省
        for (int b = 0; b < 256; ++b) cnt[b + 1] += cnt[b];
        for (int i = 0; i < n; ++i) dst[cnt[(key(src[i]) >> sh) & 255]++] = src[i];
        st.w(n);
        std::swap(src, dst);
    }
    if (src != a) { std::memcpy((void*)a, src, sizeof(T) * n); st.w(n); }
}

// ================================================================ 10. 桶排序（[0,1) 浮点，扁平数组实现）
template <class C>
void bucket_sort(float* a, int n, C& st) {
    int B = n;
    std::vector<int> cnt(B + 1, 0);
    auto bi = [&](float x) { int b = int(x * B); return b >= B ? B - 1 : b; };
    for (int i = 0; i < n; ++i) ++cnt[bi(a[i]) + 1];
    for (int b = 0; b < B; ++b) cnt[b + 1] += cnt[b];
    std::vector<float> out(n);
    std::vector<int> pos(cnt.begin(), cnt.end() - 1);
    for (int i = 0; i < n; ++i) out[pos[bi(a[i])]++] = a[i];
    auto lt = [](float x, float y) { return x < y; };
    for (int b = 0; b < B; ++b) insertion_sort(out.data() + cnt[b], cnt[b + 1] - cnt[b], lt, st);  // 桶内插排
    std::memcpy(a, out.data(), sizeof(float) * n);
}

// ---------------------------------------------------------------- 工具
using Clock = std::chrono::steady_clock;
static double ms_since(Clock::time_point t0) { return std::chrono::duration<double, std::milli>(Clock::now() - t0).count(); }

enum Pattern { RANDOM, SORTED, REVERSED, NEARLY, FEWUNIQ, PAT_N };
static const char* kPatName[] = {"随机", "已有序", "逆序", "近乎有序(1%乱)", "大量重复(16种)"};

static std::vector<int> make_data(int n, Pattern p, u32 seed = 42) {
    std::mt19937 rng(seed);
    std::vector<int> v(n);
    switch (p) {
    case RANDOM:   for (auto& x : v) x = int(rng() & 0x7fffffff); break;
    case SORTED:   for (int i = 0; i < n; ++i) v[i] = i * 3; break;
    case REVERSED: for (int i = 0; i < n; ++i) v[i] = (n - i) * 3; break;
    case NEARLY:   for (int i = 0; i < n; ++i) v[i] = i * 3;
                   for (int k = 0; k < n / 100; ++k) std::swap(v[rng() % n], v[rng() % n]); break;
    case FEWUNIQ:  for (auto& x : v) x = int(rng() % 16); break;
    default: break;
    }
    return v;
}

struct Algo { const char* name; std::function<void(int*, int)> run; };
static const auto LessI = [](int x, int y) { return x < y; };

static std::vector<Algo> all_int_algos() {
    static NoCnt nc;
    return {
        {"冒泡",       [](int* a, int n) { bubble_sort(a, n, LessI, nc); }},
        {"选择",       [](int* a, int n) { selection_sort(a, n, LessI, nc); }},
        {"插入",       [](int* a, int n) { insertion_sort(a, n, LessI, nc); }},
        {"希尔",       [](int* a, int n) { shell_sort(a, n, LessI, nc); }},
        {"归并",       [](int* a, int n) { merge_sort(a, n, LessI, nc); }},
        {"快排-朴素",  [](int* a, int n) { quick_naive(a, n, LessI, nc); }},
        {"快排-工程",  [](int* a, int n) { quick_good(a, n, LessI, nc); }},
        {"快排-三路",  [](int* a, int n) { quick3(a, n, LessI, nc); }},
        {"堆",         [](int* a, int n) { heap_sort(a, n, LessI, nc); }},
        {"基数(LSD)",  [](int* a, int n) { std::vector<int> b(n); radix_sort(a, n, 4, [](int x) { return u32(x); }, nc, b.data()); }},
        {"std::sort",  [](int* a, int n) { std::sort(a, a + n); }},
        {"stable_sort",[](int* a, int n) { std::stable_sort(a, a + n); }},
        {"qsort",      [](int* a, int n) { std::qsort(a, n, sizeof(int), [](const void* x, const void* y) {
                                              int p = *(const int*)x, q = *(const int*)y; return (p > q) - (p < q); }); }},
    };
}

static double bench(const Algo& al, const std::vector<int>& src, int reps) {
    double best = 1e30;
    std::vector<int> w;
    for (int r = 0; r < reps; ++r) {
        w = src;
        auto t0 = Clock::now();
        al.run(w.data(), int(w.size()));
        best = std::min(best, ms_since(t0));
        if (!std::is_sorted(w.begin(), w.end())) { std::printf("!! %s 结果错误\n", al.name); std::exit(1); }
    }
    return best;
}

// ================================================================ E1 正确性 + 稳定性
struct Rec { int key; int id; };
static void E1() {
    std::printf("\n========== E1 正确性 + 稳定性校验（N=3000，5 种分布）==========\n");
    auto algos = all_int_algos();
    int bad = 0;
    for (auto& al : algos)
        for (int p = 0; p < PAT_N; ++p) {
            auto v = make_data(3000, Pattern(p));
            auto ref = v; std::sort(ref.begin(), ref.end());
            al.run(v.data(), int(v.size()));
            if (v != ref) { ++bad; std::printf("  %s @ %s 错误\n", al.name, kPatName[p]); }
        }
    // 桶排序（浮点）
    { std::mt19937 rng(7); std::uniform_real_distribution<float> U(0, 1); std::vector<float> f(3000); for (auto& x : f) x = U(rng);
      auto ref = f; std::sort(ref.begin(), ref.end()); NoCnt nc; bucket_sort(f.data(), 3000, nc); if (f != ref) { ++bad; std::printf("  桶排序错误\n"); } }
    std::printf("  13 种整数排序 × 5 分布 + 桶排序(浮点)：错误 %d 个\n", bad);

    // 稳定性：5000 条记录，key 只有 50 种，按 key 排序后检查同 key 的 id 是否仍递增
    std::mt19937 rng(9);
    std::vector<Rec> recs(5000);
    for (int i = 0; i < 5000; ++i) recs[i] = {int(rng() % 50), i};
    auto lessR = [](const Rec& x, const Rec& y) { return x.key < y.key; };
    auto check = [&](const char* name, std::vector<Rec> r, auto fn) {
        fn(r);
        int inv = 0;
        for (size_t i = 1; i < r.size(); ++i) if (r[i].key == r[i - 1].key && r[i].id < r[i - 1].id) ++inv;
        std::printf("  %-12s 相邻同键逆序对 = %4d  => %s\n", name, inv, inv ? "不稳定" : "稳定");
    };
    NoCnt nc;
    std::printf("  --- 稳定性（5000 条记录，键 50 种）---\n");
    check("冒泡",      recs, [&](auto& r) { bubble_sort(r.data(), 5000, lessR, nc); });
    check("选择",      recs, [&](auto& r) { selection_sort(r.data(), 5000, lessR, nc); });
    check("插入",      recs, [&](auto& r) { insertion_sort(r.data(), 5000, lessR, nc); });
    check("希尔",      recs, [&](auto& r) { shell_sort(r.data(), 5000, lessR, nc); });
    check("归并",      recs, [&](auto& r) { merge_sort(r.data(), 5000, lessR, nc); });
    check("快排-工程", recs, [&](auto& r) { quick_good(r.data(), 5000, lessR, nc); });
    check("快排-三路", recs, [&](auto& r) { quick3(r.data(), 5000, lessR, nc); });
    check("堆",        recs, [&](auto& r) { heap_sort(r.data(), 5000, lessR, nc); });
    check("计数",      recs, [&](auto& r) { counting_sort(r.data(), 5000, 50, [](const Rec& x) { return x.key; }, nc); });
    check("基数(LSD)", recs, [&](auto& r) { std::vector<Rec> b(5000); radix_sort(r.data(), 5000, 4, [](const Rec& x) { return u32(x.key); }, nc, b.data()); });
    check("std::sort", recs, [&](auto& r) { std::sort(r.begin(), r.end(), lessR); });
    check("stable_sort", recs, [&](auto& r) { std::stable_sort(r.begin(), r.end(), lessR); });
}

// ================================================================ E2 操作计数
static void E2() {
    std::printf("\n========== E2 操作计数（N=1000）：比较次数 / 写次数 ==========\n");
    std::printf("  理论参考：n^2/2 = 499500，n·log2n ≈ 9966\n");
    std::printf("  %-10s | %10s %10s | %10s %10s | %10s %10s\n", "算法", "随机:比较", "随机:写", "有序:比较", "有序:写", "逆序:比较", "逆序:写");
    struct A { const char* n; std::function<void(int*, int, Cnt&)> f; };
    std::vector<A> as = {
        {"冒泡",      [](int* a, int n, Cnt& c) { bubble_sort(a, n, LessI, c); }},
        {"选择",      [](int* a, int n, Cnt& c) { selection_sort(a, n, LessI, c); }},
        {"插入",      [](int* a, int n, Cnt& c) { insertion_sort(a, n, LessI, c); }},
        {"希尔",      [](int* a, int n, Cnt& c) { shell_sort(a, n, LessI, c); }},
        {"归并",      [](int* a, int n, Cnt& c) { merge_sort(a, n, LessI, c); }},
        {"快排-朴素", [](int* a, int n, Cnt& c) { quick_naive(a, n, LessI, c); }},
        {"快排-工程", [](int* a, int n, Cnt& c) { quick_good(a, n, LessI, c); }},
        {"快排-三路", [](int* a, int n, Cnt& c) { quick3(a, n, LessI, c); }},
        {"堆",        [](int* a, int n, Cnt& c) { heap_sort(a, n, LessI, c); }},
    };
    for (auto& a : as) {
        std::printf("  %-10s", a.n);
        for (Pattern p : {RANDOM, SORTED, REVERSED}) {
            auto v = make_data(1000, p); Cnt c; a.f(v.data(), 1000, c);
            std::printf(" | %10llu %10llu", (unsigned long long)c.cmp, (unsigned long long)c.wr);
        }
        std::printf("\n");
    }
}

// ================================================================ E3 O(n^2) 组计时
static void print_table(int n, const std::vector<const char*>& names, int reps) {
    auto algos = all_int_algos();
    std::printf("  %-12s", "算法\\分布");
    for (int p = 0; p < PAT_N; ++p) std::printf(" | %14s", kPatName[p]);
    std::printf("\n");
    for (auto nm : names)
        for (auto& al : algos)
            if (std::strcmp(al.name, nm) == 0) {
                std::printf("  %-12s", al.name);
                for (int p = 0; p < PAT_N; ++p) {
                    auto v = make_data(n, Pattern(p));
                    std::printf(" | %11.2f ms", bench(al, v, reps));
                }
                std::printf("\n");
            }
}
static void E3() {
    std::printf("\n========== E3 计时（N=20,000，取 3 次最好）==========\n");
    std::printf("  注：本表在 256MB 栈的线程里跑——朴素快排对 2 万有序数据递归 2 万层，放在 1MB 主线程栈会直接栈溢出（见 E4）\n");
    print_table(20000, {"冒泡", "选择", "插入", "希尔", "归并", "快排-朴素", "快排-工程", "快排-三路", "堆", "std::sort"}, 3);
}

// ================================================================ E4 朴素快排退化 + 小栈溢出
#ifdef _WIN32
struct QCtx { int* a; int n; int maxDepth; int overflow; };
static void run_naive(void* p) { auto* c = (QCtx*)p; NoCnt nc; quick_naive(c->a, c->n, LessI, nc); }
static void run_good(void* p)  { auto* c = (QCtx*)p; NoCnt nc; quick_good(c->a, c->n, LessI, nc); }
static int seh_call(void (*fn)(void*), void* ctx) {
    __try { fn(ctx); return 0; }
    __except (GetExceptionCode() == EXCEPTION_STACK_OVERFLOW ? EXCEPTION_EXECUTE_HANDLER : EXCEPTION_CONTINUE_SEARCH) { return 1; }
}
struct TArg { void (*fn)(void*); QCtx* ctx; };
static DWORD WINAPI thread_main(LPVOID p) {
    auto* t = (TArg*)p;
    g_maxDepth = 0;
    t->ctx->overflow = seh_call(t->fn, t->ctx);
    t->ctx->maxDepth = g_maxDepth;
    if (t->ctx->overflow) _resetstkoflw();
    return 0;
}
static void run_in_small_stack(const char* name, void (*fn)(void*), int n, SIZE_T stack) {
    auto v = make_data(n, SORTED);
    QCtx c{v.data(), n, 0, 0};
    TArg t{fn, &c};
    auto t0 = Clock::now();
    HANDLE h = CreateThread(nullptr, stack, thread_main, &t, STACK_SIZE_PARAM_IS_A_RESERVATION, nullptr);
    WaitForSingleObject(h, INFINITE); CloseHandle(h);
    std::printf("  %-10s 有序 N=%6d，线程栈 %3zuKB：%s，最大递归深度 %d，耗时 %.2f ms\n", name, n, stack / 1024,
                c.overflow ? "【栈溢出 STACK_OVERFLOW】" : "正常完成", c.maxDepth, ms_since(t0));
}
#endif
static void run_big_stack(void (*fn)());
static void E4_table() {
    for (int n : {1000, 4000, 16000}) {
        for (Pattern p : {RANDOM, SORTED}) {
            auto v = make_data(n, p); Cnt c; g_maxDepth = 0;
            quick_naive(v.data(), n, LessI, c);
            int d1 = g_maxDepth;
            auto v2 = make_data(n, p); Cnt c2; g_maxDepth = 0;
            quick_good(v2.data(), n, LessI, c2);
            std::printf("  N=%6d %-6s 朴素: 比较 %10llu 深度 %6d | 工程: 比较 %8llu 深度 %3d\n", n, kPatName[p],
                        (unsigned long long)c.cmp, d1, (unsigned long long)c2.cmp, g_maxDepth);
        }
    }
}
static void E4() {
    std::printf("\n========== E4 朴素快排在有序输入上的退化 ==========\n");
    run_big_stack(E4_table);
#ifdef _WIN32
    std::printf("  --- 放进 64KB 栈的工作线程（模拟 Job 线程 / 协程小栈）---\n");
    run_in_small_stack("快排-朴素", run_naive, 2000, 64 * 1024);
    run_in_small_stack("快排-朴素", run_naive, 20000, 64 * 1024);
    std::printf("  --- 默认 1MB 栈（= Windows 主线程默认栈）---\n");
    run_in_small_stack("快排-朴素", run_naive, 10000, 1024 * 1024);
    run_in_small_stack("快排-朴素", run_naive, 20000, 1024 * 1024);
    run_in_small_stack("快排-工程", run_good, 1000000, 64 * 1024);
#endif
}

// ================================================================ E5 O(n log n) 组 + 非比较
static void E5() {
    std::printf("\n========== E5 计时（N=1,000,000，取 3 次最好）==========\n");
    print_table(1000000, {"希尔", "归并", "快排-工程", "快排-三路", "堆", "基数(LSD)", "std::sort", "stable_sort", "qsort"}, 3);
    // 浮点：桶排序 vs std::sort
    std::mt19937 rng(11); std::uniform_real_distribution<float> U(0, 1);
    std::vector<float> f(1000000); for (auto& x : f) x = U(rng);
    double tb = 1e30, ts = 1e30;
    for (int r = 0; r < 3; ++r) {
        auto w = f; auto t0 = Clock::now(); NoCnt nc; bucket_sort(w.data(), int(w.size()), nc); tb = std::min(tb, ms_since(t0));
        auto w2 = f; t0 = Clock::now(); std::sort(w2.begin(), w2.end()); ts = std::min(ts, ms_since(t0));
    }
    std::printf("  [0,1) 均匀浮点 1e6：桶排序 %.2f ms vs std::sort %.2f ms\n", tb, ts);
}

// ================================================================ E6 稳定性的可见后果
struct UINode { int layer; const char* name; };
static void E6() {
    std::printf("\n========== E6 稳定性的可见后果：按 layer 排 UI 绘制顺序 ==========\n");
    // 64 个节点，layer 只有 0/1/2；同 layer 内按“创建顺序”绘制（后创建的盖在上面）
    static char names[64][16];
    std::vector<UINode> nodes;
    for (int i = 0; i < 64; ++i) { std::snprintf(names[i], 16, "N%02d", i); nodes.push_back({(i * 7) % 3, names[i]}); }
    auto byLayer = [](const UINode& x, const UINode& y) { return x.layer < y.layer; };
    auto a = nodes, b = nodes;
    std::sort(a.begin(), a.end(), byLayer);
    std::stable_sort(b.begin(), b.end(), byLayer);
    auto dump = [](const char* t, const std::vector<UINode>& v) {
        std::printf("  %-12s layer0: ", t);
        int k = 0;
        for (auto& x : v) if (x.layer == 0 && k++ < 12) std::printf("%s ", x.name);
        std::printf("...\n");
    };
    dump("std::sort", a);
    dump("stable_sort", b);
    int moved = 0; for (size_t i = 0; i < a.size(); ++i) if (std::strcmp(a[i].name, b[i].name) != 0) ++moved;
    std::printf("  两种结果有 %d / 64 个位置不同（同层遮挡关系被打乱 => UI 闪烁/层级错乱）\n", moved);
}

// ================================================================ E7 严格弱序
static void E7() {
    std::printf("\n========== E7 比较器写成 <= （违反严格弱序）==========\n");
    // (a) 复刻 libstdc++ __final_insertion_sort：前 16 个有哨兵插排，其后“无哨兵”插排（假设最小值必在前 16 个里）
    {
        int buf[8 + 64];
        int* a = buf + 8;
        for (int i = 0; i < 8; ++i) buf[i] = (i == 0 ? 0 : 9);  // a 左侧的“别人的内存”：[0, 9,9,9,9,9,9,9]
        for (int i = 0; i < 64; ++i) a[i] = 5;                   // 全相等的 64 个元素
        auto le = [](int x, int y) { return x <= y; };            // 错误比较器
        long oobReads = 0;
        // 前 16：普通插排（有边界检查）
        for (int i = 1; i < 16; ++i) { int v = a[i], j = i; while (j > 0 && le(v, a[j - 1])) { a[j] = a[j - 1]; --j; } a[j] = v; }
        // 其后：无哨兵插排
        for (int i = 16; i < 64; ++i) {
            int v = a[i]; int* last = a + i; int* next = last - 1;
            while (le(v, *next)) { if (next < a) ++oobReads; *last = *next; last = next; --next; }
            *last = v;
        }
        int nines = 0; for (int i = 0; i < 64; ++i) if (a[i] == 9) ++nines;
        std::printf("  (a) libstdc++ 式无哨兵插排 + <=：越界读 %ld 次；排序后数组里出现了 %d 个从未出现过的 9；左侧内存被改成 [", oobReads, nines);
        for (int i = 0; i < 8; ++i) std::printf("%d%s", buf[i], i < 7 ? "," : "]\n");
        // 同样代码用 < ：
        for (int i = 0; i < 8; ++i) buf[i] = (i == 0 ? 0 : 9);
        for (int i = 0; i < 64; ++i) a[i] = 5;
        auto lt = [](int x, int y) { return x < y; };
        oobReads = 0;
        for (int i = 16; i < 64; ++i) { int v = a[i]; int* last = a + i; int* next = last - 1; while (lt(v, *next)) { if (next < a) ++oobReads; *last = *next; last = next; --next; } *last = v; }
        std::printf("      同一段代码改用 < ：越界读 %ld 次，左侧内存 buf[1]=%d（未被改写）\n", oobReads, buf[1]);
    }
    // (b) MSVC std::sort + <=：数组两侧放哨兵，比较器里检查地址是否越界
    {
        const int N = 2000;
        std::vector<int> big(N + 200, 5);
        for (int i = 0; i < 100; ++i) { big[i] = INT_MIN; big[N + 100 + i] = INT_MAX; }
        int* b = big.data() + 100; int* e = b + N;
        long oob = 0, calls = 0;
        const int* g0 = big.data(); const int* g1 = big.data() + big.size();
        // 只统计落在两侧哨兵区的地址（排序内部把元素拷到栈上的临时变量不算越界）
        auto inGuard = [&](const int* p) { return (p >= g0 && p < b) || (p >= e && p < g1); };
        auto le = [&](const int& x, const int& y) { ++calls; if (inGuard(&x) || inGuard(&y)) ++oob; return x <= y; };
        auto t0 = Clock::now();
#ifndef _DEBUG
        std::sort(b, e, le);
#endif
        int guardBroken = 0;
        for (int i = 0; i < 100; ++i) { guardBroken += big[i] != INT_MIN; guardBroken += big[N + 100 + i] != INT_MAX; }
        std::printf("  (b) MSVC std::sort + <=（%d 个相等元素，Release）：比较 %ld 次，访问哨兵区 %ld 次，哨兵被改写 %d 个，耗时 %.3f ms\n", N, calls, oob, guardBroken, ms_since(t0));
        calls = 0; oob = 0;
        std::vector<int> big2(N, 5);
        auto lt = [&](const int& x, const int& y) { ++calls; return x < y; };
        std::sort(big2.begin(), big2.end(), lt);
        std::printf("      同样数据用 < ：比较 %ld 次\n", calls);
        std::printf("      （另：MSVC Debug 版 _DEBUG_LT_PRED 会对 pred(a,b)&&pred(b,a) 直接断言 \"invalid comparator\"）\n");
    }
}

// ================================================================ E8 渲染队列排序
struct DrawItem {           // 64B：模拟一条 draw 命令
    u64 key;                // [63..60]层 [59]透明 [58..35]深度(24bit) [34..20]材质 [19..0]网格
    void* mesh; void* material; float world[8]; int pad[2];
};
static u64 make_key(u32 layer, bool transparent, float depth01, u32 mat, u32 mesh) {
    u32 d = u32(depth01 * 0xFFFFFF);
    if (transparent) d = 0xFFFFFF - d;  // 透明：远到近；不透明：近到远
    u64 k = (u64(layer & 0xF) << 60) | (u64(transparent) << 59);
    if (transparent) k |= (u64(d) << 35) | (u64(mat & 0x7FFF) << 20) | (mesh & 0xFFFFF);     // 透明：深度优先
    else             k |= (u64(mat & 0x7FFF) << 44) | (u64(d & 0xFFFFFF) << 20) | (mesh & 0xFFFFF);  // 不透明：材质优先减少状态切换
    return k;
}
static void E8() {
    std::printf("\n========== E8 游戏实战 A：渲染队列 64 位 SortKey ==========\n");
    for (int n : {2000, 20000, 200000}) {
        std::mt19937 rng(5); std::uniform_real_distribution<float> U(0, 1);
        std::vector<DrawItem> items(n);
        for (auto& it : items) { it.key = make_key(rng() % 4, (rng() % 10) == 0, U(rng), rng() % 300, rng() % 5000); }
        const int R = n >= 200000 ? 5 : 30;
        double t1 = 1e30, t2 = 1e30, t3 = 1e30;
        for (int r = 0; r < R; ++r) {
            auto w = items; auto t0 = Clock::now();
            std::sort(w.begin(), w.end(), [](const DrawItem& x, const DrawItem& y) { return x.key < y.key; });
            t1 = std::min(t1, ms_since(t0));
            struct KI { u64 k; u32 i; };
            std::vector<KI> ki(n); for (int i = 0; i < n; ++i) ki[i] = {items[i].key, u32(i)};
            t0 = Clock::now();
            std::sort(ki.begin(), ki.end(), [](const KI& x, const KI& y) { return x.k < y.k; });
            t2 = std::min(t2, ms_since(t0));
            for (int i = 0; i < n; ++i) ki[i] = {items[i].key, u32(i)};
            std::vector<KI> tmp(n); NoCnt nc;
            t0 = Clock::now();
            radix_sort(ki.data(), n, 8, [](const KI& x) { return x.k; }, nc, tmp.data());
            t3 = std::min(t3, ms_since(t0));
        }
        std::printf("  N=%6d  std::sort(64B 结构体) %8.3f ms | std::sort(key+idx 16B) %8.3f ms | 基数排序(key+idx) %8.3f ms\n", n, t1, t2, t3);
    }
    // 帧间相关性：上一帧已排好，这一帧 2% 的物体深度微动
    std::printf("  --- 帧间增量：上一帧顺序作初值，本帧 2%% 物体 key 微动（N=20000）---\n");
    {
        int n = 20000; std::mt19937 rng(8);
        std::vector<u64> keys(n); for (auto& k : keys) k = (u64(rng()) << 32) | rng();
        std::sort(keys.begin(), keys.end());
        std::vector<u64> next = keys;
        for (int k = 0; k < n / 50; ++k) { int i = rng() % n; next[i] += (rng() % 2000) * 0x100000000ull - 1000 * 0x100000000ull; }
        double ti = 1e30, ts = 1e30, tr = 1e30;
        for (int r = 0; r < 20; ++r) {
            auto w = next; NoCnt nc; auto t0 = Clock::now(); insertion_sort(w.data(), n, [](u64 x, u64 y) { return x < y; }, nc); ti = std::min(ti, ms_since(t0));
            auto w2 = next; t0 = Clock::now(); std::sort(w2.begin(), w2.end()); ts = std::min(ts, ms_since(t0));
            auto w3 = next; std::vector<u64> tmp(n); t0 = Clock::now(); radix_sort(w3.data(), n, 8, [](u64 x) { return x; }, nc, tmp.data()); tr = std::min(tr, ms_since(t0));
        }
        std::printf("  插入排序 %.3f ms | std::sort %.3f ms | 基数排序 %.3f ms\n", ti, ts, tr);
    }
}

// ================================================================ E9 Top-K
static void E9() {
    std::printf("\n========== E9 游戏实战 B：排行榜 Top-100（N=1,000,000 玩家积分）==========\n");
    std::mt19937 rng(3);
    std::vector<int> s(1000000); for (auto& x : s) x = int(rng() % 10000000);
    const int K = 100;
    auto cmpDesc = std::greater<int>();
    std::vector<int> ref;
    auto run = [&](const char* name, auto fn) {
        double best = 1e30; std::vector<int> out;
        for (int r = 0; r < 3; ++r) { auto w = s; auto t0 = Clock::now(); out = fn(w); best = std::min(best, ms_since(t0)); }
        if (ref.empty()) ref = out;
        std::printf("  %-28s %8.2f ms  %s\n", name, best, out == ref ? "结果一致" : "!!结果不一致");
    };
    run("全量 std::sort", [&](std::vector<int>& w) { std::sort(w.begin(), w.end(), cmpDesc); return std::vector<int>(w.begin(), w.begin() + K); });
    run("std::partial_sort", [&](std::vector<int>& w) { std::partial_sort(w.begin(), w.begin() + K, w.end(), cmpDesc); return std::vector<int>(w.begin(), w.begin() + K); });
    run("nth_element + sort 前 K", [&](std::vector<int>& w) { std::nth_element(w.begin(), w.begin() + K, w.end(), cmpDesc); std::sort(w.begin(), w.begin() + K, cmpDesc); return std::vector<int>(w.begin(), w.begin() + K); });
    run("大小为 K 的小顶堆（流式）", [&](std::vector<int>& w) {
        std::priority_queue<int, std::vector<int>, std::greater<int>> pq;
        for (int x : w) { if ((int)pq.size() < K) pq.push(x); else if (x > pq.top()) { pq.pop(); pq.push(x); } }
        std::vector<int> o; while (!pq.empty()) { o.push_back(pq.top()); pq.pop(); } std::reverse(o.begin(), o.end()); return o; });
}

// ================================================================ E10 确定性
struct Unit { int score; int id; };
static u64 fnv(const std::vector<Unit>& v) { u64 h = 1469598103934665603ull; for (auto& u : v) { h ^= u32(u.id); h *= 1099511628211ull; } return h; }
static void E10() {
    std::printf("\n========== E10 游戏实战 C：帧同步确定性（2000 个单位按威胁值排序选目标）==========\n");
    std::mt19937 rng(1);
    std::vector<Unit> us(2000); for (int i = 0; i < 2000; ++i) us[i] = {int(rng() % 10), i};  // 大量平局
    NoCnt nc;
    auto by  = [](const Unit& a, const Unit& b) { return a.score > b.score; };
    auto byT = [](const Unit& a, const Unit& b) { return a.score != b.score ? a.score > b.score : a.id < b.id; };  // tie-break
    auto test = [&](const char* title, auto less) {
        std::printf("  %s\n", title);
        auto run = [&](const char* name, auto fn) { auto w = us; fn(w); std::printf("    %-24s hash=%016llx  首个目标 id=%d\n", name, (unsigned long long)fnv(w), w[0].id); };
        run("std::sort (MSVC)",       [&](auto& w) { std::sort(w.begin(), w.end(), less); });
        run("快排-工程 (另一种实现)",  [&](auto& w) { quick_good(w.data(), 2000, less, nc); });
        run("堆排序",                 [&](auto& w) { heap_sort(w.data(), 2000, less, nc); });
        run("stable_sort",            [&](auto& w) { std::stable_sort(w.begin(), w.end(), less); });
    };
    test("只比 score（有平局）：", by);
    test("score 相同再比 id（全序）：", byT);
}

#ifdef _WIN32
static DWORD WINAPI big_main(LPVOID p) { ((void (*)())p)(); return 0; }
static void run_big_stack(void (*fn)()) {
    HANDLE h = CreateThread(nullptr, 256u << 20, big_main, (LPVOID)fn, STACK_SIZE_PARAM_IS_A_RESERVATION, nullptr);
    WaitForSingleObject(h, INFINITE); CloseHandle(h);
}
#else
static void run_big_stack(void (*fn)()) { fn(); }
#endif

int main() {
    std::setvbuf(stdout, nullptr, _IONBF, 0);
#ifdef _MSC_VER
    std::printf("排序算法实测  (MSVC %d, x64 /O2)\n", _MSC_VER);
#endif
    E1(); E2(); run_big_stack(E3); E4(); run_big_stack(E5); E6(); E7(); E8(); E9(); E10();
    return 0;
}
