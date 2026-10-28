#!/usr/bin/env python3
"""Size-study tester: once the giant battery is done (one eval job at a time), scores every size run's ~1B and ~2B
checkpoints as they appear: held-out A (hl_05_26, 1568 games) vs Kakuna, TaurosV0, SyntheticRLV2 and free-pick vs Kakuna
(448 games), same tests and seeds as the giant run's tester. Appends to evals.txt. Usage (box repo root): size_eval.py"""
import glob, os, sys, time
Z = 'reports/pokemon_experiments/20261027_size'
while 'BATTERY_DONE' not in (open('reports/pokemon_experiments/20261027_giant/battery.txt').read()
                             if os.path.exists('reports/pokemon_experiments/20261027_giant/battery.txt') else ''):
    time.sleep(300)
src = open('reports/pokemon_experiments/20261027_giant/giant_eval.py').read()
exec(src[:src.index('done = {')])  # mirror() and freepick() from the giant tester
done = {l.split()[0] + ' ' + l.split()[1] for l in open(f'{Z}/evals.txt')} if os.path.exists(f'{Z}/evals.txt') else set()
while True:
    cks = []
    for c in glob.glob(f'{Z}/h*/checkpoints/pokemon/*/*.bin'):
        step = int(os.path.basename(c)[:-4])
        if (900_000_000 < step < 1_100_000_000 or step > 1_900_000_000) and time.time() - os.path.getmtime(c) > 60:
            cks.append((-step, c))
    todo = [c for _, c in sorted(cks) if c.split('/')[3] + ' ' + os.path.basename(c) not in done]
    if not todo: time.sleep(300); continue
    ck = todo[0]; size = ck.split('/')[3]; step = os.path.basename(ck)
    e = f'{Z}/{size}/evals/{step[:-4]}'; os.makedirs(e, exist_ok=True)
    r = {o: mirror(ck, f'{e}/{o}', o) for o in ('Kakuna', 'TaurosV0', 'SyntheticRLV2')}
    fp = freepick(ck, f'{e}/freepick/eval_Kakuna')
    line = (f'{size} {step} {int(step[:-4]) / 1e6:6.0f}M  held-out A Kak {r["Kakuna"][0]:.1f} Tau {r["TaurosV0"][0]:.1f} '
            f'Syn {r["SyntheticRLV2"][0]:.1f} (+/- 2.4)  free-pick vs Kak {fp:.1f} (+/- 4.6)')
    with open(f'{Z}/evals.txt', 'a') as f: f.write(line + '\n')
    print(line, flush=True); done.add(f'{size} {step}')
