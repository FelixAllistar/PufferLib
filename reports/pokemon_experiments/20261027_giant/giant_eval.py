#!/usr/bin/env python3
"""Giant-run checkpoint tester (one eval process, newest checkpoint first): held-out mirror A (hl_05_26, 1568 games) vs
Kakuna, TaurosV0, SyntheticRLV2, plus free-pick vs Kakuna (our policy drafts its whole team vs Metamon's 28 competitive
teams, 448 games). Baselines, 2B long run 1875M: A 56.1 / 65.1 / 73.8, free-pick vs Kakuna 41.3. Appends to evals.txt.
Usage (box repo root): giant_eval.py"""
import glob, json, math, os, shutil, subprocess, sys, time
G = 'reports/pokemon_experiments/20261027_giant'
sys.argv = ['', 'reports/pokemon_experiments/20261026_final_sweep']
exec(open('reports/pokemon_experiments/20261026_final_sweep/sweep_heldout.py').read().split("if len(sys.argv) > 2")[0])
PY = 'build/pokemon/metamon-eval-env-cu128/bin/python'


def freepick(ck, out):
    if not os.path.exists(f'{out}/summary.json'):
        d = os.path.dirname(out); os.makedirs(d, exist_ok=True); shutil.rmtree(out, ignore_errors=True)
        copy = f'{d}/{os.path.basename(ck)}'; shutil.copy(ck, copy)
        lines, sec = [], None
        for line in open(f'{os.path.dirname(ck)}/config.ini'):
            s = line.strip()
            if s.startswith('['): sec = s[1:-1]
            k = s.split('=')[0].strip() if '=' in s else None
            if sec == 'env' and k in ('learner_team_pool', 'learner_team', 'learner_lead'): line = f'{k} = None\n'
            lines.append(line)
        open(f'{d}/config.ini', 'w').writelines(lines)
        subprocess.run([PY, 'ocean/pokemon/benchmarks/evaluate_batched.py', copy, '--output', out, '--opponent', 'Kakuna',
                        '--team-dir', 'reports/pokemon_benchmarks/metamon_competitive_gen1ou', '--games', '16', '--seed', '20261026',
                        '--batch', '256', '--workers', '12', '--threads', '1'], stdout=open(f'{out}.log', 'w'), stderr=subprocess.STDOUT, check=True)
        os.remove(copy)
    r = json.load(open(f'{out}/summary.json'))
    return 100 * r['score']


done = {l.split()[0] for l in open(f'{G}/evals.txt')} if os.path.exists(f'{G}/evals.txt') else set()
while True:
    cks = sorted(glob.glob(f'{G}/checkpoints/pokemon/*/*.bin'))
    todo = [c for c in cks if os.path.basename(c) not in done and time.time() - os.path.getmtime(c) > 60][::-1]
    if not todo: time.sleep(300); continue
    ck = todo[0]; step = os.path.basename(ck); e = f'{G}/evals/{step[:-4]}'; os.makedirs(e, exist_ok=True)
    r = {o: mirror(ck, f'{e}/{o}', o) for o in ('Kakuna', 'TaurosV0', 'SyntheticRLV2')}
    fp = freepick(ck, f'{e}/freepick/eval_Kakuna')
    line = (f'{step} {int(step[:-4]) / 1e6:6.0f}M  held-out A Kak {r["Kakuna"][0]:.1f} Tau {r["TaurosV0"][0]:.1f} '
            f'Syn {r["SyntheticRLV2"][0]:.1f} (+/- 2.4)  free-pick vs Kak {fp:.1f} (+/- 4.6)')
    with open(f'{G}/evals.txt', 'a') as f: f.write(line + '\n')
    print(line, flush=True); done.add(step)
