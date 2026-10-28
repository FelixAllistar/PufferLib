#!/usr/bin/env python3
"""Evals for the Metamon-teams run, on every checkpoint (~125M steps) as it appears, vs TaurosV0 and SyntheticRLV2:
  mirror: Metamon's 28 competitive teams with exact sets on both sides, every ordered pair, (X, Y) and (Y, X) on shared
          seeds, 2 seeds per pair (1568 games, +/- 2.5): who pilots the same teams better (evaluate_batched.py --mirror);
  full:   we pilot zam_egg_cloy_don (our best team; the policy drafts its moves) vs Metamon on its own 28 teams
          (16 games per team = 448 games, +/- 4.5): the full game.
Baselines (2026-10-26): OMNI-2 trial 11 mirror 45.8 / 51.5 (Tauros / Syn, 1568 games), full 65.1 / 62.9.
Appends one line per checkpoint to mm_evals.txt. Usage (box, repo root): mm_eval.py RUN_DIR"""
import glob, json, math, os, re, shutil, subprocess, sys, time

R = sys.argv[1]
PY = 'build/pokemon/metamon-eval-env-cu128/bin/python'
TEAMS = 'reports/pokemon_benchmarks/metamon_competitive_gen1ou'
OURS = re.search(r'name = zam_egg_cloy_don\s*team = (\S+)\s*lead = (\S+)', open(f'{R}/pool.ini').read()).groups()
done = set(l.split()[0] for l in open(f'{R}/mm_evals.txt')) if os.path.exists(f'{R}/mm_evals.txt') else set()


def run(ck, out, opp, extra):
    if not os.path.exists(f'{out}/summary.json'):
        subprocess.run([PY, 'ocean/pokemon/benchmarks/evaluate_batched.py', ck, '--output', out, '--opponent', opp,
                        '--team-dir', TEAMS, '--mode', 'argmax', '--seed', '20261026', '--batch', '256', '--workers', '12',
                        '--threads', '1', *extra], stdout=open(f'{out}.log', 'w'), stderr=subprocess.STDOUT, check=True)
    r = json.load(open(f'{out}/summary.json'))
    return 100 * r['score'], 196 * math.sqrt(r['score'] * (1 - r['score']) / r['games'])


while True:
    cks = sorted(f for f in glob.glob(f'{R}/checkpoints/pokemon/*/*.bin') if not f.endswith('0000000000000000.bin'))
    todo = [c for c in cks if os.path.basename(c) not in done and time.time() - os.path.getmtime(c) > 30][::-1]  # newest first
    if not todo:
        if 'TRAIN_DONE' in open(f'{R}/train.log', errors='ignore').read(): break
        time.sleep(120)
        continue
    ck = todo[0]; step = os.path.basename(ck)
    e = f'{R}/evals/{step[:-4]}'
    os.makedirs(e, exist_ok=True)
    full = f'{e}/zam_egg_cloy_don'
    os.makedirs(full, exist_ok=True)
    shutil.copy(ck, f'{full}/{step}')  # a copy: evaluate_private resolves symlinks
    with open(f'{os.path.dirname(ck)}/config.ini') as src, open(f'{full}/config.ini', 'w') as dst:
        section = None
        for line in src:
            s = line.strip()
            if s.startswith('['): section = s[1:-1]
            key = s.split('=')[0].strip() if '=' in s else None
            if section == 'env' and key == 'learner_team': line = f'learner_team = {OURS[0]}\n'
            if section == 'env' and key == 'learner_lead': line = f'learner_lead = {OURS[1]}\n'
            dst.write(line)
    res = {}
    for opp in ('TaurosV0', 'SyntheticRLV2'):
        res['mirror', opp] = run(ck, f'{e}/mirror_{opp}', opp, ['--mirror', '--games', '2', '--library',
                                                                'build/pokemon/private_adapter_mirror.so'])
        res['full', opp] = run(f'{full}/{step}', f'{full}/eval_{opp}', opp, ['--games', '16'])
    os.remove(f'{full}/{step}')
    line = (f'{step} {int(step[:-4]) / 1e6:6.0f}M  mirror Tau {res["mirror", "TaurosV0"][0]:.1f} Syn '
            f'{res["mirror", "SyntheticRLV2"][0]:.1f} (+/- {res["mirror", "TaurosV0"][1]:.1f})   full Tau '
            f'{res["full", "TaurosV0"][0]:.1f} Syn {res["full", "SyntheticRLV2"][0]:.1f} (+/- {res["full", "TaurosV0"][1]:.1f})')
    with open(f'{R}/mm_evals.txt', 'a') as f: f.write(line + '\n')
    print(line, flush=True)
    done.add(step)
