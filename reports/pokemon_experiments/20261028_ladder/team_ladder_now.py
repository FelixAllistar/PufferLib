#!/usr/bin/env python3
"""(2026-10-28: the live ladder team with the ladder-run 2B model, vs the same model on the old peak team and vs Kakuna; user sanity check; one variant.) Variants of the ladder team (Exeggutor lead, Snorlax, Tauros, Alakazam, Chansey, Lapras), Smogon-informed, and two
tests for each with the current best model (giant 4B):
  vs best: ./puffer match, the variant (seat 0) against the same model on the current ladder team (2048 games);
  vs Kakuna: the full game, our variant vs Kakuna on Metamon's 28 competitive teams (448 games).
Dimensions: sleep moves (current 4 sleepers / Exeggutor only / Chansey only / Lapras only), Snorlax (current
Body Slam-Hyper Beam-Blizzard-Self-Destruct / Smogon standard with Earthquake / Amnesia), Alakazam (current Counter /
Smogon standard Seismic Toss); plus two with Zapdos for Lapras. Results: variants.txt.
Usage (box repo root, metamon eval python): team_variants.py"""
import glob, itertools, json, math, os, re, shutil, subprocess, sys
sys.path.insert(0, 'ocean/pokemon/benchmarks')
from pathlib import Path
from evaluate_private import load_team
from private_view import PublicMechanics
D = Path('reports/pokemon_experiments/20261028_ladder/variants'); D.mkdir(parents=True, exist_ok=True)
G = 'reports/pokemon_experiments/20261027_giant'; CK = 'reports/pokemon_experiments/20261028_ladder/checkpoints/pokemon/sweep_1793048711881_0014/0000001999896576.bin'
rules = PublicMechanics(); RES = 'reports/pokemon_experiments/20261028_ladder/variants.txt'
SLEEP = {
  'cur4': {'Exeggutor': ['Hypnosis', 'Explosion', 'Sleep Powder', 'Psychic'], 'Chansey': ['Sing', 'Thunderbolt', 'Soft-Boiled', 'Ice Beam'], 'Lapras': ['Body Slam', 'Sing', 'Blizzard', 'Thunderbolt']},
  'egg1': {'Exeggutor': ['Sleep Powder', 'Psychic', 'Explosion', 'Stun Spore'], 'Chansey': ['Ice Beam', 'Thunderbolt', 'Thunder Wave', 'Soft-Boiled'], 'Lapras': ['Blizzard', 'Thunderbolt', 'Body Slam', 'Rest']},
  'cha1': {'Exeggutor': ['Psychic', 'Explosion', 'Stun Spore', 'Double-Edge'], 'Chansey': ['Sing', 'Ice Beam', 'Thunderbolt', 'Soft-Boiled'], 'Lapras': ['Blizzard', 'Thunderbolt', 'Body Slam', 'Rest']},
  'lap1': {'Exeggutor': ['Psychic', 'Explosion', 'Stun Spore', 'Double-Edge'], 'Chansey': ['Ice Beam', 'Thunderbolt', 'Thunder Wave', 'Soft-Boiled'], 'Lapras': ['Body Slam', 'Sing', 'Blizzard', 'Thunderbolt']}}
LAX = {'cur': ['Body Slam', 'Hyper Beam', 'Blizzard', 'Self-Destruct'], 'std': ['Body Slam', 'Earthquake', 'Hyper Beam', 'Self-Destruct'],
       'amn': ['Amnesia', 'Blizzard', 'Body Slam', 'Rest']}
ZAM = {'cur': ['Thunder Wave', 'Counter', 'Recover', 'Psychic'], 'std': ['Psychic', 'Seismic Toss', 'Thunder Wave', 'Recover']}
TAU = ['Earthquake', 'Blizzard', 'Body Slam', 'Hyper Beam']
ZAP = ['Thunderbolt', 'Drill Peck', 'Thunder Wave', 'Agility']
def paste(team): return '\n\n'.join(sp + ''.join(f'\n- {m}' for m in mv) for sp, mv in team) + '\n'
variants = {}
for (sk, s), (lk, l), (zk, z) in itertools.product(SLEEP.items(), LAX.items(), ZAM.items()):
    variants[f'sleep-{sk}.lax-{lk}.zam-{zk}'] = [('Exeggutor', s['Exeggutor']), ('Snorlax', l), ('Tauros', TAU), ('Alakazam', z), ('Chansey', s['Chansey']), ('Lapras', s['Lapras'])]
for sk in ('egg1', 'cha1'):
    s = SLEEP[sk]; variants[f'sleep-{sk}.lax-std.zam-std.zapdos'] = [('Exeggutor', s['Exeggutor']), ('Snorlax', LAX['std']), ('Tauros', TAU), ('Alakazam', ZAM['std']), ('Chansey', s['Chansey']), ('Zapdos', ZAP)]
variants = {"ladder-now.newmodel": [("Exeggutor", ["Stun Spore", "Explosion", "Sleep Powder", "Psychic"]), ("Snorlax", ["Body Slam", "Hyper Beam", "Self-Destruct", "Rest"]), ("Tauros", TAU), ("Alakazam", ZAM["cur"]), ("Chansey", SLEEP["cur4"]["Chansey"]), ("Lapras", SLEEP["cur4"]["Lapras"])]}
def sets(p):
    a = list(load_team(p.stem, rules, p)[0]); return 'sets:' + '/'.join(','.join(map(str, a[i:i + 5])) for i in range(0, 30, 5)), str(a[0])
base, base_lead = sets(Path('reports/pokemon_experiments/20261027_showdown_vs_metamon/giant4b_freepick.paste.txt'))
teams = {}
for name, team in variants.items():
    p = D / f'{name}.paste.txt'; p.write_text(paste(team)); teams[name] = sets(p)
done = {l.split('\t')[0] for l in open(RES)} if os.path.exists(RES) else set()
print(len(teams), 'variants', flush=True)
# Test 1: vs the best model on the current ladder team (one eval job at a time, quick).
vs_best = {}
for name, (t, lead) in teams.items():
    if name in done: continue
    out = subprocess.run(['./puffer', 'match', '--headless', '--base.env_name=pokemon', '--base.eval_episodes=2048',
        f'--base.load_model_path={CK}', f'--base.load_enemy_model_path={CK}', '--policy.hidden_size=1024', '--policy.num_layers=2',
        f'--env.learner_team={t}', f'--env.opponent_team={base}', f'--env.learner_lead={lead}', f'--env.opponent_lead={base_lead}',
        '--env.learner_team_pool=None', '--env.match_team_pool=None', '--env.native_league=None', '--env.pool_free_fraction=0',
        '--env.expert_fraction=0'], capture_output=True, text=True).stdout
    m = re.search(r'CUDA_EVAL env=pokemon score=(\S+) .* games=(\d+)', out)
    vs_best[name] = (100 * float(m.group(1)), int(m.group(2))) if m else (float('nan'), 0)
    print(name, 'vs best', vs_best[name], flush=True)
json.dump(vs_best, open(D / 'vs_best_ladder_now.json', 'w'))
# Test 2: vs Kakuna, full game, 10 variants per evaluator call.
PY = 'build/pokemon/metamon-eval-env-cu128/bin/python'
names = [n for n in teams if n not in done]
for i in range(0, len(names), 10):
    batch = names[i:i + 10]; cks, outs = [], []
    for name in batch:
        d = D / name; d.mkdir(exist_ok=True); shutil.copy(CK, d / os.path.basename(CK)); t, lead = teams[name]
        if not (d / 'eval_Kakuna/summary.json').exists(): shutil.rmtree(d / 'eval_Kakuna', ignore_errors=True)
        lines, sec = [], None
        for line in open(Path(CK).parent / 'config.ini'):
            s = line.strip()
            if s.startswith('['): sec = s[1:-1]
            k = s.split('=')[0].strip() if '=' in s else None
            if sec == 'env' and k == 'learner_team_pool': line = 'learner_team_pool = None\n'
            if sec == 'env' and k == 'learner_team': line = f'learner_team = {t}\n'
            if sec == 'env' and k == 'learner_lead': line = f'learner_lead = {lead}\n'
            lines.append(line)
        (d / 'config.ini').write_text(''.join(lines)); cks.append(str(d / os.path.basename(CK))); outs.append(str(d / 'eval_Kakuna'))
    subprocess.run([PY, 'ocean/pokemon/benchmarks/evaluate_batched.py', *cks, '--output', *outs, '--skip-done', '--opponent', 'Kakuna',
                    '--team-dir', 'reports/pokemon_benchmarks/metamon_competitive_gen1ou', '--games', '16', '--seed', '20261026',
                    '--batch', '256', '--workers', '12', '--threads', '1'], stdout=open(D / f'kakuna_batch{i}.log', 'w'), stderr=subprocess.STDOUT, check=True)
    with open(RES, 'a') as f:
        for name, o, c in zip(batch, outs, cks):
            r = json.load(open(f'{o}/summary.json')); s = 100 * r['score']; h = 196 * math.sqrt(r['score'] * (1 - r['score']) / r['games'])
            vb = vs_best.get(name, (float('nan'), 0))
            f.write(f'{name}\t{vb[0]:.1f}\t{vb[1]}\t{s:.1f}\t{h:.1f}\n'); os.remove(c)
print('VARIANTS_DONE', flush=True)
