#!/usr/bin/env python3
"""Poker gen3 sweep scores only (cash tables, 256x3 from scratch): one row per finished trial (score = panel_worst, BB/hand vs the hardest panel bot) with
its main settings; refreshes every 30 s. Usage: scores.py SWEEP_LOG"""
import glob, os, re, sys, time
LOG = sys.argv[1]
KEYS = ['total_timesteps', 'learning_rate', 'ent_coef', 'magnet_coef', 'total_agents', 'horizon', 'minibatch_size']
def params(run):
    f = sorted(glob.glob(f'checkpoints/poker/sweep_*_{run:04d}/config.ini'))
    if not f: return {}
    text = open(f[-1]).read().split('[sweep]')[0]
    return {k: re.search(rf'^{k} = (\S+)', text, re.M).group(1) for k in KEYS if re.search(rf'^{k} = ', text, re.M)}
while True:
    rows = [(int(m.group(1)), float(m.group(2))) for m in re.finditer(r'sweep run=(\d+) score=(\S+)', open(LOG, errors='ignore').read())]
    best = max((s for _, s in rows), default=None)
    out = [f'Poker gen3 sweep  {time.strftime("%H:%M:%S")}  {len(rows)} trials  best {best}  (score: BB/hand vs the hardest panel bot; gen1 -1.8 vs tight, gen2 best +0.07)', '',
           f'{"run":>4} {"score":>8}  ' + ' '.join(f'{k[:12]:>12}' for k in KEYS)]
    for run, score in rows:
        p = params(run)
        out.append(f'{run:>4} {score:8.3f}  ' + ' '.join(f'{float(p[k]):>12.3g}' if k in p else f'{"-":>12}' for k in KEYS) + (' *' if score == best else ''))
    os.system('clear'); print('\n'.join(out), flush=True); time.sleep(30)
