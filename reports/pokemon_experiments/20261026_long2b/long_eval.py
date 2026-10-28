#!/usr/bin/env python3
"""Held-out Metamon test on every checkpoint of the long run as it appears (newest first): the final sweep's mirror test
(28 held-out hl_05_26 teams, identical sets both sides, 1568 games, +/- 2.4) vs Kakuna, TaurosV0 and SyntheticRLV2.
Baselines: 1B start 50.4 / 59.7 / 68.2; sweep trial 14 at 150M 50.9 / 62.4 / 68.6. Appends to evals.txt.
Usage (box repo root): long_eval.py"""
import glob, os, sys, time
sys.argv = ['', 'reports/pokemon_experiments/20261026_final_sweep']
exec(open('reports/pokemon_experiments/20261026_final_sweep/sweep_heldout.py').read().split("if len(sys.argv) > 2")[0])
D = 'reports/pokemon_experiments/20261026_long2b'
done = {l.split()[0] for l in open(f'{D}/evals.txt')} if os.path.exists(f'{D}/evals.txt') else set()
while True:
    cks = sorted(glob.glob(f'{D}/checkpoints/pokemon/*/*.bin'))
    todo = [c for c in cks if os.path.basename(c) not in done and time.time() - os.path.getmtime(c) > 60][::-1]
    if not todo:
        time.sleep(300)
        continue
    ck = todo[0]; step = os.path.basename(ck)
    e = f'{D}/evals/{step[:-4]}'; os.makedirs(e, exist_ok=True)
    r = {o: mirror(ck, f'{e}/{o}', o) for o in ('Kakuna', 'TaurosV0', 'SyntheticRLV2')}
    line = (f'{step} {int(step[:-4]) / 1e6:6.0f}M  held-out mirror Kak {r["Kakuna"][0]:.1f} Tau {r["TaurosV0"][0]:.1f} '
            f'Syn {r["SyntheticRLV2"][0]:.1f} (+/- {r["TaurosV0"][1]:.1f}, {r["TaurosV0"][2]} games each)')
    with open(f'{D}/evals.txt', 'a') as f: f.write(line + '\n')
    print(line, flush=True); done.add(step)
