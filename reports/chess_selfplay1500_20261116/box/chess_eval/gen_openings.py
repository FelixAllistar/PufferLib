#!/usr/bin/env python3
"""Human-like opening positions for chess evals (2026-11-06): N games of 8 plies (4 moves each side) from the start,
each move sampled from Maia-1900's policy (uci_maia_sample.py, temperature 1), deduplicated. Evals start every game
from a random line so a deterministic opponent (top-move Maia, Stockfish) does not replay the same game.
Usage: gen_openings.py OUT N"""
import os, sys, chess, chess.engine
out, n = sys.argv[1], int(sys.argv[2])
root = '/workspace/PufferLib'
os.environ.update(MAIA_LC0_BIN=f'{root}/lc0bin', MAIA_TEMP='1', MAIA_SAMPLE_SEED='11')
eng = chess.engine.SimpleEngine.popen_uci([f'{root}/reports/chess_20261101/uci_maia_sample.py',
                                           f'--weights={root}/lc0/maia-1900.pb.gz'])
seen = []
for g in range(n * 3):
    b = chess.Board()
    while b.ply() < 8 and not b.is_game_over():
        b.push(eng.play(b, chess.engine.Limit(nodes=1)).move)
    if not b.is_game_over() and b.fen() not in seen:
        seen.append(b.fen())
    if len(seen) == n:
        break
eng.quit()
open(out, 'w').write('\n'.join(seen) + '\n')
print(len(seen), 'openings')
