#!/usr/bin/env python3
"""Held-out Metamon test for the final sweep: every trial that enters the top 3 by sweep score gets the mirror test on
the 28 held-out high-ladder teams (reports/pokemon_benchmarks/metamon_hl_05_26_sample28; identical sets both sides,
every ordered pair on shared seeds, 2 seeds per pair = 1568 games) vs Kakuna (Metamon's current best), TaurosV0 and
SyntheticRLV2. Baseline: the Metamon-teams 1B checkpoint, Kakuna 50.4 / TaurosV0 59.7 / SyntheticRLV2 68.2. Results: heldout.txt. Checks every 5 minutes.
Usage (box repo root): sweep_heldout.py SWEEP_DIR [--test CHECKPOINT TEAM_DIR] (test: one quick run, then exit)"""
import glob, json, math, os, re, shutil, subprocess, sys, time

D = sys.argv[1]
PY = 'build/pokemon/metamon-eval-env-cu128/bin/python'


def mirror(ck, out, opp, teams='reports/pokemon_benchmarks/metamon_hl_05_26_sample28'):
    if not os.path.exists(f'{out}/summary.json'):
        shutil.rmtree(out, ignore_errors=True)  # an unfinished earlier attempt (e.g. a restart mid-run)
        subprocess.run([PY, 'ocean/pokemon/benchmarks/evaluate_batched.py', ck, '--output', out, '--opponent', opp,
                        '--mirror', '--team-dir', teams, '--games', '2', '--seed', '20261027', '--batch', '256',
                        '--workers', '12', '--threads', '1', '--library', 'build/pokemon/private_adapter_mirror.so'],
                       stdout=open(f'{out}.log', 'w'), stderr=subprocess.STDOUT, check=True)
    r = json.load(open(f'{out}/summary.json'))
    return 100 * r['score'], 196 * math.sqrt(r['score'] * (1 - r['score']) / r['games']), r['games']


if len(sys.argv) > 2 and sys.argv[2] == '--test':
    print(mirror(sys.argv[3], f'{D}/heldout_test', 'TaurosV0', sys.argv[4]))
    sys.exit()
done = {int(l.split()[1]) for l in open(f'{D}/heldout.txt') if l.startswith('trial')} if os.path.exists(f'{D}/heldout.txt') else set()
while True:
    scores = {}
    for line in open(f'{D}/sweep.log', errors='ignore'):
        m = re.match(r'sweep run=(\d+) score=(\S+)', line)
        if m: scores[int(m.group(1))] = float(m.group(2))
    todo = [r for r in sorted(scores, key=lambda r: -scores[r])[:3] if r not in done]
    if not todo:
        time.sleep(300)
        continue
    run = todo[0]
    ck = sorted(f for f in glob.glob(f'{D}/checkpoints/pokemon/sweep_*_{run:04d}/*.bin') if not f.endswith('0000000000000000.bin'))[-1]
    os.makedirs(f'{D}/heldout', exist_ok=True)
    res = {opp: mirror(ck, f'{D}/heldout/t{run:04d}_{opp}', opp) for opp in ('Kakuna', 'TaurosV0', 'SyntheticRLV2')}
    line = (f'trial {run:4d} heldout mirror Kak {res["Kakuna"][0]:.1f} Tau {res["TaurosV0"][0]:.1f} Syn {res["SyntheticRLV2"][0]:.1f} '
            f'(+/- {res["TaurosV0"][1]:.1f}, {res["TaurosV0"][2]} games each)')
    with open(f'{D}/heldout.txt', 'a') as f: f.write(line + '\n')
    print(line, flush=True)
    done.add(run)
