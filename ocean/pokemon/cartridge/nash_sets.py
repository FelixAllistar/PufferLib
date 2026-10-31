"""Best picks from the every-set tournament (env.trainer_tournament_sets, 2026-10-31).

Every pair of trainers played every set each could bring (min(party, 3) Pokemon and the lead) against every set of the
other, N games per cell, our model piloting both sides. Picks are simultaneous, so each pair is a zero-sum matrix game:
solved here by regret matching+ (batched over pairs with the same matrix shape). Writes
- PAIRS.tsv: per pair the equilibrium score as "i j a b 0" out of 1000 games (rank.py's input), and
- OUT.json: per trainer its most used equilibrium sets over all opponents, and the equilibrium vs uniform-pick score.
Usage: python3 nash_sets.py CELLS.txt trainers_l100.json GAMES PAIRS.tsv OUT.json
"""
import itertools
import json
import sys

import numpy as np

cells_path, trainers_path, games, pairs_path, out_path = sys.argv[1:6]
games = int(games)
T = json.load(open(trainers_path))
n = len(T)


def sets(size):
    # Same order as pk_build_sets: party subsets by bit mask, then the lead, the rest in party order.
    k = min(size, 3)
    out = []
    for mask in range(1 << size):
        if bin(mask).count('1') != k:
            continue
        members = [i for i in range(size) if mask >> i & 1]
        out += [[lead] + [i for i in members if i != lead] for lead in members]
    return out


S = [sets(len(t['party'])) for t in T]
count = [len(s) for s in S]
score = {}
for line in open(cells_path):
    c, s2 = line.split()
    score[int(c)] = int(s2) / (2 * games)

# Cell layout: pairs i < j in order, each a count[i] x count[j] block.
blocks, cell = [], 0
for i in range(n):
    for j in range(i + 1, n):
        blocks.append((i, j, cell))
        cell += count[i] * count[j]
missing = cell - len(score)
print(f'{len(score)} of {cell} cells ({missing} missing)')


def solve(M, iters=3000):
    """Batched zero-sum games M[b, rows, cols] (row player's score): average strategies of regret matching+."""
    B, R, C = M.shape
    qr, qc = np.zeros((B, R)), np.zeros((B, C))
    ar, ac = np.zeros((B, R)), np.zeros((B, C))
    for t in range(1, iters + 1):
        x = np.where(qr.sum(1, keepdims=True) > 0, qr, 1.0)
        x /= x.sum(1, keepdims=True)
        y = np.where(qc.sum(1, keepdims=True) > 0, qc, 1.0)
        y /= y.sum(1, keepdims=True)
        ar += t * x
        ac += t * y
        u_r = np.einsum('brc,bc->br', M, y)
        u_c = -np.einsum('brc,br->bc', M, x)
        qr = np.maximum(qr + u_r - (u_r * x).sum(1, keepdims=True), 0)
        qc = np.maximum(qc + u_c - (u_c * y).sum(1, keepdims=True), 0)
    ar /= ar.sum(1, keepdims=True)
    ac /= ac.sum(1, keepdims=True)
    value = np.einsum('br,brc,bc->b', ar, M, ac)
    gap = np.einsum('brc,bc->br', M, ac).max(1) - np.einsum('br,brc->bc', ar, M).min(1)
    return ar, ac, value, gap


groups = {}
for b, (i, j, c0) in enumerate(blocks):
    groups.setdefault((count[i], count[j]), []).append(b)
value = np.zeros(len(blocks))
uniform = np.zeros(len(blocks))
usage = [np.zeros(c) for c in count]
gaps = []
for (r, c), members in sorted(groups.items()):
    M = np.full((len(members), r, c), 0.5)
    for k, b in enumerate(members):
        c0 = blocks[b][2]
        for a, d in itertools.product(range(r), range(c)):
            M[k, a, d] = score.get(c0 + a * c + d, 0.5)
    x, y, v, gap = solve(M)
    gaps.append(gap.max())
    for k, b in enumerate(members):
        i, j, _ = blocks[b]
        value[b] = v[k]
        uniform[b] = M[k].mean()
        usage[i] += x[k]
        usage[j] += y[k]
print(f'equilibrium gap (max over pairs) {max(gaps):.4f}')

with open(pairs_path, 'w') as f:
    for b, (i, j, _) in enumerate(blocks):
        a = int(round(1000 * value[b]))
        f.write(f'{i} {j} {a} {1000 - a} 0\n')
gain = np.zeros(n)
for b, (i, j, _) in enumerate(blocks):
    gain[i] += value[b] - uniform[b]
    gain[j] -= value[b] - uniform[b]
out = []
for t in range(n):
    u = usage[t] / (n - 1)
    top = np.argsort(-u)[:3]
    out.append({'name': f"{T[t]['name']} #{T[t]['index']}",
                'party': [m['species'] for m in T[t]['party']],
                'sets': [{'lead': T[t]['party'][S[t][s][0]]['species'],
                          'with': [T[t]['party'][m]['species'] for m in S[t][s][1:]], 'share': round(float(u[s]), 3)}
                         for s in top if u[s] > 0.01],
                'pick_gain': round(float(gain[t] / (n - 1)), 4)})
json.dump(out, open(out_path, 'w'), indent=1)
print('mean score gain from equilibrium picks over uniform picks:',
      f'{np.mean([abs(v) for v in value - uniform]):.4f} per pair')
