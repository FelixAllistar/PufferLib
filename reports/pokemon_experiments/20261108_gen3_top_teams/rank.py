# Win rate per pool team from a draft_log (2026-11-08). Line format (pokemon3.h draft_log): result d0 d1 turns team0[6]
# team1[6]; result 1 = seat 0 wins, 2 = seat 1 wins, 3 or 4 = draw. Teams are matched to pool lines by their six set indices.
# Score = (wins + 1) / (games + 2) (Laplace), so teams with few games sit near 0.5. Writes ranked.txt (pool line, games,
# raw win rate, score) and topN.txt (the top N teams, pool format) for N in 500, 1000, 2000, 5000.
# Optional 4th argument: a pool-format file; only its teams are ranked (stage 2: candidates vs the field).
# Usage: rank.py POOL GAMES_LOG OUT_DIR [CANDIDATES]
import sys, collections
pool_path, log_path, out = sys.argv[1:4]
only = None
pool = [l.split()[:6] for l in open(pool_path)]
index = {tuple(t): i for i, t in enumerate(pool)}
if len(sys.argv) > 4: only = {index[tuple(l.split()[:6])] for l in open(sys.argv[4])}
g = collections.Counter(); w = collections.Counter(); miss = 0
for line in open(log_path):
    f = line.split()
    if len(f) < 16: continue
    r = {1: 1.0, 2: 0.0, 3: 0.5, 4: 0.5}.get(int(f[0]))
    if r is None: continue
    teams = (tuple(f[4:10]), tuple(f[10:16]))
    for seat, score in ((0, r), (1, 1 - r)):
        i = index.get(teams[seat])
        if i is None: miss += 1; continue
        g[i] += 1; w[i] += score
ranked = sorted((i for i in g if only is None or i in only), key=lambda i: -(w[i] + 1) / (g[i] + 2))
with open(f"{out}/ranked.txt", "w") as f:
    for i in ranked: f.write(f"{i} {g[i]} {w[i] / g[i]:.3f} {(w[i] + 1) / (g[i] + 2):.3f}\n")
for n in (500, 1000, 2000, 5000):
    with open(f"{out}/top{n}.txt", "w") as f:
        for i in ranked[:n]: f.write(" ".join(pool[i]) + "\n")
gs = sorted(g[i] for i in ranked)
print(f"{log_path}: teams seen {len(g)} of {len(pool)}, unmatched seats {miss}, games per team median {gs[len(gs) // 2]}, "
      f"teams ranked {len(ranked)}, top1000 mean raw {sum(w[i] / g[i] for i in ranked[:1000]) / max(1, len(ranked[:1000])):.3f}")
