#!/usr/bin/env python3
"""MCTS check (2026-11-19): engine2 net + mcts.py vs top-move Maia (lc0, 1 node) from the eval openings, colors
alternating, with the per-move time. Usage: check_mcts.py OPENINGS LC0 WEIGHTS games sims [prior_t] [c_puct]"""
import os, subprocess, sys, time
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import engine2 as E
from mcts import MCTS

fens = [l.strip() for l in open(sys.argv[1]) if l.strip()]
lc0 = subprocess.Popen([sys.argv[2], '--weights=' + sys.argv[3], '--threads=1'], stdin=subprocess.PIPE,
                       stdout=subprocess.PIPE, stderr=subprocess.DEVNULL, text=True, bufsize=1)
games, sims = int(sys.argv[4]), int(sys.argv[5])
pt = float(sys.argv[6]) if len(sys.argv) > 6 else 4.0
cp = float(sys.argv[7]) if len(sys.argv) > 7 else 1.5


def cmd(c):
    lc0.stdin.write(c + '\n')


def wait(tok):
    while True:
        l = lc0.stdout.readline()
        if l.startswith(tok):
            return l


cmd('uci'); wait('uciok')
r, n = E.Rules(), E.Net2(E.MODEL)
search = MCTS(r, n, E.action_uci, sims=sims, c_puct=cp, prior_t=pt)
pts, tsum, tn = 0.0, 0.0, 0
for g in range(games):
    fen, moves, me = fens[g % len(fens)], [], g % 2
    st, res = [n.zero(), n.zero()], None
    cmd('ucinewgame')
    for ply in range(400):
        side, obs, mask, legal, status = r.state(fen, moves, me)
        if status:
            res = 0.5 if status != 1 else (0.0 if side == me else 1.0)
            break
        if side == me:
            t0 = time.time()
            mv, root = search.run(fen, moves, st)
            tsum += time.time() - t0; tn += 1
            st = root.states
        else:
            for view in (0, 1):
                _, o, *_ = r.state(fen, moves, view)
                _, st[view] = n.step(o, st[view])
            cmd('position fen ' + fen + (' moves ' + ' '.join(moves) if moves else '')); cmd('go nodes 1')
            mv = wait('bestmove').split()[1]
        moves.append(mv)
    if res is None:
        res = 0.5
    pts += res
    print(f'game {g + 1}: {"white" if me == 0 else "black"} {res}  running {pts / (g + 1):.3f}  {tsum / max(tn, 1):.2f}s/move', flush=True)
cmd('quit')
print(f'score {pts / games:.3f} over {games} games (mcts sims {sims}, prior_t {pt}, c {cp}), {tsum / max(tn, 1):.2f}s/move')
