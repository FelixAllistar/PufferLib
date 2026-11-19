#!/usr/bin/env python3
"""Baseline for endgame_probe.py's convert test: Stockfish (same node limit) plays the strong side too, same positions
(same seed), so we know how many of the random positions are really won. Usage: sf_baseline.py [--n2 60] [--sets ...]"""
import argparse, collections, random, sys
import endgame_probe as ep
import chess, chess.engine

ap = argparse.ArgumentParser()
ap.add_argument('--n2', type=int, default=60); ap.add_argument('--sets', default='KRPPKPP,KQRKR,KQNPPKRPP,KQPPKPP')
ap.add_argument('--nodes', type=int, default=4000); ap.add_argument('--seed', type=int, default=1)
a = ap.parse_args()
sf = chess.engine.SimpleEngine.popen_uci(ep.SF_BIN); sf.configure({'Threads': 1, 'Hash': 16})
for name in a.sets.split(','):
    rng, c = random.Random(a.seed + 1), collections.Counter()
    for g in range(a.n2):
        strong = g % 2 == 0
        b = ep.rand_pos(rng, name, strong)
        seen, out = collections.Counter([b._transposition_key()]), None
        while out is None:
            b.push(sf.play(b, chess.engine.Limit(nodes=a.nodes)).move)
            seen[b._transposition_key()] += 1
            out = (('mate' if b.turn != strong else 'got_mated') if b.is_checkmate() else 'stalemate' if b.is_stalemate()
                   else 'lost_material' if b.is_insufficient_material() else 'threefold' if seen[b._transposition_key()] >= 3
                   else 'fifty' if b.halfmove_clock >= 100 else 'cap' if b.ply() > 600 else None)
        c[out] += 1
    print(f'stockfish as strong side {name}: n={a.n2} ' + ' '.join(f'{k}={v}' for k, v in c.most_common()), flush=True)
sf.quit()
