// event_vs_function_sim.cpp —— 用纯 C++ 模拟 UE 里 "Function / Event" 的两条调用路径（不是 UE 源码，是同构玩具）
//
//  D1  UHT 为 BlueprintNativeEvent 生成的包装：Foo() -> ProcessEvent(FindFunction("Foo")) -> BP 覆写 or exec thunk -> Foo_Implementation
//      对比直接调 Foo_Implementation() 会绕过蓝图覆写
//  D2  蓝图 Event 的局部数据放在对象级 UberGraphFrame（跨帧保留）-> 能挂起（Delay）；
//      Function 的局部变量在调用栈帧上，返回即销毁 -> 不能挂起
//  D3  调用开销：C++ 直调 vs 经 ProcessEvent（名字查表 + 参数打包 + 函数指针）
//
// 编译：cl /nologo /std:c++latest /EHsc /O2 /utf-8 event_vs_function_sim.cpp
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <functional>
#include <string>
#include <unordered_map>
#include <vector>

// ---------------- 极简反射：UClass / UFunction / ProcessEvent ----------------
struct UObject;
using ExecThunk = void (*)(UObject* self, void* parms);   // 相当于 DEFINE_FUNCTION(execFoo)
struct UFunction { std::string name; uint32_t flags; ExecThunk func; };
enum : uint32_t { FUNC_Native = 1, FUNC_Event = 2, FUNC_BlueprintEvent = 4, FUNC_BlueprintCallable = 8 };

struct UClass {
    UClass* super = nullptr;
    std::unordered_map<uint32_t, UFunction*> funcMap;   // 真实引擎是 TMap<FName, UFunction*>
    UFunction* Find(uint32_t name) const {             // 先找自己（蓝图子类的覆写），再找父类
        for (const UClass* c = this; c; c = c->super)
            if (auto it = c->funcMap.find(name); it != c->funcMap.end()) return it->second;
        return nullptr;
    }
};
static uint32_t FName(const char* s) { uint32_t h = 2166136261u; while (*s) h = (h ^ (uint8_t)*s++) * 16777619u; return h; }

struct UObject {
    UClass* cls = nullptr;
    virtual ~UObject() = default;
    void ProcessEvent(UFunction* f, void* parms) { f->func(this, parms); }   // 真实引擎：分配栈帧、拷参数、Native 走 thunk、蓝图走字节码 VM
};

// ---------------- 模拟 UHT 对 AMyActor 的生成结果 ----------------
// 头文件里写的是：
//   UFUNCTION(BlueprintCallable)   int  AddScore(int Delta);
//   UFUNCTION(BlueprintNativeEvent) void OnHit(int Damage);   virtual void OnHit_Implementation(int Damage);
struct AMyActor : UObject {
    int score = 0, hp = 100;
    std::vector<std::string> log;

    int AddScore(int d) { return score += d; }                        // 普通 C++ 成员函数：C++ 里直接调，零反射开销
    void OnHit(int Damage);                                          // 函数体由 UHT 生成（见下）
    virtual void OnHit_Implementation(int Damage) { hp -= Damage; log.push_back("C++ 默认实现：扣血 " + std::to_string(Damage)); }
};
struct AMyActor_eventOnHit_Parms { int Damage; };                   // UHT 生成的参数结构体（注意名字里的 event）
struct AMyActor_AddScore_Parms { int Delta; int ReturnValue; };
static const uint32_t NAME_OnHit = FName("OnHit"), NAME_AddScore = FName("AddScore");

void AMyActor::OnHit(int Damage) {                                  // .gen.cpp：事件的 C++ 入口只是“发一个反射调用”
    AMyActor_eventOnHit_Parms Parms{Damage};
    ProcessEvent(cls->Find(NAME_OnHit), &Parms);
}
static void execOnHit(UObject* self, void* p) {                     // exec thunk：蓝图 VM / ProcessEvent 走到 Native 时进这里
    static_cast<AMyActor*>(self)->OnHit_Implementation(static_cast<AMyActor_eventOnHit_Parms*>(p)->Damage);
}
static void execAddScore(UObject* self, void* p) {
    auto* P = static_cast<AMyActor_AddScore_Parms*>(p);
    P->ReturnValue = static_cast<AMyActor*>(self)->AddScore(P->Delta);
}

static UFunction F_OnHit{"OnHit", FUNC_Native | FUNC_Event | FUNC_BlueprintEvent, execOnHit};
static UFunction F_AddScore{"AddScore", FUNC_Native | FUNC_BlueprintCallable, execAddScore};
static UClass AMyActor_StaticClass;

// 蓝图子类 BP_MyActor 覆写了 OnHit 事件：编译出一个新的 UFunction 放在子类的 funcMap 里
static void BP_OnHit(UObject* self, void* p) {
    auto* a = static_cast<AMyActor*>(self);
    int dmg = static_cast<AMyActor_eventOnHit_Parms*>(p)->Damage;
    a->log.push_back("蓝图覆写：播受击特效，然后调用父类");
    a->OnHit_Implementation(dmg);                                   // 相当于蓝图里右键 Add Call to Parent Function
}
static UFunction F_BP_OnHit{"OnHit", FUNC_Event | FUNC_BlueprintEvent, BP_OnHit};
static UClass BP_MyActor_Class;

static void Demo1() {
    std::puts("[D1] BlueprintNativeEvent 的分派路径");
    AMyActor_StaticClass.funcMap = {{NAME_OnHit, &F_OnHit}, {NAME_AddScore, &F_AddScore}};
    BP_MyActor_Class.super = &AMyActor_StaticClass;
    BP_MyActor_Class.funcMap = {{NAME_OnHit, &F_BP_OnHit}};

    AMyActor cppOnly; cppOnly.cls = &AMyActor_StaticClass;
    AMyActor withBP;  withBP.cls = &BP_MyActor_Class;
    cppOnly.OnHit(10);
    withBP.OnHit(10);
    withBP.OnHit_Implementation(10);   // 错误写法：绕过 ProcessEvent
    auto dump = [](const char* tag, AMyActor& a) {
        std::printf("  %-34s hp=%d\n", tag, a.hp);
        for (auto& s : a.log) std::printf("      - %s\n", s.c_str());
    };
    dump("纯 C++ 类：OnHit(10)", cppOnly);
    dump("蓝图子类：OnHit(10) + OnHit_Impl(10)", withBP);
    std::printf("  flags：OnHit=0x%X（Native|Event|BlueprintEvent）  AddScore=0x%X（Native|BlueprintCallable）\n\n",
                F_OnHit.flags, F_AddScore.flags);
}

// ---------------- D2：UberGraphFrame vs 函数栈帧 ----------------
struct LatentManager {   // 模拟 FLatentActionManager：到时间后用 (回调函数, 入口偏移) 重新进入 ubergraph
    struct Item { double wakeAt; std::function<void(int)> exec; int linkage; };
    std::vector<Item> items;
    void Tick(double now) {
        for (size_t i = 0; i < items.size();)
            if (items[i].wakeAt <= now) { auto it = items[i]; items.erase(items.begin() + i); it.exec(it.linkage); }
            else ++i;
    }
};
struct BP_Door {
    struct UberGraphFrame { int K2Node_Event_Who = 0; bool delayPending = false; } frame;   // 事件图所有临时值都放在对象身上
    LatentManager* lm = nullptr;
    void ExecuteUbergraph(int entry, double now) {
        switch (entry) {
        case 0:   // 入口 0 = 事件 OnInteract 的起点
            if (frame.delayPending) { std::printf("  t=%.1f OnInteract(who=%d)：Delay 正在等待，本次执行被忽略（但 Who 引脚已被改成 %d）\n", now, frame.K2Node_Event_Who, frame.K2Node_Event_Who); return; }
            std::printf("  t=%.1f OnInteract(who=%d)：开门动画，Delay 2.0s 后自动关门\n", now, frame.K2Node_Event_Who);
            frame.delayPending = true;
            lm->items.push_back({now + 2.0, [this, now](int l) { ExecuteUbergraph(l, now + 2.0); }, 1});
            return;
        case 1:   // 入口 1 = Delay 完成后的续点
            frame.delayPending = false;
            std::printf("  t=%.1f Delay 完成，从续点继续：关门，读取 Who=%d  <- 读到的是最后一次调用写进来的值\n", now, frame.K2Node_Event_Who);
            return;
        }
    }
    void OnInteract(int who, double now) { frame.K2Node_Event_Who = who; ExecuteUbergraph(0, now); }   // 事件 stub：写参数到帧，再跳入口
};
static int CalcDamage_Function(int atk, int def) {   // 蓝图 Function：局部变量在本次调用的栈帧里，返回就没了，无处保存“续点”
    int local = atk * 2 - def;
    return local > 1 ? local : 1;
}
static void Demo2() {
    std::puts("[D2] 事件图的 UberGraphFrame（可挂起） vs Function 栈帧（不可挂起）");
    LatentManager lm; BP_Door door; door.lm = &lm;
    door.OnInteract(1, 0.0);
    door.OnInteract(2, 0.5);   // Delay 未完成时再次触发
    lm.Tick(1.0); lm.Tick(2.0);
    std::printf("  Function CalcDamage(50,30)=%d：同步返回，sizeof(UberGraphFrame)=%zu 字节常驻在每个实例上\n\n",
                CalcDamage_Function(50, 30), sizeof(BP_Door::UberGraphFrame));
}

// ---------------- D3：调用开销 ----------------
static void Demo3() {
    std::puts("[D3] 调用开销（1 亿次，/O2）");
    AMyActor a; a.cls = &AMyActor_StaticClass;
    const int N = 100'000'000;
    using C = std::chrono::steady_clock;
    auto t0 = C::now();
    for (int i = 0; i < N; ++i) a.AddScore(1);
    double direct = std::chrono::duration<double, std::milli>(C::now() - t0).count();
    a.score = 0; t0 = C::now();
    for (int i = 0; i < N; ++i) {
        AMyActor_AddScore_Parms P{1, 0};
        a.ProcessEvent(a.cls->Find(NAME_AddScore), &P);   // 每次都查表（相当于 FindFunction 不缓存）
    }
    double reflect = std::chrono::duration<double, std::milli>(C::now() - t0).count();
    UFunction* cached = a.cls->Find(NAME_AddScore);
    a.score = 0; t0 = C::now();
    for (int i = 0; i < N; ++i) { AMyActor_AddScore_Parms P{1, 0}; a.ProcessEvent(cached, &P); }
    double cachedMs = std::chrono::duration<double, std::milli>(C::now() - t0).count();
    std::printf("  C++ 直调 AddScore            %8.1f ms  (%.2f ns/次)\n", direct, direct * 1e6 / N);
    std::printf("  ProcessEvent + 每次查表      %8.1f ms  (%.2f ns/次)\n", reflect, reflect * 1e6 / N);
    std::printf("  ProcessEvent + 缓存 UFunction %7.1f ms  (%.2f ns/次)   score=%d\n", cachedMs, cachedMs * 1e6 / N, a.score);
}

int main() {
    std::printf("event_vs_function_sim  _MSC_VER=%d\n\n", _MSC_VER);
    Demo1(); Demo2(); Demo3();
}
