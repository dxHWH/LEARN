# Controller / PlayerState / Pawn 三者的 Possess 关系

## 提问记录

| 时间 | 原始提问 | 回答对应章节 |
|---|---|---|
| 2026-10-13 16:39 | controller 、 playerstate、 pawn(charactor) 三者之间的possess关系是什么？ | 全文（§0 30 秒版 → §1 关系 → §2 源码流程 → §3 网络 → §4 生命周期 → §5 坑位 → §6 自测） |

> 基准：UE 5.x 源码（`Controller.cpp` / `PlayerController.cpp` / `Pawn.cpp` / `GameModeBase.cpp`）。与 GAS 电子书第 3 章（ASC 放 PlayerState、两端 InitAbilityActorInfo）配套阅读。

---

## 0. 30 秒面试版答案

**Possess 只发生在 Controller 和 Pawn 之间，PlayerState 不参与"附身"。**

- **Controller 是大脑**：PlayerController（玩家输入）或 AIController（行为树）。它通过 `Possess(Pawn)` 附身一个 Pawn，同一时刻**一对一**：一个 Controller 只控制一个 Pawn，一个 Pawn 只被一个 Controller 控制。
- **Pawn 是身体**：网格、碰撞、移动组件。被附身时 `PossessedBy` 里做三件事：`Owner = Controller`、`Pawn->Controller = Controller`、`Pawn->PlayerState = Controller->PlayerState`；玩家附身时还把 Pawn 设为 **AutonomousProxy**。
- **PlayerState 是档案**：由 Controller 在服务器上创建并**持有**（`Owner = Controller`），记录名字、分数、队伍等。Pawn 只是**引用**当前控制者的 PlayerState。
- **生命周期不同**：Pawn 死了就销毁、重生后换一个新 Pawn 再 Possess；Controller 与 PlayerState 跟着"玩家"走，跨重生存活。所以要跨命保存的数据放 PlayerState（GAS 的 ASC 也推荐放这）。
- **网络**：Possess **只能在服务器调用**；PlayerController 只存在于服务器和本机，PlayerState 和 Pawn 复制给所有人，所以其他客户端上 `Pawn->GetController()` 为空，要用 `Pawn->GetPlayerState()` 拿玩家信息。

---

## 1. 关系与类比

| 对象 | 类比 | 谁创建 | 存在于哪些端 | 生命周期 | 典型数据 |
|---|---|---|---|---|---|
| PlayerController | 玩家的"大脑 / 驾驶员" | GameMode 在 Login 时 | 服务器 + 拥有者客户端 | 连接期间一直存在 | 输入、相机、HUD、本地 UI |
| AIController | AI 的大脑 | `SpawnDefaultController` / `AutoPossessAI` | 仅服务器 | 随 AI | 行为树、黑板、感知 |
| PlayerState | 玩家的"身份证 / 计分板记录" | Controller（`InitPlayerState`） | **所有端**（默认 AlwaysRelevant） | 跨重生；断线可暂存 | 名字、分数、队伍、Ping、（GAS 的 ASC） |
| Pawn / Character | 身体 / 载具 | GameMode `SpawnDefaultPawnFor` 或关卡放置 | 所有端（受相关性影响） | 死亡即销毁，可替换 | 网格、移动、碰撞、血条表现 |

三条指针关系（全部在服务器上建立，再复制下去）：

```
Controller ──Possess──▶ Pawn            AController::Pawn         (OnRep_Pawn)
Pawn ──────回指──────▶ Controller       APawn::Controller         (OnRep_Controller)
Controller ──创建/Owner─▶ PlayerState    AController::PlayerState  (OnRep_PlayerState)
Pawn ──────引用──────▶ PlayerState      APawn::PlayerState        (OnRep_PlayerState)
PlayerState ──反查───▶ Pawn             APlayerState::GetPawn()   (UE5，由 Pawn::SetPlayerState 维护)
```

Owner 链：`PlayerState.Owner = Controller`，`Pawn.Owner = Controller`（Possess 后）。网络连接是沿 Owner 链找到 PlayerController 来确定的，这决定了"谁能对这个 Actor 发 Server RPC"。

---

## 2. Possess 在源码里做了什么（服务器）

### 2.1 入口

- `AController::Possess(APawn*)` 是 **final**（4.22 起），子类重写 `OnPossess` / `OnUnPossess`。
- 调用前检查 `HasAuthority()`，客户端调用直接无效（会打 Warning）。

### 2.2 调用链

```
GameModeBase::RestartPlayer(Controller)
  └─ SpawnDefaultPawnFor → 生成 Pawn
  └─ FinalizeRestartPlayer → Controller->Possess(NewPawn)
        └─ OnPossess(InPawn)                       // PlayerController 版本
             ├─ 若 InPawn 已被别的 Controller 控制 → 对方 UnPossess
             ├─ 若自己已控制旧 Pawn          → 自己 UnPossess 旧的
             ├─ InPawn->PossessedBy(this)          ★
             ├─ SetPawn(InPawn)                     // AController::Pawn，复制给拥有者
             ├─ SetViewTarget(InPawn)               // 相机
             ├─ ClientRestart(InPawn)               // Client RPC → 拥有者
             └─ ChangeState(NAME_Playing)

APawn::PossessedBy(NewController)
  ├─ SetOwner(NewController)                        // Owner 链接上，Pawn 上的 Server RPC 才能用
  ├─ Controller = NewController                     // 复制
  ├─ if (Controller->PlayerState) SetPlayerState(Controller->PlayerState)   // 复制
  ├─ if 是 PlayerController 且联网 → SetAutonomousProxy(true)
  │     否则（AI）                 → 远端角色保持 SimulatedProxy
  ├─ ForceNetUpdate()
  └─ 控制者变化时：ReceivePossessed（蓝图 Event Possessed，仅服务器）+ NotifyControllerChanged
```

### 2.3 UnPossess

`APawn::UnPossessed()`：`SetPlayerState(nullptr)`、`SetOwner(nullptr)`、`Controller = nullptr`、触发 `ReceiveUnpossessed`。Controller 侧 `SetPawn(nullptr)`，PlayerController 可能进入 Spectating 状态。

**PlayerState 在整个过程中不被创建也不被销毁**，只是被 Pawn 挂上 / 摘下。

### 2.4 PlayerState 是怎么来的

- `APlayerController::PostInitializeComponents` 在**非客户端**上调用 `InitPlayerState()`：用 `GameMode->PlayerStateClass` 生成，`SpawnInfo.Owner = this`，PlayerState 注册进 `GameState->PlayerArray`。
- AIController 默认**没有** PlayerState，需要 `bWantsPlayerState = true`（MOBA Bot 想和玩家同一套 GAS 代码时这样做）。
- 客户端经 `AController::OnRep_PlayerState` 得到指针。

---

## 3. 网络视角：三端各看到什么

| 数据 | 服务器 | 拥有者客户端 | 其他客户端 |
|---|---|---|---|
| PlayerController | ✅ | ✅（自己的） | ❌ 不存在 |
| `PC->Pawn` / `PC->PlayerState` | ✅ | ✅ | — |
| Pawn | ✅ Authority | ✅ **AutonomousProxy** | ✅ SimulatedProxy |
| `Pawn->GetController()` | ✅ | ✅ | ❌ **nullptr**（PC 不复制给别人） |
| `Pawn->GetPlayerState()` | ✅ | ✅ | ✅ |
| PlayerState | ✅ | ✅ | ✅（AlwaysRelevant） |

### 3.1 拥有者客户端的接管时序

```
服务器                                   拥有者客户端
Possess → PossessedBy
  ├─ Pawn.Controller / PlayerState 复制 ─▶ Pawn::OnRep_Controller / OnRep_PlayerState
  ├─ PC.Pawn 复制                       ─▶ PC::OnRep_Pawn
  └─ ClientRestart(Pawn) [Reliable]      ─▶ PC::ClientRestart → SetPawn
                                              → Pawn->PawnClientRestart（输入组件、相机）
                                              → AcknowledgePossession(Pawn)
AcknowledgedPawn = Pawn ◀─ ServerAcknowledgePossession(Pawn) [Server RPC]
```

**关键：这几份复制和 RPC 的到达顺序不保证。** 客户端上 PC、PS、Pawn 谁先到都可能，所以：

- 客户端初始化"需要 Pawn + PlayerState 同时存在"的东西（例如 GAS 的 `InitAbilityActorInfo(PS, Pawn)`），放在 **`APawn::OnRep_PlayerState`**（这时 Pawn 肯定在，PS 也到了）。
- 需要 PlayerController 的（本地预测技能要缓存 PC），再在 **`APlayerController::OnRep_PlayerState`** 里 `RefreshAbilityActorInfo()` 兜底。
- 服务器侧统一放在 **`APawn::PossessedBy`**。

### 3.2 其他客户端

只收到 Pawn 和 PlayerState 的复制。要显示敌人头顶的名字、队伍颜色，走 `Pawn->GetPlayerState<AMyPlayerState>()`，**不要**走 Controller。

### 3.3 为什么 Possess 后 Pawn 才能发 Server RPC

Server RPC 要求 Actor 的 Owner 链能追到该客户端的 PlayerController（NetConnection）。Possess 前 Pawn 没有 Owner，客户端在它身上调 Server RPC 会被丢弃（日志 `No owning connection`）。PlayerState 的 Owner 是 PC，所以 PlayerState 上的 Server RPC 一直可用（GAS 把 ASC 放在 PS 上也依赖这一点）。

---

## 4. 生命周期场景

| 场景 | Controller | PlayerState | Pawn |
|---|---|---|---|
| 玩家登录 | `Login` 生成 PC | PC 的 PostInitializeComponents 生成 | `RestartPlayer` 生成并 Possess |
| 角色死亡 | 保留，`UnPossess`（或 `Pawn->DetachFromControllerPendingDestroy()`） | **保留**（分数、队伍、GAS 冷却都在） | 播完死亡后 Destroy |
| 重生 | 同一个 PC 再 `Possess(NewPawn)` | 同一个 PS，被挂到新 Pawn 上 | 新 Pawn |
| 上载具 / 变身 | `Possess(Vehicle)`，旧 Pawn 自动 UnPossess | 跟着挂到载具上 | 换成载具 Pawn |
| 观战 | 无 Pawn，`ChangeState(Spectating)` | `bIsSpectator` | 无（或 SpectatorPawn） |
| 断线 | PC 销毁（`Logout`） | GameMode `AddInactivePlayer` 暂存副本，按 UniqueId 重连时恢复 | 通常销毁 |
| 无缝切图 | PC 可保留 | `CopyProperties` / `SeamlessTravelTo` 拷到新 PS | 重新生成 |

自动附身：
- `APawn::AutoPossessPlayer = Player0`：关卡中放置的 Pawn 被本地 0 号玩家附身（单机 / Listen 主机测试用）。
- `APawn::AutoPossessAI = PlacedInWorldOrSpawned` + `AIControllerClass`：生成时 `SpawnDefaultController()` 自动给一个 AIController 并 Possess。

---

## 5. 游戏客户端实战：数据该放哪

| 数据 | 放哪 | 理由 |
|---|---|---|
| 输入映射、相机、准星、本地 UI 状态 | PlayerController | 只有自己需要，不复制给别人 |
| 名字、队伍、K/D、段位、皮肤选择 | PlayerState | 所有人要看，跨重生保留 |
| GAS 的 ASC / AttributeSet（要保留冷却与成长） | PlayerState | 跨重生；Owner=PC 天然可发 RPC |
| 当前血条表现、骨骼、移动 | Pawn | 跟身体走，死亡清空 |
| AI 的决策 | AIController | 仅服务器 |

MOBA 类比：英雄 = Pawn（死亡后在泉水重生一个新的），召唤师 / 账号 = PlayerState（经济、等级、装备记录都在），手 = PlayerController。

---

## 6. 坑位清单

1. **客户端调 Possess 没反应**：Possess 只在服务器生效。客户端发 Server RPC，由服务器调用。
2. **`BeginPlay` 里 `GetController()` / `GetPlayerState()` 为空**：Pawn 生成时还没被附身（尤其客户端）。服务器用 `PossessedBy`，客户端用 `OnRep_PlayerState` / `OnRep_Controller`，UE5 还可以用 `NotifyControllerChanged` 或 Controller 的 `OnPossessedPawnChanged` 委托。
3. **模拟端 `GetController()` 返回 nullptr 导致崩溃或逻辑错**：其他客户端没有 PlayerController，统一用 PlayerState。
4. **蓝图 `Event Possessed` 只在服务器触发**：客户端要用 `Event Restarted`（`ReceiveRestarted`）/ OnRep 类事件。
5. **把要跨命保存的数据放 Pawn**：死亡即丢。放 PlayerState。
6. **以为 PlayerState 也要 Possess**：不需要，它由 Controller 持有，Pawn 自动引用。
7. **Possess 新 Pawn 后旧 Pawn 还在接收输入 / 还在相机里**：OnPossess 会先 UnPossess 旧 Pawn，但自己重写 `OnPossess` 时忘了调 `Super::OnPossess` 会漏掉这些步骤。
8. **AI 想用 PlayerState（Bot）却拿到空**：AIController 设 `bWantsPlayerState = true`。
9. **PlayerState 默认 NetUpdateFrequency 很低**：把 GAS 或频繁变化的数据放上面时要调高（GAS 书 3.3）。

---

## 7. 自测题

<details><summary>Q1：Pawn 死亡重生后，为什么 GAS 的冷却还在？</summary>
ASC 放在 PlayerState 上，PlayerState 属于 Controller，重生只换 Pawn，PlayerState 不变；新 Pawn 被 Possess 时在 PossessedBy / OnRep_PlayerState 里重新调用 InitAbilityActorInfo(PS, NewPawn) 把 Avatar 换成新身体即可。
</details>

<details><summary>Q2：其他玩家客户端上，想拿到某个 Character 的队伍号，怎么写？</summary>
`Character->GetPlayerState<AMyPlayerState>()->TeamId`。不能走 GetController()，因为别人的 PlayerController 不会复制到本机。
</details>

<details><summary>Q3：Possess 之后，Pawn 的网络角色发生了什么变化？</summary>
服务器上 PossessedBy 对 PlayerController 调用 SetAutonomousProxy(true)，拥有者客户端上该 Pawn 的 LocalRole 变成 AutonomousProxy，于是 CharacterMovement 走客户端预测 + ServerMove；其他客户端仍是 SimulatedProxy。被 AI 附身时保持 SimulatedProxy。
</details>

<details><summary>Q4：客户端在 Pawn 上调了一个 Server RPC，日志说 No owning connection，原因？</summary>
Pawn 还没被该客户端的 PlayerController Possess（Owner 为空），或者 Owner 是别的连接。Possess 后 Owner = PC 才能发。
</details>

<details><summary>Q5：一个 Controller 能同时 Possess 两个 Pawn 吗？</summary>
不能。Possess 新 Pawn 时会先 UnPossess 当前 Pawn；同理一个 Pawn 被新 Controller Possess 时，旧 Controller 会被 UnPossess。需要"一人控多单位"（RTS）时，通常是 PC 不 Possess 单位，而是给单位的 AIController 下命令。
</details>
