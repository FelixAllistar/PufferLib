#!/usr/bin/env python3
"""On-policy positions for the Stockfish teacher (2026-11-12): the current checkpoint plays itself (sampled moves,
each side with its own MinGRU state, as in self-play training) from start positions, writing 'FEN\\tUCI moves\\tsearch'
lines that sf_label.py labels. search[i] is what net + search would play at ply i (play.py --search 2: of the policy's
top 5, the most likely move within 50 cp of the best material score after a 2-ply + quiescence search). Starts: a file of FENs (eval openings, generated states), one per line.
Usage: selfplay_games.py CKPT STARTS OUT [--games N] [--seed S] [--max-plies 300]
2026-11-14 (DAgger round for the Stockfish-BC nets): --hidden/--move-feats for the feature nets, --temp for sampling,
--no-search to skip the search column, --lines: STARTS is a lichess_games.py file and each game starts from the start
position after a random 6-16 ply prefix of one of its games (the prefix is part of the written moves)."""
import argparse, os, sys, random
import numpy as np
import torch
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from play import Rules, Net, action_uci  # noqa: E402


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('ckpt'); ap.add_argument('starts'); ap.add_argument('out')
    ap.add_argument('--games', type=int, default=1000); ap.add_argument('--seed', type=int, default=1)
    ap.add_argument('--max-plies', type=int, default=300)
    ap.add_argument('--depth', type=int, default=2); ap.add_argument('--k', type=int, default=5)
    ap.add_argument('--margin', type=int, default=50)
    ap.add_argument('--hidden', type=int, default=1024); ap.add_argument('--layers', type=int, default=2)
    ap.add_argument('--move-feats', action='store_true'); ap.add_argument('--temp', type=float, default=1.0)
    ap.add_argument('--no-search', action='store_true'); ap.add_argument('--lines', action='store_true')
    args = ap.parse_args()
    START = 'rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1'
    if args.lines:
        # every 50th line (offset by seed): all 1M lines as lists cost ~3 GB per process and 56 jobs ran out of memory
        starts = [l.rstrip('\n').split('\t')[1].split()[:16] for i, l in enumerate(open(args.starts)) if i % 50 == args.seed % 50 and '\t' in l]
    else:
        starts = [l.split('\t')[0].strip() for l in open(args.starts) if l.strip()]
    rng = random.Random(args.seed); torch.manual_seed(args.seed)
    rules = Rules(); net = Net(args.ckpt, rules.obs_n, args.hidden, args.layers, 7 if args.move_feats else 0)
    out = open(args.out, 'a')
    for g in range(args.games):
        moves, st, srch = [], [net.zero(), net.zero()], []
        if args.lines:
            fen, line = START, rng.choice(starts)
            for n in range(min(len(line), rng.randint(6, 16))):  # human prefix: both views observe it
                for view in (0, 1):
                    _, obs, *_ = rules.state(fen, moves, view); _, _, st[view] = net.step(obs, st[view])
                moves.append(line[n]); srch.append(line[n])
        else:
            fen = rng.choice(starts)
        while len(moves) < args.max_plies:
            side = None
            for view in (0, 1):  # both players observe every ply (the side not to move passes)
                s, obs, mask, legal, status = rules.state(fen, moves, view)
                if status:
                    break
                mf = rules.move_feats(fen, moves) if args.move_feats and view == s else None
                logits, _, st[view] = net.step(obs, st[view], mf)
                if view == s:
                    side, lg, lm = s, logits, (mask, legal)
            if status:
                break
            mask, legal = lm
            pr = torch.softmax(lg.masked_fill(torch.from_numpy(mask == 0), -1e9), 0)
            if not args.no_search:
                kt = torch.topk(pr, min(args.k, int(mask.sum())))
                cands = [action_uci(int(i), side, legal) for i in kt.indices]
                sc = rules.search(fen, moves, cands, args.depth)
                ok = [i for i in range(len(cands)) if sc[i] >= max(sc) - args.margin]
                srch.append(cands[min(ok)])  # top-k is sorted by probability: the most likely acceptable move
            pt = torch.softmax(lg.masked_fill(torch.from_numpy(mask == 0), -1e9) / args.temp, 0)
            moves.append(action_uci(int(torch.multinomial(pt, 1)), side, legal))
        out.write(f'{fen}\t{" ".join(moves)}' + ('' if args.no_search else f'\t{" ".join(srch)}') + '\n'); out.flush()


if __name__ == '__main__':
    main()
