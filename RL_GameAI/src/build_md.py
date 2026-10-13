"""把电子书章节转换成 Markdown（与电子书同源：src/chapters/*.html）
产物：
  ../RL_GameAI_强化学习面试指南.md      合并版（顶部「提问记录」+ 目录 + 全部章节）
  ../md/ChNN_*.md / Outline_*.md         单章版（顶部只列与本章相关的提问记录）
规则：
  - 公式保留 LaTeX 源码（$...$ / $$...$$），自定义宏（\\E、\\KL…）按 build.MACROS 展开，Typora / VS Code 可直接渲染
  - deriv 推导 → aligned 公式块 + 逐步依据列表
  - 数据宏（conv / bars）用 build.py 同一套算法生成表格；[[id.key]] 数字与电子书同源
  - flow 图：优先用下面 ASCII 字典里的字符图（按「图 x-y」查），否则自动列出箭头关系
"""
import html
import json
import pathlib
import re
from html.parser import HTMLParser

import build as B

ROOT = pathlib.Path(__file__).resolve().parent
OUT = ROOT.parent / "RL_GameAI_强化学习面试指南.md"
MD_DIR = ROOT.parent / "md"
BOOK = "《游戏 AI 系统工程师面试：强化学习与机器学习理论基础》"

# ---------------------------------------------------------------- 提问记录（keys = 回答落在哪些章节的 section id）
QA = [
    {"time": "2026-10-13 10:29",
     "q": "新增 AI 系统工程师（游戏 AI / 强化学习）岗位的面试准备：贴出 JD（NPC / AI 陪伴 / PVE AI / AI Bot；强化学习落地；引擎侧 AI 工具链；C++ + Python；Unreal 优先；AI 工具链与内容生成管线），希望带着复习强化学习与机器学习（核心：伏羲强化学习理论基础），最终生成电子书；本次先生成整个大纲，后续按大纲与提问逐章更新单章 md 与整本电子书，每章做好数学推理、文图并茂",
     "a": "第 0 章（JD 逐条拆解与权重、「伏羲 RL 理论基础」三层含义、算法族谱图、游戏 Bot 全景图、三轮复习路线、推导写法样张 + γ-压缩收敛计算图、统一符号表、后续推进方式）· 全书大纲（5 部分 21 章 + 5 附录；每章小节 / 推导 / 图解 / demo / 高频追问 / 前置 / JD 对标）",
     "keys": ["ch0", "outline"]},
    {"time": "2026-10-13 11:23",
     "q": "适当弱化 Part I、开始写 Part II",
     "a": "大纲调整（Part I 改为“数学工具速查 / ML 面试速查”两章，数学工具改为在用到处以「工具箱」框就地讲；C++ MLP 挪到第 8 / 20 章）· **第 3 章**（RL vs 监督学习、交互循环与游戏帧循环、对线 MDP、γ 三层含义与有效视界、决策频率换算、终止 vs 截断、V / Q / A 及其转换、Bellman 期望方程与 backup 图、解析 / 迭代 / 蒙特卡洛三方对拍、Bellman 最优方程、8 个确定性策略穷举、γ 改变最优策略、T* 压缩与确定性最优策略存在性证明、POMDP 信念更新、MOBA / FPS 建模表）· **第 4 章**（迭代策略评估与 Sutton 4×4 网格、策略改进定理完整证明、策略迭代有限收敛与平局陷阱、价值迭代停止准则与贪心损失界、地牢地图 PI / VI / 滑步改变最优路线、γ 与扫描次数实测、规模化计时与“躲避格”、GPI 统一框架、DP 在游戏里的实际用途、手撕 VI / PI）；实测 `demos/ch03_mdp.cpp`、`demos/ch04_dp.cpp`、`demos/ch04_interview.py`，构建时 Python ⇄ C++ 自动对拍",
     "keys": ["outline", "ch3", "ch4"]},
    {"time": "2026-10-13 12:56",
     "q": "继续未完成的任务",
     "a": "**第 5 章**（从 DP 到无模型、MC 首次 / 每次访问与增量均值、常数 α = 指数加权、TD(0) 期望更新与偏差来源、终止 vs 截断的自举处理、5 状态随机游走 MC vs TD 学习曲线、目标方差对比、批量更新：MC 收敛到样本均值 / TD 收敛到确定性等价解与 A/B 例子、n 步回报与误差缩减界、λ-回报与递推、TD 误差累加恒等式 ①②（即 GAE）、前向 = 后向视角证明、资格迹（累积 / 替换）与在线 TD(λ)、宽度 × 深度分类、手撕 MC / TD / TD(λ) / λ-回报）；实测 `demos/ch05_random_walk.cpp`、`demos/ch05_interview.py`，Python ⇄ C++ 公共随机数逐位对拍",
     "keys": ["outline", "ch5"]},
    {"time": "2026-10-13 13:45",
     "q": "继续完成 Part III 和 Part IV 部分",
     "a": "**第 8 章**（半梯度、致命三角与线性 TD 稳定性证明、DQN 回放 + 目标网络、最大化偏差与 Double DQN、Dueling 减均值、PER 的 IS 权重与 sum-tree、Rainbow、游戏 Bot 为什么少用 DQN）；新增公共头文件 `demos/rlcommon.h`（手写 MLP + Adam、CartPole、并行跑种子）；实测 `demos/ch08_dqn.cpp`、`demos/ch08_interview.py`",
     "keys": ["outline", "ch8"]},
    {"time": "2026-10-13 14:24",
     "q": "继续",
     "a": "**第 9 章**策略梯度（对数导数技巧、策略梯度定理两种形式与有限差分验证、reward-to-go 与基线无偏、最优基线、丢掉 γ^t 的实用版）· **第 10 章** Actor-Critic 与 GAE（E[δ]=A、GAE 闭式与递推、critic 精确时任何 λ 无偏、终止 / 截断 / rollout 末尾、A3C vs A2C、熵梯度）· **第 11 章** PPO（性能差异引理、替代目标、Fisher 与自然梯度、表格 softmax 自然梯度 = 优势、PPO-clip 六种情形、k1/k2/k3、工程细节与动作掩码、消融与训练诊断）· **第 12 章** DDPG/TD3/SAC（DPG、actor 利用 critic 误差、最大熵最优策略、tanh 雅可比、自动温度、SAC vs PPO 选型）· **第 13 章** RLHF/DPO/GRPO（BT 奖励模型、KL 正则闭式解、DPO 推导、GRPO 组内优势与 k3、三法同数据对比）· **Part IV**：**第 14 章**模仿学习与离线 RL（复合误差、DAgger 与噪声示范、GAIL 判别器最优解、外推误差与 BCQ/CQL、RL+IL 拟人化与评估）· **第 15 章**多智能体与自博弈（纳什均衡、朴素自博弈循环 vs FSP、CTDE、QMIX 单调 ⇒ IGM、Elo = logistic 回归、非传递性、难度分档）· **第 16 章**奖励设计（势函数塑形证明、刷分环路反例、塑形 = 换初值、HER、SMDP）· **第 17 章** MCTS 到 AlphaZero/MuZero（α-β、UCB1 推导、UCT/PUCT 实测、AlphaZero 损失）· **第 18 章**游戏 Bot 系统设计（九步框架、观测与网络结构、多头自回归动作的 log-prob 与熵分解、frame skip 与 γ 换算、MOBA / FPS 设计卡、RL vs 行为树）；每章配 C++ / Python 实测与手撕代码对拍",
     "keys": ["ch9", "ch10", "ch11", "ch12", "ch13", "ch14", "ch15", "ch16", "ch17", "ch18"]},
    {"time": "2026-10-13 17:15",
     "q": "完成第 1–2 章（数学和 ML 速查）、第 6–7 章（Sarsa / Q-learning、探索与利用）",
     "a": "**第 1 章** RL 数学工具速查（全期望 / 全方差、重要性采样无偏与 e^{μ²} 二阶矩、普通 vs 加权 IS、KL ≥ 0 与前向 / 反向 KL、MLE ⇔ 交叉熵、MAP ⇔ 正则、得分函数 vs 重参数化、常用不等式、优化器）· **第 2 章** ML / DL 面试速查 20 问（偏差-方差分解实测、L1 稀疏 vs L2、Dropout、逻辑回归与 softmax+CE 梯度 p−y、SVM、RF vs GBDT、AUC 秩和、反传与梯度消失实测、残差、BN vs LN、√d 缩放实测、Transformer / LSTM、学习率、大模型）· **第 6 章** Sarsa / Expected Sarsa / Q-learning（ε-greedy 改进定理、Cliff Walking 路线与在线回报、α 扫描、GLIE 的有限训练表现、off-policy MC 重要性采样与无穷方差例子、最大化偏差随动作数增长与 Double Q）· **第 7 章**探索与利用（遗憾分解、10 臂测试平台、Hoeffding → UCB1、Thompson 采样、梯度老虎机 = 期望奖励 SGD、参数研究、累积遗憾与 Lai-Robbins 下界、链式 MDP 深度探索、RND / ICM / Go-Explore、噪声电视）；新增 gpath 网格路径图宏",
     "keys": ["outline", "ch1", "ch2", "ch6", "ch7"]},
    {"time": "2026-10-13 20:12",
     "q": "完成 Part V",
     "a": "**第 19 章**分布式训练（采样瓶颈、A3C → IMPALA → SEED RL 演进、V-trace 不动点推导与表格实测、多线程 actor-learner 吞吐 / learner 饱和 / 策略滞后与 Little 定律、滞后 × V-trace 的 CartPole 实验、参数同步与复现性）· **第 20 章** UE 集成（UE AI 栈、训练环境化清单、Learning Agents 组件与训练闭环、NNE 三种运行时接口、GEMV vs GEMM 访存 / 计算比与 roofline、逐个 / 合批 / AVX2 推理实测、int8 量化与“忘了归一化”、AI LOD 与错峰帧预算、行为树 + RL 子树、联机、给 UE 双人射击 Demo 加 RL 僵尸的方案）· **第 21 章**工具链（AI 生产管线、实验管理、Wald / Wilson / 精确区间覆盖率、所需局数、偷看的假阳性与 SPRT、配对设计、调试面板设计、策划编辑器工具、LLM NPC 架构与安全、伏羲 AOP 实践、测试与平衡 Bot）",
     "keys": ["outline", "ch19", "ch20", "ch21"]},
]

ASCII = {
"图 0-1": r"""
基石（理论与工具）：MDP·Bellman(Ch3) | 动态规划 PI/VI(Ch4) | MC·TD·TD(λ)(Ch5) | 探索与利用(Ch7) | ML/DL 速查(Ch1–2)
   │
   ├─ 价值派  ：Sarsa/Q-learning(Ch6) ─→ DQN(Ch8) ─→ Double·Dueling·PER(Ch8) ─→ Rainbow·分布式 RL(Ch8)
   │                                      ┆ critic = 价值网络
   ├─ 策略派  ：REINFORCE(Ch9) ─→ Actor-Critic·GAE(Ch10) ─→ TRPO→PPO(Ch11) ─→ RLHF·DPO·GRPO(Ch13)
   │                                      ┆ 确定性 actor 的 AC
   ├─ 连续控制：DPG→DDPG(Ch12) ─→ TD3·SAC(Ch12) ─→ 离线 RL(Ch14)
   ├─ 规划搜索：Minimax/α-β(Ch17) ─→ MCTS/UCT(Ch17) ─→ AlphaZero(Ch17) ─→ MuZero(Ch17)
   │                                                       ┆ 自对弈训练
   └─ 游戏专题：模仿学习(Ch14) ─→ 自博弈·MARL(Ch15) ─→ 游戏 Bot 系统(Ch18)

贯穿全书：奖励设计/课程/分层 Ch16 · 分布式训练 Ch19 · UE 集成与推理部署 Ch20 · AI 工具链 Ch21 · 手撕代码 附录 C
""",
"图 0-2": r"""
① 需求定义    （强度·拟人·风格·线上成本   ｜ Ch18·Ch14）
 → ② 环境化    （UE 训练环境·加速仿真·并行 ｜ Ch20）
 → ③ MDP 设计  （观测·动作·奖励·动作掩码   ｜ Ch18·Ch16）
 → ④ 算法选型  （PPO/DQN/SAC/BC/MCTS       ｜ Ch8–Ch14·Ch17）
 → ⑤ 训练系统  （自博弈·种群·分布式采样     ｜ Ch15·Ch19）
 → ⑥ 评估      （胜率·Elo·拟人度·难度分档   ｜ Ch14·Ch15·Ch21）
 → ⑦ 部署推理  （ONNX/NNE·合批·AI LOD       ｜ Ch20）
 → ⑧ 工具链与迭代（对局回放·可视化·实验管理 ｜ Ch21）
 ↺ 线上数据·玩家反馈回流到 ①

分工：行为树/StateTree 负责流程框架、规则兜底与可控性；RL 策略负责战斗等高维、对抗性的决策子树（Ch18·Ch20）
""",
"图 3-1": r"""
┌ 智能体 Agent ───────────┐   动作 Aₜ（移动 / 技能 / 目标）      ┌ 环境 Environment ──────────────┐
│ 策略 π(a|s)：Bot 的大脑 │ ─────────────────────────────────►  │ 游戏世界：规则 + 物理 + 对手     │
│ 输入观测 → 输出动作     │ ◄─────────────────────────────────  │ p(s′, r | s, a)，通常未知       │
└─────────────────────────┘   下一状态 Sₜ₊₁ · 奖励 Rₜ₊₁ · 是否结束 └────────────────────────────────┘

落到游戏帧循环（每个决策步）：
 ① 采集观测 → ② 网络推理 → ③ 执行动作 → ④ 引擎 Tick（跑 k 帧）→ ⑤ 结算奖励 / 终止 / 截断 ─┐
 ▲                                                                                           │
 └─────────────────────────────── 下一个决策步 t+1 ─────────────────────────────────────────┘
""",
"图 3-2": r"""
对线 ─┬─ 稳健补刀 r=1.2 ──► 对线 0.9 │ 团战 0.1
      └─ 激进压线 r=0.8 ──► 对线 0.3 │ 团战 0.5 │ 回城 0.2
团战 ─┬─ 后撤拉扯 r=1.5 ──► 对线 0.4 │ 团战 0.6
      └─ 强行开团 r=2.5 ──► 对线 0.2 │ 团战 0.2 │ 回城 0.6
回城 ─┬─ 快速上线 r=−1.2 ─► 对线 1.0
      └─ 补满状态 r=−0.8 ─► 对线 0.8 │ 回城 0.2
（第 1 层状态 s → 第 2 层动作 a 与期望即时奖励 r(s,a) → 第 3 层下一状态 s′ 与转移概率）
""",
"图 3-4": r"""
V_π(s) 的 backup                                Q_π(s,a) 的 backup
            (s)                                          ●(s,a)
      ┌──────┼──────┐   ← 按 π(a|s) 对动作求期望       ┌─────┴─────┐   ← 按 p(s′,r|s,a) 转移
      ●      ●      ●  a                              (s′)        (s′)
     ┌┴┐    ┌┴┐    ┌┴┐  ← 按 p(s′,r|s,a) 对转移求期望  ┌┴┐         ┌┴┐  ← 按 π(a′|s′) 选 a′
   (s′)(s′)(s′)(s′)(s′)(s′)                           ● ●         ● ●
V(s) = Σₐ π(a|s) Σ p(s′,r|s,a)[r + γV(s′)]      Q(s,a) = Σ p(s′,r|s,a)[r + γ Σₐ′ π(a′|s′) Q(s′,a′)]
（空心 = 状态，实心 = 动作；把左图第一层的 Σπ 换成 max 就是 V* 的 backup）
""",
"图 4-7": r"""
┌ 策略评估 ──────────────────┐   给出 V      ┌ 策略改进 ──────────────────────┐        ┌ 同时稳定 ────────────┐
│ V ← V_π（解或近似解期望方程）│ ───────────► │ π ← greedy(V) 或沿梯度走一小步 │ ─────► │ V = V*，π = π*        │
│ 可以只做几步，甚至一步       │ ◄─────────── │ 可以只改部分状态               │        │ ⇔ Bellman 最优方程    │
└────────────────────────────┘   给出新 π    └────────────────────────────────┘        └──────────────────────┘
评估让 V 贴近当前策略的真实价值，改进让 π 对当前 V 贪心——交替进行就会一起收敛到最优（GPI）
""",
"图 5-1": r"""
   DP（全宽 · 一步）            MC（采样 · 到终止）           TD(0)（采样 · 一步）
        (s)                          (s)                           (s)
   ┌─────┼─────┐                      │ a                           │ a
   ●     ●     ●   所有动作           (s′)                         (s′)  ← 用 V(S′) 顶替后面的一切
 ┌┴┐   ┌┴┐   ┌┴┐   所有转移            │
(s′)(s′)(s′)(s′)…   用 V(s′) 自举       ⋮  一直采样到终止 ■
需要模型 p(s′,r|s,a)            目标 G_t（无偏、方差大）       目标 R + γV(S′)（有偏、方差小）
""",
"图 5-2": r"""
 ■ 左端 ── A ── B ── C ── D ── E ── 右端 ■
 r = 0                                r = +1
 每步等概率左 / 右，其余奖励 0，从 C 出发，γ = 1
 真值 V = 1/6, 2/6, 3/6, 4/6, 5/6 ＝ 从该格出发“最终从右端结束（赢）”的概率
""",
"图 5-10": r"""
               backup 深度 →   一步自举                         一直到终止
 宽度 全部求期望      动态规划（Ch4）：需要模型            穷举搜索：代价随深度指数增长
         ↕ 期望↔样本
      采样一条        TD(0)：偏差↑ 方差↓  ══ n 步 TD / TD(λ) / GAE（n↑ 或 λ↑）══►  蒙特卡洛：无偏 · 方差↑
 MCTS（Ch17）在两者之间：选择性展开一部分分支，叶子处用价值网络自举或 rollout
""",
"图 8-2": r"""
┌ 游戏环境 ┐ ──s──► ┌ 在线网络 Q_w（ε-greedy）┐ ── 每 C 步复制 w̄ ← w ──► ┌ 目标网络 Q_w̄（冻结）┐
└──────────┘ ◄──a── └─────────────────────────┘                             └──────────┬──────────┘
     │ (s, a, r, s′, term)          ▲ 更新 w       │ Q_w(s,a)                           │ y = r + γ(1−term)·max Q_w̄(s′,·)
     ▼                              │              ▼                                    │
┌ 回放缓冲区（环形数组）┐ ─随机采样─► ┌ Huber 损失 ℓ(Q_w(s,a) − y) + Adam ┐ ◄───────────────┘
经验回放：打乱相关性、复用样本　　目标网络：C 步内回归目标固定
""",
"图 8-7": r"""
                         [42]  ← 根 = 总优先级，取 u = 24
              ┌───────────┴───────────┐
           [29] u<29 → 往左          [13]
        ┌─────┴─────┐            ┌─────┴─────┐
      [13]        [16] u≥13 → 往右，u −= 13   [3]  [10]
     ┌─┴─┐       ┌─┴─┐                ┌─┴─┐  ┌─┴─┐
    [3] [10]   [12] [4]               [1] [2] [8] [2]
               ↑ 剩余 u = 11 < 12 → 落在叶子 2
红色路径：根 → 29 → 16 → 12，比较 3 次；更新叶子时把差值沿同一路径加到所有祖先
""",
"图 10-2": r"""
A3C（异步）                                   A2C（同步）
┌ 全局参数服务器 θ（Hogwild 写入）┐            ┌ 一个 learner：等所有 rollout 到齐再更新 ┐
  ▲ 梯度        ▲          ▲                      │ 动作（batch）          ▲ N×T 条数据
┌ worker 1 ┐ ┌ worker 2 ┐ ┌ worker n ┐           ┌ N 个并行环境（同一份参数合批推理，GPU 友好）┐
（各自环境 + 网络副本，参数可能过期）            同步：可复现、能合批；异步：陈旧梯度
""",
"图 11-3": r"""
① 采样（N 环境 × T 步，存 s, a, r, logπ_old, V）→ ② GAE（Â 与 R̂，区分终止 / 截断）→ ③ 优势归一化
 → ④ K 个 epoch × 小批量（clip 策略损失 + 价值损失 − 熵；梯度裁剪；Adam）→ ⑤ approx KL 超标则早停
 ↺ 丢掉这批数据，用新策略重新采样（on-policy）
同一批数据只在 θ_old 附近复用 K 次；学习率退火；观测 / 奖励归一化
""",
"图 13-1": r"""
① SFT（人写示范 → π_ref）──采样回答对──► ② 奖励模型（人标哪个好，Bradley-Terry）──r̂(x,y)──► ③ PPO：max E[r̂] − β·KL(π‖π_ref)
     └────────────────────── π_ref 作为 KL 锚点和初始化 ──────────────────────────────┘
DPO 把 ②③ 合成一步；GRPO 保留 ③ 的在线采样但去掉价值模型
""",
"图 14-3": r"""
人类录像（按段位 / 风格筛选）→ BC 预训练 π_human → RL 微调（L_RL + w·L_BC 或 KL 约束）→ 评估（胜率 · 拟人度）→ 调 w / β
                                                    ▲                                                      │
                                                    └──────────── 更新混合系数，继续训练 ◄───────────────────┘
RL 的高分且符合风格的轨迹 → 自模仿（SIL）→ 回到训练数据
""",
"图 15-3": r"""
                ┌ 集中式 critic（只在训练时用）：输入全局状态 / 所有人的观测与动作 ┐
                         ┊ 优势 / 价值            ┊                     ┊
┌ 智能体 1：π¹(a¹|o¹) 只看自己 ┐  ┌ 智能体 2：参数共享 + 角色 ID ┐  ┌ 智能体 n：执行时完全分散 ┐
CTDE：训练集中（降低非平稳 / 信用分配难度），执行分散（Bot 不开全图）
""",
"图 16-1": r"""
 (距离 2) ──走近：+0.2──► (距离 1，目标正下方) ──进门：+1 + 0.2，结束──► ((目标))
     ▲                         │
     └────走远：0（不扣分）─────┘
退一步再回来的循环价值 γ·0.2/(1−γ²) > 直接进门的 1.2 ⇒ 最优策略永远不进门
""",
"图 16-3": r"""
                 ┌ 高层策略（每 N 帧或 option 结束时决策）：推线 / 打野 / 支援 / 回城 / 开团 ┐
                      │ 目标点                 │ 交战目标                     │ 指令
┌ 低层：移动控制（寻路、走位）┐ ┌ 低层：战斗微操（RL 策略）┐ ┌ 低层：规则 / 脚本（买装备、回城流程）┐
""",
"图 17-1": r"""
① 选择（按 UCT 往下走到未完全展开的节点）→ ② 扩展（新建子节点；AlphaZero 一次展开全部 + 先验）
 → ③ 模拟 / 估值（随机走到终局；AlphaZero 用价值网络）→ ④ 回传（N += 1、W += 结果，每层翻转视角）
 ↺ 重复 N 次，最后选根节点下访问次数最多的着法
""",
"图 17-5": r"""
自对弈（每步 MCTS，按访问分布 π 采样，根加 Dirichlet 噪声）→ 存 (s, π, z) → 训练 f_θ(s) = (p, v)：L = (z − v)² − πᵀlog p + c‖θ‖²
   ▲                                                                                               │
   └──────────────────────────── 更新后的网络用于下一轮自对弈 ◄────────────────────────────────────────┘
""",
"图 18-1": r"""
① 需求 → ② 环境 → ③ 观测 → ④ 动作 → ⑤ 奖励
                                          ↓
线上数据 ← ⑨ 部署 ← ⑧ 评估 ← ⑦ 训练 ← ⑥ 网络
（线上数据回流到 ①）
""",
"图 18-2": r"""
全局标量 ─MLP──┐
小地图网格 ─CNN─┼─► 拼接（+ 自身状态）─► LSTM ─┬─► 类型头（做什么）──类型嵌入→条件化──┐
实体列表 ─共享 MLP + 注意力─┘                    ├─► 目标头（对实体嵌入做指针注意力）◄────┘
          └──── 实体嵌入（作为指针的 key）────────►├─► 方向 / 位置头
                                                └─► 价值头（可多头）
""",
"图 18-3": r"""
                          类型头（5 选 1，掩码：冷却 / 状态）
   ┌──────────────┬──────────────┼──────────────┬──────────────┐
  移动           普攻          技能 1          技能 2          回城
 → 方向头（8）  → 目标头（射程掩码）→ 目标头    → 方向头       无子头
联合 log-prob = log π(类型) + 该类型用到的子头的 log-prob（子头输入拼上类型嵌入）
""",
"图 19-1": r"""
┌ actor 1 ┐ ┌ actor 2 ┐ … ┌ actor N ┐   每个 actor：游戏实例 + 本地策略副本（μ = 旧版本参数，每段 rollout 前拉一次）
     └──────────┴─── 轨迹 + μ(a|x) + 版本号 ───┘
                          ▼
                ┌ 轨迹队列（有界，满了 actor 阻塞）┐ ── batch ──► ┌ learner（GPU）：V-trace + 策略梯度，版本 +1 ┐
                                                                              ▼
           ◄────────────── 广播新参数（actor 下一段才用上 → 策略滞后）── ┌ 参数服务器：θ 与版本号 ┐
""",
"图 20-1": r"""
┌ UE 进程（一个关卡里可放多个训练场并行）──────────────────────────────┐
│ Interactor（采观测 / 执行动作）─obs→ Policy（进程内推理）─act→ TrainingEnvironment（奖励 / 终止 / 重置）│
│                                         PPOTrainer（缓存经验）◄── r, done ──┘                      │
└──────────────────────────────────────────┬───────────────────────────┘
                    经验（共享内存 / 网络）▼        ▲ 新权重
                  ┌ 外部 Python 进程：PyTorch PPO / BC 训练、TensorBoard、检查点 ┐
""",
"图 20-4": r"""
                         Selector（根）
   ┌──────────────┬──────────┴──────────────┬───────────────┐
 死亡 / 受控     脚本行为（出生、剧情、    战斗子树：BTTask_RLDecide     巡逻 / 待机（兜底）
（IsDead）       回城、购买）            （HasTarget && InCombat）
                                          ▼
                        采观测（感知 + 黑板 + EQS）→ 推理（合批，工作线程）→ 动作 → MoveTo / 能力 / 瞄准
""",
"图 21-1": r"""
① 配置 → ② 训练 → ③ 自动评测（置信区间、回归门禁）→ ④ 导出（ONNX + 归一化参数 + 版本号）→ ⑤ 打包
                                                                                         ↓
问题定位（回放调试）← ⑨ 数据回流 ← ⑧ 回滚（上一版本 / 规则 AI）← ⑦ 线上监控 ← ⑥ 灰度上线（AB）
""",
"图 21-4": r"""
┌ ① 对局回放视图：任意帧暂停 / 拖动；叠加视野、感知、候选目标、路径 ┐ ┌ ② 当前帧：观测（字段名、归一化前后）、各动作头概率、掩码 ┐
│                                                                  │ ├ ③ 解释：V(s)、奖励分量预测、显著性                     ┤
└──────────────────────────────────────────────────────────────────┘ └───────────────────────────────────────────────────┘
┌ ④ 时间线（整局）：价值曲线 · 奖励分解堆叠条 · 策略熵 · 动作色带 · 关键事件；点击跳帧；可叠加两个版本对比 ┐
""",
"图 21-5": r"""
                                   上下文组装（人设 + 世界观 + 记忆 + 检索 + 游戏状态）
                                                  ▼
玩家输入（文字 / ASR）→ 输入过滤（敏感词、注入）→ 大模型（对话 + 结构化意图）→ 输出过滤（安全 / 剧透 / 出戏）─┬→ 意图 → 白名单函数 + 游戏逻辑校验 → 动作
                                                  ▲                                                    └→ 文本 → TTS / 口型
                                   记忆 / 知识库（短期对话、长期关系、向量检索 RAG）
""",
"图 0-3": r"""
第一轮·核心理论链（必考）
  Ch3 MDP★ → Ch4 DP → Ch5 MC/TD → Ch6 Sarsa/Q → Ch8 DQN → Ch9 策略梯度★ → Ch10 AC/GAE → Ch11 PPO★ → Ch13 GRPO★
  ✓ 脱稿推出 Bellman → 策略梯度定理 → GAE → PPO-clip，讲清 Q-learning vs Sarsa、PPO vs GRPO
第二轮·游戏 AI 专题（对标职责 1/2）
  Ch18 Bot 设计★ → Ch15 自博弈 → Ch14 模仿学习 → Ch16 奖励设计 → Ch20 UE 集成
  ✓ 3 分钟答完「DOTA2 / FPS 的 AI 你会怎么做」，并扛住观测/动作/奖励/评估追问
第三轮·地基与工程（加分项）
  Ch1 数学速查 → Ch2 ML 八股 → Ch7 探索 → Ch12 SAC → Ch17 MCTS → Ch19 分布式 → Ch21 工具链 → 附录 手撕代码
  ✓ 手撕 Q-learning/GAE/PPO loss/MCTS 能编译运行；ML 八股（BN、交叉熵、过拟合）不卡壳
★ = 面经高频考点
""",
}


# ---------------------------------------------------------------- 公式
def fix_math(s):
    """把文本里 $..$ / $$..$$ 内的自定义宏展开（用于直接进入 md 的片段）。"""
    s = B.DISPLAY_RE.sub(lambda m: "$$" + B.expand_macros(m.group(1)) + "$$", s)
    return B.INLINE_RE.sub(lambda m: "$" + B.expand_macros(m.group(1)) + "$", s)


def plain(s):
    """HTML 片段 → 纯文本（保留 $ 公式）。"""
    s = re.sub(r"<(strong|b)>(.*?)</\1>", r"**\2**", s)
    s = re.sub(r"<code>(.*?)</code>", r"`\1`", s)
    return html.unescape(re.sub(r"<[^>]+>", "", s))


# ---------------------------------------------------------------- 宏 → Markdown
def md_bars(d):
    unit = d.get("unit", "")
    rows = [f"| {it[0]} | {it[3] if len(it) > 3 else f'{it[1]:g} {unit}'.strip()} |" for it in d["items"]]
    return f"**{d.get('title', '')}**\n\n| 项 | 值 |\n|---|---|\n" + "\n".join(rows)


def md_conv(d):
    r = B.conv_run(d)
    if d.get("id"):
        B.VALS[d["id"]] = r["vals"]
    ks = list(range(0, r["K"] + 1, 5))
    rows = [f"| {k} | {r['errs'][k]:.4g} | {r['bound'][k]:.4g} |" for k in ks]
    return ("| 迭代 k | 实际误差 ‖V_k − V_π‖∞ | 上界 γ^k·‖V_0 − V_π‖∞ |\n|---|---|---|\n" + "\n".join(rows))


def md_flow(d, cap):
    key = re.search(r"图 [A-Z]?\d+-\d+", cap)
    art = ASCII.get(key.group(0)) if key else None
    if art:
        return f"```text{art}```"
    B.flow_norm(d)
    boxes = B.flow_boxes(d)
    name = lambda i: boxes[i]["t"] + (f"（{boxes[i]['s']}）" if boxes[i].get("s") else "")
    edges = [f"- {name(e['from'])} → {name(e['to'])}" + (f"：{e['label']}" if e.get("label") else "")
             for e in d.get("arrows", []) if "from" in e]
    print("[warn] flow 图没有 ASCII 版，自动列出箭头：", cap[:30])
    return "\n".join(edges)


def md_deriv(title, body):
    title, rows = B.deriv_parse(title, body)
    out = [f"**推导 · {plain(title)}**", ""]
    eqs, notes, n = [], [], 0

    def flush():
        if eqs:
            out.append("$$\n\\begin{aligned}\n" + " \\\\\n".join(eqs) + "\n\\end{aligned}\n$$")
            out.append("")
            out.extend(notes)
            out.append("")
            eqs.clear(); notes.clear()

    for r in rows:
        if r[0] == "c":
            flush()
            out.append(fix_math(plain(r[1])))
            out.append("")
            continue
        n += 1
        lhs = B.expand_macros(r[1])
        rhs = B.expand_macros(r[2])
        eqs.append(f"{lhs} &{rhs} && \\text{{({n})}}")
        notes.append(f"- ({n}) {fix_math(plain(r[3]))}" + ("　**← 结论**" if r[0] == "k" else ""))
    flush()
    return "\n".join(out).rstrip()


# ---------------------------------------------------------------- 极简 DOM
class Node:
    def __init__(self, tag, attrs, parent=None):
        self.tag, self.attrs, self.parent, self.kids = tag, dict(attrs), parent, []

    def cls(self):
        return self.attrs.get("class", "").split()


VOID = {"br", "img", "input", "hr", "meta", "link"}


class P(HTMLParser):
    def __init__(self):
        super().__init__(convert_charrefs=True)
        self.root = Node("root", {})
        self.cur = self.root

    def handle_starttag(self, tag, attrs):
        n = Node(tag, attrs, self.cur)
        self.cur.kids.append(n)
        if tag not in VOID:
            self.cur = n

    def handle_endtag(self, tag):
        c = self.cur
        while c is not self.root and c.tag != tag:
            c = c.parent
        if c is not self.root:
            self.cur = c.parent

    def handle_data(self, data):
        self.cur.kids.append(data)


def inline(n, code=False):
    if isinstance(n, str):
        t = re.sub(r"\s+", " ", n)
        return t if code else t.replace("<", "&lt;").replace(">", "&gt;")
    t = "".join(inline(k, code or n.tag == "code") for k in n.kids)
    if n.tag in ("strong", "b"):
        return f"**{t.strip()}**" if t.strip() else ""
    if n.tag == "code":
        return f"`{t}`"
    if n.tag == "em":
        return f"*{t}*"
    if n.tag == "br":
        return "  \n"
    return t


def find(n, pred):
    for k in n.kids:
        if isinstance(k, Node):
            if pred(k):
                return k
            r = find(k, pred)
            if r:
                return r
    return None


def walk(n):
    for k in n.kids:
        yield k
        if isinstance(k, Node):
            yield from walk(k)


def table(n):
    rows = []
    span = {}   # 列号 -> [文本, 剩余行数]：rowspan 的单元格在后续行重复显示
    for tr in [k for k in walk(n) if isinstance(k, Node) and k.tag == "tr"]:
        cells = [c for c in tr.kids if isinstance(c, Node) and c.tag in ("td", "th")]
        if len(cells) == 1 and cells[0].attrs.get("colspan"):
            rows.append([inline(cells[0]).strip().replace("|", "\\|")])
            continue
        row, col, queue = [], 0, list(cells)
        while queue or col in span:
            if col in span:
                row.append(span[col][0])
                span[col][1] -= 1
                if span[col][1] == 0:
                    del span[col]
                col += 1
                continue
            c = queue.pop(0)
            txt = inline(c).strip().replace("|", "\\|")
            row.append(txt)
            if int(c.attrs.get("rowspan", 1)) > 1:
                span[col] = [txt, int(c.attrs["rowspan"]) - 1]
            col += 1
        rows.append(row)
    if not rows:
        return ""
    w = max(len(r) for r in rows)
    rows = [r + [""] * (w - len(r)) for r in rows]
    return "\n".join(["| " + " | ".join(rows[0]) + " |", "|" + "---|" * w] + ["| " + " | ".join(r) + " |" for r in rows[1:]])


def lst(n, depth=0):
    out = []
    i = 0
    for li in [k for k in n.kids if isinstance(k, Node) and k.tag == "li"]:
        i += 1
        txt = "".join(inline(k) for k in li.kids if not (isinstance(k, Node) and k.tag in ("ul", "ol"))).strip()
        out.append("  " * depth + (f"{i}. " if n.tag == "ol" else "- ") + txt)
        for sub in [k for k in li.kids if isinstance(k, Node) and k.tag in ("ul", "ol")]:
            out.append(lst(sub, depth + 1))
    return "\n".join(out)


def quote(s):
    return "\n".join("> " + l if l.strip() else ">" for l in s.strip().split("\n"))


def ch_no(no):
    m = re.match(r"CHAPTER\s+(\d+)", no)
    if m:
        return f"第 {int(m.group(1))} 章"
    if no.startswith("OUTLINE"):
        return "大纲"
    if no.startswith("APPENDIX"):
        return "附录 " + no.split("·")[-1].strip()
    return no


def block(n):
    if isinstance(n, str):
        s = n.strip()
        return (re.sub(r"\s+", " ", s) + "\n\n") if s else ""
    c = n.cls()
    tag = n.tag
    if tag in ("h1", "h2", "h3", "h4"):
        return "#" * {"h1": 1, "h2": 2, "h3": 3, "h4": 4}[tag] + " " + inline(n).strip() + "\n\n"
    if tag == "p":
        return inline(n).strip() + "\n\n"
    if tag in ("ul", "ol"):
        return lst(n) + "\n\n"
    if tag == "table" or "tbl" in c:
        return table(n) + "\n\n"
    if "part-head" in c:
        return "---\n\n# " + inline(find(n, lambda k: k.tag == "h1")).strip() + "\n\n> " + inline(find(n, lambda k: k.tag == "p")).strip() + "\n\n"
    if "cover" in c:
        return ""
    if "ch-head" in c:
        no = ch_no(inline(find(n, lambda k: "no" in k.cls())).strip())
        h = inline(find(n, lambda k: k.tag == "h1")).strip()
        lead = find(n, lambda k: "lead" in k.cls())
        return f"# {no} · {h}\n\n*{inline(lead).strip() if lead else ''}*\n\n"
    if "goals" in c:
        return "**本章目标**\n\n" + lst(find(n, lambda k: k.tag == "ul")) + "\n\n"
    if "oc" in c:
        oh = find(n, lambda k: "oh" in k.cls())
        ono = inline(find(oh, lambda k: "ono" in k.cls())).strip()
        ot = inline(find(oh, lambda k: "ot" in k.cls())).strip()
        pri = find(oh, lambda k: "pri" in k.cls())
        out = f"### {ono} · {ot}" + (f"（{inline(pri).strip()}）" if pri else "") + "\n\n"
        oq = find(n, lambda k: "oq" in k.cls())
        if oq:
            out += quote(inline(oq).strip()) + "\n\n"
        og = find(n, lambda k: "og" in k.cls())
        for cell in [k for k in og.kids if isinstance(k, Node)]:
            lab = find(cell, lambda k: "ol" in k.cls())
            lst_ = find(cell, lambda k: k.tag in ("ul", "ol"))
            out += f"**{inline(lab).strip()}**\n\n" + (lst(lst_) if lst_ else "") + "\n\n"
        meta = find(n, lambda k: "ometa" in k.cls())
        if meta:
            out += f"*{inline(meta).strip()}*\n\n"
        return out
    if "co" in c:
        h = find(n, lambda k: "h" in k.cls() and k.tag == "span")
        title = inline(h).strip() if h else ""
        body = "".join(block(k) for k in n.kids if k is not h)
        return quote(f"**{title}**\n\n{body}") + "\n\n"
    if "recap" in c:
        t = find(n, lambda k: "t" in k.cls())
        return quote(f"**{inline(t).strip()}**\n\n" + "".join(block(k) for k in n.kids if k is not t)) + "\n\n"
    if tag == "details":
        s = find(n, lambda k: k.tag == "summary")
        body = "".join(inline(k) for k in n.kids if k is not s and isinstance(k, Node)).strip()
        return f"**{inline(s).strip()}**\n\n> 答：{body}\n\n"
    if "cards" in c:
        out = []
        for card in [k for k in n.kids if isinstance(k, Node) and "card" in k.cls()]:
            t = find(card, lambda k: "t" in k.cls())
            p = find(card, lambda k: k.tag == "p")
            out.append(f"- **{inline(t).strip()}**：{inline(p).strip() if p else ''}")
        return "\n".join(out) + "\n\n"
    if "lab" in c:
        return ""
    return "".join(block(k) for k in n.kids)


PH = []      # 块级占位（代码、图、推导、公式块）
MT = []      # 行内公式占位


def pre(t):
    def ph(s):
        PH.append(s)
        return f"\n§§{len(PH) - 1}§§\n"

    def code(m):
        lang, title, body = m.group(1), m.group(2).strip(), m.group(3).strip("\n")
        return ph(f"*{title}*\n\n```{ {'cpp': 'cpp', 'cs': 'csharp', 'py': 'python', 'bash': 'bash'}.get(lang, 'text') }\n{body}\n```")
    t = B.CODE_RE.sub(code, t)
    t = B.DERIV_RE.sub(lambda m: ph(md_deriv(m.group(1), m.group(2))), t)

    def mac(m):
        kind, d = m.group(1), json.loads(m.group(2))
        cap = plain(d.get("cap", ""))
        key = re.search(r"图 [A-Z]?\d+-\d+", cap)
        if kind in ("flow", "graph") and key and key.group(0) in ASCII:
            body = f"```text{ASCII[key.group(0)]}```"
        else:
            body = {"bars": lambda: md_bars(d), "conv": lambda: md_conv(d), "flow": lambda: md_flow(d, cap)}.get(kind, lambda: B.F.MD[kind](d))()
        if kind == "graph" and not (key and key.group(0) in ASCII):
            print("[warn] graph 图没有 ASCII 版：", cap[:30])
        return ph(f"{body}\n\n*{fix_math(cap.replace('**', ''))}*" if cap else body)
    t = B.MACRO_RE.sub(mac, t)
    t = B.VAL_RE.sub(B.val_sub, t)

    def figure(m):
        cap = re.search(r"<figcaption>(.*?)</figcaption>", m.group(0), re.S)
        cap = plain(cap.group(1)) if cap else ""
        key = re.search(r"图 [A-Z]?\d+-\d+", cap)
        art = ASCII.get(key.group(0)) if key else None
        if art is None:
            print("[warn] 图没有 ASCII 版：", cap[:30])
        return ph((f"```text{art}```\n\n" if art else "") + f"*{fix_math(cap.strip().replace('**', ''))}*")
    t = re.sub(r"<figure[^>]*>[\s\S]*?</figure>", figure, t)
    t = B.DISPLAY_RE.sub(lambda m: ph("$$\n" + B.expand_macros(m.group(1).strip()) + "\n$$"), t)

    def im(m):
        MT.append("$" + B.expand_macros(m.group(1).strip()) + "$")
        return f"⟦{len(MT) - 1}⟧"
    t = B.INLINE_RE.sub(im, t)
    return t


def unmath(s):
    s = re.sub(r"[ \t]*§§(\d+)§§[ \t]*", lambda m: "\n\n" + PH[int(m.group(1))] + "\n\n", s)
    s = re.sub(r"⟦(\d+)⟧", lambda m: MT[int(m.group(1))], s).replace("\\$", "$")
    return B.VAL_RE.sub(B.val_sub, s)


def header_rows(keys=None):
    rows = [r for r in QA if keys is None or set(r["keys"]) & set(keys)]
    return "\n".join(f"| {r['time']} | {r['q']} | {r['a']} |" for r in rows) or "| — | （暂无） | — |"


HEADER = f"""# {BOOK}

> 配套电子书：`RL_GameAI.html` / `RL_GameAI.pdf`；单章 Markdown：`md/`；实测程序：`demos/`：`ch03_mdp.cpp` → `ch03_output.txt/json`（E1~E4：三方对拍 / MC 误差 / 价值迭代 / 8 策略穷举），`ch04_dp.cpp` → `ch04_output.txt/json`（E1~E4：4×4 网格 / 地牢 VI·PI / γ 与扫描次数 / 随机迷宫计时），`ch04_interview.py` → `ch04_interview_output.txt`（手撕 VI / PI 对拍），`ch05_random_walk.cpp` → `ch05_output.txt/json`（E1~E4：5 状态随机游走学习曲线 / 批量 MC vs TD / 19 状态 n 步与 TD(λ) / 恒等式数值验证），`ch05_interview.py` → `ch05_interview_output.txt`（手撕 MC / TD / TD(λ) / λ-回报对拍）；Part III / IV：公共头文件 `rlcommon.h`，`ch08_dqn.cpp`、`ch09_pg.cpp`、`ch10_ac_gae.cpp`、`ch11_ppo.cpp`、`ch12_sac.cpp`、`ch13_rlhf.cpp`、`ch14_imitation.cpp`、`ch15_selfplay.cpp`、`ch16_shaping.cpp`、`ch17_mcts.cpp` → 各自的 `chNN_output.txt/json`，`ch18_action_head.py` → `ch18_output.txt/json`，以及 `ch08`~`ch17` 的 `chNN_interview.py` → `chNN_interview_output.txt`（手撕代码对拍）；Part I：`ch01_math.py` → `ch01_output.txt/json`、`ch02_ml.py` → `ch02_output.txt/json`；第 6、7 章：`ch06_control.cpp`、`ch07_bandit.cpp` → `ch06/ch07_output.txt/json`，`ch06_interview.py`（与 C++ 公共随机数逐回合对拍）、`ch07_interview.py`；Part V：`ch19_distributed.cpp` + `ch19_interview.py`、`ch20_infer_bench.cpp`（计时数字每次运行有波动）、`ch21_eval_ci.py`。
> 本文档与电子书由同一份源文件（`src/chapters/*.html`）生成：公式保留 LaTeX 源码，图中数字由构建脚本或 demo 实测算出。

## 提问记录

| 时间 | 原始提问（摘要） | 回答对应章节 |
|---|---|---|
{{ROWS}}

## 目录

{{TOC}}

"""


def safe(s):
    return re.sub(r'[\\/:*?"<>|：，、（）()\s]+', "", s)[:24]


def main():
    B.load_data()
    chapters = []   # (keys, title_for_file, md)
    for f in sorted((ROOT / "chapters").glob("*.html")):
        t = pre(B.include(f.read_text(encoding="utf-8")))
        p = P()
        p.feed(t)
        md = unmath(block(p.root))
        md = re.sub(r"\n{3,}", "\n\n", md)
        ids = re.findall(r'<section class="chapter" id="([^"]+)"', f.read_text(encoding="utf-8"))
        chapters.append((ids, md))
    body = "".join(md for _, md in chapters)
    toc = []
    for m in re.finditer(r"^(#{1,2}) (.+)$", body, re.M):
        lvl, title = len(m.group(1)), m.group(2).strip()
        if lvl == 1 and title.startswith(("导读", "Part", "附录")) and "·" not in title[:4]:
            toc.append(f"- **{title}**")
        elif lvl == 1:
            toc.append(f"- {title}")
    md = HEADER.replace("{ROWS}", header_rows()).replace("{TOC}", "\n".join(toc)) + body
    left = re.findall(r"<[a-zA-Z/][^>]*>", re.sub(r"```[\s\S]*?```|`[^`\n]*`|\$\$[\s\S]*?\$\$|\$[^$\n]*\$|<https?://[^>]+>", "", md))
    if left:
        print("[warn] 残留标签：", left[:8])
    OUT.write_text(md, encoding="utf-8")
    print(f"OK -> {OUT.name} ({OUT.stat().st_size / 1024:.1f} KB, lines={md.count(chr(10))}, "
          f"math_inline={len(MT)}, code={md.count('```') // 2})")

    MD_DIR.mkdir(exist_ok=True)
    for ids, cmd in chapters:
        if not ids:
            continue
        h = re.search(r"^# ((?:第 \d+ 章|大纲|附录 \w+) · .+)$", cmd, re.M)
        title = h.group(1) if h else ids[0]
        if ids[0] == "outline":
            fn = "Outline_" + safe(title.split("·", 1)[-1].split("：")[0]) + ".md"
        else:
            m = re.match(r"第 (\d+) 章 · (.+)", title)
            fn = (f"Ch{int(m.group(1)):02d}_" + safe(m.group(2).split("：")[0]) + ".md") if m else safe(title) + ".md"
        head = (f"# {BOOK} · 单章\n\n> 合并版：`../RL_GameAI_强化学习面试指南.md`　电子书：`../RL_GameAI.html`\n\n"
                f"## 本章提问记录\n\n| 时间 | 原始提问（摘要） | 回答对应章节 |\n|---|---|---|\n{header_rows(ids)}\n\n")
        (MD_DIR / fn).write_text(head + cmd, encoding="utf-8")
        print("   单章 ->", f"md/{fn}")


if __name__ == "__main__":
    main()
