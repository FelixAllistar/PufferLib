#!/usr/bin/env python3
"""Port check (2026-11-19): how often the Python net's top move equals the move the native eval played, over native
eval game logs (CHESS_GAMES_OUT: color, result, start fen, all moves). Tries several weight orders and two ways of
advancing the recurrent state (every ply, or only on our own turns as in the bot-eval env).
Usage: agree.py CKPT GAMES.txt [games]"""
import itertools, os, sys
import numpy as np, torch
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import engine2 as E

H = 1024
S = {'enc': (H, E.OBS_END), 'dec': (E.A + 1, H), 'w1': (E.CH, E.CF), 'w2': (E.CH,), 'mf_w': (12,), 'mf_u': (9, H),
     'gru0': (3 * H, H), 'gru1': (3 * H, H)}
flat = np.fromfile(sys.argv[1], dtype=np.float32)
lines = [l.rstrip('\n').split('\t') for l in open(sys.argv[2]) if l.strip()][:int(sys.argv[3]) if len(sys.argv) > 3 else 20]
r = E.Rules(); n = E.Net2(sys.argv[1])
blocks = ['enc', 'dec', 'w1w2', 'mf', 'gru']
expand = {'enc': ['enc'], 'dec': ['dec'], 'w1w2': ['w1', 'w2'], 'mf': ['mf_w', 'mf_u'], 'gru': ['gru0', 'gru1']}
for perm in itertools.permutations(blocks):
    order = [k for b in perm for k in expand[b]]
    i = 0
    for k in order:
        m = int(np.prod(S[k])); n.p[k] = torch.from_numpy(flat[i:i + m].reshape(S[k]).copy()); i += m
    n.gru = [n.p['gru0'], n.p['gru1']]
    for every_ply in (1, 0):
        hit = tot = 0
        for color, _, fen, moves in lines:
            moves = moves.split(); me = 0 if color == 'w' else 1
            st = n.zero()
            for p in range(len(moves)):
                side, obs, mask, legal, _ = r.state(fen, moves[:p], me)
                if side != me and not every_ply:
                    continue
                lg, st = n.step(obs, st)
                if side == me:
                    a = int(torch.argmax(lg.masked_fill(torch.from_numpy(mask == 0), -1e9)))
                    hit += E.action_uci(a, side, legal)[:4] == moves[p][:4]; tot += 1
        print(f'{",".join(perm):28s} every_ply={every_ply}  agree {hit}/{tot} = {hit / max(tot, 1):.3f}', flush=True)
