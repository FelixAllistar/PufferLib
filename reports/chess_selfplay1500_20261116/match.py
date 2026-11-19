#!/usr/bin/env python3
"""Head-to-head between two chess checkpoints (2026-11-17), CPU, greedy policies, no search, env rules
(threefold, 50-move). Each opening is played twice with colors swapped. Nets may differ in size and in whether they
have per-move features, so arms from different builds can meet (the trainer cannot do that).
Checkpoints must be in play.py's order (plain, or mf_to_native.py --reverse for a features net).
Usage: match.py A.bin B.bin OUT.jsonl [--a-hidden 256] [--a-feats] [--b-hidden 256] [--b-feats] [--openings 200]
       [--fens FILE] [--first 0]. Prints A's score."""
import argparse, collections, json, os, sys
ENG = os.path.expanduser('~/tools/puffer_chess_engine')
sys.path.insert(0, ENG)
import numpy as np, torch  # noqa: E402
from play import Rules, Net, action_uci  # noqa: E402


class Player:
    def __init__(self, rules, path, hidden, feats):
        self.rules, self.feats = rules, feats
        self.net = Net(path, rules.obs_n, hidden, 2, 7 if feats else 0)

    def start(self, fen, side):
        self.fen, self.side, self.state = fen, side, self.net.zero()

    def see(self, moves):
        """Step the net on the position after `moves` (every ply, as in self-play); returns its move if it is to move."""
        side, obs, mask, legal, status = self.rules.state(self.fen, moves, self.side)
        mf = self.rules.move_feats(self.fen, moves) if self.feats and side == self.side else None
        logits, _, self.state = self.net.step(obs, self.state, mf)
        if side != self.side or status:
            return None
        return action_uci(int(logits.masked_fill(torch.from_numpy(mask == 0), -1e9).argmax()), side, legal)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('a'); ap.add_argument('b'); ap.add_argument('out')
    ap.add_argument('--a-hidden', type=int, default=256); ap.add_argument('--a-feats', action='store_true')
    ap.add_argument('--b-hidden', type=int, default=256); ap.add_argument('--b-feats', action='store_true')
    ap.add_argument('--openings', type=int, default=200); ap.add_argument('--first', type=int, default=0)
    ap.add_argument('--fens', default='/mnt/d/puffer_media/box_backups/rtx5080_box_60549588/loc/chess_eval/resources/chess/fens.txt')
    ap.add_argument('--max-plies', type=int, default=600)
    args = ap.parse_args()
    torch.set_num_threads(1)
    rules = Rules()
    A = Player(rules, args.a, args.a_hidden, args.a_feats); B = Player(rules, args.b, args.b_hidden, args.b_feats)
    fens = [l.strip() for l in open(args.fens) if l.strip()]
    out = open(args.out, 'a'); score = 0.0; n = 0; res = collections.Counter()
    for g in range(args.first, args.first + args.openings):
        for a_side in (0, 1):
            fen, moves = fens[g % len(fens)], []
            A.start(fen, a_side); B.start(fen, 1 - a_side)
            status = 0
            while len(moves) < args.max_plies:
                side, _, _, _, status = rules.state(fen, moves, 0)
                if status:
                    break
                ma, mb = A.see(moves), B.see(moves)
                moves.append(ma if side == a_side else mb)
            # status 1 = side to move is mated; anything else (or the ply cap) is a draw
            side = rules.state(fen, moves, 0)[0]
            r = 0.5 if status != 1 else (0.0 if side == a_side else 1.0)
            score += r; n += 1; res[r] += 1
            out.write(json.dumps({'opening': g, 'a_side': a_side, 'result': r, 'status': status, 'plies': len(moves),
                                  'fen': fen, 'moves': ' '.join(moves)}) + '\n'); out.flush()
    se = (score / n * (1 - score / n) / n) ** 0.5
    print(f'A {os.path.basename(args.a)} vs B {os.path.basename(args.b)}: A scores {score / n:.3f} +-{se:.3f} over {n} games '
          f'(W/D/L {res[1.0]}/{res[0.5]}/{res[0.0]})')


if __name__ == '__main__':
    main()
