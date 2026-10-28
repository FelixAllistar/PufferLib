#!/usr/bin/env python3
"""Full battery vs Metamon for the long run's best checkpoint, the counterpart of the 1B battery
(20261026_metamon_teams_1b: battery.sh, heldout*.sh, our_turf.sh, kakuna.sh; same team sets, seeds and game counts),
plus free-pick. Waits until training is done and the checkpoint tester has scored the last checkpoint and is idle, so it
never shares the GPU, then runs every eval strictly one at a time.
  best checkpoint: highest mean of Kakuna / TaurosV0 / SyntheticRLV2 on held-out test A (evals.txt) over 1B-2B
                   (all scored before the battery starts; the tester is then stopped). Test A therefore
                   picked the checkpoint and is slightly optimistic for it; tests B, our turf, competitive, full game and
                   free-pick are clean.
  mirror tests (identical teams and sets both sides, every ordered pair, 2 shared seeds): unseen A hl_05_26 (1568 games,
    reused from the tester for Kakuna/Tau/Syn), unseen B modern_replays_v2 (1568), our turf our_turf30 (1800),
    Metamon's competitive gen1ou (1568);
  full game: zam_egg_cloy_don (the policy drafts its moves) vs Metamon's 28 competitive teams, 16 games each;
  free-pick: no team given (the policy drafts species and moves) vs the same 28 teams, 16 games each.
Opponents: Kakuna, TaurosV0, SyntheticRLV2, Abra. Results: battery.txt (one line per cell) and battery.json.
Usage (box repo root): battery.py"""
import glob, json, math, os, re, shutil, subprocess, time
D = 'reports/pokemon_experiments/20261026_long2b'; B = 'reports/pokemon_benchmarks'
PY = 'build/pokemon/metamon-eval-env-cu128/bin/python'
OPPS = ('Kakuna', 'TaurosV0', 'SyntheticRLV2', 'Abra')
MIRRORS = {'unseenA': ('metamon_hl_05_26_sample28', 20261027), 'unseenB': ('metamon_modern_replays_v2_sample28', 20261028),
           'ourturf': ('our_turf30', 20261029), 'competitive': ('metamon_competitive_gen1ou', 20261026)}


def busy():
    return any(subprocess.run(['pgrep', '-f', p], capture_output=True).returncode == 0
               for p in ('^[.]/puffer train', 'evaluate_[b]atched'))


while True:
    cks = sorted(glob.glob(f'{D}/checkpoints/pokemon/*/*.bin'))
    scored = {l.split()[0] for l in open(f'{D}/evals.txt')}
    late = [c for c in cks if int(os.path.basename(c)[:-4]) >= 1_000_000_000]
    training = subprocess.run(['pgrep', '-f', '^[.]/puffer train'], capture_output=True).returncode == 0
    if not training and late and all(os.path.basename(c) in scored for c in late):
        # The tester would go on to the older checkpoints; stop it so the battery runs alone (finish its current eval).
        while subprocess.run(['pgrep', '-f', 'evaluate_[b]atched'], capture_output=True).returncode == 0: time.sleep(60)
        subprocess.run(['tmux', 'kill-session', '-t', 'long2b_eval'])
        time.sleep(30)
        if not busy(): break
    time.sleep(300)
rows = {}
for l in open(f'{D}/evals.txt'):
    m = re.match(r'(\S+)\s+\S+\s+held-out mirror Kak (\S+) Tau (\S+) Syn (\S+)', l)
    if m and int(m.group(1)[:-4]) >= 1_000_000_000: rows[m.group(1)] = [float(x) for x in m.groups()[1:]]
step = max(rows, key=lambda k: sum(rows[k]))
ck = [c for c in cks if os.path.basename(c) == step][0]
O = f'{D}/battery'; os.makedirs(O, exist_ok=True)
log = open(f'{D}/battery.txt', 'a')
log.write(f'best checkpoint by held-out A mean: {step} (Kak/Tau/Syn {rows[step]})\n'); log.flush()


def team_copy(arm, over):
    d = f'{O}/{arm}'; os.makedirs(d, exist_ok=True)
    if not os.path.exists(f'{d}/{step}'): shutil.copy(ck, f'{d}/{step}')  # a copy: the evaluator resolves symlinks
    out, sec = [], None
    for line in open(f'{os.path.dirname(ck)}/config.ini'):
        s = line.strip()
        if s.startswith('['): sec = s[1:-1]
        k = s.split('=')[0].strip() if '=' in s else None
        if sec == 'env' and k in over: line = f'{k} = {over[k]}\n'
        out.append(line)
    open(f'{d}/config.ini', 'w').writelines(out)
    return f'{d}/{step}'


ZAM = re.search(r'name = zam_egg_cloy_don\s*team = (\S+)\s*lead = (\S+)',
                open('reports/pokemon_experiments/20261026_metamon_teams_1b/pool.ini').read()).groups()
FULL = team_copy('full', {'learner_team': ZAM[0], 'learner_lead': ZAM[1]})
FREE = team_copy('freepick', {'learner_team_pool': 'None', 'learner_team': 'None', 'learner_lead': 'None'})
res = json.load(open(f'{O}/battery.json')) if os.path.exists(f'{O}/battery.json') else {}


def record(key, out):
    r = json.load(open(f'{out}/summary.json')); s, n = r['score'], r['games']
    h = 1.96 * math.sqrt(s * (1 - s) / n); e = lambda p: 400 * math.log10(p / (1 - p))
    res[key] = {'score': 100 * s, 'ci': 100 * h, 'games': n, 'elo': e(s), 'elo_ci': [e(s - h), e(s + h)]}
    json.dump(res, open(f'{O}/battery.json', 'w'), indent=1)
    log.write(f'{key:28s} {100 * s:5.1f} +/- {100 * h:.1f}  Elo {e(s):+.0f} [{e(s - h):+.0f}, {e(s + h):+.0f}]  {n} games\n'); log.flush()


def run(key, ck_path, out, opp, extra):
    if key in res: return
    if not os.path.exists(f'{out}/summary.json'):
        shutil.rmtree(out, ignore_errors=True)
        subprocess.run([PY, 'ocean/pokemon/benchmarks/evaluate_batched.py', ck_path, '--output', out, '--opponent', opp,
                        *extra, '--batch', '256', '--workers', '12', '--threads', '1'],
                       stdout=open(f'{out}.log', 'w'), stderr=subprocess.STDOUT, check=True)
    record(key, out)


for opp in OPPS:
    for test, (teams, seed) in MIRRORS.items():
        out = f'{O}/{opp}_{test}'
        reuse = f'{D}/evals/{step[:-4]}/{opp}'
        if test == 'unseenA' and os.path.exists(f'{reuse}/summary.json'): out = reuse
        run(f'{opp} {test}', ck, out, opp, ['--mirror', '--team-dir', f'{B}/{teams}', '--games', '2', '--seed', str(seed),
                                            '--library', 'build/pokemon/private_adapter_mirror.so'])
    for arm, path in (('full', FULL), ('freepick', FREE)):
        run(f'{opp} {arm}', path, f'{O}/{arm}/eval_{opp}', opp,
            ['--team-dir', f'{B}/metamon_competitive_gen1ou', '--games', '16', '--seed', '20261026'])
for p in (FULL, FREE): os.remove(p)
log.write('BATTERY_DONE\n'); log.close()
