#!/usr/bin/env python3
"""Verify critic fitting changed only the value head, including actor forward parity."""
import argparse
import json
import pathlib
import re
import sys
import numpy as np


def verify(before, after, log, data):
    a = np.fromfile(before, np.float32)
    b = np.fromfile(after, np.float32)
    if a.shape != b.shape or not np.isfinite(a).all() or not np.isfinite(b).all():
        raise ValueError('invalid checkpoint dimensions or nonfinite weights')
    match = re.search(r'ranges=(\d+):(\d+),(\d+):(\d+)', pathlib.Path(log).read_text())
    if not match:
        raise ValueError('missing explicit critic parameter ranges')
    lo1, hi1, lo2, hi2 = map(int, match.groups())
    if not 0 <= lo1 < hi1 <= lo2 < hi2 <= a.size:
        raise ValueError('invalid critic ranges')
    mask = np.zeros(a.size, dtype=bool)
    mask[lo1:hi1] = True; mask[lo2:hi2] = True
    different = a.view(np.uint32) != b.view(np.uint32)
    if np.any(different & ~mask) or not np.any(different & mask):
        raise ValueError('actor/encoder changed, or critic did not change')
    sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent / 'submission'))
    from entity_agent import EntityModel
    hidden = int(pathlib.Path(str(before) + '.hidden_size').read_text())
    layers = int(pathlib.Path(str(before) + '.num_layers').read_text())
    m1, m2 = EntityModel(before, hidden, layers), EntityModel(after, hidden, layers)
    # Existing dataset header is 88 bytes; test a real recurrent prefix.
    obs = np.memmap(data, mode='r', dtype=np.float32, offset=88, shape=(24, 1424))
    for row in obs:
        x, y = m1.forward(row), m2.forward(row)
        if not np.array_equal(x[:-1], y[:-1]):
            raise ValueError('actor forward parity failed')
    return dict(actor_encoder_bitwise_unchanged=True, actor_logit_parity_rows=24,
                changed_critic_parameters=int(np.count_nonzero(different)), ranges=[lo1, hi1, lo2, hi2])


if __name__ == '__main__':
    p = argparse.ArgumentParser(description=__doc__)
    for name in ('before', 'after', 'log', 'data'):
        p.add_argument('--' + name, required=True, type=pathlib.Path)
    args = p.parse_args()
    print(json.dumps(verify(args.before, args.after, args.log, args.data), indent=2))
