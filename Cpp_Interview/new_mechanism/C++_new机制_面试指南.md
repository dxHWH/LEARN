# C++ `new` 机制完全指南（游戏客户端面试向）

> 配套代码：同目录 `new_mechanism_demo.cpp`（MSVC 14.44 / VS2022 x64，`/std:c++latest /EHsc /O2` 编译运行；文中「实测」都来自它，完整输出见 `run_output.txt`）
> 反例：`compile_fail/` 下 2 个，编译器诊断见 `compile_fail/compile_errors.txt`
> 编译：`cl /nologo /std:c++latest /EHsc /O2 /utf-8 new_mechanism_demo.cpp`
> 相关：初始化细节见 [Q03 new int() 与 new int 的区别](../../Interview_QA/Q03_new_init/Q03_new_int()与new_int的区别.md)；替换全局 new 做泄漏检测见 [Q01 内存泄漏检测工具](../../Interview_QA/Q01_memory_leak_tool/Q01_如何设计内存泄漏检测工具.md)

---

## 提问记录

| 时间 | 原始提问 | 回答对应章节 |
|---|---|---|
| 2026-10-09 | 秋招面试的岗位背景是游戏客户端，你需要详细回答我的问题，并做出深层次的技术解答，并将每一次提问的内容和回答的内容都落盘为md文档。适当控制深度，增可读性，并点出面试需要注意的问题。详细解析一下 C++ 中的 new 机制 | 第 0～11 章 |

---

## 目录

0. 30 秒面试版答案（先背这个）
1. 三个「new」别混：new 表达式 / operator new / placement new
2. operator new 家族与查找规则
3. `new[]` / `delete[]` 与数组 cookie
4. 分配失败：new_handler、bad_alloc、nothrow
5. 初始化形式速览
6. placement new 与对象生命周期
7. 过对齐类型与 `align_val_t`
8. 底层与性能：new 到底慢在哪
9. 游戏客户端实战
10. 面试注意点：易错说法 + 高频追问
11. 速记卡

---

## 0. 30 秒面试版答案（先背这个）

> `new T(args)` 是一个**表达式**，编译器把它拆成两步：**① 调用 `operator new(sizeof(T))` 分配原始内存**（默认实现内部调 `malloc`，失败时循环调用 `new_handler`，最终抛 `std::bad_alloc`）；**② 在这块内存上调用 T 的构造函数**。如果构造函数抛异常，编译器会自动调用**匹配的 `operator delete`** 把内存还回去，不会泄漏。`delete p` 反过来：**先析构，再 `operator delete` 释放**。
>
> 关键区分：**`new` 表达式不能重载，能重载/替换的是 `operator new` 这个分配函数**；它可以全局替换，也可以给某个类单独定义。**placement new** 是 `operator new(size_t, void*)` 的一个版本，只返回传入的地址、**只构造不分配**，是对象池、`std::vector`、`std::optional` 的实现基础，用完要**手动调析构**。
>
> `new T[n]` 对**非平凡析构**的类型会在块头多存一个 **cookie（元素个数）**，`delete[]` 靠它知道要析构几个，所以 `new[]` 和 `delete` 混用是未定义行为。C++17 起过对齐类型（如 `alignas(64)`）自动走 `operator new(size_t, align_val_t)`。
>
> **游戏客户端视角**：通用 new 每次几十 ns 且有锁、碎片和 cache 不友好问题（实测 100 万次 64B 分配释放 63ms），所以热路径用**对象池 / 帧分配器 / pmr**（同场景 3.5～5.6ms），并通过替换全局 operator new 做**内存统计与标签**（UE 就是把全局 new 转到 `FMemory` → `GMalloc`）。

---

## 1. 三个「new」别混：new 表达式 / operator new / placement new

### 1.1 一张表分清

| 名字 | 是什么 | 能否自定义 | 做什么 |
|---|---|---|---|
| **new 表达式** `new T(...)` | 语言关键字/表达式 | ❌ 不能重载 | 分配 + 构造（+ 异常时回滚） |
| **operator new** `operator new(size_t)` | 普通函数（分配函数） | ✅ 可全局替换、可类内重载 | 只分配原始字节，像 `malloc` |
| **placement new** `new (ptr) T(...)` | new 表达式的一种写法，调用 `operator new(size_t, void*)` | ❌ 标准版不可替换 | 只在给定地址上构造 |

> 🎯 面试一句话：**「new 运算符」不能重载，我们重载的是 `operator new` 函数。** 说成「重载 new 运算符」会被追问。

### 1.2 编译器展开 `new` / `delete`（伪代码）

```cpp
// T* p = new T(args);
void* mem = operator new(sizeof(T));        // ① 查找规则：先类作用域，再全局（见 2.2）
T* p;
try {
    p = ::new (mem) T(args);                // ② 在 mem 上构造
} catch (...) {
    operator delete(mem);                   // ③ 构造失败：调用「匹配的」释放函数，不调析构
    throw;
}

// delete p;
if (p != nullptr) {                         // delete nullptr 是安全的空操作
    p->~T();                                // ① 析构（虚析构则走虚表，见 6.4）
    operator delete(p);                     // ② 释放（C++14 起可能调用 sized 版本）
}
```

### 1.3 实测（D1）

```
[1] Tracer* t = new Tracer(1); delete t;
    Tracer::operator new(size=4) -> 000001C8DDA30650     ← 先分配
    Tracer(1) 构造, this=000001C8DDA30650                 ← 再构造，地址相同
    ~Tracer(1) 析构                                       ← delete：先析构
    Tracer::operator delete(000001C8DDA30650)             ← 再释放
[2] 构造函数抛异常
    Tracer::operator new(size=4) -> 000001C8DDA306F0
    Tracer(-1) 构造, this=000001C8DDA306F0
    Tracer::operator delete(000001C8DDA306F0)             ← 自动回收，没有析构
    捕获异常: 构造失败
[4] ::new / ::delete 强制走全局版本
    [全局 operator new] size=4
    [全局 operator delete(sized)] 000001C8DDA305F0 size=4 ← MSVC 默认用 C++14 sized delete
```

要点：
- 构造失败**不调析构**（对象从未完整构造），但**已构造完的成员/基类会被析构**，然后回收内存。
- `::new` / `::delete` 跳过类专属版本，直接用全局版本。

### 1.4 必考：new 和 malloc 的区别

| 维度 | `new` / `delete` | `malloc` / `free` |
|---|---|---|
| 本质 | 语言运算符（表达式） | C 库函数 |
| 构造/析构 | ✅ 调用 | ❌ 不调用 |
| 返回类型 | `T*`，类型安全 | `void*`，需强转 |
| 大小 | 编译器按类型算 | 手动传字节数 |
| 失败 | 抛 `std::bad_alloc`（nothrow 版返回 nullptr），会走 `new_handler` | 返回 `NULL` |
| 可定制 | 可替换/重载 `operator new` | 只能整体换分配器（如 hook / LD_PRELOAD） |
| 数组 | `new[]`/`delete[]`，可能带 cookie | 自己算 `n*sizeof` |
| 扩容 | 无 `realloc` 对应物（对象不能按位搬） | 有 `realloc` |
| 对齐 | C++17 起自动支持过对齐类型 | 只保证 `max_align_t`，过对齐要 `aligned_alloc`/`_aligned_malloc` |

> ⚠️ 不要说「new 底层一定是 malloc」——**标准没规定**，只是主流实现（MSVC UCRT、libstdc++、libc++）默认这么做。UE 里全局 new 走的就是 `FMemory`。

---

## 2. operator new 家族与查找规则

### 2.1 全家桶（`<new>`）

| 版本 | 签名 | 引入 | 说明 |
|---|---|---|---|
| 普通 | `void* operator new(size_t)` | C++98 | 失败抛 `bad_alloc` |
| 数组 | `void* operator new[](size_t)` | C++98 | 默认转调 `operator new` |
| nothrow | `operator new(size_t, const nothrow_t&) noexcept` | C++98 | 失败返回 nullptr；默认实现转调普通版并 catch |
| placement | `operator new(size_t, void* p) noexcept` | C++98 | 直接 `return p`，**不可替换** |
| 对齐 | `operator new(size_t, align_val_t)` | C++17 | `alignof(T) > __STDCPP_DEFAULT_NEW_ALIGNMENT__`（MSVC x64 = 16）时使用 |
| sized delete | `operator delete(void*, size_t)` | C++14 | 释放时告诉分配器大小，省去查表 |
| destroying delete | `operator delete(T*, destroying_delete_t)` | C++20 | 仅类内，**析构也由它负责** |
| 自定义 placement | `operator new(size_t, Arena&)` / `(size_t, const char* file, int line)` | — | 任意额外参数，用 `new (arena) T` 调用 |

**可替换（replaceable）**：除标准 placement 版本外，全局的普通/数组/nothrow/对齐版本都可以由程序**替换**——在任意一个 .cpp 里定义同签名函数，链接器优先选用户版本（demo 就是这么做计数的）。

**自定义 placement 的配对 delete**：`operator delete(void*, Arena&)` 这种带额外参数的 delete **只会在构造函数抛异常时被编译器自动调用**，正常 `delete p` 永远不会调用它。所以 `new (arena) T` 创建的对象，要么手动 `p->~T()` + 还给 arena，要么包一层 `arena.Destroy(p)`。

### 2.2 查找规则：类作用域优先 —— 以及「隐藏」陷阱

`new T` 会**先在 T 的类作用域（含基类）里找 `operator new`**，找到任何一个名字就停止，不再看全局；找不到才用全局。

坑：类里只要声明了 `operator new(size_t)`，**全局的 placement new 和 nothrow new 都被隐藏了**：

```cpp
struct Tracer {
    static void* operator new(std::size_t sz);
    static void  operator delete(void* p) noexcept;
};
Tracer* a = new (buf) Tracer;            // ❌ C2660
Tracer* b = new (std::nothrow) Tracer;   // ❌ C2660
Tracer* c = ::new (buf) Tracer;          // ✅ 强制全局查找
```

MSVC 实测诊断（`compile_fail/compile_errors.txt`）：

```
cf1_hidden_placement_new.cpp(10): error C2660: “Tracer::operator new”: 函数不接受 3 个参数
cf1_hidden_placement_new.cpp(11): error C2660: “Tracer::operator new”: 函数不接受 3 个参数
```

> 💡 修法：要么用 `::new`，要么在类里把 placement / nothrow 版本也一起声明（转发到 `::operator new`）。Effective C++ 条款 52 讲的就是这个。

### 2.3 类内 operator new 的几条性质

- **隐式 static**：写不写 `static` 都是静态成员函数（调用时对象还不存在）。
- **会被继承**：基类定义的 `operator new` 子类也会用，但 `size` 参数是子类的大小——如果基类实现是「定长池」，要判断 `sz != sizeof(Base)` 时转给 `::operator new`。
- 对齐版本、数组版本要分别声明，否则 `new T[n]` 仍走全局。

---

## 3. `new[]` / `delete[]` 与数组 cookie

### 3.1 为什么需要 cookie

`delete[] p` 只拿到一个指针，却要对**每个元素**调用析构。元素个数从哪来？——`new[]` 时**多申请几个字节，把 n 存在块头**，返回给你的是块头之后的地址。这几个字节就叫 **array cookie**。

只有「`delete[]` 需要知道 n」时才放 cookie：
- 元素类型有**非平凡析构函数** → 要 cookie（要知道析构几个）
- 平凡析构（POD、`int` 等）→ 通常不要（直接整块 free）

### 3.2 实测（D2，MSVC x64）

```
[new PodItem[5]]  sizeof(PodItem)=4, 平凡析构=是
    PodItem::operator new[](size=20)        原始块=...44520
    表达式返回=...44520, 与原始块偏移=0 字节                     ← 无 cookie
[new DtorItem[5]]  sizeof(DtorItem)=4, 平凡析构=否
    DtorItem::operator new[](size=28)       原始块=...4F990      ← 20 + 8
    表达式返回=...4F998, 与原始块偏移=8 字节 → cookie 里存的元素个数=5
    DtorItem::operator delete[](...4F990)                         ← 释放的是原始块地址
[new SizedItem[5]] 平凡析构，但类内 operator delete[](void*, size_t)
    SizedItem::operator new[](size=20)  偏移=0 字节
    SizedItem::operator delete[](..., size=4)                     ← ⚠️ 见 3.4
[new int[0]] 合法，返回非空且唯一的指针
    [全局 operator new] size=0  →  返回 000001C8DDA30640 (非空=是)
```

DtorItem 只是多写了一个**空的** `~DtorItem() {}`，就多了 8 字节 cookie —— 用户提供的析构即使是空的也是「非平凡」的。能写 `= default` 就别写 `{}`。

### 3.3 `new[]` 配 `delete` 为什么会崩

以 `DtorItem* p = new DtorItem[5]; delete p;` 为例（MSVC）：
1. `delete p` 只析构 `p[0]`，其余 4 个析构没跑（资源泄漏）；
2. 然后 `operator delete(p)`——但 `p` 是**原始块 + 8**，不是 malloc 返回的地址 → 堆管理器校验失败 → Debug 下断言、Release 下堆损坏/崩溃。

反过来 `new` 配 `delete[]` 会把对象前 8 个字节当 cookie 读出一个随机 n，然后析构 n 次——更糟。

对 `int` 这种无 cookie 的类型混用「碰巧不崩」，但**依然是未定义行为**。面试时要说「UB，取决于实现」，不要说「int 数组用 delete 也没事」。

> 💡 现代写法：用 `std::vector` / `std::unique_ptr<T[]>`（自动 `delete[]`）；`std::shared_ptr<T[]>`（C++17）同理。

### 3.4 冷知识：MSVC 对类内 sized `delete[]` 的处理

Itanium C++ ABI（GCC/Clang）规定：如果类的 `operator delete[]` 是两参数 `(void*, size_t)` 版本，**即使元素平凡析构也要放 cookie**，好把正确的总大小传回去。实测 MSVC 不放 cookie，传入的 size=**4**（单个元素大小，而不是 20）。结论：**不要依赖类内 sized `delete[]` 的 size 参数做跨平台逻辑**（比如定长池按 size 分桶）。

---

## 4. 分配失败：new_handler、bad_alloc、nothrow

### 4.1 标准规定的失败流程

```
operator new(sz):
  loop:
    p = 尝试分配(sz)
    if p: return p
    h = get_new_handler()
    if h == nullptr: throw std::bad_alloc
    h()     // handler 必须做以下之一：释放内存让重试成功 / 换 handler / 卸载 handler / 抛 bad_alloc / 终止
```

**new_handler 是个循环**：handler 返回后会再次尝试分配，所以 handler 如果什么都不做就返回，会无限循环。

### 4.2 实测（D3）

```
[1] 普通 new（请求 1PB）
    new_handler 第 1 次被调用 → 释放 64MB 应急内存后重试
    new_handler 第 2 次被调用 → 已无可释放内存，卸载 handler
    捕获 bad allocation
[2] new (std::nothrow)：返回 0000000000000000
[3] new (std::nothrow) Thrower：
    [全局 operator new] size=1                  ← nothrow 版默认转调了我们替换的普通版
    [全局 operator delete] 000001C8DDA305D0      ← 构造抛异常 → 自动释放
    捕获 Thrower 构造失败                         ← nothrow 不吞构造函数的异常！
[4] new int[SIZE_MAX/2]、new int[-1]
    捕获 bad_alloc: bad allocation               ← 标准要求 bad_array_new_length，MSVC 抛的是基类 bad_alloc
```

几个结论：
- **`nothrow` 只保证分配不抛**，构造函数的异常照样往外抛。「用了 nothrow 就不用 try」是错的。
- 数组长度溢出：标准（C++11 起）要求抛 `std::bad_array_new_length`，MSVC 实测抛的是 `bad_alloc`。写 `catch (const std::bad_alloc&)` 两者都能接住。
- `new` 失败后**不能**再检查 `if (p == nullptr)`——普通 new 永远不返回 null，这段判断是死代码（老代码/VC6 时代的遗留习惯）。

### 4.3 现实中的「new 失败」比你想象的少见，也更危险

- **Linux / Android** 默认 overcommit：`new` 往往「成功」，真正写页时才缺页，内存不足时由 **OOM Killer / LMK 直接杀进程**，你的 `catch(bad_alloc)` 根本没机会执行。
- **Windows** 按提交（commit）计费，提交上限到了才会失败，32 位进程更常见的是**地址空间碎片**导致大块分配失败。
- **主机平台**内存固定，项目通常自己管理内存预算，超预算就断言。

**游戏客户端的做法**：
1. 启动时预留一块「应急内存」，`new_handler` 里释放它，并触发卸载缓存资源 / 强制 GC，然后上报一次 OOM 日志与内存快照；
2. 更重要的是**事前**控制：资源预算、内存水位监控、低内存回调（iOS `didReceiveMemoryWarning`、Android `onTrimMemory`）里主动卸载。
3. 很多引擎直接**关闭异常**（`-fno-exceptions`），此时分配失败只能断言/崩溃上报，`bad_alloc` 根本不会出现。

---

## 5. 初始化形式速览

（详细推导与实测见 Q03，这里只放结论）

| 写法 | 内置类型 / POD | 有 user-provided 默认构造的类 | 无 user-provided 默认构造的类 |
|---|---|---|---|
| `new T` | **不初始化**（垃圾值） | 调默认构造 | 成员不初始化 |
| `new T()` | **值初始化 → 0** | 调默认构造 | **先清零**再隐式构造 |
| `new T{}` | 同 `()`，且禁止窄化 | 同上 | 同上（聚合则聚合初始化） |
| `new T[n]` / `new T[n]()` | 不初始化 / 全 0 | — | — |

> ⚠️ 面试坑：类外 `T::T() = default;` 算 **user-provided**，`new T()` 不会清零。大块缓冲用 `new char[n]()` 会白白 memset（Q03 实测 256MB 18.5ms），C++20 用 `std::make_unique_for_overwrite<char[]>(n)` 避免。

---

## 6. placement new 与对象生命周期

### 6.1 基本用法

```cpp
alignas(Bullet) std::byte buf[sizeof(Bullet)];   // ① 自己准备内存（注意对齐！）
Bullet* b = ::new (buf) Bullet(1.0f);            // ② 只构造
b->~Bullet();                                    // ③ 必须手动析构（没有 placement delete 表达式）
// ④ buf 的内存怎么回收由它自己的来源决定
```

C++20 更推荐 `std::construct_at(p, args...)` / `std::destroy_at(p)`，可读性更好且可用于 `constexpr`。

### 6.2 谁在用 placement new

| 场景 | 说明 |
|---|---|
| `std::vector` | `reserve` 只分配不构造，`push_back` 时在尾部 placement new；`pop_back` 只析构不释放 |
| `std::optional` / `std::variant` | 内部是一块对齐存储，有值时 placement new |
| 对象池 / ECS 组件数组 | 预分配连续内存，按需构造/析构（见 D7 `ObjectPool`） |
| 网络/IPC 共享内存 | 在 mmap / 共享内存上构造对象 |
| 重用对象 | `p->~T(); new (p) T(...)` 原地「重建」 |

### 6.3 三个容易出事的点

1. **对齐**：`char buf[sizeof(T)]` 不保证 `alignof(T)` 对齐，必须 `alignas(T)`。SIMD 类型不对齐会直接崩（`movaps`）。
2. **忘记析构**：placement new 出来的对象，析构完全是你的责任；内部持有 `std::string`/`vector` 的话会泄漏。
3. **原地重建后的旧指针**：如果 T 有 `const` 成员或引用成员，`new (p) T(...)` 之后通过**旧指针**访问新对象，在 C++17 前是 UB，需要 `std::launder(p)`（C++20 放宽了大部分情况）。日常记住：**原地重建后用 placement new 的返回值**，别用旧指针。

### 6.4 sized delete 与虚析构、destroying delete（D5）

```
sizeof(Base)=16, sizeof(Derived)=120
[delete Base* 指向 Derived，有虚析构]
    Base::operator delete(000001C8DDA3E5D0, size=120)   ← 传入的是 Derived 的真实大小
[delete PooledEnemy*]
    destroying delete：手动析构 hp=100 的对象并还给池子
```

- 有虚析构时，编译器生成「**删除析构函数**」（MSVC 叫 scalar deleting destructor）放进虚表，它在**派生类上下文**里调用 `operator delete`，所以 size 是 120 而不是 16。
- 没有虚析构却通过基类指针 delete 派生对象 → **UB**：析构只跑一半，size 也是错的（定长池/sized 分配器会直接坏掉）。这是「基类析构为什么要 virtual」的**第二个理由**（第一个是派生类资源泄漏），面试说出来很加分。
- **C++20 destroying delete**：`operator delete(T*, std::destroying_delete_t)` 接管「析构 + 释放」，可用于：对象归还池子、变长对象（尾部带数组）按真实大小释放、在析构前根据动态类型做分派。

---

## 7. 过对齐类型与 `align_val_t`

```cpp
struct alignas(64) CacheLineData { float v[16]; };   // 缓存行对齐，避免多线程伪共享 / 适配 AVX-512
```

实测（D4）：

```
__STDCPP_DEFAULT_NEW_ALIGNMENT__ = 16, alignof(CacheLineData) = 64
    [全局 operator new(align_val_t)] size=64 align=64   地址 % 64 = 0（3 次均为 0）
[vector<CacheLineData>] std::allocator 也会自动走对齐版本:
    [全局 operator new(align_val_t)] size=256 align=64  data() % 64 = 0
```

- **C++17 之前**：`new CacheLineData` 只调 `operator new(size_t)`，只保证 8/16 字节对齐，`alignas(64)` 对堆对象**静默失效**，老代码要手写 `_aligned_malloc` + placement new。
- **C++17 起**：对齐超过 `__STDCPP_DEFAULT_NEW_ALIGNMENT__` 时编译器自动传 `std::align_val_t`。
- 如果你**替换了全局 operator new**（做内存追踪），记得把对齐那一族也替换，并且分配/释放必须成对（MSVC 上 `_aligned_malloc` 的内存不能 `free`）。

---

## 8. 底层与性能：new 到底慢在哪

### 8.1 调用链（以 Windows MSVC 为例）

```
new T  →  operator new (UCRT)  →  malloc  →  HeapAlloc(进程堆)  →  LFH 低碎片堆 / 段堆
                                                       ↓ 大块或池子不够
                                                 VirtualAlloc（系统调用，按 64KB/4KB 粒度）
Linux:  operator new (libstdc++)  →  malloc (glibc ptmalloc2: tcache → fastbin/smallbin → brk/mmap)
```

### 8.2 通用分配器慢的原因

| 原因 | 说明 |
|---|---|
| 通用性 | 要支持任意大小、任意顺序释放，需要分桶、合并、查找 |
| 线程安全 | 需要锁或线程缓存（tcache / LFH），竞争激烈时更慢 |
| 元数据 | 每块有头部信息；小对象额外开销比例大 |
| 碎片 | 长时间运行后外部碎片导致大块分配失败或占用变高 |
| 缓存局部性 | 逐个 new 的对象散落在堆上，遍历时 cache miss 多 |
| 偶发系统调用 | 池子耗尽时 `VirtualAlloc` / `mmap`，产生**帧时间毛刺** |
| Debug 堆 | MSVC Debug 版额外填充 0xCD（新分配）/ 0xDD（已释放）/ 0xFD（边界护栏）并校验，慢数倍 |

### 8.3 隐藏的分配次数（D6）

```
std::string 10 字符（SSO）                 分配   0 次,      0 字节
std::string 33 字符                         分配   1 次,     48 字节
vector push_back x1000（无 reserve）        分配  18 次,  12851 字节   ← MSVC 1.5 倍扩容
vector push_back x1000（先 reserve）        分配   1 次,   4000 字节
shared_ptr<Monster>(new Monster)            分配   2 次,     40 字节   ← 对象 + 控制块
make_shared<Monster>()                      分配   1 次,     32 字节   ← 合并成一次
make_unique<Monster>()                      分配   1 次,     16 字节
```

做性能排查时，很多「new」不是你写的，而是藏在 `std::string` 拼接、`vector` 扩容、`std::function` 大捕获、`shared_ptr` 控制块里。替换全局 operator new 统计「每帧分配次数」是最直接的抓手。

> 补充：`make_shared` 一次分配的代价是——只要还有 `weak_ptr` 活着，**对象内存就不能单独释放**（控制块和对象在同一块里）。大对象 + 长寿 weak_ptr 时反而要用 `shared_ptr(new T)`。

### 8.4 性能对比（D7：100 万个 64B 子弹，分配 → 读写 → 释放，5 次取最小）

| 方案 | 耗时 | 每个 | 倍数 |
|---|---|---|---|
| 逐个 `new` / `delete`（malloc → HeapAlloc） | 63.17 ms | 63.2 ns | 1.0× |
| `ObjectPool` + placement new（空闲链表） | 5.59 ms | 5.6 ns | **11.3×** |
| `FrameArena`（bump 指针 + 整体 Reset） | 3.54 ms | 3.5 ns | **17.8×** |
| `std::pmr::monotonic_buffer_resource` | 4.02 ms | 4.0 ns | **15.7×** |

为什么快：对象池的 `Create` 只是「弹出链表头 + 构造」，帧分配器只是「指针加法」，没有锁、没有查找、内存连续。代价是：
- 对象池：**定长**，容量要预估，用完要策略（扩容/拒绝/回收最老的）；
- 帧分配器：**不能单独释放**，生命周期必须整齐（本帧内有效），对象最好平凡析构；
- pmr：多一层虚调用，但能直接接 `std::pmr::vector` 等标准容器，非常适合「一帧内的临时容器」。

> ⚠️ 写 benchmark 的坑：C++14 起标准允许编译器**省略或合并**对可替换 `operator new` 的调用（[expr.new]），Clang `-O2` 会把 `delete new int(5)` 整个删掉，测出来 0ns。需要把指针存起来、读写结果，防止被优化。

---

## 9. 游戏客户端实战

### 9.1 UE 里的 new

- **全局 new 被接管**：每个模块通过 `REPLACEMENT_OPERATOR_NEW_AND_DELETE`（ModuleBoilerplate.h）把全局 `operator new/delete` 转发到 `FMemory::Malloc/Free`，再到 `GMalloc`（Windows 上通常是 `FMallocBinned2/3` 分桶分配器）。带 `-ansimalloc` 启动可切回系统 malloc，方便用 ASan / 系统工具查内存问题。
- **UObject 不能 `new`**：必须 `NewObject<T>()` / `CreateDefaultSubobject` / `SpawnActor`，因为对象要注册进 `GUObjectArray`、走反射和 GC。自己 `new` 出来的 UObject 不被 GC 认识。
- **非 UObject 的 C++ 对象**照常 `new`，但推荐 `TUniquePtr` / `TSharedPtr`（`MakeShared` 同样一次分配）。
- **内存追踪**：`LLM`（Low Level Memory Tracker）、Memory Insights 本质也是在分配入口打标签和调用栈。

### 9.2 自研引擎 / 通用客户端常见规则

| 规则 | 原因 |
|---|---|
| **热路径（每帧 Tick、技能结算、伤害计算）禁止 new** | 避免分配毛刺和碎片；用对象池、预分配容器、帧分配器 |
| 子弹、特效、飘字、伤害数字用**对象池** | 高频创建/销毁、大小固定 |
| 一帧内的临时数组用**帧分配器 / pmr / 栈上 `InlineAllocator`** | 帧末整体丢弃 |
| 替换全局 operator new 做**内存标签**（按模块/场景统计） | 内存预算、泄漏定位（见 Q01） |
| 加载阶段集中分配，战斗中不分配 | 碎片控制、帧率稳定 |
| **谁分配谁释放**，不要跨 DLL/so 的 CRT 边界 new/delete | `/MT` 静态 CRT 时每个 DLL 有自己的堆，A 分配 B 释放会崩；接口应提供 `Create/Destroy` 成对函数 |
| 移动端关注峰值内存 | Android LMK / iOS Jetsam 直接杀进程，`bad_alloc` 不会出现 |

### 9.3 一个最小可用的「类专属池」写法

```cpp
class Bullet {
public:
    static void* operator new(std::size_t sz) {
        if (sz != sizeof(Bullet)) return ::operator new(sz);   // 派生类尺寸不同 → 交给全局
        return s_pool.Alloc();
    }
    static void operator delete(void* p, std::size_t sz) noexcept {
        if (!p) return;
        if (sz != sizeof(Bullet)) { ::operator delete(p); return; }
        s_pool.Free(p);
    }
    // 别忘了：声明了上面的版本后，placement / nothrow 被隐藏，需要的话一起声明
private:
    static FixedPool s_pool;
};
```

业务代码依然写 `new Bullet(...)` / `delete b`，零改动接入池子——这就是重载类专属 operator new 的价值。

---

## 10. 面试注意点

### 10.1 易错说法对照

| ❌ 容易说错 | ✅ 正确说法 |
|---|---|
| 「我重载了 new 运算符」 | 重载/替换的是 **`operator new` 函数**，new 表达式不可重载 |
| 「new 底层就是 malloc」 | 主流实现默认如此，**标准没规定**；UE 就转到了 FMemory |
| 「new 失败返回 NULL，要判空」 | 普通 new 失败**抛 `bad_alloc`**；只有 `new (std::nothrow)` 返回 nullptr |
| 「用了 nothrow 就不会抛异常」 | nothrow 只管分配，**构造函数异常照样抛** |
| 「构造函数抛异常会内存泄漏」 | new 表达式会自动调用**匹配的 operator delete** 回收 |
| 「int 数组用 delete 也没事」 | **UB**；只是 MSVC 上无 cookie 碰巧不崩 |
| 「delete[] 通过 malloc 头里的大小算出元素个数」 | 靠的是 `new[]` 额外存的 **cookie**，且只有非平凡析构类型才有 |
| 「placement new 要配 placement delete」 | 没有 placement delete **表达式**，要**手动调析构**；placement 形式的 operator delete 只在构造抛异常时被自动调用 |
| 「alignas 对 new 出来的对象一直有效」 | **C++17 起**才保证（`align_val_t`） |
| 「delete 之后指针自动变 nullptr」 | 不会，需要手动置空；`delete nullptr` 安全，**double delete 是 UB** |

### 10.2 高频追问 + 答题要点

1. **new 和 malloc 的区别？** → 第 1.4 节表格，重点讲「构造/析构、类型安全、失败方式、可定制」四点，再补一句 operator new 默认基于 malloc 但可替换。
2. **构造函数里抛异常，内存会泄漏吗？** → 不会，编译器调用匹配的 operator delete；已构造的成员会被析构；但构造函数里自己 `new` 的裸指针会泄漏 → 用 RAII 成员。
3. **`delete[]` 怎么知道析构几个？** → cookie；只在非平凡析构时存在；MSVC x64 实测多 8 字节。
4. **placement new 有什么用？怎么释放？** → 对象池/容器/共享内存；手动析构 + 内存按来源回收；注意对齐。
5. **如何让某个类只能在堆上 / 只能在栈上创建？**
   - 只能在堆上：析构函数设为 `private/protected`，提供 `Destroy()`（栈对象离开作用域要调析构，编译不过）。
   - 只能在栈上：`operator new` 设为 `= delete`（也把 `new[]` 一起删）。
6. **如何检测内存泄漏？** → 替换全局 operator new/delete 记录（地址、大小、调用栈、标签）→ 快照对比；或 CRT Debug Heap、ASan、VLD、UE Memory Insights（详见 Q01）。
7. **为什么游戏里要用内存池？** → 第 8.2 表 + D7 实测数据（63ms vs 3.5～5.6ms），再说碎片、毛刺、cache 局部性。
8. **基类析构不是 virtual，delete 基类指针会怎样？** → UB：派生类析构不跑，且 sized delete 拿到错误大小（第 6.4 节）。
9. **`make_shared` 和 `shared_ptr(new T)` 的区别？** → 1 次 vs 2 次分配（实测 32B vs 40B）、异常安全（C++17 前 `f(shared_ptr<T>(new T), g())` 可能泄漏）、weak_ptr 拖住内存。
10. **`delete this` 合法吗？** → 合法，但必须保证对象是 `new` 出来的、之后不再访问任何成员、调用者不再使用该指针；常见于引用计数的 `Release()`。

---

## 11. 速记卡

```
new T(a)   = operator new(sizeof T) → 构造；构造抛异常 → 匹配的 operator delete
delete p   = 析构 → operator delete（C++14 可能是 sized 版）；delete nullptr 安全
可重载的是 operator new 函数，不是 new 表达式；类内版本优先并隐藏全局 placement/nothrow → 用 ::new
placement new：只构造不分配，手动 ~T()；注意 alignas；C++20 用 construct_at/destroy_at
new[]：非平凡析构才有 cookie（MSVC x64 +8B 存 n）；new[]/delete 混用 = UB
失败：循环调 new_handler → bad_alloc；nothrow 返回 nullptr 但不吞构造异常
  MSVC 数组长度溢出抛 bad_alloc（标准要求 bad_array_new_length）
对齐：C++17 起 alignof > 16 自动走 operator new(size_t, align_val_t)
虚析构：删除析构函数传真实 size（Derived 120 而非 Base 16）；C++20 destroying delete 接管析构+释放
性能（MSVC /O2，100 万 × 64B）：new/delete 63ms｜对象池 5.6ms｜帧分配器 3.5ms｜pmr 4.0ms
隐藏分配：string > 15 字符、vector 扩容（1000 次 push 18 次分配）、shared_ptr(new) 2 次、make_shared 1 次
游戏：热路径不 new；池子 / 帧分配器 / pmr；全局替换 new 做标签统计；跨 DLL 不混用 new/delete
UE：全局 new → FMemory → GMalloc(Binned)；UObject 只能 NewObject
```
