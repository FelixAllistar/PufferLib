#!/usr/bin/env python3
"""Team-comp evaluation with the current best model (giant run 4B): the model pilots each candidate team in the full
game vs Kakuna on Metamon's 28 competitive teams (16 games per team = 448 games, +/- 4.5). Candidates: the model's own
free-pick draft (exact sets), species-fixed teams where the model drafts the moves (zam_egg_cloy_don and our league
teams), Princess Klitty's teams, and Metamon's 28 competitive teams piloted by us (exact sets). One eval job at a time,
10 teams per evaluator call; checkpoint copies are deleted after each batch. Results: team_eval.txt (one line per team).
Usage (box repo root): team_eval.py [OPPONENT]"""
import glob, json, math, os, re, shutil, subprocess, sys
sys.path.insert(0, 'ocean/pokemon/benchmarks')
from pathlib import Path
from evaluate_private import load_team
from private_view import PublicMechanics
G = 'reports/pokemon_experiments/20261027_giant'; OUT = f'{G}/team_eval'; RES = f'{G}/team_eval.txt'
OPP = sys.argv[1] if len(sys.argv) > 1 else 'Kakuna'
PY = 'build/pokemon/metamon-eval-env-cu128/bin/python'
ck = glob.glob(f'{G}/checkpoints/pokemon/*/0000003999793152.bin')[0]
rules = PublicMechanics(); names = {p['id']: p['name'] for p in rules.data['species']}

def sets_of(path):
    arr = list(load_team(Path(path).stem, rules, Path(path))[0]); mons = [arr[i:i + 5] for i in range(0, 30, 5)]
    return 'sets:' + '/'.join(','.join(map(str, m)) for m in mons), str(mons[0][0]), '/'.join(names[m[0]] for m in mons)

teams = []  # (name, source, team string, lead, species list)
t, l, sp = sets_of('reports/pokemon_experiments/20261027_showdown_vs_metamon/giant4b_freepick.paste.txt')
teams.append(('giant4b_freepick', 'the 4B model\'s own free-pick draft (ladder team)', t, l, sp))
pool = re.findall(r'# ([^\n]*)\n\[bank\.\d+\]\s*name = (\S+)\s*team = (\S+)\s*lead = (\S+)', open('reports/pokemon_experiments/20261026_final_sweep/pool.ini').read())
for note, name, team, lead in pool:
    if name.startswith('ladder') or '.hl' in name or '.mo' in name: continue
    if team.startswith('species:'):
        teams.append((name, 'our team, species fixed; the model drafts moves', team, lead, '/'.join(names[int(x)] for x in team[8:].split(','))))
    elif 'klitty' in name:
        teams.append((name, 'Princess Klitty (exact sets)', team, lead, '/'.join(names[int(g.split(',')[0])] for g in team[5:].split('/'))))
for p in sorted(glob.glob('reports/pokemon_benchmarks/metamon_competitive_gen1ou/*.gen1ou_team')):
    t, l, sp = sets_of(p); teams.append((f'mm_{Path(p).stem}', 'Metamon competitive (human-made, exact sets)', t, l, sp))
done = {l.split('\t')[0] for l in open(RES)} if os.path.exists(RES) else set()
todo = [x for x in teams if x[0] not in done]
print(len(teams), 'teams,', len(todo), 'to evaluate', flush=True)
for i in range(0, len(todo), 10):
    batch = todo[i:i + 10]; cks, outs = [], []
    for name, src, team, lead, sp in batch:
        d = f'{OUT}/{name}'; os.makedirs(d, exist_ok=True); shutil.copy(ck, f'{d}/{os.path.basename(ck)}')
        lines, sec = [], None
        for line in open(f'{os.path.dirname(ck)}/config.ini'):
            s = line.strip()
            if s.startswith('['): sec = s[1:-1]
            k = s.split('=')[0].strip() if '=' in s else None
            if sec == 'env' and k == 'learner_team_pool': line = 'learner_team_pool = None\n'
            if sec == 'env' and k == 'learner_team': line = f'learner_team = {team}\n'
            if sec == 'env' and k == 'learner_lead': line = f'learner_lead = {lead}\n'
            lines.append(line)
        open(f'{d}/config.ini', 'w').writelines(lines)
        if not os.path.exists(f'{d}/eval_{OPP}/summary.json'): shutil.rmtree(f'{d}/eval_{OPP}', ignore_errors=True)  # unfinished earlier try
        cks.append(f'{d}/{os.path.basename(ck)}'); outs.append(f'{d}/eval_{OPP}')
    subprocess.run([PY, 'ocean/pokemon/benchmarks/evaluate_batched.py', *cks, '--output', *outs, '--skip-done', '--opponent', OPP,
                    '--team-dir', 'reports/pokemon_benchmarks/metamon_competitive_gen1ou', '--games', '16', '--seed', '20261026',
                    '--batch', '256', '--workers', '12', '--threads', '1'], stdout=open(f'{OUT}/batch{i}.log', 'w'),
                   stderr=subprocess.STDOUT, check=True)
    with open(RES, 'a') as f:
        for (name, src, team, lead, sp), o, c in zip(batch, outs, cks):
            r = json.load(open(f'{o}/summary.json')); s = r['score']; h = 196 * math.sqrt(s * (1 - s) / r['games'])
            f.write(f'{name}\t{100 * s:.1f}\t{h:.1f}\t{r["games"]}\t{OPP}\t{sp}\t{src}\n')
            os.remove(c)
    print(f'batch {i // 10 + 1} done', flush=True)
print('TEAM_EVAL_DONE', flush=True)
