"""第 17 章手撕代码：MCTS 四步（UCT 选择 / 扩展 / rollout / 视角翻转的回传）、PUCT 打分、AlphaZero 损失。
书中代码块与本文件逐字一致；运行时在 Nim（每次取 1~3 根，取到最后一根者胜）上检查 MCTS 是否找到必胜着法。纯 Python。
"""
import math
import random


# ---------------------------------------------------------------- 书中代码块（开始）
class Node:
    def __init__(self, state, parent=None, move=None):
        self.state, self.parent, self.move = state, parent, move
        self.children, self.N, self.W = [], 0, 0.0      # W：从“走进这个节点的一方”的视角累计的价值
        self.untried = None


def uct_score(child, parent_N, c=1.4):
    return child.W / child.N + c * math.sqrt(math.log(parent_N) / child.N)


def puct_score(q, prior, parent_N, child_N, c=1.5):
    """AlphaZero：Q + c·P·√N(s) / (1 + N(s,a))；没访问过的子节点 Q 记 0，探索项由先验 P 决定。"""
    return q + c * prior * math.sqrt(parent_N) / (1 + child_N)


def mcts(root_state, game, n_sims, rng, c=1.4):
    """game 需要提供 moves(s)、play(s, m)、terminal_value(s)（终局时“刚走完的一方”的得分，非终局返回 None）。"""
    root = Node(root_state)
    root.untried = game.moves(root_state)
    for _ in range(n_sims):
        node = root
        # ① 选择：子节点都展开过就按 UCT 往下走
        while not node.untried and node.children:
            node = max(node.children, key=lambda ch: uct_score(ch, node.N, c))
        # ② 扩展：还有没试过的着法就展开一个
        if node.untried:
            m = node.untried.pop(rng.randrange(len(node.untried)))
            child = Node(game.play(node.state, m), node, m)
            child.untried = game.moves(child.state)
            node.children.append(child)
            node = child
        # ③ 模拟：随机走到终局；value 是“走进 node 的一方”的得分
        s, sign = node.state, 1.0
        v = game.terminal_value(s)
        while v is None:
            s = game.play(s, rng.choice(game.moves(s)))
            sign = -sign
            v = game.terminal_value(s)
        value = v * sign
        # ④ 回传：每往上一层，视角翻转一次
        while node is not None:
            node.N += 1
            node.W += value
            value = -value
            node = node.parent
    return max(root.children, key=lambda ch: ch.N).move     # 最终按访问次数选（比按 Q 选更稳）


def alphazero_loss(z, v, pi, logp, params, c=1e-4):
    """(z − v)² − πᵀ log p + c‖θ‖²：价值回归到对局结果，策略拟合 MCTS 的访问分布，加 L2 正则。"""
    return (z - v) ** 2 - sum(a * b for a, b in zip(pi, logp)) + c * sum(w * w for w in params)
# ---------------------------------------------------------------- 书中代码块（结束）


class Nim:
    """状态 = 剩余根数；取到最后一根的人赢。必胜策略：让对手面对 4 的倍数。"""
    @staticmethod
    def moves(s):
        return [k for k in (1, 2, 3) if k <= s]

    @staticmethod
    def play(s, m):
        return s - m

    @staticmethod
    def terminal_value(s):
        return 1.0 if s == 0 else None          # 走到 0 的那一方取走了最后一根：赢


def main():
    rng = random.Random(17)
    ok_all = True
    for n in [5, 6, 7, 9, 10, 11, 13, 14, 15]:
        best = n % 4
        votes = [mcts(n, Nim, 3000, rng) for _ in range(5)]
        hit = sum(v == best for v in votes)
        ok_all &= hit >= 4
        print(f"[1] Nim n={n:2d}：必胜着法取 {best} 根；5 次 MCTS（各 3000 次模拟）选择 {votes}，命中 {hit}/5")
    s = puct_score(0.0, 0.6, 100, 0), puct_score(0.0, 0.1, 100, 0), puct_score(0.5, 0.1, 100, 20)
    print(f"[2] PUCT：先验 0.6 未访问 {s[0]:.3f}；先验 0.1 未访问 {s[1]:.3f}；Q=0.5、先验 0.1、访问 20 次 {s[2]:.3f}")
    L = alphazero_loss(1.0, 0.6, [0.7, 0.2, 0.1], [math.log(0.5), math.log(0.3), math.log(0.2)], [0.5, -0.5])
    print(f"[3] AlphaZero 损失：(1−0.6)² + 交叉熵 + L2 = {L:.6f}")
    exp = 0.16 - (0.7 * math.log(0.5) + 0.2 * math.log(0.3) + 0.1 * math.log(0.2)) + 1e-4 * 0.5
    print("全部通过" if ok_all and abs(L - exp) < 1e-12 else "有不一致！")


if __name__ == "__main__":
    main()
