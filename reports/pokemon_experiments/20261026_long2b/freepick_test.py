#!/usr/bin/env python3
"""Side test of a long-run checkpoint while training continues, vs Metamon's 28 competitive gen1ou teams (Metamon
pilots its own team choice; 16 games per team = 448 games, +/- 4.5) against Kakuna, TaurosV0 and SyntheticRLV2:
  free-pick: no team given (learner_team_pool / learner_team / learner_lead = None): the policy drafts its species
             and all moves, like the free-pick runs (FP-4 375M: Tau 54.7 / Syn 54.5 / Kakuna 46.9);
  full game: zam_egg_cloy_don (species fixed, the policy drafts the moves), like the 1B run's battery
             (1B start: Kakuna 64.5 / Tau 72.5 / Syn 77.3).
Results: freepick.txt. Usage (box repo root): freepick_test.py [CHECKPOINT] (default: the newest)."""
import glob, json, math, os, re, shutil, subprocess, sys
D = 'reports/pokemon_experiments/20261026_long2b'
PY = 'build/pokemon/metamon-eval-env-cu128/bin/python'
ck = sys.argv[1] if len(sys.argv) > 1 else sorted(glob.glob(f'{D}/checkpoints/pokemon/*/*.bin'))[-1]
step = os.path.basename(ck)
ZAM = re.search(r'name = zam_egg_cloy_don\s*team = (\S+)\s*lead = (\S+)',
                open('reports/pokemon_experiments/20261026_metamon_teams_1b/pool.ini').read()).groups()
arms = {'freepick': {'learner_team_pool': 'None', 'learner_team': 'None', 'learner_lead': 'None'},
        'zam_egg_cloy_don': {'learner_team': ZAM[0], 'learner_lead': ZAM[1]}}
cks = []
for arm, over in arms.items():
    d = f'{D}/side_tests/{step[:-4]}/{arm}'; os.makedirs(d, exist_ok=True)
    if not os.path.exists(f'{d}/{step}'): shutil.copy(ck, f'{d}/{step}')  # a copy: the evaluator resolves symlinks
    out, sec = [], None
    for line in open(f'{os.path.dirname(ck)}/config.ini'):
        s = line.strip()
        if s.startswith('['): sec = s[1:-1]
        k = s.split('=')[0].strip() if '=' in s else None
        if sec == 'env' and k in over: line = f'{k} = {over[k]}\n'
        out.append(line)
    open(f'{d}/config.ini', 'w').writelines(out)
    cks.append(f'{d}/{step}')
res = {}
for opp in ('Kakuna', 'TaurosV0', 'SyntheticRLV2'):
    outs = [f'{os.path.dirname(c)}/eval_{opp}' for c in cks]
    subprocess.run([PY, 'ocean/pokemon/benchmarks/evaluate_batched.py', *cks, '--output', *outs, '--skip-done',
                    '--opponent', opp, '--team-dir', 'reports/pokemon_benchmarks/metamon_competitive_gen1ou',
                    '--games', '16', '--seed', '20261026', '--batch', os.environ.get('FP_BATCH', '256'), '--workers', '12', '--threads', '1'],
                   stdout=open(f'{D}/side_tests/{step[:-4]}/eval_{opp}.log', 'w'), stderr=subprocess.STDOUT, check=True)
    for arm, o in zip(arms, outs):
        r = json.load(open(f'{o}/summary.json')); s, n = r['score'], r['games']
        res[arm, opp] = (100 * s, 196 * math.sqrt(s * (1 - s) / n), n)
with open(f'{D}/freepick.txt', 'a') as f:
    for arm in arms:
        line = (f'{step} {int(step[:-4]) / 1e6:6.0f}M  {arm:16s} Kak {res[arm, "Kakuna"][0]:.1f} Tau {res[arm, "TaurosV0"][0]:.1f} '
                f'Syn {res[arm, "SyntheticRLV2"][0]:.1f} (+/- {res[arm, "TaurosV0"][1]:.1f}, {res[arm, "TaurosV0"][2]} games each)')
        print(line, flush=True); f.write(line + '\n')
for c in cks: os.remove(c)
