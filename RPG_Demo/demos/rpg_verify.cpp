// rpg_verify.cpp —— RPGDemo（dxHWH/RPGDemo@822270c，Unity 2022.3.54f1c1）关键逻辑的 C++ 逐行移植实测
//
// 本机没有 Unity、C# 编译器被策略禁用，所以把“纯逻辑”逐行移植成 C++ 来跑；序列化数值全部取自
// Assets/Scenes/SampleScene.unity / *.asset / *.anim（见 tools/scan_refs.py、tools/anim_dump.txt）。
// 依赖 Unity 运行时的部分（PlayerLoop 顺序、协程恢复时机、动画事件触发帧）写成“模型规则”，在书里标【模型】：
//   R1 每帧顺序：FixedUpdate×N（物理）→ Update（所有脚本）→ 协程 WaitForSeconds 恢复 → 动画更新（触发动画事件）
//   R2 动画事件：状态进入帧记为片段 0 时刻，在“片段时间首次 ≥ 事件时间”的那一帧的动画阶段触发
//   R3 Destroy 延迟到帧末；输入在帧首采样（两帧之间的按键在下一帧生效）
//   R4 Unity 的 time / deltaTime / 计时器都是 float，这里同样用 float
//
// 编译：cl /nologo /std:c++latest /EHsc /O2 /utf-8 rpg_verify.cpp
// 运行：rpg_verify.exe          跑全部实验，写 figdata.json
//       rpg_verify.exe E5 E9    只跑指定实验，写 figdata_partial.json（不覆盖正式数据）
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <map>
#include <set>
#include <string>
#include <utility>
#include <vector>

// ---------------------------------------------------------------- 工具：随机数 / JSON
struct Rng {  // xorshift64*；Range 语义与 UnityEngine.Random 一致：int 版 [a,b)，float 版 [a,b]
    uint64_t s;
    explicit Rng(uint64_t seed) : s(seed * 0x9E3779B97F4A7C15ull + 1) {}
    uint64_t next() { s ^= s >> 12; s ^= s << 25; s ^= s >> 27; return s * 2685821657736338717ull; }
    double u() { return (double)(next() >> 11) * (1.0 / 9007199254740992.0); }
    int range(int a, int b) { return a + (int)(u() * (b - a)); }
    float value() { return (float)u(); }
};

using KV = std::vector<std::pair<std::string, std::string>>;
static std::string jnum(double v, int prec = 10) {
    if (!std::isfinite(v)) return "null";
    char b[64]; snprintf(b, sizeof b, "%.*g", prec, v); return b;
}
static std::string jstr(const std::string& s) {
    std::string o = "\"";
    for (char c : s) { if (c == '"' || c == '\\') { o += '\\'; o += c; } else if (c == '\n') o += "\\n"; else o += c; }
    return o + "\"";
}
static std::string jobj(const KV& kv) {
    std::string o = "{"; bool f = true;
    for (auto& [k, v] : kv) { if (!f) o += ","; f = false; o += jstr(k) + ":" + v; }
    return o + "}";
}
static std::string jarr(const std::vector<std::string>& a) {
    std::string o = "[";
    for (size_t i = 0; i < a.size(); ++i) { if (i) o += ","; o += a[i]; }
    return o + "]";
}
static std::string jb(bool b) { return b ? "true" : "false"; }
static KV OUT;  // 顶层 JSON

// Mathf.RoundToInt = Math.Round（银行家舍入：x.5 取偶数）
static int RoundToInt(double x) { return (int)std::nearbyint(x); }

// ================================================================ E1 玩家状态机（逐行移植 Assets/Script/player/*.cs）
namespace e1 {
struct In {   // 每帧脚本化的传感器与输入
    bool g = true, w = false; float x = 0, y = 0; bool setvy = false; float vy = 0;
    bool shift = false, q = false, m0 = false, m1d = false, m1u = false, jump = false, die = false, swordBack = false;
};
struct Player;
struct State {
    Player* p; std::string name, anim;
    float stateTimer = 0; bool triggerCalled = false; float xInput = 0, yInput = 0;
    State(Player* p_, const char* n, const char* a) : p(p_), name(n), anim(a) {}
    virtual ~State() = default;
    virtual void Enter(); virtual void Update(); virtual void Exit();
    virtual void AnimationFinishTrigger() { triggerCalled = true; }
    virtual float AnimEvent() { return -1; }   // 模型：该状态动画里 AnimationTrigger 事件的时间（秒），-1 = 没有
};
struct Player {
    // ---- SampleScene.unity 里 player 的序列化值
    float moveSpeed = 7, jumpForce = 20, dashSpeed = 25, dashDuration = 0.2f, wallSlideSpeed = 2;
    float wjH = 4, wjV = 18, counterDur = 1, swordImpact = 6;
    float attackMove[3] = {2, 1, 3};
    // ---- 运行时
    float vx = 0, vy = 0; int facing = 1; bool facingRight = true, isKnocked = false;
    bool isBusy = false; std::vector<float> busyEnds;          // BusyForSeconds 协程（可重叠）
    float time = 0, dt = 1 / 60.f;
    bool hasSword = false, swordReturning = false, dots = false; int clones = 0, dashDir = 1;
    float dashCdTimer = 0;                                       // Skill_Dash.coolDownTimer（coolDown=1）
    std::map<std::string, bool> bools; State* cur = nullptr; In in;
    float enterTime = 0; bool evtFired = false;
    std::vector<std::string> trans;
    State *idle, *move, *jump, *fall, *dash, *wallSlide, *wallJump, *attack, *counter, *aim, *catchS, *death;
    void SetVelocity(float x, float y) { if (isKnocked) return; vx = x; vy = y; FlipController(x); }
    void Flip() { facing = -facing; facingRight = !facingRight; }
    void FlipController(float x) { if (x > 0 && !facingRight) Flip(); else if (x < 0 && facingRight) Flip(); }
    void Init(State* s) { cur = s; enterTime = time; evtFired = false; cur->Enter(); }
    void Change(State* s) { trans.push_back(cur->name + "→" + s->name); cur->Exit(); cur = s; enterTime = time; evtFired = false; cur->Enter(); }
    void StartBusy(float t) { isBusy = true; busyEnds.push_back(time + t); }
    bool CanUseDash() { if (dashCdTimer < 0) { dashCdTimer = 1; return true; } return false; }
    void Update() {   // Player.Update：先全局检查 Dash，再更新当前状态
        if (in.shift && CanUseDash()) { dashDir = (int)in.x; if (dashDir == 0) dashDir = facing; Change(dash); }
        cur->Update();
    }
    void Die() { Change(death); }
    void CatchTheSword() { Change(catchS); hasSword = false; }
};
void State::Enter() { p->bools[anim] = true; triggerCalled = false; }
void State::Update() { stateTimer -= p->dt; xInput = p->in.x; yInput = p->in.y; }
void State::Exit() { p->bools[anim] = false; }

struct Grounded : State {
    using State::State;
    bool HasNoSword() { if (!p->hasSword) return true; p->swordReturning = true; return false; }   // “查询”里有副作用
    void Update() override {
        State::Update();
        if (p->in.m1d && HasNoSword()) { p->Change(p->aim); return; }
        if (p->in.m0) { p->Change(p->attack); return; }
        if (p->in.q) { p->Change(p->counter); return; }
        if (p->in.jump && p->in.g) { p->Change(p->jump); return; }
        if (!p->in.g) { p->Change(p->fall); return; }
    }
};
struct Idle : Grounded {
    using Grounded::Grounded;
    void Enter() override { Grounded::Enter(); p->vx = 0; p->vy = 0; }
    void Update() override { Grounded::Update(); if (p->cur != this) return; if (xInput != 0 && !p->isBusy) p->Change(p->move); }
};
struct Move : Grounded {
    using Grounded::Grounded;
    void Update() override {
        Grounded::Update(); if (p->cur != this) return;
        p->SetVelocity(xInput * p->moveSpeed, p->vy);
        if (xInput == 0) p->Change(p->idle);
    }
};
struct Jump : State {
    using State::State;
    void Enter() override { State::Enter(); p->SetVelocity(p->vx, p->jumpForce); }
    void Update() override { State::Update(); if (p->vy < 0) p->Change(p->fall); }
};
struct Fall : State {
    using State::State;
    void Exit() override { State::Exit(); p->SetVelocity(xInput, p->vy); }
    void Update() override {   // 注意：三个 if 之间没有 return
        State::Update();
        if (p->in.g) p->Change(p->idle);
        if (xInput != 0) p->SetVelocity(xInput * p->moveSpeed, p->vy);
        if (p->in.w) p->Change(p->wallSlide);
    }
};
struct Dash : State {
    using State::State;
    void Enter() override { State::Enter(); stateTimer = p->dashDuration; p->clones++; }
    void Exit() override { State::Exit(); p->SetVelocity(0, p->vy); }
    void Update() override {
        State::Update();
        if (!p->in.g && p->in.w) { p->Change(p->wallSlide); return; }
        p->SetVelocity(p->dashDir * p->dashSpeed, 0);
        if (stateTimer < 0) p->Change(p->idle);
    }
};
struct WallSlide : State {
    using State::State;
    void Update() override {   // 没有“墙没了 → Fall”的出口
        State::Update();
        if (xInput != 0 && p->facing != xInput) { p->Change(p->idle); return; }
        if (p->in.jump) { p->Change(p->wallJump); return; }
        if (yInput < 0) p->vx = 0; else { p->vx = 0; p->vy = -p->wallSlideSpeed; }
        if (p->in.g) p->Change(p->idle);
    }
};
struct WallJump : State {
    using State::State;
    void Enter() override { State::Enter(); p->SetVelocity(-p->facing * p->wjH, p->wjV); }
    void Update() override { State::Update(); if (p->vy < 0) p->Change(p->fall); }
};
struct Attack : State {
    int combo = 0; float comboWindow = 1.5f, lastAttackTime = 0, inertia = 0.15f;
    std::vector<int> comboAtEnter;
    using State::State;
    float AnimEvent() override { static const float t[3] = {0.7143f, 0.3333f, 0.7f}; return t[combo]; }
    void Enter() override {
        State::Enter();
        if (combo > 2 || p->time >= lastAttackTime + comboWindow) combo = 0;
        comboAtEnter.push_back(combo);
        stateTimer = inertia;
        float dir = (float)p->facing; xInput = p->in.x; if (xInput != 0) dir = xInput;
        p->SetVelocity(p->attackMove[combo] * dir, 0);
    }
    void Exit() override { combo++; lastAttackTime = p->time; p->StartBusy(0.1f); State::Exit(); }
    void Update() override { State::Update(); if (stateTimer < 0) p->SetVelocity(0, 0); if (triggerCalled) p->Change(p->idle); }
};
struct Counter : State {
    using State::State;
    void Enter() override { State::Enter(); stateTimer = p->counterDur; }
    void Update() override { State::Update(); p->SetVelocity(0, 0); if (stateTimer < 0 || triggerCalled) p->Change(p->idle); }
};
struct Aim : State {
    using State::State;
    void Enter() override { State::Enter(); p->SetVelocity(0, 0); p->dots = true; }
    void Update() override { State::Update(); if (p->in.m1u) p->Change(p->idle); }
};
struct Catch : State {
    using State::State;
    float AnimEvent() override { return 0.2143f; }
    void Enter() override { State::Enter(); p->vx = -p->facing * p->swordImpact; p->vy = 0; }
    void Exit() override { State::Exit(); p->SetVelocity(0, 0); p->StartBusy(0.1f); }
    void Update() override { State::Update(); if (triggerCalled) { p->Change(p->idle); return; } }
};
struct Death : State {
    using State::State;
    void Enter() override { State::Enter(); p->SetVelocity(0, p->vy); }
    void Update() override { State::Update(); p->SetVelocity(0, p->vy); }
};

struct Sim {
    Player P;
    Idle sIdle{&P, "Idle", "Idle"}; Move sMove{&P, "Move", "Move"}; Jump sJump{&P, "Jump", "Air"}; Fall sFall{&P, "Fall", "Air"};
    Dash sDash{&P, "Dash", "Dash"}; WallSlide sWS{&P, "WallSlide", "WallSlide"}; WallJump sWJ{&P, "WallJump", "Air"};
    Attack sAtk{&P, "PrimaryAttack", "Attack"}; Counter sCtr{&P, "CounterAttack", "CounterAttack"}; Aim sAim{&P, "AimSword", "AimSword"};
    Catch sCatch{&P, "CatchSword", "CatchSword"}; Death sDeath{&P, "Death", "Die"};
    Sim() {
        P.idle = &sIdle; P.move = &sMove; P.jump = &sJump; P.fall = &sFall; P.dash = &sDash; P.wallSlide = &sWS; P.wallJump = &sWJ;
        P.attack = &sAtk; P.counter = &sCtr; P.aim = &sAim; P.catchS = &sCatch; P.death = &sDeath;
    }
    std::string boolsOn() { std::string s; for (auto& [k, v] : P.bools) if (v) { if (!s.empty()) s += "+"; s += k; } return s.empty() ? "-" : s; }
    // 跑一帧：R1 顺序
    std::vector<std::string> step(const In& in) {
        P.time += P.dt; P.in = in; P.trans.clear();
        if (in.setvy) P.vy = in.vy;
        P.dashCdTimer -= P.dt;                         // Skill.Update（假设先于 Player.Update）
        if (in.die) P.Die();                           // 上一帧敌人动画事件里的 TakeDamage → Die（模型：放在本帧开头）
        if (in.swordBack && P.hasSword) P.CatchTheSword();
        P.Update();
        for (auto it = P.busyEnds.begin(); it != P.busyEnds.end();) {   // 协程恢复：每个 BusyForSeconds 到点都写 false
            if (P.time >= *it) { P.isBusy = false; it = P.busyEnds.erase(it); } else ++it;
        }
        float ev = P.cur->AnimEvent();                 // 动画阶段：AnimationTrigger
        if (ev >= 0 && !P.evtFired && P.time - P.enterTime >= ev) { P.evtFired = true; P.cur->AnimationFinishTrigger(); }
        return P.trans;
    }
};

static std::string run(const char* key, const char* title, State* (*init)(Sim&), In (*script)(int), int frames, bool hasSword,
                       std::vector<int> keep) {
    Sim S; S.P.hasSword = hasSword; S.P.dashCdTimer = 0;
    State* s0 = init(S); S.P.Init(s0);
    std::vector<std::string> rows; int totalTrans = 0, multi = 0;
    std::map<std::string, int> stateFrames;
    printf("  [%s] %s\n", key, title);
    for (int f = 0; f < frames; ++f) {
        In in = script(f);
        std::string before = S.P.cur->name;
        auto tr = S.step(in);
        totalTrans += (int)tr.size(); if (tr.size() >= 2) multi++;
        stateFrames[S.P.cur->name]++;
        std::string t; for (auto& x : tr) { if (!t.empty()) t += "，"; t += x; }
        bool show = !tr.empty() || std::find(keep.begin(), keep.end(), f) != keep.end();
        if (show) {
            std::string inp;
            if (in.die) inp += "死亡 ";
            if (in.shift) inp += "Shift ";
            if (in.m0) inp += "左键 ";
            if (in.swordBack) inp += "剑飞回 ";
            if (in.x != 0) inp += (in.x > 0 ? "→ " : "← ");
            inp += std::string(in.g ? "着地" : "空中") + (in.w ? "+贴墙" : "");
            printf("    f%-3d t=%.3f  %-12s %-34s → %-13s vx=%5.1f vy=%5.1f  bools=%s\n", f, S.P.time, before.c_str(),
                   t.empty() ? "（无转移）" : t.c_str(), S.P.cur->name.c_str(), S.P.vx, S.P.vy, S.boolsOn().c_str());
            rows.push_back(jobj({{"f", jnum(f)}, {"t", jnum(S.P.time, 4)}, {"in", jstr(inp)}, {"before", jstr(before)},
                                 {"trans", jstr(t.empty() ? "—" : t)}, {"after", jstr(S.P.cur->name)},
                                 {"vx", jnum(S.P.vx, 4)}, {"vy", jnum(S.P.vy, 4)}, {"bools", jstr(S.boolsOn())}}));
        }
    }
    std::vector<std::string> combos; for (int c : S.sAtk.comboAtEnter) combos.push_back(jnum(c));
    return jobj({{"title", jstr(title)}, {"rows", jarr(rows)}, {"trans", jnum(totalTrans)}, {"multi", jnum(multi)},
                 {"wsFrames", jnum(stateFrames["WallSlide"])}, {"clones", jnum(S.P.clones)}, {"combo", jarr(combos)}});
}
}  // namespace e1

static void E1() {
    using namespace e1;
    printf("\n==== E1 玩家状态机：脚本化输入逐帧回放（60 fps，帧号从 0 开始）\n");
    KV r;
    // S1 空中朝墙移动，落地那一帧刚好贴墙
    r.push_back({"S1", run("S1", "空中向右移动，落地那一帧刚好贴墙", [](Sim& s) -> State* { return &s.sFall; },
                           [](int f) { In i; i.x = 1; if (f < 5) { i.g = false; i.setvy = true; i.vy = -10; } else { i.g = true; i.w = true; i.setvy = true; i.vy = 0; } return i; },
                           9, false, {4, 8})});
    // S2 贴墙下滑，墙在半空结束（平台侧面），按住朝墙方向
    r.push_back({"S2", run("S2", "贴墙下滑到墙的下沿以下（墙在半空结束），一直按住 →", [](Sim& s) -> State* { s.P.vy = -2; return &s.sWS; },
                           [](int f) { In i; i.x = 1; i.g = (f >= 70); i.w = (f < 10); i.setvy = true; i.vy = i.g ? 0.f : -2.f; return i; },
                           72, false, {9, 10, 40, 69})});
    // S3 死亡后按 Shift
    r.push_back({"S3", run("S3", "死亡 → 按 Shift 冲刺 → 再按 →", [](Sim& s) -> State* { return &s.sIdle; },
                           [](int f) { In i; i.die = (f == 10); i.shift = (f == 40); i.x = (f >= 60) ? 1.f : 0.f; return i; },
                           64, false, {11, 39})});
    // S4 死亡后飞剑回到身边
    r.push_back({"S4", run("S4", "掷出飞剑后死亡 → 剑飞回身边 → 再按 →", [](Sim& s) -> State* { return &s.sIdle; },
                           [](int f) { In i; i.die = (f == 10); i.swordBack = (f == 40); i.x = (f >= 60) ? 1.f : 0.f; return i; },
                           64, true, {11, 39})});
    // S5 第一段攻击在命中帧前被 Dash 取消
    r.push_back({"S5", run("S5", "第 1 段攻击在命中帧（0.357 s）前被 Dash 取消，随后再按左键", [](Sim& s) -> State* { return &s.sIdle; },
                           [](int f) { In i; i.m0 = (f == 0 || f == 40); i.shift = (f == 12); return i; },
                           42, false, {})});
    OUT.push_back({"E1", jobj(r)});
}

// ================================================================ E2 弹反判定窗口（Player.DamageEffect → CounterAttack）
static void E2() {
    printf("\n==== E2 弹反窗口：Q 的按下时刻 τ（相对骷髅进入攻击状态）落在哪里能弹反成功\n");
    printf("     骷髅 Skeleton_Attack 事件：OpenCounterAttackWindow 0.0833 s；AttackTrigger 与 CloseCounterAttackWindow 同在 0.4167 s\n");
    printf("     本项目：受击那一刻处于 CounterAttack 状态才算成功；原课程写法（已被注释）：反击状态的每帧 Update 里检查窗口\n");
    const double tOpen = 1.0 / 12, tHit = 5.0 / 12;   // 片段采样率 12：第 1 帧开窗、第 5 帧命中 + 关窗
    std::vector<std::string> rows;
    for (int fps : {30, 60, 144}) {
        double dt = 1.0 / fps;
        double cur_lo_any = 9, cur_hi_any = -9, org_lo_any = 9, org_hi_any = -9;
        double cur_lo_all = -9, cur_hi_all = 9, org_lo_all = -9, org_hi_all = 9;   // 所有相位都成功的公共区间
        const int PH = 24;
        for (int ph = 0; ph < PH; ++ph) {
            double phi = 0.001 * ph / PH;   // 骷髅进入攻击的那帧时刻 = 0；帧时刻 t_k = k*dt；1 ms 扫描步长内再取 24 个亚毫秒相位
            // 找出“开窗帧”“命中帧”
            int kOpen = 0, kHit = 0;
            while (kOpen * dt < tOpen - 1e-6) kOpen++;   // R2：片段时间首次 ≥ 事件时间（容差 1 µs）
            while (kHit * dt < tHit - 1e-6) kHit++;
            double clo = 9, chi = -9, olo = 9, ohi = -9;
            for (int ms = -1600; ms <= 700; ++ms) {
                double tau = ms / 1000.0 + phi;
                int ke = (int)std::ceil(tau / dt - 1e-9);          // R3：按键在下一帧生效
                // 反击状态：进入帧 ke（Enter 设 stateTimer=1，本帧不再 Update），之后每帧 -dt，<0 的那帧 Update 里退出
                float timer = 1.0f; int kx = ke;
                while (true) { kx++; timer -= (float)dt; if (timer < 0) break; }
                bool cur = (ke <= kHit) && (kx > kHit);            // 命中帧的动画阶段，玩家仍在反击状态
                // 原写法：反击状态的 Update（帧 ke+1 … kx，含退出帧）里，若窗口已开且未关（开窗帧之后、命中帧及之前）即成功
                int a = std::max(ke + 1, kOpen + 1), b = std::min(kx, kHit);
                bool org = a <= b;
                double t = tau;
                if (cur) { clo = std::min(clo, t); chi = std::max(chi, t); }
                if (org) { olo = std::min(olo, t); ohi = std::max(ohi, t); }
            }
            cur_lo_any = std::min(cur_lo_any, clo); cur_hi_any = std::max(cur_hi_any, chi);
            org_lo_any = std::min(org_lo_any, olo); org_hi_any = std::max(org_hi_any, ohi);
            cur_lo_all = std::max(cur_lo_all, clo); cur_hi_all = std::min(cur_hi_all, chi);
            org_lo_all = std::max(org_lo_all, olo); org_hi_all = std::min(org_hi_all, ohi);
        }
        printf("  %3d fps  本项目：稳定成功 τ∈[%+.3f, %+.3f] 宽 %.3f s   原写法：稳定成功 τ∈[%+.3f, %+.3f] 宽 %.3f s\n", fps,
               cur_lo_all, cur_hi_all, cur_hi_all - cur_lo_all, org_lo_all, org_hi_all, org_hi_all - org_lo_all);
        rows.push_back(jobj({{"fps", jnum(fps)}, {"clo", jnum(cur_lo_all, 4)}, {"chi", jnum(cur_hi_all, 4)}, {"cw", jnum(cur_hi_all - cur_lo_all, 4)},
                             {"olo", jnum(org_lo_all, 4)}, {"ohi", jnum(org_hi_all, 4)}, {"ow", jnum(org_hi_all - org_lo_all, 4)},
                             {"cloAny", jnum(cur_lo_any, 4)}, {"chiAny", jnum(cur_hi_any, 4)}}));
    }
    OUT.push_back({"E2", jobj({{"rows", jarr(rows)}, {"open", jnum(tOpen, 4)}, {"hit", jnum(tHit, 4)}})});
}

// ================================================================ E3 伤害公式（CharacterStats.DoDamage）
static void E3() {
    printf("\n==== E3 暴击 / 闪避的真实概率（Random.Range(0,100) 是 0..99 的整数）\n");
    Rng R(3);
    const int N = 2000000;
    std::vector<std::string> crit, eva;
    for (int c : {0, 5, 20, 50, 99}) {
        int hit = 0; for (int i = 0; i < N; ++i) if (R.range(0, 100) <= c) hit++;    // CanCrit：<=
        double exact = std::min(100, c + 1) / 100.0;
        printf("  critRate+agility=%3d  设计 %5.1f%%  公式 (c+1)/100=%5.1f%%  蒙特卡洛 %6.2f%%\n", c, (double)c, exact * 100, 100.0 * hit / N);
        crit.push_back(jobj({{"c", jnum(c)}, {"design", jnum(c)}, {"exact", jnum(exact * 100, 4)}, {"mc", jnum(100.0 * hit / N, 4)}}));
    }
    for (int e : {0, 10, 30}) {
        int hit = 0; for (int i = 0; i < N; ++i) if (R.range(0, 100) < e) hit++;      // CanTargetAvoidAttack：<
        printf("  evasion+agility=%3d   设计 %5.1f%%  蒙特卡洛 %6.2f%%\n", e, (double)e, 100.0 * hit / N);
        eva.push_back(jobj({{"e", jnum(e)}, {"mc", jnum(100.0 * hit / N, 4)}}));
    }
    // 冰冻判定用错对象：if (isChilled) 判的是“攻击者”自己
    printf("  护甲 10、伤害 30 的一次普通攻击（RoundToInt 为银行家舍入：10×0.85=8.5 → 8）：\n");
    std::vector<std::string> chill;
    struct C { const char* who; bool atk, tgt; } cs[] = {{"双方都正常", false, false}, {"目标被冰冻（设计意图：破甲）", false, true}, {"攻击者被冰冻", true, false}};
    for (auto& c : cs) {
        int armor = 10, dmg = 30;
        int bug = dmg - (c.atk ? RoundToInt(armor * 0.85) : armor);          // 源码：看攻击者 isChilled
        int fix = dmg - (c.tgt ? RoundToInt(armor * 0.85) : armor);          // 修正：看 _targetStats.isChilled
        printf("    %-28s 源码 %2d   修正后 %2d\n", c.who, bug, fix);
        chill.push_back(jobj({{"who", jstr(c.who)}, {"bug", jnum(bug)}, {"fix", jnum(fix)}}));
    }
    // 暴击伤害：critPower 在 Start 里被 SetDefaultValue(150) 覆盖
    std::vector<std::string> cp;
    for (int str : {0, 10, 50}) {
        int base = 20 + str; int crit = RoundToInt(base * ((150 + str) * 0.01));
        cp.push_back(jobj({{"str", jnum(str)}, {"base", jnum(base)}, {"crit", jnum(crit)}, {"mul", jnum((double)crit / base, 4)}}));
        printf("  damage 20 + strength %2d：普通 %d，暴击 %d（×%.2f）\n", str, base, crit, (double)crit / base);
    }
    OUT.push_back({"E3", jobj({{"crit", jarr(crit)}, {"eva", jarr(eva)}, {"chill", jarr(chill)}, {"cp", jarr(cp)}})});
}

// ================================================================ E4 敌人等级成长（EnemyStats.Modify）
static void E4() {
    printf("\n==== E4 EnemyStats.Modify：每级加“当前值的 25%%” → 复利；设计直觉是线性\n");
    std::vector<std::string> rows;
    for (int lv = 1; lv <= 10; ++lv) {
        auto modify = [&](int base) { std::vector<int> mods; for (int i = 1; i < lv; ++i) { int v = base; for (int m : mods) v += m; mods.push_back(RoundToInt(v * 0.25)); } int v = base; for (int m : mods) v += m; return v; };
        int d = modify(10), h = modify(100);
        double ld = 10 * (1 + 0.25 * (lv - 1)), lh = 100 * (1 + 0.25 * (lv - 1));
        printf("  Lv%-2d  damage %4d（线性 %5.1f）  maxHealth %5d（线性 %6.1f）  倍率 %.2fx / 线性 %.2fx\n", lv, d, ld, h, lh, h / 100.0, lh / 100.0);
        rows.push_back(jobj({{"lv", jnum(lv)}, {"dmg", jnum(d)}, {"ldmg", jnum(ld, 4)}, {"hp", jnum(h)}, {"lhp", jnum(lh, 5)},
                             {"mul", jnum(h / 100.0, 4)}, {"lmul", jnum(lh / 100.0, 4)}}));
    }
    OUT.push_back({"E4", jobj({{"rows", jarr(rows)}})});
}
// ================================================================ E5 掉落（ItemDrop.GenerateDrop）
static void E5() {
    printf("\n==== E5 骷髅掉落：5 个候选、PossibleItemDrop=3；先按 dropChance 逐个筛选，再无放回抽 min(3, 入选数) 个\n");
    struct It { const char* n; float c; } items[5] = {{"Animal_Skin", 84.8f}, {"Iron", 88.2f}, {"Nice_Armor", 35.4f}, {"Stolen_Sword", 32.f}, {"Wooden_Sword", 21.3f}};
    const int K = 3;
    double pf[5];
    for (int i = 0; i < 5; ++i) { int cnt = 0; for (int r = 0; r < 100; ++r) if ((float)r <= items[i].c) cnt++; pf[i] = cnt / 100.0; }
    double fin[5] = {0}, expCnt = 0, pNone = 0;
    for (int m = 0; m < 32; ++m) {        // 精确：枚举 2^5 种筛选结果
        double p = 1; int n = 0;
        for (int i = 0; i < 5; ++i) { bool in = m >> i & 1; p *= in ? pf[i] : 1 - pf[i]; n += in; }
        int k = std::min(K, n); expCnt += p * k; if (n == 0) pNone += p;
        for (int i = 0; i < 5; ++i) if (m >> i & 1) fin[i] += p * (double)k / n;
    }
    Rng R(5); const int N = 1000000; int mc[5] = {0}; long long tot = 0;
    for (int t = 0; t < N; ++t) {         // 蒙特卡洛：逐行复刻 GenerateDrop
        std::vector<int> list;
        for (int i = 0; i < 5; ++i) if ((float)R.range(0, 100) <= items[i].c) list.push_back(i);
        int n = std::min(K, (int)list.size());
        for (int j = 0; j < n; ++j) { int idx = R.range(0, (int)list.size()); mc[list[idx]]++; list.erase(list.begin() + idx); tot++; }
    }
    std::vector<std::string> rows;
    for (int i = 0; i < 5; ++i) {
        printf("  %-13s dropChance %5.1f  →  通过筛选 %5.1f%%  →  最终掉落 %5.1f%%（蒙特卡洛 %5.2f%%）\n", items[i].n, items[i].c, pf[i] * 100, fin[i] * 100, 100.0 * mc[i] / N);
        rows.push_back(jobj({{"name", jstr(items[i].n)}, {"chance", jnum(items[i].c, 4)}, {"filt", jnum(pf[i] * 100, 4)},
                             {"final", jnum(fin[i] * 100, 4)}, {"mc", jnum(100.0 * mc[i] / N, 4)}}));
    }
    int zero = 0; for (int r = 0; r < 100; ++r) if ((float)r <= 0.f) zero++;
    printf("  平均每只掉 %.3f 件（蒙特卡洛 %.3f）；什么都不掉 %.3f%%；dropChance 填 0 仍有 %d%% 概率通过筛选\n", expCnt, (double)tot / N, pNone * 100, zero);
    // 掉落初速度：Random.Range(-5, 5) 是 int 版 → -5..4
    int neg = 0, zer = 0, pos = 0; double sum = 0;
    for (int v = -5; v < 5; ++v) { (v < 0 ? neg : v == 0 ? zer : pos)++; sum += v; }
    printf("  掉落横向初速度 Random.Range(-5,5)：向左 %d0%%，0 占 %d0%%，向右 %d0%%，均值 %.1f m/s\n", neg, zer, pos, sum / 10);
    OUT.push_back({"E5", jobj({{"rows", jarr(rows)}, {"exp", jnum(expCnt, 4)}, {"expMc", jnum((double)tot / N, 4)}, {"none", jnum(pNone * 100, 4)},
                               {"zero", jnum(zero)}, {"vneg", jnum(neg * 10)}, {"vzero", jnum(zer * 10)}, {"vpos", jnum(pos * 10)}, {"vmean", jnum(sum / 10, 3)}})});
}

// ================================================================ E6 合成（Inventory.CanCraft）
static void E6() {
    printf("\n==== E6 合成 Nice_Armor（配方：Animal_Skin×2 + Iron×1）：CanCraft 校验数量，但每种材料只 RemoveItem 一次\n");
    std::vector<std::string> rows;
    for (auto [s0, i0] : std::vector<std::pair<int, int>>{{2, 1}, {3, 1}, {4, 2}, {6, 6}, {10, 10}}) {
        int res[2][3];
        for (int fix = 0; fix < 2; ++fix) {
            int skin = s0, iron = i0, crafted = 0;
            while (skin >= 2 && iron >= 1) { skin -= fix ? 2 : 1; iron -= 1; crafted++; }
            res[fix][0] = crafted; res[fix][1] = skin; res[fix][2] = iron;
        }
        printf("  初始 皮×%-2d 铁×%-2d  源码：合成 %d 件，剩 皮×%d 铁×%d   修正：合成 %d 件，剩 皮×%d 铁×%d\n", s0, i0,
               res[0][0], res[0][1], res[0][2], res[1][0], res[1][1], res[1][2]);
        rows.push_back(jobj({{"s0", jnum(s0)}, {"i0", jnum(i0)}, {"bc", jnum(res[0][0])}, {"bs", jnum(res[0][1])}, {"bi", jnum(res[0][2])},
                             {"fc", jnum(res[1][0])}, {"fs", jnum(res[1][1])}, {"fi", jnum(res[1][2])}}));
    }
    OUT.push_back({"E6", jobj({{"rows", jarr(rows)}})});
}

// ================================================================ E7 骷髅 AI（逐行移植 Enemy_Skeleton*.cs）
namespace e7 {
struct Enemy;
struct St {
    Enemy* e; std::string name, anim; float stateTimer = 0; bool triggerCalled = false;
    St(Enemy* e_, const char* n, const char* a) : e(e_), name(n), anim(a) {}
    virtual ~St() = default;
    virtual void Enter() { triggerCalled = false; } virtual void Update(); virtual void Exit();
};
struct Enemy {
    float x = 0, y = 0, vx = 0; int facing = 1; bool facingRight = true;
    float moveSpeed = 2, idleTime = 1, battleTime = 5, battleDistance = 10, attackRange = 1.75f, attackCoolDown = 1, checkDist = 5;
    float lastAttackTime = 0, time = 0, dt = 1 / 60.f, px = 0, py = 0, enterTime = 0;
    bool fixedVer = false; St *cur = nullptr, *idle, *move, *battle, *attack;
    int finds = 0, battleEnter = 0, selfTrans = 0, attacks = 0, flips = 0, multi = 0, nTrans = 0, frameTrans = 0;
    bool hitFired = false;
    std::string lastAnim;
    std::pair<bool, float> Detect() {   // Physics2D.Raycast(wallCheck=根节点, right*facing, 5, Player 层)；玩家胶囊半宽 0.385
        float rel = (px - x) * facing - 0.385f;
        bool hit = std::fabs(py - y) < 1.0f && rel >= 0 && rel <= checkDist;
        return {hit, rel};
    }
    float Dist() { return std::hypot(px - x, py - y); }
    void Flip() { facing = -facing; facingRight = !facingRight; flips++; }
    void SetVelocity(float v) { vx = v; if (v > 0 && !facingRight) Flip(); else if (v < 0 && facingRight) Flip(); }
    bool Grounded() { float gx = x + 0.144f * facing; return gx > -20 && gx < 20; }
    void Change(St* s) {
        if (s == cur) selfTrans++;
        nTrans++; frameTrans++;
        cur->Exit(); cur = s; enterTime = time; hitFired = false; cur->Enter();
    }
};
void St::Update() { stateTimer -= e->dt; }
void St::Exit() { e->lastAnim = anim; }
struct Ground : St {
    using St::St;
    void Enter() override { St::Enter(); e->finds++; }        // GameObject.Find("player")
    void Update() override {
        St::Update();
        if (e->fixedVer && e->cur == e->battle) return;        // 修正 1：Battle 不再自转移
        if (e->Detect().first || e->Dist() < 2) { e->Change(e->battle); return; }
    }
};
struct Idle : Ground {
    using Ground::Ground;
    void Enter() override { Ground::Enter(); stateTimer = e->idleTime; }
    void Update() override { Ground::Update(); if (stateTimer < 0) e->Change(e->move); }
};
struct Move : Ground {
    using Ground::Ground;
    void Enter() override { Ground::Enter(); stateTimer = 1; }
    void Update() override {
        Ground::Update();
        e->SetVelocity(e->moveSpeed * e->facing);
        if (!e->Grounded()) { e->Change(e->idle); e->Flip(); }
    }
};
struct Battle : Ground {
    int moveDir = 1;
    using Ground::Ground;
    void Enter() override { Ground::Enter(); e->battleEnter++; if (e->fixedVer) stateTimer = e->battleTime; }   // 修正 2：进入时初始化
    void Update() override {
        Ground::Update();
        if (!e->fixedVer) stateTimer = e->battleTime;          // 源码：每帧重置 → 永远不会超时
        auto [hit, d] = e->Detect();
        if (hit) {
            if (e->fixedVer) stateTimer = e->battleTime;       // 修正 3：只有看见玩家才续命
            if (d <= e->attackRange) {
                e->SetVelocity(0);
                if (e->time >= e->lastAttackTime + e->attackCoolDown) e->Change(e->attack);
                return;
            }
        } else if (stateTimer < 0 || e->Dist() > e->battleDistance) { e->Change(e->idle); return; }
        if (e->px > e->x) moveDir = 1; else if (e->px < e->x) moveDir = -1;
        e->SetVelocity(e->moveSpeed * moveDir * 1.5f);
    }
};
struct Attack : St {
    using St::St;
    void Enter() override { St::Enter(); e->SetVelocity(0); }
    void Exit() override { St::Exit(); e->lastAttackTime = e->time; }
    void Update() override { St::Update(); if (triggerCalled) e->Change(e->battle); e->SetVelocity(0); }
};
struct Sim {
    Enemy E; Idle sI{&E, "Idle", "Idle"}; Move sM{&E, "Move", "Move"}; Battle sB{&E, "Battle", "Move"}; Attack sA{&E, "Attack", "Attack"};
    Sim(bool fixedVer) { E.fixedVer = fixedVer; E.time = 5; E.idle = &sI; E.move = &sM; E.battle = &sB; E.attack = &sA; E.cur = &sI; sI.Enter(); }   // 遭遇发生在开局 5 s 后
    void step() {
        E.time += E.dt; E.frameTrans = 0;
        E.cur->Update();
        if (E.frameTrans >= 2) E.multi++;
        if (E.cur == &sA) {                                 // 动画事件（R2）
            float at = E.time - E.enterTime;
            if (!E.hitFired && at >= 0.4167f) { E.hitFired = true; E.attacks++; }
            if (at >= 1.25f) sA.triggerCalled = true;
        }
        E.x += E.vx * E.dt;                                  // 简化：位移按帧积分
    }
};
}  // namespace e7

static std::string e7run(const char* key, bool fixedVer, int scen) {
    using namespace e7;
    Sim S(fixedVer);
    S.E.px = (scen == 1) ? -1.5f : 2.5f; S.E.py = 0;
    const int F = 60 * ((scen == 2) ? 13 : 10);
    int battleFrames = 0, inBattleAfter = 0, phaseFrames = 0; float xmin = 1e9, xmax = -1e9; int flips0 = 0, finds0 = 0; float giveUp = -1;
    for (int f = 0; f < F; ++f) {
        if (scen == 2 && f == 180) { S.E.px = S.E.x + 0.5f; S.E.py = 4.0f; flips0 = S.E.flips; finds0 = S.E.finds; }   // 第 3 s 玩家跳上 4 m 高的平台
        std::string bef = S.E.cur->name; int at0 = S.E.attacks;
        S.step();
#ifdef DBG
        if (bef != S.E.cur->name || S.E.attacks != at0) printf("      dbg f%d t=%.3f %s->%s x=%.3f face=%d att=%d\n", f, S.E.time, bef.c_str(), S.E.cur->name.c_str(), S.E.x, S.E.facing, S.E.attacks);
#endif
        if (S.E.cur == &S.sB) battleFrames++;
        if (scen == 2 && f >= 180) {
            phaseFrames++; if (S.E.cur == &S.sB || S.E.cur == &S.sA) inBattleAfter++;
            else if (giveUp < 0) giveUp = (f - 180 + 1) / 60.f;
            xmin = std::min(xmin, S.E.x - S.E.px); xmax = std::max(xmax, S.E.x - S.E.px);
        }
    }
    float secs = F / 60.f;
    printf("  [%s] %-6s %4.0f s：Battle.Enter %4d 次（自转移 %4d），GameObject.Find %4d 次（%.1f 次/秒），出手 %2d 次，一帧两次转移 %d 次",
           key, fixedVer ? "修正版" : "源码", secs, S.E.battleEnter, S.E.selfTrans, S.E.finds, S.E.finds / secs, S.E.attacks, S.E.multi);
    KV kv = {{"secs", jnum(secs)}, {"battleEnter", jnum(S.E.battleEnter)}, {"self", jnum(S.E.selfTrans)}, {"finds", jnum(S.E.finds)},
             {"fps", jnum(S.E.finds / secs, 4)}, {"attacks", jnum(S.E.attacks)}, {"multi", jnum(S.E.multi)}, {"battleFrames", jnum(battleFrames)}};
    if (scen == 2) {
        int fl = S.E.flips - flips0;
        printf("\n        玩家跳上平台后 10 s：仍在追击 %.1f s，%s，转身 %d 次，在玩家脚下 [%.2f, %.2f] m 来回",
               inBattleAfter / 60.f, giveUp < 0 ? "从不放弃" : ("第 " + std::to_string(giveUp).substr(0, 4) + " s 放弃").c_str(), fl, xmin, xmax);
        kv.push_back({"chase", jnum(inBattleAfter / 60.f, 4)}); kv.push_back({"giveUp", jnum(giveUp, 4)});
        kv.push_back({"flips", jnum(fl)}); kv.push_back({"xmin", jnum(xmin, 3)}); kv.push_back({"xmax", jnum(xmax, 3)});
        kv.push_back({"finds2", jnum(S.E.finds - finds0)});
    }
    printf("\n");
    return jobj(kv);
}
static void E7() {
    printf("\n==== E7 骷髅 AI：Battle 继承 GroundState → 看见玩家时每帧“自己切到自己”；battle 计时每帧重置 → 永不超时\n");
    KV r;
    r.push_back({"A", e7run("A 玩家站在前方 2.5 m", false, 0)});
    r.push_back({"Af", e7run("A 玩家站在前方 2.5 m", true, 0)});
    r.push_back({"B", e7run("B 玩家站在身后 1.5 m", false, 1)});
    r.push_back({"C", e7run("C 玩家跳上 4 m 高平台", false, 2)});
    r.push_back({"Cf", e7run("C 玩家跳上 4 m 高平台", true, 2)});
    OUT.push_back({"E7", jobj(r)});
}

// ================================================================ E8 冻结协程重叠（Enemy.FreezeTimeFor × 旋转剑多段命中）
static void E8() {
    printf("\n==== E8 每次命中都 StartCoroutine(FreezeTimeFor 0.7s)：最早那个协程到点就解冻，后面的命中白冻\n");
    std::vector<std::string> rows;
    for (int fps : {60, 144}) for (float cd : {0.f, 0.2f, 0.35f, 0.5f, 0.7f}) {
        float dt = 1.f / fps, t = 0, hitTimer = 0, spinTimer = 2.f; bool spinning = true, frozen = false;
        std::vector<float> ends; float until = -1; int hits = 0;
        int frames = 0, frozenF = 0, fixF = 0; float lastHit = 0;
        for (int k = 0; k < fps * 4; ++k) {
            t += dt;
            if (spinning) {                         // SkillController_ThrowSword.SpinLogic（isSpinStoped 之后）
                spinTimer -= dt; if (spinTimer <= 0) spinning = false;
                hitTimer -= dt;
                if (hitTimer <= 0) { hitTimer = cd; hits++; lastHit = t; frozen = true; ends.push_back(t + 0.7f); until = std::max(until, t + 0.7f); }
            }
            for (auto it = ends.begin(); it != ends.end();) { if (t >= *it) { frozen = false; it = ends.erase(it); } else ++it; }  // R1：协程在 Update 之后恢复
            if (t <= lastHit + 0.7f + 1e-6f) { frames++; frozenF += frozen; fixF += (t < until); }
        }
        printf("  %3d fps  hitCoolDown=%.2f  命中 %3d 次  应全程冻结，实际冻结 %5.1f%%（修正版 %5.1f%%）\n", fps, cd, hits, 100.0 * frozenF / frames, 100.0 * fixF / frames);
        rows.push_back(jobj({{"fps", jnum(fps)}, {"cd", jnum(cd, 3)}, {"hits", jnum(hits)}, {"pct", jnum(100.0 * frozenF / frames, 4)}, {"fix", jnum(100.0 * fixF / frames, 4)}}));
    }
    OUT.push_back({"E8", jobj({{"rows", jarr(rows)}})});
}

// ================================================================ E9 瞄准点 vs 物理轨迹（Skill_ThrowSword.DotsPosition vs Box2D）
static void E9() {
    printf("\n==== E9 瞄准虚线用连续抛物线，Rigidbody2D 用半隐式欧拉（Δt=0.02）：同一 x 处剑比虚线低 ½·g·s·Δt·t\n");
    const double g = -9.81, s = 4.5, h = 0.02, LX = 35, LY = 28;
    std::vector<std::string> rows, curve;
    for (int ang : {20, 45, 70}) {
        double a = ang * 3.14159265358979 / 180, vx = LX * std::cos(a), vy = LY * std::sin(a);
        double px = 0, py = 0, qx = vx, qy = vy;   // Box2D：v += h·g·s；p += h·v
        double maxExact = 0;
        for (int n = 1; n <= 90; ++n) {
            qy += h * g * s; px += h * qx; py += h * qy;
            double t = n * h;
            double dot = vy * t + 0.5 * g * s * t * t;                        // 虚线公式（连续时间）
            double exact = vy * t + g * s * h * h * n * (n + 1) / 2.0;        // 离散闭式解
            maxExact = std::max(maxExact, std::fabs(exact - py));
            if (n == 25 || n == 50 || n == 75 || n == 84) {
                printf("  %2d°  t=%.2f s  x=%6.2f  虚线 y=%7.2f  实际 y=%7.2f  差 %+.3f m（公式 %+.3f）\n", ang, t, px, dot, py, py - dot, 0.5 * g * s * h * t);
                rows.push_back(jobj({{"ang", jnum(ang)}, {"t", jnum(t, 3)}, {"x", jnum(px, 5)}, {"dot", jnum(dot, 5)}, {"sim", jnum(py, 5)},
                                     {"dy", jnum(py - dot, 4)}, {"f", jnum(0.5 * g * s * h * t, 4)}}));
            }
            if (ang == 45 && n % 6 == 0) curve.push_back(jarr({jnum(px, 5), jnum(dot, 5), jnum(py, 5)}));
        }
        printf("        离散闭式解 y_n = v·nΔt + g·s·Δt²·n(n+1)/2 与逐步积分最大误差 %.2e\n", maxExact);
    }
    OUT.push_back({"E9", jobj({{"rows", jarr(rows)}, {"curve45", jarr(curve)}})});
}

// ================================================================ E10 帧率依赖（Dash 距离 / 点燃跳数 / 旋转剑命中数）
static double dashRun(double fps, double phase, int hitchFrame, double hitchDt, int* framesOut) {
    // R1：每帧先跑 FixedUpdate（fixedTime + 0.02 <= time 时推进一步），再 Update；deltaTime 上限 0.3333
    const double FD = 0.02, MAXDT = 0.33333334; double time = 0, fixedTime = -phase, x = 0, vx = 0;
    float stateTimer = 0; bool dashing = false, done = false; int k = 0, start = 5, frames = 0;
    while (!done && k < 2000) {
        double dt = (k == hitchFrame) ? hitchDt : 1.0 / fps; dt = std::min(dt, MAXDT);
        time += dt;
        while (fixedTime + FD <= time + 1e-12) { fixedTime += FD; x += vx * FD; }
        if (k == start) { dashing = true; stateTimer = 0.2f; }        // DashInputCheck → Enter
        if (dashing) {
            stateTimer -= (float)dt; vx = 25; frames++;               // Dash.Update：SetVelocity(25) 后判断
            if (stateTimer < 0) { vx = 0; dashing = false; done = true; }
        }
        k++;
    }
    if (framesOut) *framesOut = frames;
    return x;
}
static void E10() {
    printf("\n==== E10 帧率依赖：Update 里计时 + 物理在 FixedUpdate 里积分\n");
    std::vector<std::string> dash, hitch, ign, spin;
    for (int fps : {30, 60, 120, 144, 240}) {
        double mn = 1e9, mx = -1e9, sum = 0; int fr = 0; const int PH = 400;
        for (int i = 0; i < PH; ++i) { double d = dashRun(fps, 0.02 * i / PH, -1, 0, &fr); mn = std::min(mn, d); mx = std::max(mx, d); sum += d; }
        printf("  Dash  %3d fps：持续 %2d 帧，距离 %.2f ~ %.2f m（均值 %.3f，设计 5.00）\n", fps, fr, mn, mx, sum / PH);
        dash.push_back(jobj({{"fps", jnum(fps)}, {"frames", jnum(fr)}, {"min", jnum(mn, 4)}, {"max", jnum(mx, 4)}, {"mean", jnum(sum / PH, 4)}}));
    }
    for (double hd : {0.05, 0.1, 0.25, 0.5}) {
        double mx = -1e9, mn = 1e9;
        for (int i = 0; i < 100; ++i) { double d = dashRun(60, 0.02 * i / 100, 7, hd, nullptr); mx = std::max(mx, d); mn = std::min(mn, d); }
        printf("  Dash  60 fps，冲刺第 2 帧卡顿 %3.0f ms：距离 %.2f ~ %.2f m\n", hd * 1000, mn, mx);
        hitch.push_back(jobj({{"ms", jnum(hd * 1000)}, {"min", jnum(mn, 4)}, {"max", jnum(mx, 4)}}));
    }
    for (int fps : {30, 60, 144, 240}) {           // CharacterStats.Update：HandleStatus → ApplyIgnitDamage
        float dt = 1.f / fps, ignitedTimer = 2.f, igTimer = 0.f; bool ig = true; int ticks = 0; float first = -1, t = 0, last = 0;
        while (ig) {
            t += dt;
            ignitedTimer -= dt; if (ignitedTimer <= 0) ig = false;
            if (ig) { igTimer -= dt; if (igTimer < 0) { igTimer = 0.3f; ticks++; if (first < 0) first = t; last = t; } }
        }
        float iv = ticks > 1 ? (last - first) / (ticks - 1) : 0;
        printf("  点燃 %3d fps：2 s 内跳 %d 次伤害，首跳 %.3f s，间隔 %.4f s（设计 0.3）\n", fps, ticks, first, iv);
        ign.push_back(jobj({{"fps", jnum(fps)}, {"ticks", jnum(ticks)}, {"first", jnum(first, 4)}, {"iv", jnum(iv, 4)}}));
    }
    for (float cd : {0.f, 0.1f, 0.35f}) for (int fps : {30, 60, 144, 240}) {
        float dt = 1.f / fps, spinTimer = 2.f, hitTimer = 0; int hits = 0; bool sp = true;
        while (sp) { spinTimer -= dt; if (spinTimer <= 0) sp = false; hitTimer -= dt; if (hitTimer <= 0) { hitTimer = cd; hits++; } }
        printf("  旋转剑 hitCoolDown=%.2f %3d fps：2 s 命中 %3d 次\n", cd, fps, hits);
        spin.push_back(jobj({{"cd", jnum(cd, 3)}, {"fps", jnum(fps)}, {"hits", jnum(hits)}}));
    }
    OUT.push_back({"E10", jobj({{"dash", jarr(dash)}, {"hitch", jarr(hitch)}, {"ign", jarr(ign)}, {"spin", jarr(spin)}})});
}

// ================================================================ E11 分身朝向（SkillController_Clone.FaceClosesttTarget）
static void E11() {
    printf("\n==== E11 分身朝向：旋转判断写在 foreach 里面 → 找到敌人后每遍历一个碰撞体就可能再转 180°\n");
    Rng R(11); const int N = 200000; std::vector<std::string> rows;
    for (int n = 1; n <= 8; ++n) {
        int ok1 = 0, ok2 = 0, t2 = 0;
        for (int t = 0; t < N; ++t) {
            for (int ne = 1; ne <= 2; ++ne) {
                if (ne == 2 && n < 2) continue;
                // 构造 n 个碰撞体，其中 ne 个敌人，位置随机；分身在 x=0
                std::vector<int> isE(n, 0); std::vector<float> ex(n, 0);
                std::vector<int> idx(n); for (int i = 0; i < n; ++i) idx[i] = i;
                for (int i = n - 1; i > 0; --i) std::swap(idx[i], idx[R.range(0, i + 1)]);
                for (int j = 0; j < ne; ++j) { isE[idx[j]] = 1; float v; do { v = (float)(R.u() * 20 - 10); } while (std::fabs(v) < 0.5f); ex[idx[j]] = v; }
                // 逐行复刻
                float closest = 1e30f; int ci = -1; bool rotated = false;
                for (int i = 0; i < n; ++i) {
                    if (isE[i]) { float d = std::fabs(ex[i]); if (d < closest) { closest = d; ci = i; } }
                    if (ci >= 0) { if (0.f > ex[ci]) rotated = !rotated; }   // transform.position.x > closetEnemy.position.x
                }
                // 正确答案：朝向真正最近的敌人
                bool shouldFaceLeft = ex[ci] < 0;
                bool ok = (rotated == shouldFaceLeft);
                if (ne == 1) ok1 += ok; else { ok2 += ok; t2++; }
            }
        }
        double f1 = 100.0 * ok1 / N, f2 = t2 ? 100.0 * ok2 / t2 : 0;
        printf("  碰撞体 %d 个：1 个敌人时朝向正确 %5.1f%%（理论：敌人在右 100%%、在左 ⌈n/2⌉/n=%5.1f%% → 平均 %5.1f%%）  2 个敌人 %5.1f%%\n",
               n, f1, 100.0 * ((n + 1) / 2) / n, 50 + 50.0 * ((n + 1) / 2) / n, f2);
        rows.push_back(jobj({{"n", jnum(n)}, {"one", jnum(f1, 4)}, {"theo", jnum(50 + 50.0 * ((n + 1) / 2) / n, 4)}, {"two", jnum(f2, 4)}}));
    }
    OUT.push_back({"E11", jobj({{"rows", jarr(rows)}})});
}

int main(int argc, char** argv) {
    setvbuf(stdout, nullptr, _IONBF, 0);
    std::set<std::string> want; for (int i = 1; i < argc; ++i) want.insert(argv[i]);
    auto on = [&](const char* k) { return want.empty() || want.count(k); };
    printf("RPGDemo 逻辑移植实测（dxHWH/RPGDemo@822270c）\n");
    if (on("E1")) E1(); if (on("E2")) E2(); if (on("E3")) E3(); if (on("E4")) E4(); if (on("E5")) E5(); if (on("E6")) E6();
    if (on("E7")) E7(); if (on("E8")) E8(); if (on("E9")) E9(); if (on("E10")) E10(); if (on("E11")) E11();
    const char* path = want.empty() ? "figdata.json" : "figdata_partial.json";
    FILE* f = fopen(path, "wb"); std::string js = jobj(OUT); fwrite(js.data(), 1, js.size(), f); fclose(f);
    printf("\n写出 %s（%zu 字节）\n", path, js.size());
    return 0;
}
