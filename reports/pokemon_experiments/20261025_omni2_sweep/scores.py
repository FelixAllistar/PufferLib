#!/usr/bin/env python3
"""OMNI-2 sweep scores only: one row per finished trial (score = vs_league_score) with its swept settings, best marked.
Usage: scores.py SWEEP_DIR   (refreshes every 30 s; Ctrl-C to quit)"""
import glob, os, re, sys, time
D = sys.argv[1]
KEYS = ['learning_rate', 'min_lr_ratio', 'ent_coef', 'min_ent_coef_ratio', 'magnet_coef', 'replay_ratio']
def params(run):
    f = glob.glob(f'{D}/checkpoints/pokemon/sweep_*_{run:04d}/config.ini')
    if not f: return {}
    text = open(f[0]).read().split('[sweep]')[0]
    return {k: re.search(rf'^{k} = (\S+)', text, re.M).group(1) for k in KEYS if re.search(rf'^{k} = ', text, re.M)}
while True:
    rows = []
    for line in open(f'{D}/sweep.log', errors='ignore'):
        m = re.match(r'sweep run=(\d+) score=(\S+)', line)
        if m: rows.append((int(m.group(1)), float(m.group(2))))
        elif re.match(r'sweep worker run=(\d+) failed', line):
            rows.append((int(re.match(r'sweep worker run=(\d+)', line).group(1)), None))
    meta = {}
    if os.path.exists(f'{D}/metamon.txt'):
        for line in open(f'{D}/metamon.txt'):
            m = re.match(r'trial +(\d+) metamon +(\S+) \(Syn (\S+) Tau (\S+)\)', line)
            if m: meta[int(m.group(1))] = f'{m.group(2)} ({m.group(3)}/{m.group(4)})'
    best = max((s for _, s in rows if s is not None), default=None)
    out = [f'OMNI-2 sweep  {time.strftime("%H:%M:%S")}  {len(rows)} trials  best {best if best is None else round(best, 3)}  (metamon: top-3 trials on all 19 teams, omni2_eval.py; score = win rate vs LeagueV2, all league games in the last 20% of each trial)', '']
    out.append(f'{"run":>4} {"score":>7}  ' + ' '.join(f'{k[:12]:>12}' for k in KEYS) + '  metamon, 19 teams (Syn/Tau)')
    for run, score in rows:
        p = params(run)
        mark = ' *' if score is not None and score == best else ''
        out.append(f'{run:>4} {("FAILED" if score is None else f"{score:.3f}"):>7}  ' + ' '.join(f'{float(p[k]):>12.3g}' if k in p else f'{"-":>12}' for k in KEYS) + mark + ('  ' + meta[run] if run in meta else ''))
    os.system('clear'); print('\n'.join(out), flush=True)
    time.sleep(30)
