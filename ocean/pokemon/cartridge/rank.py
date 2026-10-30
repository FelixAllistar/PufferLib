"""Rank the Gen 1 trainers from a round robin (arena.c output) (2026-10-30).

Fits a Bradley-Terry model to every pair's results (draws count half to each side) by the MM algorithm, which uses all
matchups at once (unlike Elo, the order of games does not matter), and reports it on the Elo scale
(400 * log10 of the strength, mean 1500). Also: overall win/draw/loss record, best win and worst loss (the strongest
trainer beaten in at least half the games, the weakest one lost to in at least half).

Usage: python3 ocean/pokemon/cartridge/rank.py trainers.json RESULTS.tsv OUT.json [TOP]
"""
import json
import math
import sys

import numpy as np

trainers = json.load(open(sys.argv[1]))
n = len(trainers)
wins = np.zeros((n, n))
games = np.zeros((n, n))
record = np.zeros((n, 3))  # wins, draws, losses
for line in open(sys.argv[2]):
    i, j, a, b, d = map(int, line.split())
    wins[i, j] += a + d / 2
    wins[j, i] += b + d / 2
    games[i, j] = games[j, i] = a + b + d
    record[i] += (a, d, b)
    record[j] += (b, d, a)

# MM updates (Hunter 2004): p_i <- W_i / sum_j n_ij / (p_i + p_j). A tiny prior game against an average opponent keeps
# trainers that never win (or never lose) finite.
p = np.ones(n)
total = wins.sum(1) + 0.5
for _ in range(10000):
    denom = (games / (p[:, None] + p[None, :])).sum(1) + 1 / (p + 1)
    new = total / denom
    new /= math.exp(np.log(new).mean())
    done = np.abs(np.log(new / p)).max() < 1e-10
    p = new
    if done:
        break
elo = 400 * np.log10(p)
elo += 1500 - elo.mean()

rate = np.divide(wins, games, out=np.full_like(wins, np.nan), where=games > 0)
order = np.argsort(-elo)
out = []
for rank, i in enumerate(order, 1):
    t = trainers[i]
    beaten = [j for j in range(n) if rate[i, j] > 0.5]
    lost = [j for j in range(n) if rate[i, j] < 0.5]
    best = max(beaten, key=lambda j: elo[j], default=None)
    worst = min(lost, key=lambda j: elo[j], default=None)
    name = lambda j: None if j is None else f"{trainers[j]['name']} #{trainers[j]['index']}"
    out.append({'rank': rank, 'name': f"{t['name']} #{t['index']}", 'class': t['class'], 'elo': round(float(elo[i]), 1),
                'wins': int(record[i, 0]), 'draws': int(record[i, 1]), 'losses': int(record[i, 2]),
                'best_win': name(best), 'worst_loss': name(worst),
                'party': [f"{m['species']} {m['level']}" for m in t['party']]})
json.dump(out, open(sys.argv[3], 'w'), indent=1)
top = int(sys.argv[4]) if len(sys.argv) > 4 else 25
for r in out[:top]:
    print(f"{r['rank']:3d} {r['elo']:7.1f} {r['name']:<22} W-D-L {r['wins']}-{r['draws']}-{r['losses']}  "
          f"{', '.join(r['party'])}")
print('...')
for r in out[-5:]:
    print(f"{r['rank']:3d} {r['elo']:7.1f} {r['name']:<22} {', '.join(r['party'])}")
