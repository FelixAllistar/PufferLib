#!/usr/bin/env python3
"""Team round robin (2026-10-28): which mix of teams should the ladder bot rotate? The ladder-run 2B model pilots both
sides (our engine, ./puffer match) in every pair of 10 candidate teams; the resulting zero-sum matrix gives the Nash mix
(the rotation weights: no single opposing team beats the mix). Candidates: the old peak team, the live ladder team, the
best one-sleeper variant, Metamon's best human team, and the six best species-fixed teams from team_eval.txt, whose
moves this model drafts here (most common argmax draft, as draft_team.py). Outputs in this directory: teams/*.paste.txt,
matrix.json, round_robin.txt, rotation.txt (weights for client --teams). Usage (box repo root): round_robin.py [GAMES]"""
import ctypes as C, collections, itertools, json, os, re, shutil, subprocess, sys, tempfile
sys.path.insert(0, 'ocean/pokemon/benchmarks')
from pathlib import Path
from evaluate_private import load_team, library
from private_view import PublicMechanics, ROOT
GAMES = int(sys.argv[1]) if len(sys.argv) > 1 else 1024
R = Path('reports/pokemon_experiments/20261028_round_robin'); (R / 'teams').mkdir(parents=True, exist_ok=True)
CK = 'reports/pokemon_experiments/20261028_ladder/checkpoints/pokemon/sweep_1793048711881_0014/0000001999896576.bin'
SD = 'reports/pokemon_experiments/20261027_showdown_vs_metamon'
rules = PublicMechanics(); species = {p['id']: p['name'] for p in rules.data['species']}; moves = {m['id']: m['name'] for m in rules.data['moves']}
EXACT = {'old_peak': f'{SD}/giant4b_freepick.paste.txt', 'ladder_now': f'{SD}/ladder_rest_snorlax.paste.txt',
         'egg1_std_zapdos': 'reports/pokemon_experiments/20261028_ladder/variants/sleep-egg1.lax-std.zam-std.zapdos.paste.txt',
         'mm_team_1003': 'reports/pokemon_benchmarks/metamon_competitive_gen1ou/team_1003.gen1ou_team'}
SPECIES = ['zam_cha_egg_lap_tau_zap.s1', 'zam_egg_gar', 'zam_egg_zap', 'zam_cha_lap_don_mie_tau.s1', 'gar_egg_mie_zap', 'zam_cha_lap_lax_mie_tau.s1']
pool = {n: (t, l) for n, t, l in re.findall(r'\[bank\.\d+\]\s*name = (\S+)\s*team = (\S+)\s*lead = (\S+)', open('reports/pokemon_experiments/20261026_final_sweep/pool.ini').read())}

def paste(mons): return '\n\n'.join(species[m[0]] + ''.join(f'\n- {moves[x]}' for x in m[1:] if x) for m in mons) + '\n'
def draft(team, lead):
    """Most common argmax draft of this model with the species fixed (vs 6 Metamon teams x 2 seeds)."""
    lib = library(ROOT / 'build/pokemon/private_adapter_mirror.so'); lib.bench_team.argtypes = [C.c_void_p, C.c_void_p]; lib.bench_team.restype = None
    tmp = Path(tempfile.mkdtemp()); shutil.copy(CK, tmp / 'w.bin'); sec = None
    with open(Path(CK).with_name('config.ini')) as src, open(tmp / 'config.ini', 'w') as dst:
        for line in src:
            s = line.strip()
            if s.startswith('['): sec = s[1:-1]
            k = s.split('=')[0].strip() if '=' in s else None
            if sec == 'env' and k == 'learner_team_pool': line = 'learner_team_pool = None\n'
            if sec == 'env' and k == 'pool_free_fraction': line = 'pool_free_fraction = 0\n'
            if sec == 'env' and k == 'learner_team': line = f'learner_team = {team}\n'
            if sec == 'env' and k == 'learner_lead': line = f'learner_lead = {lead}\n'
            dst.write(line)
    seen, teams = collections.Counter(), {}
    for t in sorted(Path('reports/pokemon_benchmarks/metamon_competitive_gen1ou').glob('*.gen1ou_team'))[:6]:
        opp = load_team(t.stem, rules, t)[0]
        for seed in (1, 2):
            h = lib.bench_create(str(tmp / 'w.bin').encode(), 1); assert lib.bench_reset(h, 20261100 + seed, 0, opp) == 0
            d = (C.c_uint8 * 30)(); lib.bench_team(h, d); lib.bench_destroy(h)
            mons = [list(d)[i:i + 5] for i in range(0, 30, 5)]; key = paste(mons); seen[key] += 1; teams[key] = mons
    shutil.rmtree(tmp)
    return seen.most_common(1)[0][0], seen.most_common(1)[0][1]

files = {}
for n, p in EXACT.items(): files[n] = Path(p)
for n in SPECIES:
    out = R / 'teams' / f'{n}.paste.txt'
    if not out.exists():
        text, count = draft(*pool[n]); out.write_text(text); print(f'drafted {n} ({count}/12 drafts agree)', flush=True)
    files[n] = out
for n, p in EXACT.items():
    if not (R / 'teams' / f'{n}.paste.txt').exists():
        mons = list(load_team(Path(p).stem, rules, Path(p))[0]); (R / 'teams' / f'{n}.paste.txt').write_text(paste([mons[i:i + 5] for i in range(0, 30, 5)]))
def sets(p):
    a = list(load_team(Path(p).stem, rules, Path(p))[0]); return 'sets:' + '/'.join(','.join(map(str, a[i:i + 5])) for i in range(0, 30, 5)), str(a[0])
names = list(files); S = {n: sets(files[n]) for n in names}
mj = R / 'matrix.json'; M = json.load(open(mj)) if mj.exists() else {}
for a, b in itertools.combinations(names, 2):
    if f'{a}|{b}' in M: continue
    (ta, la), (tb, lb) = S[a], S[b]
    out = subprocess.run(['./puffer', 'match', '--headless', '--base.env_name=pokemon', f'--base.eval_episodes={GAMES}',
        f'--base.load_model_path={CK}', f'--base.load_enemy_model_path={CK}', '--policy.hidden_size=1024', '--policy.num_layers=2',
        f'--env.learner_team={ta}', f'--env.opponent_team={tb}', f'--env.learner_lead={la}', f'--env.opponent_lead={lb}',
        '--env.learner_team_pool=None', '--env.match_team_pool=None', '--env.native_league=None', '--env.pool_free_fraction=0',
        '--env.expert_fraction=0'], capture_output=True, text=True).stdout
    m = re.search(r'CUDA_EVAL env=pokemon score=(\S+) .* games=(\d+)', out)
    M[f'{a}|{b}'] = [float(m.group(1)), int(m.group(2))]; json.dump(M, open(mj, 'w'), indent=1)
    print(f'{a} vs {b}: {100 * float(m.group(1)):.1f}% ({m.group(2)} games)', flush=True)
n = len(names); A = [[0.5] * n for _ in range(n)]
for i, j in itertools.combinations(range(n), 2):
    s = M[f'{names[i]}|{names[j]}'][0]; A[i][j], A[j][i] = s, 1 - s
# Nash mix of the symmetric zero-sum game (payoff A - 0.5) by regret matching; the average strategy converges.
reg, avg = [0.0] * n, [0.0] * n
for it in range(200000):
    pos = [max(r, 0) for r in reg]; z = sum(pos); x = [p / z for p in pos] if z > 0 else [1 / n] * n
    for k in range(n): avg[k] += x[k]
    u = [sum(A[k][j] * x[j] for j in range(n)) for k in range(n)]; v = sum(x[k] * u[k] for k in range(n))
    for k in range(n): reg[k] += u[k] - v
w = [a / sum(avg) for a in avg]; w = [x if x >= 0.01 else 0 for x in w]; w = [x / sum(w) for x in w]
worst = min(sum(A[k][j] * w[k] for k in range(n)) for j in range(n))
with open(R / 'round_robin.txt', 'w') as f:
    f.write(f'Row team win % vs column team; the ladder-run 2B model pilots both sides; {GAMES} games per pair.\n')
    f.write('team'.ljust(28) + ''.join(f'{i:>6}' for i in range(n)) + '   mean  nash_weight\n')
    for i in range(n):
        f.write(f'{i} {names[i]}'.ljust(28) + ''.join(f'{100 * A[i][j]:6.1f}' for j in range(n)) + f'  {100 * sum(A[i]) / n:5.1f}  {w[i]:.3f}\n')
    f.write(f'Nash mix: no candidate team scores above {100 * (1 - worst):.1f}% against it.\n')
with open(R / 'rotation.txt', 'w') as f:
    for i in range(n):
        if w[i] > 0: f.write(f'{w[i]:.3f} {R}/teams/{names[i]}.paste.txt {names[i]}\n')
print(open(R / 'round_robin.txt').read()); print('ROUND_ROBIN_DONE', flush=True)
