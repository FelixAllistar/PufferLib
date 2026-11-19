#!/usr/bin/env python3
"""AlphaZero-style search (PUCT) for engine2's nets (2026-11-19). Priors = the policy (with its per-move features) from
the side to move's view, softened by PRIOR_T; leaf value = the net's critic for the side to move; terminal positions
are scored exactly (mate -1 for the mated side, draws 0, using the real game history for repetition). The net is
recurrent: every node carries both players' MinGRU states, each advanced on that node's position as in self-play.
Each expansion costs 2 net steps. The move played is the most visited root child."""
import math
import numpy as np, torch


class Node:
    __slots__ = ('moves', 'stm', 'legal', 'prior', 'children', 'N', 'W', 'value', 'terminal', 'states')

    def __init__(self, moves):
        self.moves, self.children, self.N, self.W = moves, {}, 0, 0.0
        self.terminal, self.prior, self.states = None, None, None


class MCTS:
    def __init__(self, rules, net, action_uci, sims=128, c_puct=1.5, prior_t=4.0, fpu=0.2):
        self.r, self.n, self.uci = rules, net, action_uci
        self.sims, self.c, self.t, self.fpu = sims, c_puct, prior_t, fpu

    def expand(self, node, fen, parent_states):
        """Step both views on this node's position; set priors (side to move) and the critic value."""
        stm, obs_w_view, mask, legal, status = None, None, None, None, None
        states, logits_stm, obs_stm = [None, None], None, None
        for view in (0, 1):
            side, obs, m, lg_list, st = self.r.state(fen, node.moves, view)
            logits, states[view] = self.n.step(obs, parent_states[view])
            if view == side:
                stm, mask, legal, status, logits_stm, obs_stm = side, m, lg_list, st, logits, obs
        node.stm, node.states = stm, states
        if status:  # 1 mate (side to move lost), 2-5 draws
            node.terminal = -1.0 if status == 1 else 0.0
            node.value = node.terminal
            return node.value
        idx = np.nonzero(mask)[0]
        lg = logits_stm[torch.from_numpy(idx)] / self.t
        p = torch.softmax(lg, 0).numpy()
        node.legal = [self.uci(int(a), stm, legal) for a in idx]
        node.prior = dict(zip(node.legal, p))
        node.value = self.n.value(obs_stm)
        return node.value

    def select(self, node):
        sq = math.sqrt(node.N)
        q_parent = node.W / node.N if node.N else 0.0
        best, best_s = None, -1e9
        for mv, p in node.prior.items():
            ch = node.children.get(mv)
            if ch is None or ch.N == 0:
                q = q_parent - self.fpu
                u = self.c * p * sq
            else:
                q = -ch.W / ch.N  # child values are from the child's side to move
                u = self.c * p * sq / (1 + ch.N)
            if q + u > best_s:
                best, best_s = mv, q + u
        return best

    def run(self, fen, moves, root_states):
        """root_states: both views' MinGRU states BEFORE the root position (the root is stepped here)."""
        root = Node(list(moves))
        self.expand(root, fen, root_states)
        root.N, root.W = 1, root.value
        if root.terminal is not None or len(root.prior) == 1:
            return (root.legal[0] if root.legal else None), root
        for _ in range(self.sims):
            node, path = root, [root]
            while node.terminal is None and node.N > 0 and node.prior is not None:
                mv = self.select(node)
                ch = node.children.get(mv)
                if ch is None:
                    ch = node.children[mv] = Node(node.moves + [mv])
                    v = self.expand(ch, fen, node.states)
                    path.append(ch)
                    break
                node = ch
                path.append(node)
                if node.terminal is not None:
                    v = node.terminal
                    break
            else:
                v = node.value
            # back up: v is from the last node's side to move
            for nd in reversed(path):
                nd.N += 1
                nd.W += v
                v = -v
        best = max(root.children.items(), key=lambda kv: kv[1].N)[0]
        return best, root
