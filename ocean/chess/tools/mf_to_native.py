#!/usr/bin/env python3
"""Reorder a bc_train.py --move-feats checkpoint for the native trainer built with -DCHESS_MOVE_FEATS (2026-11-14).
BC order: enc, dec, w1, w2, gru[0..L-1], mf_w (7), mf_u. Native registration order (encoder, decoder, network):
enc, dec, w1, w2, mf_w (8: 7 + one unused 0, see CHESS_MF_STORE in chess_decoder.cu), mf_u, gru[0..L-1].
Same float32 layout per tensor. Checkpoints written by trainer builds from before 2026-11-17 are NOT in this layout
(their tensors after mf_w were one float off) and cannot be converted.
Usage: mf_to_native.py IN.bin OUT.bin [--hidden 2048] [--layers 2] [--obs 1307] [--reverse (native -> BC)]"""
import argparse
import numpy as np

A, CH, CF, MF, MF_STORE = 4097, 256, 794, 7, 8
ap = argparse.ArgumentParser()
ap.add_argument('src'); ap.add_argument('dst')
ap.add_argument('--hidden', type=int, default=2048); ap.add_argument('--layers', type=int, default=2)
ap.add_argument('--obs', type=int, default=1307); ap.add_argument('--reverse', action='store_true')
a = ap.parse_args()
H = a.hidden
sizes = [('enc', H * a.obs), ('dec', (A + 1) * H), ('w1', CH * CF), ('w2', CH)] + \
        [(f'gru{i}', 3 * H * H) for i in range(a.layers)] + [('mf_w', MF), ('mf_u', MF * H)]
bc_order = [k for k, _ in sizes]
native_order = ['enc', 'dec', 'w1', 'w2', 'mf_w', 'mf_u'] + [f'gru{i}' for i in range(a.layers)]
src_order, order = (native_order, bc_order) if a.reverse else (bc_order, native_order)
n_of = dict(sizes)
flat = np.fromfile(a.src, dtype=np.float32)
pad = MF_STORE - MF  # the native file's mf_w carries this many unused floats
assert len(flat) == sum(n for _, n in sizes) + (pad if a.reverse else 0), (len(flat), sum(n for _, n in sizes))
parts, i = {}, 0
for k in src_order:
    parts[k] = flat[i:i + n_of[k]]; i += n_of[k]
    if k == 'mf_w' and a.reverse:
        i += pad
out = []
for k in order:
    out.append(parts[k])
    if k == 'mf_w' and not a.reverse:
        out.append(np.zeros(pad, np.float32))
np.concatenate(out).astype(np.float32).tofile(a.dst)
print(a.dst, 'mf_w', parts['mf_w'], '|mf_u|', float(np.abs(parts['mf_u']).mean()))
