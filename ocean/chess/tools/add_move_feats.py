#!/usr/bin/env python3
"""Give a plain chess checkpoint the per-move feature weights, all zero (2026-11-16), so a net trained without
-DCHESS_MOVE_FEATS continues in a trainer built with it and plays exactly as before until the weights move.
Plain order: enc, dec, w1, w2, gru[0..L-1]. Native -DCHESS_MOVE_FEATS order: enc, dec, w1, w2, mf_w (8 floats, see
CHESS_MF_STORE in chess_decoder.cu), mf_u (7 x hidden), gru[0..L-1].
Run it on the .bin and on the .bin.magnet. Usage: add_move_feats.py IN.bin OUT.bin [--hidden 1024] [--layers 2] [--obs 1307]"""
import argparse
import numpy as np

A, CH, CF, MF, MF_STORE = 4097, 256, 794, 7, 8
ap = argparse.ArgumentParser()
ap.add_argument('src'); ap.add_argument('dst')
ap.add_argument('--hidden', type=int, default=1024); ap.add_argument('--layers', type=int, default=2)
ap.add_argument('--obs', type=int, default=1307)
a = ap.parse_args()
H = a.hidden
head = H * a.obs + (A + 1) * H + CH * CF + CH
gru = a.layers * 3 * H * H
flat = np.fromfile(a.src, dtype=np.float32)
assert len(flat) == head + gru, f'{a.src}: {len(flat)} floats, a plain {H}x{a.layers} net has {head + gru}'
out = np.concatenate([flat[:head], np.zeros(MF_STORE + MF * H, np.float32), flat[head:]])
out.tofile(a.dst)
print(a.dst, len(out), 'floats')
