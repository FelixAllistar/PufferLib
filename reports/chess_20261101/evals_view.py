"""Live eval board (2026-11-04): Maia-1100 / random-mover results per checkpoint from a chess_watch5.sh evals.txt.
Usage: python3 evals_view.py EVALS.txt   (refresh with `watch` or a loop)"""
import re
import sys

rows = []
for line in open(sys.argv[1]):
    m = re.search(r'/(\d+)\.bin random: score=([0-9.]+) draw=([0-9.]+)\s+maia1100: score=([0-9.]+) draw=([0-9.]+)', line)
    if m:
        rows.append((int(m.group(1)), float(m.group(2)), float(m.group(4)), float(m.group(5))))
if not rows:
    print('no evals yet')
    sys.exit()
best = max(r[2] for r in rows)
print(f"{'steps':>7}  {'vs random':>9}  {'vs Maia-1100':>12}  {'draws':>5}  ")
for steps, rnd, maia, draw in rows:
    bar = '#' * int(round(maia * 100))
    print(f"{steps / 1e6:6.0f}M  {rnd:8.1%}  {maia:11.1%}  {draw:5.0%}  {bar}{'  <- best' if maia == best else ''}")
recent = [r[2] for r in rows[-4:]]
print(f"\nlast {len(recent)} vs Maia: {' '.join(f'{x:.1%}' for x in recent)}   (600 games each: about +-1.5 points of noise)")
