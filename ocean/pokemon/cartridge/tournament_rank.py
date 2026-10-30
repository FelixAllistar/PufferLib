"""Turn the model tournament's per-game results (env.trainer_tournament) into the arena's per-pair format and rank it.

Input lines: task p1_trainer p2_trainer result (1 P1 won, 2 P2 won, 3/5/6 draw). Output: i j a_wins b_wins draws for
i < j, then rank.py.  Usage: python3 tournament_rank.py RESULTS TRAINERS.json OUT_PAIRS.tsv OUT_RANK.json
"""
import collections
import subprocess
import sys

pairs = collections.defaultdict(lambda: [0, 0, 0])
for line in open(sys.argv[1]):
    _, p1, p2, r = map(int, line.split())
    i, j = min(p1, p2), max(p1, p2)
    winner = p1 if r == 1 else p2 if r == 2 else None
    pairs[i, j][0 if winner == i else 1 if winner == j else 2] += 1
with open(sys.argv[3], 'w') as f:
    for (i, j), (a, b, d) in sorted(pairs.items()):
        f.write(f'{i} {j} {a} {b} {d}\n')
print(f'{len(pairs)} pairs, {sum(sum(v) for v in pairs.values())} games')
subprocess.run([sys.executable, __file__.replace('tournament_rank.py', 'rank.py'), sys.argv[2], sys.argv[3], sys.argv[4]])
