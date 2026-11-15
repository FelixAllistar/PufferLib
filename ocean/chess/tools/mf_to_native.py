#!/usr/bin/env python3
"""Reorder a bc_train.py --move-feats checkpoint for the native trainer built with -DCHESS_MOVE_FEATS (2026-11-14).
BC order: enc, dec, w1, w2, gru[0..L-1], mf_w, mf_u. Native registration order (encoder, decoder, network):
enc, dec, w1, w2, mf_w, mf_u, gru[0..L-1]. Same float32 layout per tensor.
Usage: mf_to_native.py IN.bin OUT.bin [--hidden 2048] [--layers 2] [--obs 1307] [--reverse (native -> BC)]"""
import argparse
import numpy as np

A, CH, CF, MF = 4097, 256, 794, 7
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
assert len(flat) == sum(n for _, n in sizes), (len(flat), sum(n for _, n in sizes))
parts, i = {}, 0
for k in src_order:
    parts[k] = flat[i:i + n_of[k]]; i += n_of[k]
np.concatenate([parts[k] for k in order]).astype(np.float32).tofile(a.dst)
print(a.dst, 'mf_w', parts['mf_w'], '|mf_u|', float(np.abs(parts['mf_u']).mean()))
