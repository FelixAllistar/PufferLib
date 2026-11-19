#!/usr/bin/env python3
"""Sanity check for engine2.py (2026-11-19): the Python main2 engine (policy only, or with its search) vs top-move Maia
(lc0, 1 node) from the eval openings, colors alternating. The native eval scored main2 .877 vs Maia 1100 (1000 games);
a correct port lands near that. Usage: check_vs_maia.py OPENINGS LC0 WEIGHTS [games] [search]"""
import os, subprocess, sys
import numpy as np, torch
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import engine2 as E

fens = [l.strip() for l in open(sys.argv[1]) if l.strip()]
lc0 = subprocess.Popen([sys.argv[2], '--weights=' + sys.argv[3], '--threads=1'], stdin=subprocess.PIPE,
                       stdout=subprocess.PIPE, stderr=subprocess.DEVNULL, text=True, bufsize=1)
games, depth = (int(sys.argv[4]) if len(sys.argv) > 4 else 60), (int(sys.argv[5]) if len(sys.argv) > 5 else 0)


def cmd(c):
    lc0.stdin.write(c + '\n')


def wait(tok):
    while True:
        l = lc0.stdout.readline()
        if l.startswith(tok):
            return l


cmd('uci'); wait('uciok')
r, n = E.Rules(), E.Net2(E.MODEL)
pts = 0.0
for g in range(games):
    fen, moves, me = fens[g % len(fens)], [], g % 2  # me: our color
    side0, *_ = r.state(fen, [], 0)
    st, res = n.zero(), None
    cmd('ucinewgame')
    for ply in range(400):
        side, obs, mask, legal, status = r.state(fen, moves, me)
        if status:
            res = 0.5 if status != 1 else (0.0 if side == me else 1.0)
            break
        logits, st = n.step(obs, st)  # our net sees every ply, as in self-play
        if side == me:
            lg = logits.masked_fill(torch.from_numpy(mask == 0), -1e9)
            kt = torch.topk(lg, min(5 if depth else 1, int(mask.sum())))
            cands = [E.action_uci(int(i), side, legal) for i in kt.indices]
            if depth and len(cands) > 1:
                sc = r.search(fen, moves, cands, depth)
                mv = cands[min(i for i in range(len(cands)) if sc[i] >= max(sc) - 50)]
            else:
                mv = cands[0]
        else:
            cmd('position fen ' + fen + (' moves ' + ' '.join(moves) if moves else '')); cmd('go nodes 1')
            mv = wait('bestmove').split()[1]
        moves.append(mv)
    if res is None:
        res = 0.5
    pts += res
    print(f'game {g + 1}: {"white" if me == 0 else "black"} {res}  running {pts / (g + 1):.3f}', flush=True)
cmd('quit')
print(f'score {pts / games:.3f} over {games} games (search {depth})')
