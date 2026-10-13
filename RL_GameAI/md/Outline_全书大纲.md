# 《游戏 AI 系统工程师面试：强化学习与机器学习理论基础》 · 单章

> 合并版：`../RL_GameAI_强化学习面试指南.md`　电子书：`../RL_GameAI.html`

## 本章提问记录

| 时间 | 原始提问（摘要） | 回答对应章节 |
|---|---|---|
| 2026-10-13 10:29 | 新增 AI 系统工程师（游戏 AI / 强化学习）岗位的面试准备：贴出 JD（NPC / AI 陪伴 / PVE AI / AI Bot；强化学习落地；引擎侧 AI 工具链；C++ + Python；Unreal 优先；AI 工具链与内容生成管线），希望带着复习强化学习与机器学习（核心：伏羲强化学习理论基础），最终生成电子书；本次先生成整个大纲，后续按大纲与提问逐章更新单章 md 与整本电子书，每章做好数学推理、文图并茂 | 第 0 章（JD 逐条拆解与权重、「伏羲 RL 理论基础」三层含义、算法族谱图、游戏 Bot 全景图、三轮复习路线、推导写法样张 + γ-压缩收敛计算图、统一符号表、后续推进方式）· 全书大纲（5 部分 21 章 + 5 附录；每章小节 / 推导 / 图解 / demo / 高频追问 / 前置 / JD 对标） |
| 2026-10-13 11:23 | 适当弱化 Part I、开始写 Part II | 大纲调整（Part I 改为“数学工具速查 / ML 面试速查”两章，数学工具改为在用到处以「工具箱」框就地讲；C++ MLP 挪到第 8 / 20 章）· **第 3 章**（RL vs 监督学习、交互循环与游戏帧循环、对线 MDP、γ 三层含义与有效视界、决策频率换算、终止 vs 截断、V / Q / A 及其转换、Bellman 期望方程与 backup 图、解析 / 迭代 / 蒙特卡洛三方对拍、Bellman 最优方程、8 个确定性策略穷举、γ 改变最优策略、T* 压缩与确定性最优策略存在性证明、POMDP 信念更新、MOBA / FPS 建模表）· **第 4 章**（迭代策略评估与 Sutton 4×4 网格、策略改进定理完整证明、策略迭代有限收敛与平局陷阱、价值迭代停止准则与贪心损失界、地牢地图 PI / VI / 滑步改变最优路线、γ 与扫描次数实测、规模化计时与“躲避格”、GPI 统一框架、DP 在游戏里的实际用途、手撕 VI / PI）；实测 `demos/ch03_mdp.cpp`、`demos/ch04_dp.cpp`、`demos/ch04_interview.py`，构建时 Python ⇄ C++ 自动对拍 |
| 2026-10-13 12:56 | 继续未完成的任务 | **第 5 章**（从 DP 到无模型、MC 首次 / 每次访问与增量均值、常数 α = 指数加权、TD(0) 期望更新与偏差来源、终止 vs 截断的自举处理、5 状态随机游走 MC vs TD 学习曲线、目标方差对比、批量更新：MC 收敛到样本均值 / TD 收敛到确定性等价解与 A/B 例子、n 步回报与误差缩减界、λ-回报与递推、TD 误差累加恒等式 ①②（即 GAE）、前向 = 后向视角证明、资格迹（累积 / 替换）与在线 TD(λ)、宽度 × 深度分类、手撕 MC / TD / TD(λ) / λ-回报）；实测 `demos/ch05_random_walk.cpp`、`demos/ch05_interview.py`，Python ⇄ C++ 公共随机数逐位对拍 |
| 2026-10-13 13:45 | 继续完成 Part III 和 Part IV 部分 | **第 8 章**（半梯度、致命三角与线性 TD 稳定性证明、DQN 回放 + 目标网络、最大化偏差与 Double DQN、Dueling 减均值、PER 的 IS 权重与 sum-tree、Rainbow、游戏 Bot 为什么少用 DQN）；新增公共头文件 `demos/rlcommon.h`（手写 MLP + Adam、CartPole、并行跑种子）；实测 `demos/ch08_dqn.cpp`、`demos/ch08_interview.py` |
| 2026-10-13 17:15 | 完成第 1–2 章（数学和 ML 速查）、第 6–7 章（Sarsa / Q-learning、探索与利用） | **第 1 章** RL 数学工具速查（全期望 / 全方差、重要性采样无偏与 e^{μ²} 二阶矩、普通 vs 加权 IS、KL ≥ 0 与前向 / 反向 KL、MLE ⇔ 交叉熵、MAP ⇔ 正则、得分函数 vs 重参数化、常用不等式、优化器）· **第 2 章** ML / DL 面试速查 20 问（偏差-方差分解实测、L1 稀疏 vs L2、Dropout、逻辑回归与 softmax+CE 梯度 p−y、SVM、RF vs GBDT、AUC 秩和、反传与梯度消失实测、残差、BN vs LN、√d 缩放实测、Transformer / LSTM、学习率、大模型）· **第 6 章** Sarsa / Expected Sarsa / Q-learning（ε-greedy 改进定理、Cliff Walking 路线与在线回报、α 扫描、GLIE 的有限训练表现、off-policy MC 重要性采样与无穷方差例子、最大化偏差随动作数增长与 Double Q）· **第 7 章**探索与利用（遗憾分解、10 臂测试平台、Hoeffding → UCB1、Thompson 采样、梯度老虎机 = 期望奖励 SGD、参数研究、累积遗憾与 Lai-Robbins 下界、链式 MDP 深度探索、RND / ICM / Go-Explore、噪声电视）；新增 gpath 网格路径图宏 |
| 2026-10-13 20:12 | 完成 Part V | **第 19 章**分布式训练（采样瓶颈、A3C → IMPALA → SEED RL 演进、V-trace 不动点推导与表格实测、多线程 actor-learner 吞吐 / learner 饱和 / 策略滞后与 Little 定律、滞后 × V-trace 的 CartPole 实验、参数同步与复现性）· **第 20 章** UE 集成（UE AI 栈、训练环境化清单、Learning Agents 组件与训练闭环、NNE 三种运行时接口、GEMV vs GEMM 访存 / 计算比与 roofline、逐个 / 合批 / AVX2 推理实测、int8 量化与“忘了归一化”、AI LOD 与错峰帧预算、行为树 + RL 子树、联机、给 UE 双人射击 Demo 加 RL 僵尸的方案）· **第 21 章**工具链（AI 生产管线、实验管理、Wald / Wilson / 精确区间覆盖率、所需局数、偷看的假阳性与 SPRT、配对设计、调试面板设计、策划编辑器工具、LLM NPC 架构与安全、伏羲 AOP 实践、测试与平衡 Bot） |

# 大纲 · 全书大纲：5 部分 · 21 章 · 5 个附录

*每章列出：小节、要做的数学推导、图解、实测 demo、高频追问、前置章节和对标的 JD 条目。写作时按这份大纲推进；你追问的内容会补进对应章节，大纲同步标注。*

## 大纲总览

| 章 | 标题 | 优先级 | 代表性推导 | 主要 demo | 状态 |
|---|---|---|---|---|---|
| 0 | 导读：JD 拆解、知识地图与复习路线 | — | Bellman 期望方程样张、γ-压缩 | 构建时计算 | 已写 |
| **Part I · 数学与 ML 速查（已弱化：按需查阅，不单独精讲）** |  |  |  |  |  |
| 1 | RL 数学工具速查 | ★ | 全期望公式、IS 无偏、KL≥0（其余就地讲） | — | 已写（速查） |
| 2 | ML / DL 面试速查（问答式） | ★★ | softmax+CE 梯度 $p-y$、√d 缩放 | — | 已写（速查） |
| **Part II · 强化学习理论基础** |  |  |  |  |  |
| 3 | RL 问题建模：MDP、回报与价值函数 | ★★★★ | Bellman 期望 / 最优方程、压缩映射 | 解析解 / 迭代 / 模拟三方对拍 | 已写 |
| 4 | 动态规划：策略迭代与价值迭代 | ★★★ | 策略改进定理、VI 误差界 | GridWorld PI / VI | 已写 |
| 5 | 无模型预测：蒙特卡洛与时序差分 | ★★★ | TD 误差累加 = MC 误差、λ-回报 | Random Walk MC vs TD | 已写 |
| 6 | 无模型控制：Sarsa、Q-learning 与 off-policy | ★★★★ | ε-greedy 改进、最大化偏差 | Cliff Walking、Double Q | 已写 |
| 7 | 探索与利用：从多臂老虎机到内在动机 | ★★ | Hoeffding → UCB | 10 臂测试平台 | 已写 |
| **Part III · 深度强化学习** |  |  |  |  |  |
| 8 | 价值函数近似与 DQN 家族 | ★★★★ | 半梯度、Double DQN、PER 权重 | numpy DQN / sum-tree | 已写 |
| 9 | 策略梯度 | ★★★★ | 策略梯度定理、baseline 无偏 | REINFORCE ± baseline | 已写 |
| 10 | Actor-Critic 与 GAE | ★★★★ | $\mathbb{E}[\delta]=A$、GAE 闭式与递推 | GAE C++ / Python 对拍 | 已写 |
| 11 | 信任域与 PPO | ★★★★ | 性能差异引理、自然梯度、clip 四象限 | numpy PPO + 消融 | 已写 |
| 12 | 连续控制与最大熵：DDPG / TD3 / SAC | ★★ | DPG、soft Bellman、tanh 雅可比 | Pendulum（可选） | 已写 |
| 13 | RLHF、DPO 与 GRPO | ★★★★ | BT 模型、KL 正则闭式解、DPO、GRPO | 玩具偏好任务 | 已写 |
| **Part IV · 游戏 AI 专题** |  |  |  |  |  |
| 14 | 模仿学习、离线 RL 与拟人化 | ★★★ | BC 复合误差、GAIL 判别器最优解 | BC vs DAgger | 已写 |
| 15 | 多智能体 RL 与自博弈 | ★★★ | 纳什均衡、Elo、QMIX 单调性 | 石头剪刀布自博弈、Elo | 已写 |
| 16 | 奖励设计、课程学习与分层 RL | ★★★ | 势函数塑形不改最优策略 | 塑形 vs 刷分环路 | 已写 |
| 17 | 规划与搜索：MCTS 到 AlphaZero / MuZero | ★★ | UCT / PUCT、AlphaZero 损失 | 井字棋 MCTS | 已写 |
| 18 | 游戏 AI 系统设计：从需求到 Bot | ★★★★ | 多头动作 log-prob、掩码 softmax | 动作头数值校验 | 已写 |
| **Part V · 工程落地与工具链** |  |  |  |  |  |
| 19 | 分布式训练系统与样本效率 | ★★ | V-trace | actor-learner 吞吐 | 已写 |
| 20 | 引擎侧集成：UE 中的 AI 与 ML | ★★★ | 合批推理代价模型 | C++ 推理：逐个 / 合批 / SIMD | 已写 |
| 21 | AI 工具链、内容生成管线与调试 | ★★ | 胜率置信区间与评测局数 | 评测局数计算 | 已写 |
| 附录 | A 追问 100 题 · B 公式卡 · C 手撕代码 · D 论文清单 · E 项目叙事 | ★★★ | — | 手撕代码全部编译运行 | 待写 |

优先级：★★★★ = 面经高频 / 必须能手推；★★★ = 高频；★★ = 加分项。

## Part I · 数学与 ML 速查（已弱化）

> **调整说明（2026-10-13 第二次提问）**
>
> 按你的要求弱化 Part I：不再单独精讲概率论、优化和深度学习，改成两张“速查章”。真正要用的数学工具在 Part II~III 用到的地方以 **「工具箱」框**就地讲清（例如第 3 章讲全期望公式、第 6 章讲 Jensen、第 7 章讲 Hoeffding、第 9 章讲得分函数、第 11 章讲 KL 与 Fisher、第 13 章讲 MLE 与 Bradley-Terry）。原计划放在第 2 章的 C++ 手写 MLP 挪到第 8 章（numpy 版）和第 20 章（C++ 推理版），网络结构选型（LSTM / Transformer / 实体编码）挪到第 18 章。章节编号不变。

### 第 1 章 · RL 数学工具速查（★ 速查）

> 一句话：一页纸列出全书推导用到的所有数学工具，每条给出“结论 + 在哪章用到”，需要时回来查；完整推导只保留三条最基础的。

**速查条目**

1. 条件期望、全期望公式、全方差公式
2. 重要性采样（普通 / 加权）与方差
3. 熵、交叉熵、KL；前向 / 反向 KL
4. MLE ⇔ 交叉熵；MAP ⇔ 正则化
5. 得分函数估计 vs 重参数化
6. 常用不等式：Jensen、Hoeffding、Cauchy-Schwarz
7. 优化器一句话：SGD / Momentum / Adam / AdamW、梯度裁剪

**保留的推导**

- 全期望公式 $\mathbb{E}[X]=\mathbb{E}[\mathbb{E}[X\mid Y]]$
- 重要性采样无偏性
- $D_{\mathrm{KL}}(p\Vert q)\ge0$（Jensen）

**高频追问（30 秒答案）**

- KL 为什么不对称？RLHF 用的是哪个方向？
- 交叉熵和极大似然什么关系？
- RL 里哪些地方用到重要性采样？（off-policy MC、PPO 比值、PER、V-trace）

*前置：无 · 对标 JD：要求 2 · 状态：**已写（速查）**（2026-10-13） · demo：`demos/ch01_math.py` · 篇幅：约为原计划的三分之一*

### 第 2 章 · ML / DL 面试速查（问答式）（★★ 速查）

> 一句话：AI 岗面试绕不开的 ML / DL 八股，用“问题 → 30 秒答案 → 一个关键公式”的问答形式列出 20 题左右。

**问答清单**

1. 偏差-方差分解；过拟合 / 欠拟合的手段
2. L1 / L2 / Dropout / 早停
3. 逻辑回归与 softmax + 交叉熵的梯度
4. 反向传播；梯度消失 / 爆炸；残差
5. BN vs LN（为什么 RL / Transformer 多用 LN）
6. 注意力为什么除以 $\sqrt{d_k}$
7. GBDT vs 随机森林；评估指标（P / R / F1 / AUC）

**保留的推导**

- softmax + 交叉熵的梯度 $p-y$（第 9 章 softmax 策略梯度直接复用）
- 缩放点积注意力的方差论证

*前置：无 · 对标 JD：要求 2 · 状态：**已写（速查）**（2026-10-13） · demo：`demos/ch02_ml.py` · 挪出：C++ MLP → 第 8 / 20 章，网络结构选型 → 第 18 章*

## Part II · 强化学习理论基础（伏羲 RL 面试核心）

### 第 3 章 · RL 问题建模：MDP、回报与价值函数（★★★★ 必考）

> 一句话：把“玩游戏”写成 MDP，定义回报和价值，推出 Bellman 方程，并证明它有唯一解。整本书的地基。

**小节**

1. RL 与监督学习的本质区别：交互、延迟奖励、非 i.i.d.、探索
2. MDP 五元组与马尔可夫性；MRP；POMDP 与信念状态（战争迷雾）
3. 回报与折扣：γ 的三重含义、有效视界、回合制 vs 持续任务
4. 策略与价值：$V_\pi$、$Q_\pi$、$A_\pi$ 及相互转换
5. Bellman 期望方程（V / Q 两种形式、backup 图）与矩阵解
6. 最优价值、Bellman 最优方程、确定性最优策略的存在性
7. Bellman 算子的 γ-压缩性与不动点定理
8. 建模练习：MOBA 对线、FPS 对枪写成 MDP

**数学推导**

- $G_t=R_{t+1}+\gamma G_{t+1}$ 与 Bellman 期望方程（全期望公式 + 马尔可夫性）
- $V_\pi(s)=\sum_a\pi(a\mid s)Q_\pi(s,a)$，$Q_\pi(s,a)=\sum_{s',r}p(s',r\mid s,a)[r+\gamma V_\pi(s')]$
- 矩阵形式 $V_\pi=(I-\gamma P_\pi)^{-1}R_\pi$ 与可逆性
- $T^\pi$、$T^*$ 在 ∞-范数下是 γ-压缩（含 $|\max_a f-\max_a g|\le\max_a|f-g|$）
- 奖励有界时 $|V|\le R_{\max}/(1-\gamma)$

**图解**

- 智能体-环境交互环（映射到游戏帧循环）
- V 与 Q 的 backup 图
- 游戏 MDP 示例状态转移图
- γ-压缩收敛曲线（构建时计算，见图 0-4）

**实测 Demo**

- `ch03_mdp.cpp`：同一个 MDP 的解析解 / 迭代解 / 蒙特卡洛模拟三方对拍

**高频追问**

- γ 为什么要小于 1？等于 1 行不行？
- V 和 Q 的区别，怎么互相转换？
- 手推 Bellman 方程。为什么最优策略可以是确定性的（多智能体呢）？
- 观测不完全怎么办？（帧堆叠、RNN、信念状态）

*前置：无（全期望公式就地讲） · 对标 JD：要求 2 · 状态：**已写**（2026-10-13） · demo：`demos/ch03_mdp.cpp`*

### 第 4 章 · 动态规划：策略评估、策略迭代与价值迭代（★★★ 高频）

> 一句话：已知模型时怎么求最优策略；策略改进定理是后面所有“评估—改进”循环（GPI）的理论依据。

**小节**

1. 迭代策略评估：同步更新 vs 原地更新
2. 策略改进定理
3. 策略迭代及其收敛性
4. 价值迭代 = 截断的策略迭代
5. 广义策略迭代（GPI）
6. 异步 DP、优先扫描；维度灾难与 DP 的局限 → 引出无模型方法

**数学推导**

- 策略改进定理：$Q_\pi(s,\pi'(s))\ge V_\pi(s)\ \forall s\Rightarrow V_{\pi'}\ge V_\pi$（逐步展开证明）
- 策略迭代有限步收敛（单调 + 确定性策略有限）
- 价值迭代误差界 $\|V_k-V_*\|_\infty\le\gamma^k\|V_0-V_*\|_\infty$ 与停止准则
- 贪心策略的次优性界 $\|V_{\pi_{\text{greedy}}}-V_*\|_\infty\le\frac{2\gamma\epsilon}{1-\gamma}$

**图解**

- GridWorld 价值热力图 + 策略箭头的逐轮演化（构建时真实运行）
- PI vs VI 的迭代次数与耗时对比（实测）

**实测 Demo**

- `ch04_gridworld.cpp`：PI / VI / 原地 VI

**高频追问**

- 策略迭代和价值迭代的区别？哪个更快？
- 价值迭代为什么也能收敛到最优？
- DP 为什么在游戏里很少直接用？

*前置：Ch3 · 对标 JD：要求 2 · 状态：**已写**（2026-10-13） · demo：`demos/ch04_dp.cpp`*

### 第 5 章 · 无模型预测：蒙特卡洛与时序差分（★★★ 高频）

> 一句话：不知道模型，就用采样估计价值。MC 等回合结束、无偏但方差大；TD 用“猜测更新猜测”、有偏但方差小。GAE 的数学基础就在这里。

**小节**

1. 蒙特卡洛预测：首次访问 / 每次访问、增量均值
2. TD(0)：自举、TD 目标、TD 误差
3. MC vs TD：偏差-方差、是否利用马尔可夫性、批量更新下的不同解（确定性等价）
4. n 步 TD 与 λ-回报
5. TD(λ)：前向视角 vs 后向视角、资格迹
6. DP / MC / TD 统一视角（采样 × 自举）

**数学推导**

- 增量均值 $V\leftarrow V+\tfrac1N(G-V)$ 与常数步长的指数加权含义
- MC 目标无偏、TD 目标在 $V$ 不准时有偏
- **TD 误差累加 = MC 误差**：$G_t-V(S_t)=\sum_{k\ge0}\gamma^k\delta_{t+k}$（GAE 的基础）
- λ-回报 $G^\lambda_t=(1-\lambda)\sum_{n\ge1}\lambda^{n-1}G^{(n)}_t$ 及其递推
- 前向 / 后向视角的离线等价

**图解**

- DP / MC / TD 三种 backup 对比
- Random Walk 上 MC vs TD 的 RMS 学习曲线（实测，多 α、多随机种子）
- n 步与 λ 的 U 型误差曲线（实测）

**实测 Demo**

- `ch05_random_walk.cpp`：MC / TD / n 步 / TD(λ) / 批量
- `ch05_interview.py`：手撕代码对拍

**高频追问**

- MC 和 TD 的区别？TD 为什么有偏，偏差来自哪里？
- λ 起什么作用？资格迹是什么？

*前置：Ch3 · 对标 JD：要求 2 · 状态：**已写**（2026-10-13） · demo：`demos/ch05_random_walk.cpp`*

### 第 6 章 · 无模型控制：Sarsa、Q-learning 与 off-policy（★★★★ 必考）

> 一句话：在评估的同时改进策略。Sarsa 学“自己正在用的策略”，Q-learning 学“贪心策略”；这一字之差决定了 on / off-policy、悬崖边的路线和 Q 值高估。

**小节**

1. MC 控制：探索性开端、ε-greedy、GLIE
2. Sarsa 与 Expected Sarsa
3. Q-learning 及其收敛条件（Robbins-Monro 步长、无限次访问）
4. on-policy vs off-policy：行为策略 / 目标策略、重要性采样修正
5. 最大化偏差与 Double Q-learning
6. Cliff Walking：Sarsa 走安全路，Q-learning 贴着悬崖走

**数学推导**

- ε-greedy 策略改进定理（在 ε-soft 策略类中仍单调改进）
- Sarsa / Expected Sarsa / Q-learning 更新目标的对比
- 为什么一步 Q-learning 不需要重要性采样
- off-policy MC 的比值 $\rho_{t:T-1}=\prod_{k=t}^{T-1}\frac{\pi(A_k\mid S_k)}{b(A_k\mid S_k)}$
- 最大化偏差 $\mathbb{E}[\max_a\hat Q(a)]\ge\max_a\mathbb{E}[\hat Q(a)]$（Jensen）与 Double Q 的修正

**图解**

- Cliff Walking 上两种算法学到的路径（构建时真实运行）
- 每回合回报曲线（实测）
- 最大化偏差实验（Sutton 例 6.7）的“走错方向”比例曲线（实测）

**实测 Demo**

- `ch06_control.cpp`：Cliff Walking 三算法 / α 扫描 / GLIE / 普通 vs 加权 IS / ε-greedy 改进定理 / 最大化偏差 vs 动作数
- `ch06_interview.py`：与 C++ 公共随机数逐回合对拍（Sutton 例 6.7 的回合曲线见第 8 章 E1）

**高频追问**

- Sarsa 和 Q-learning 的区别？为什么 Q-learning 是 off-policy？
- Q-learning 为什么会高估？怎么缓解？
- on-policy 和 off-policy 各自的优缺点？
- Q 表太大怎么办？（→ 第 8 章函数近似）

*前置：Ch4 · Ch5 · 对标 JD：要求 2 · 状态：**已写**（2026-10-13）*

### 第 7 章 · 探索与利用：从多臂老虎机到内在动机（★★ 加分）

> 一句话：探索是 RL 独有的问题。老虎机给出最干净的理论（UCB、遗憾界），深度 RL 用熵、噪声和“好奇心”把它扩展到大状态空间。

**小节**

1. 多臂老虎机与累积遗憾（regret）
2. ε-greedy、衰减 ε、乐观初始值
3. UCB1
4. Thompson Sampling
5. 梯度老虎机（最简单的策略梯度）
6. 深度 RL 的探索：熵正则、噪声网络、计数奖励、RND、ICM、Go-Explore
7. 游戏场景：地图探索、“噪声电视”问题

**数学推导**

- Hoeffding 不等式 → UCB 置信半径 $\sqrt{2\ln t/N_a}$
- 遗憾分解 $\mathcal{R}_T=\sum_a\Delta_a\,\mathbb{E}[N_a(T)]$
- 梯度老虎机的更新 = 期望奖励的随机梯度上升（策略梯度预告）

**图解**

- 10 臂测试平台：平均奖励 / 最优动作比例曲线（实测，2000 次独立运行平均）
- UCB 置信区间随拉动次数收缩的示意

**实测 Demo**

- `ch07_bandit.cpp`：10 臂测试平台 / 参数研究 / 伯努利累积遗憾 / Hoeffding 检验 / 链式 MDP 深度探索
- `ch07_interview.py`

**高频追问**

- 探索与利用怎么平衡？UCB 公式每一项的含义？
- RND 为什么能鼓励探索？
- PPO 的熵正则系数怎么调？

*前置：Ch1 · Ch6 · 对标 JD：要求 2 · 状态：**已写**（2026-10-13）*

## Part III · 深度强化学习

### 第 8 章 · 价值函数近似与 DQN 家族（★★★★ 必考）

> 一句话：用神经网络近似 Q 表会遇到“致命三角”而发散；DQN 用经验回放和目标网络把它稳住，后续改进各自修一个具体毛病。

**小节**

1. 价值函数近似：线性 / 神经网络；半梯度 TD
2. 致命三角：函数近似 + 自举 + off-policy
3. DQN：经验回放、目标网络、Huber 损失、帧堆叠
4. 改进：Double DQN、Dueling、PER、n-step、Noisy Net、C51 / QR-DQN、Rainbow
5. 大离散 / 连续动作的困境与动作分解
6. 工程：环形回放缓冲区、sum-tree

**数学推导**

- 半梯度为什么是“半”：目标里的 $\hat V(S',w)$ 不求导
- DQN 损失 $L=\mathbb{E}\big[(r+\gamma\max_{a'}Q_{\bar w}(s',a')-Q_w(s,a))^2\big]$
- Double DQN 目标 $r+\gamma\,Q_{\bar w}\big(s',\operatorname*{arg\,max}_{a'}Q_w(s',a')\big)$
- Dueling 聚合中减去均值的可辨识性
- PER：$P(i)=p_i^\alpha/\sum_k p_k^\alpha$ 与 IS 权重 $w_i=(N\cdot P(i))^{-\beta}$ 的来由

**图解**

- DQN 训练数据流：在线网络 / 目标网络 / 回放缓冲区
- sum-tree 结构与一次采样过程（构建时生成）
- DQN vs Double DQN 的 Q 值高估曲线（实测）

**实测 Demo**

- `ch08_dqn.cpp`：手写 MLP 梯度检查 / 最大化偏差 / Baird 反例 / sum-tree / CartPole DQN·无目标网络·Double DQN
- `ch08_interview.py`：sum-tree + PER + DQN 目标

**高频追问**

- DQN 的两大技巧分别解决什么问题？
- 为什么需要目标网络？软更新和硬更新的区别？
- Double DQN 为什么能缓解高估？PER 为什么要做 IS 修正？Dueling 为什么要减均值？

*前置：Ch6（MLP 与反传在本章就地补） · 对标 JD：要求 2 / 3 · 状态：**已写**（2026-10-13）*

### 第 9 章 · 策略梯度（★★★★ 必考）

> 一句话：不学价值、直接对策略参数求梯度。策略梯度定理告诉我们：梯度里不需要环境模型，只需要 $\nabla\log\pi$ 乘上回报。

**小节**

1. 为什么直接优化策略：随机策略、连续动作、平滑性
2. 目标函数与轨迹概率
3. 策略梯度定理
4. REINFORCE、reward-to-go（因果性）
5. baseline：无偏性与最优 baseline
6. 常见参数化：softmax 策略、高斯策略
7. 与监督学习的关系：“按回报加权的极大似然”

**数学推导**

- log-derivative trick
- **策略梯度定理完整推导**：轨迹形式（环境动力学项求导后消失）→ 状态分布形式
- reward-to-go 的合法性：过去的奖励与未来动作的 score 乘积期望为 0
- baseline 无偏：$\mathbb{E}_{a\sim\pi}[\nabla_\theta\log\pi_\theta(a\mid s)\,b(s)]=0$；方差最小的 baseline
- softmax / 高斯策略的 score function

**图解**

- 策略梯度的直觉：抬高好轨迹的概率
- 有 / 无 baseline 的梯度方差与学习曲线（实测）

**实测 Demo**

- `ch09_pg.cpp`：策略梯度定理 vs 有限差分 / 四种估计量的偏差方差 / CartPole REINFORCE ± 基线
- `ch09_interview.py`

**高频追问**

- 手推策略梯度定理。
- baseline 为什么不引入偏差？
- 策略梯度为什么是 on-policy？
- value-based 和 policy-based 各适合什么场景？

*前置：Ch3（得分函数就地讲）· 对标 JD：要求 2 · 状态：**已写**（2026-10-13）*

### 第 10 章 · Actor-Critic 与 GAE（★★★★ 必考）

> 一句话：用 critic 估计的优势代替整条回报来降方差；GAE 用一个 λ 在“一步 TD”和“蒙特卡洛”之间连续调节偏差与方差。

**小节**

1. 用 critic 代替回报：QAC、A2C
2. TD 误差是优势函数的无偏估计
3. A3C（异步去相关）与 A2C（同步批量）
4. GAE(γ, λ)
5. 网络结构：共享 vs 分离、value loss 系数、熵正则
6. actor-critic 的偏差来源

**数学推导**

- $\mathbb{E}[\delta_t\mid S_t=s,A_t=a]=A_\pi(s,a)$（critic 准确时）
- k 步优势 $\hat A^{(k)}_t=\sum_{l=0}^{k-1}\gamma^l\delta_{t+l}$
- GAE 闭式 $\hat A^{\text{GAE}}_t=\sum_{l\ge0}(\gamma\lambda)^l\delta_{t+l}$ 的推导；λ=0 / λ=1 两端
- 倒序递推 $\hat A_t=\delta_t+\gamma\lambda(1-d_{t+1})\hat A_{t+1}$（含回合结束与截断的处理）

**图解**

- A2C / A3C 架构对比
- λ 对偏差-方差的影响（实测）
- GAE 倒序递推的逐步计算图（构建时生成）

**实测 Demo**

- `ch10_ac_gae.cpp`：GAE 逐步样例 / 偏差-方差解析计算 / CartPole A2C × λ
- `ch10_interview.py`：手撕 GAE，与 C++ 逐项对拍

**高频追问**

- critic 的作用是什么？
- 手写 GAE。λ=0 和 λ=1 分别是什么？
- done 和 truncated 在 GAE 里怎么处理？
- A3C 为什么后来被 A2C / PPO 取代？

*前置：Ch5 · Ch9 · 对标 JD：要求 2 / 3 · 状态：**已写**（2026-10-13）*

### 第 11 章 · 信任域与 PPO（面试第一高频）（★★★★ 必考）

> 一句话：策略一步走太大就会崩。TRPO 用 KL 约束限制步长，PPO 用 clip 达到类似效果且实现简单，是游戏 Bot 训练的事实标准。

**小节**

1. 步长困境：小步太慢、大步崩溃
2. 性能差异引理
3. 替代目标与单调改进下界（TRPO 定理）
4. TRPO：KL 约束、自然梯度、Fisher 矩阵、共轭梯度 + 线搜索
5. PPO-Clip：目标逐情形分析、为什么取 min；PPO-Penalty（自适应 β）
6. 完整算法与工程细节：GAE、优势归一化、value clipping、多 epoch × minibatch、观测 / 奖励归一化、正交初始化、学习率退火、梯度裁剪、KL early stop、**动作掩码**
7. 训练诊断：approx KL、clipfrac、熵、explained variance
8. PPO 到底是 on-policy 还是 off-policy

**数学推导**

- 性能差异引理 $J(\pi')-J(\pi)=\frac{1}{1-\gamma}\mathbb{E}_{s\sim d^{\pi'},\,a\sim\pi'}[A_\pi(s,a)]$
- 用旧状态分布 + IS 得到替代目标 $L_\pi(\pi')$，一阶近似成立
- KL 的二阶近似 = Fisher 矩阵 → 自然梯度 $F^{-1}g$ 与步长 $\sqrt{2\delta/(g^\top F^{-1}g)}$
- PPO-clip 目标对 $r_t(\theta)$ 的梯度：A&gt;0 / A&lt;0 × 比值在区间内 / 外，四种情形
- approx KL 估计 $(r-1)-\log r$ 的无偏性与非负性
- 动作掩码把 logits 置为 −∞ 后梯度仍正确

**图解**

- clip 目标 $L(r)$ 曲线（A&gt;0 / A&lt;0，构建时计算）
- TRPO 信任域示意
- PPO 一次迭代的数据流
- 训练诊断指标曲线（实测）

**实测 Demo**

- `ch11_ppo.cpp`：性能差异引理 / 自然梯度 = 优势 / TRPO 步长 / KL 估计量 / PPO 消融与诊断
- `ch11_interview.py`：PPO-clip + 掩码 + k3

**高频追问**

- PPO 为什么要 clip？为什么还要取 min？
- PPO 和 TRPO 的区别？PPO 是 on-policy 吗？
- 训练崩了怎么排查，看哪些曲线？
- 无效动作怎么屏蔽？多头动作的 log-prob 怎么算？

*前置：Ch1（KL、IS）· Ch10 · 对标 JD：职责 2、要求 2 · 状态：**已写**（2026-10-13）*

### 第 12 章 · 连续控制与最大熵 RL：DDPG / TD3 / SAC（★★ 加分）

> 一句话：连续动作下用确定性策略梯度（DDPG），用双 Q 和延迟更新修高估（TD3），用最大熵让探索和鲁棒性内建在目标里（SAC）。

**小节**

1. 确定性策略梯度定理（DPG）
2. DDPG：目标网络软更新、探索噪声
3. TD3：截断双 Q、延迟策略更新、目标策略平滑
4. 最大熵 RL：目标、soft V / Q、soft Bellman
5. SAC：重参数化、tanh 压缩的 log-prob 修正、自动温度
6. 离散 SAC；SAC 与 PPO 怎么选

**数学推导**

- DPG：$\nabla_\theta J=\mathbb{E}_s\big[\nabla_a Q(s,a)\big|_{a=\mu_\theta(s)}\nabla_\theta\mu_\theta(s)\big]$
- soft Bellman 方程
- 最大熵最优策略 $\pi^*(a\mid s)\propto\exp\big(Q^*(s,a)/\alpha\big)$
- tanh 变换的雅可比修正 $\log\pi(a\mid s)=\log\mu(u\mid s)-\sum_i\log(1-\tanh^2u_i)$
- 温度 α 的对偶目标

**图解**

- DDPG / TD3 / SAC 结构对比
- 温度对 $\text{softmax}(Q/\alpha)$ 策略分布的影响（构建时计算）

**实测 Demo**

- `ch12_sac.cpp`：tanh 修正 / actor 利用 critic 误差 / 最大熵老虎机 + 自动温度
- `ch12_interview.py`

**高频追问**

- DDPG 为什么会高估？TD3 的三个技巧分别解决什么？
- SAC 中熵起什么作用？α 怎么自动调？
- 游戏 Bot 为什么多用 PPO 而不是 SAC？

*前置：Ch8 · Ch10 · 对标 JD：要求 2 · 状态：**已写**（2026-10-13）*

### 第 13 章 · RLHF、DPO 与 GRPO：大模型时代的策略优化（★★★★ 必考）

> 一句话：同一套策略优化理论用在大模型上：RLHF 用 PPO + KL 惩罚，DPO 把奖励模型消掉变成分类损失，GRPO 用组内平均回报替代 critic。伏羲面经原题“讲一下 PPO、GRPO”。

**小节**

1. RLHF 三阶段：SFT → 奖励模型 → PPO + KL 惩罚
2. KL 正则目标的闭式最优解
3. DPO：把奖励模型“消掉”
4. GRPO：组采样、组内标准化优势、去掉 critic、KL 写进损失
5. PPO / DPO / GRPO 对比与选型
6. 游戏中的用法：RLHF 微调游戏 Bot（伏羲分享）、LLM NPC 的偏好对齐

**数学推导**

- Bradley-Terry 偏好模型的 MLE 损失 $-\log\sigma\big(r(x,y_w)-r(x,y_l)\big)$
- $\max_\pi\mathbb{E}[r]-\betaD_{\mathrm{KL}}(\pi\Vert\pi_{\text{ref}})$ 的最优解 $\pi^*\propto\pi_{\text{ref}}\exp(r/\beta)$
- DPO 损失 $-\log\sigma\Big(\beta\log\frac{\pi_\theta(y_w\mid x)}{\pi_{\text{ref}}(y_w\mid x)}-\beta\log\frac{\pi_\theta(y_l\mid x)}{\pi_{\text{ref}}(y_l\mid x)}\Big)$
- GRPO 优势 $\hat A_i=\big(r_i-\text{mean}(r)\big)/\text{std}(r)$ 与目标函数
- k3 估计 $\frac{\pi_{\text{ref}}}{\pi_\theta}-\log\frac{\pi_{\text{ref}}}{\pi_\theta}-1$ 的无偏性与非负性

**图解**

- RLHF 流水线
- PPO（策略 / 参考 / 奖励 / 价值四个模型）vs GRPO（去 critic）结构对比
- 组内优势计算示意

**实测 Demo**

- `ch13_rlhf.cpp`：BT 奖励模型 / 闭式解与奖励-KL 前沿 / RLHF-PPO vs GRPO vs DPO
- `ch13_interview.py`

**高频追问**

- 讲一下 PPO 和 GRPO（面经原题）。
- GRPO 为什么可以不要 critic？代价是什么？
- DPO 为什么不需要奖励模型？
- KL 惩罚起什么作用？β 大小有什么影响？

*前置：Ch1（KL、MLE）· Ch11 · 对标 JD：要求 2（业界主流算法） · 状态：**已写**（2026-10-13）*

## Part IV · 游戏 AI 专题

### 第 14 章 · 模仿学习、离线 RL 与拟人化（★★★ 高频）

> 一句话：RL 能练出超人 Bot，但“像人”要靠人类数据。BC / DAgger / GAIL 学人，离线 RL 用日志数据训练，RL + IL 混合是伏羲做拟人化与风格化的核心思路。

**小节**

1. 行为克隆（BC）与分布漂移
2. DAgger
3. 逆强化学习与最大熵 IRL（概要）
4. GAIL：占用度量匹配 ↔ GAN
5. 离线 RL：外推误差、BCQ / CQL / IQL / TD3+BC（概要）
6. RL + IL：BC 预训练 + RL 微调、KL 约束到人类策略、BC+RL 损失自动调权、自模仿学习（SIL）做风格化
7. 拟人化评估：行为分布匹配、多样性、竞技性、领域指标 + 图灵测试

**数学推导**

- BC 的复合误差 $O(\epsilon T^2)$ 与 DAgger 的 $O(\epsilon T)$
- GAIL 目标与判别器最优解 $D^*=\frac{\rho_E}{\rho_E+\rho_\pi}$
- CQL 保守项的直觉
- 混合损失 $L=L_{\text{RL}}+w\,L_{\text{BC}}$ 中 $w$ 的规则化调节（按胜率阈值升降）

**图解**

- BC 误差累积示意（实测漂移）
- 拟人化雷达图：RL vs IL vs 混合
- 风格化：样本筛选 + 自模仿流程

**实测 Demo**

- `ch14_imitation.cpp`：复合误差 / 迷宫 BC vs DAgger / 离线 RL 外推误差
- `ch14_interview.py`

**高频追问**

- BC 的问题是什么？DAgger 怎么解决？
- 怎么让 Bot 像人？拟人化怎么量化评估？
- 怎么做多风格 Bot？

*前置：Ch2 · Ch11 · 对标 JD：职责 1 / 2 · 状态：**已写**（2026-10-13）*

### 第 15 章 · 多智能体 RL 与自博弈（★★★ 高频）

> 一句话：对抗游戏的 Bot 几乎都靠自博弈训练；难点是策略循环和非平稳性，解决办法是“和过去的自己、和一群对手打”，用 Elo 衡量强度。

**小节**

1. 随机博弈、纳什均衡、零和 / 合作 / 混合
2. MARL 的难点：非平稳、信用分配、维度爆炸；CTDE 范式
3. 值分解：IQL → VDN → QMIX（IGM 与单调性约束）
4. 策略方法：MADDPG、IPPO / MAPPO
5. 自博弈：朴素自博弈的策略循环、FSP、PFSP、种群训练、AlphaStar League、OpenAI Five、腾讯绝悟
6. 评估：Elo、TrueSkill、可利用度
7. 难度分级与匹配：checkpoint 池、温度、反应延迟、观测噪声

**数学推导**

- 石头剪刀布的纳什均衡（混合策略的无差异条件）
- Elo 期望胜率 $E_A=1/\big(1+10^{(R_B-R_A)/400}\big)$ 与更新公式的 logistic 回归解释
- QMIX 单调约束 $\partial Q_{\text{tot}}/\partial Q_i\ge0\Rightarrow$ IGM
- FSP 平均策略收敛的直觉

**图解**

- 石头剪刀布：朴素自博弈的策略循环 vs FSP 收敛（实测轨迹）
- AlphaStar League 结构；CTDE 结构
- Elo 收敛模拟（实测）

**实测 Demo**

- `ch15_selfplay.cpp`：石头剪刀布自博弈 vs FSP / Elo 的 K / 非传递性
- `ch15_interview.py`

**高频追问**

- 自博弈为什么会策略循环？怎么缓解？
- MARL 的非平稳性怎么理解？QMIX 的原理与局限？
- Elo 怎么算？怎么做出不同难度的 AI？

*前置：Ch11 · Ch17（自对弈）· 对标 JD：职责 1 / 2 · 状态：**已写**（2026-10-13）*

### 第 16 章 · 奖励设计、课程学习与分层 RL（★★★ 高频）

> 一句话：奖励决定 Bot 学成什么样。势函数塑形能加速学习且不改最优策略；乱加的塑形奖励会被“刷分”；稀疏奖励靠课程、重标记和分层来解决。

**小节**

1. 奖励设计原则：稀疏 vs 稠密；reward hacking（篮球 Bot 互相传球不进攻）
2. 势函数奖励塑形
3. 伏羲 NeurIPS 2020：学习如何利用塑形奖励（双层优化，概要）
4. 稀疏奖励：HER、课程学习、自动课程
5. 分层 RL：options、SMDP、宏动作（技能 / 连招）
6. 多目标奖励与风格：奖励权重 → 风格；多目标进化 RL（伏羲 IJCAI 2020 EMOGI）

**数学推导**

- **势函数塑形保持最优策略**：$F=\gamma\Phi(s')-\Phi(s)$ 时 $Q'_*(s,a)=Q_*(s,a)-\Phi(s)$（逐步证明）
- 非势函数塑形产生“正奖励环路”的反例
- options 的 SMDP Bellman 方程

**图解**

- 势函数塑形 vs 普通塑形下的 GridWorld 策略（构建时运行）
- HER 重标记示意
- 分层策略结构

**实测 Demo**

- `ch16_shaping.cpp`：势函数 vs 朴素塑形 / Q-learning 学习曲线 / 等价于换初值
- `ch16_interview.py`

**高频追问**

- 奖励怎么设计？遇到过 reward hacking 吗，怎么避免？
- 为什么势函数塑形不改变最优策略？
- 稀疏奖励有哪些办法？

*前置：Ch3 · Ch6 · 对标 JD：职责 1 / 2 · 状态：**已写**（2026-10-13）*

### 第 17 章 · 规划与搜索：MCTS 到 AlphaZero / MuZero（★★ 加分）

> 一句话：有模型（或能模拟）时，用搜索弥补网络的不足；MCTS + 策略 / 价值网络 + 自对弈就是 AlphaZero，棋牌、卡牌类游戏 AI 的主流方案。

**小节**

1. 博弈树搜索：minimax、α-β 剪枝
2. MCTS 四步与 UCT
3. AlphaGo → AlphaZero：PUCT、策略先验 + 价值网络、自对弈训练目标
4. MuZero：学到的模型
5. model-based RL：Dyna-Q、World Models / Dreamer（概要）
6. 游戏落地：卡牌 / 棋牌 AI、搜索预算与帧时间、不完全信息（信息集 MCTS）

**数学推导**

- UCT 选择公式与 UCB 的关系
- PUCT：$\operatorname*{arg\,max}_a\ Q(s,a)+c\,P(s,a)\frac{\sqrt{N(s)}}{1+N(s,a)}$
- AlphaZero 损失 $(z-v)^2-\pi^\top\log p+c\|\theta\|^2$

**图解**

- MCTS 四步
- 井字棋 MCTS 搜索树（构建时真实运行）
- 模拟次数 vs 胜率（实测）

**实测 Demo**

- `ch17_mcts.cpp`：minimax / α-β / UCT / PUCT 对完美对手
- `ch17_interview.py`：Nim 上的 MCTS

**高频追问**

- MCTS 的四个步骤？UCT 中常数 c 的作用？
- AlphaZero 和 AlphaGo 的区别？
- model-based 的优缺点？

*前置：Ch7（UCB）· Ch11 · 对标 JD：职责 1 · 状态：**已写**（2026-10-13）*

### 第 18 章 · 游戏 AI 系统设计：从需求到 Bot（面试大题）（★★★★ 必考）

> 一句话：把前面所有理论拼成一个能上线的 Bot。直接回答面经原题：“DOTA2 / FPS 的 AI 你会怎么做”。

**小节**

1. 答题框架：需求 → 环境接口 → 观测 → 动作 → 奖励 → 网络 → 训练 → 评估 → 部署（图 0-2）
2. 观测设计：标量 / 空间网格（小地图）/ 实体列表；相对坐标与归一化；合法动作掩码；记忆（LSTM）
3. 动作设计：多头离散（类型 + 目标 + 方向）、自回归动作头、宏动作 vs 微操、决策频率与 frame skip、反应时间与 APM 限制；FPS 瞄准的表示（离散角增量 vs 连续）、切枪 / 道具
4. 奖励设计：胜负 + 塑形（血量差、经济、击杀）、零和化、团队奖励分配（team spirit）
5. 案例：MOBA 1v1 / 5v5、FPS Bot、ACT Boss、篮球体育 Bot、AI 陪伴队友
6. 评估与上线：胜率、Elo、拟人度、难度分档、AB 实验、推理成本、兜底与降级
7. RL vs 行为树 / FSM / GOAP / Utility AI：怎么选、怎么组合

**数学推导**

- 多头动作联合 log-prob 与熵的分解（条件独立 / 自回归）
- 动作掩码下的 softmax 归一化
- 零和奖励 $r_A-r_B$ 对优势估计的影响
- 每 $k$ 帧决策一次时的等效折扣 $\gamma^k$ 与有效视界换算

**图解**

- MOBA Bot 系统全景
- 网络结构：三路编码 → LSTM → 多头输出
- 动作空间树
- “MOBA 1v1 Bot”观测-动作-奖励设计卡

**实测 Demo**

- `ch18_action_head.py`：多头 + 自回归 + 掩码的 log-prob / 熵 / 采样校验，决策频率与 γ 换算

**高频追问**

- （面经原题）DOTA2 的 AI 你会怎么做？了解腾讯绝悟吗？
- （面经原题）FPS 的地图、单位状态怎么表示？输入输出是什么？奖励怎么给、胜负怎么判？瞄准怎么体现？切枪切道具怎么做？
- 行为树和 RL 怎么选、怎么结合？AI 太强 / 太弱怎么办？

*前置：Ch11 · Ch14 · Ch15 · Ch16 · 对标 JD：职责 1 / 2 · 状态：**已写**（2026-10-13）*

## Part V · 工程落地与工具链

### 第 19 章 · 分布式训练系统与样本效率（★★ 加分）

> 一句话：游戏 Bot 动辄需要上亿步样本，瓶颈往往在采样而不在 GPU；actor-learner 解耦后，策略滞后要用 V-trace 一类方法修正。

**小节**

1. Actor-Learner 解耦；采样吞吐与 GPU / CPU 分工
2. A3C → IMPALA（V-trace）→ SEED RL；Ape-X / R2D2 的分布式回放
3. Ray / RLlib 与“强化编程框架”：参数同步、模型版本、策略滞后
4. 吞吐估算与瓶颈分析：样本 / 秒、环境实例数、推理批大小
5. 复现性：随机种子、确定性仿真、评测基线

**数学推导**

- V-trace 目标 $v_s=V(x_s)+\sum_{t\ge s}\gamma^{t-s}\big(\prod_{i=s}^{t-1}c_i\big)\rho_t\delta_t$
- 截断系数 $\bar\rho$ 决定收敛到哪个策略的价值（不动点分析，概要）

**图解**

- IMPALA 架构
- 吞吐瓶颈分析（实测）

**实测 Demo**

- `ch19_distributed.cpp`：V-trace 不动点 / 多线程 actor-learner 吞吐与滞后（有界阻塞队列 + 背压）/ 策略滞后 × V-trace 的 CartPole 实验
- `ch19_interview.py`：V-trace 递推 + Little 定律估算滞后

**高频追问**

- IMPALA 为什么需要 V-trace？
- 分布式 RL 的瓶颈在哪？参数滞后有什么影响？

*前置：Ch10 · Ch11 · 对标 JD：要求 5 · 状态：**已写**（2026-10-13）*

### 第 20 章 · 引擎侧集成：UE 中的 AI 与 ML（★★★ 高频）

> 一句话：把训练好的策略放进 UE：游戏要先变成可并行加速的训练环境，模型要能在每帧几毫秒的预算里给几十个 NPC 推理，还要和行为树共存。

**小节**

1. UE 传统 AI 栈：AIController、行为树（Blackboard / Decorator / Service / Task）、StateTree、EQS、AI Perception、NavMesh、Smart Objects、Mass
2. 把游戏变成训练环境：reset / step 接口、无渲染与加速时钟、多实例并行、固定步长与确定性、观测采集与动作执行的帧对齐
3. Learning Agents 插件（实验性）：Interactor / TrainingEnvironment / Policy / Critic / PPOTrainer；UE 进程内推理 + 外部 Python（PyTorch）训练进程，只回传权重；内置 PPO 与 BC
4. 推理部署：ONNX、NNE、CPU / GPU、量化、多 NPC 合批、每帧推理预算、AI LOD
5. C++ 手写小网络推理：权重数组 + SIMD GEMV
6. 联机：Bot 在 DS 上运行、服务器权威、带宽与延迟

**数学推导**

- 合批推理的代价模型：GEMV → GEMM 的访存 / 计算比
- 决策频率与帧预算的估算

**图解**

- UE 训练闭环：UE 进程 ↔ Python 训练进程
- 推理合批时序
- 行为树 + RL 子树的混合架构

**实测 Demo**

- `ch20_infer_bench.cpp`：逐个 / 合批 / AVX2 4×8 微内核（两种网络规模）、int8 量化与“忘了归一化”、AI LOD 错峰的帧预算

**高频追问**

- 怎么把 RL 模型接入 UE？推理性能怎么优化？
- 行为树和 RL 怎么结合？训练环境和线上环境不一致怎么办？
- 结合你的 UE 双人射击 Demo，讲讲加一个 RL Bot 的方案。

*前置：Ch2（MLP）· Ch18 · 对标 JD：职责 3、要求 3 / 4 · 状态：**已写**（2026-10-13）*

### 第 21 章 · AI 工具链、内容生成管线与调试（★★ 加分）

> 一句话：JD 职责 3 和要求 5 的落点。训练出模型只是一半，另一半是让策划、QA 和算法都能看懂、调整、验证它的工具和管线。

**小节**

1. AI 生产管线：配置 → 训练 → 评估 → 导出 → 打包 → 上线 → 监控 → 回滚
2. 实验管理：超参 / 种子 / 指标、TensorBoard、自动评测对局、回归测试
3. 调试工具：对局回放 + 观测 / 动作 / 奖励分解时间线、价值曲线叠加、注意力 / 显著性、Visual Logger、Gameplay Debugger
4. 编辑器工具：奖励配置面板、难度 / 风格滑杆、批量对战看板
5. 内容生成管线：PCG 关卡、LLM NPC（人设、记忆、RAG、动作约束、安全）、伏羲“有灵”AOP、《逆水寒》智能 NPC
6. 测试与平衡 Bot：自动化 QA、数值平衡探测

**数学推导**

- 评测的统计显著性：胜率置信区间 $\hat p\pm z\sqrt{\hat p(1-\hat p)/n}$，以及要打多少局

**图解**

- AI 生产管线
- 调试面板示意
- 评测局数 vs 置信区间宽度曲线（构建时计算）

**实测 Demo**

- `ch21_eval_ci.py`：Wald / Wilson / Clopper-Pearson 覆盖率、所需局数与功效、偷看的假阳性与 SPRT、配对设计

**高频追问**

- 你会怎么设计一个 AI 调试工具？
- 怎么保证训练出的模型能上线？
- LLM NPC 怎么控制不胡说、不越权？

*前置：Ch18 · Ch20 · 对标 JD：职责 3、要求 5 · 状态：**已写**（2026-10-13）*

## 附录

- **附录 A · 面试追问 100 题**：按章组织，每题给 30 秒答案并指向详细章节；高频题标 ★。
- **附录 B · 公式速查卡**：Bellman、TD、λ-回报、策略梯度、GAE、PPO、TRPO、SAC、DPO、GRPO、UCB、Elo 一页看完。
- **附录 C · 手撕代码合集**：Q-learning、Sarsa、DQN loss、REINFORCE、GAE、PPO loss、UCB、MCTS、sum-tree、Elo，C++ 与 Python 双实现，全部编译运行对拍；附面经算法题（如二叉树翻转）。
- **附录 D · 论文与资料清单**：经典论文（DQN、TRPO、PPO、SAC、AlphaZero…）、伏羲公开论文与分享、OpenAI Five / AlphaStar / 绝悟等游戏 AI 工作。
- **附录 E · 项目叙事**：把 UE 双人射击 Demo 和客户端实习经历，讲成“游戏 AI 系统”的项目叙事（只用可公开的内容）。

## 实测与构建约定

- **语言分工**：表格型算法、数据结构、推理性能用 C++（MSVC `/O2`）；深度 RL 用 Python + numpy 手写网络与优化器（不依赖 gym / PyTorch，环境自写，固定随机种子，保证可复现）；GAE、PPO loss 等关键公式 C++ / Python 双实现对拍。
- **数字来源**：正文和图里的实验数字只来自 `demos/chNN_*_output.txt` 或构建脚本的确定性计算；追问时只跑新实验，不重跑旧实验，避免已引用的数字失效。
- **公式**：电子书用 KaTeX 在构建时渲染，字体内嵌，离线和打印 PDF 都能正常显示；md 保留 LaTeX 源码（自定义宏已展开），可直接在 Typora / VS Code 中渲染。
- **产物**：`RL_GameAI.html` / `RL_GameAI.pdf`（电子书）、`RL_GameAI_强化学习面试指南.md`（合并版，含提问记录）、`md/NN_*.md`（单章）。

**怎么用这份大纲自查进度？**

> 答：每写完一章，“大纲总览”表的状态改为“已写”，封面的进度标签同步更新。复习时先看总览表的“代表性推导”一列：能脱稿写出来的打勾，写不出来的就是下一次要问我的知识点。

