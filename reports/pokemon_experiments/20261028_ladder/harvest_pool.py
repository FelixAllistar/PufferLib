#!/usr/bin/env python3
"""Ladder-run team pool (2026-10-28): every team we can get, for training with and against.
Sources, each counted in pool_counts.txt:
  - the giant run's pool (3605: Metamon sets, our teams, Klitty);
  - Smogon: 256 random Smogon OU teams built from Smogon sets (20261023_next/smogon_pool);
  - the former held-out sets A (hl_05_26), B (modern_replays_v2), C (gl_05_26) and our turf (no longer held out);
  - every opponent team we met on the Showdown ladder (ladder_audit.json from the saved battle logs), completed:
    revealed species and moves are kept; unseen moves come from the most frequent real set of that species (Metamon
    hl_05_26 + gl_05_26 + modern_replays_v2 + competitive, ~60K teams) that contains the revealed moves; missing
    species come from the real team containing the most revealed species. Ladder teams are weighted 10x.
Every team is checked in our engine (a mirror reset) before it goes in. Writes 20261028_ladder/pool.ini.
Usage (box repo root, metamon eval python): harvest_pool.py"""
import collections, ctypes as C, glob, json, os, re, sys
sys.path.insert(0, 'ocean/pokemon/benchmarks')
from pathlib import Path
from evaluate_private import load_team, library
from private_view import PublicMechanics, ROOT, normalize
OUT = Path('reports/pokemon_experiments/20261028_ladder'); OUT.mkdir(parents=True, exist_ok=True)
rules = PublicMechanics()
SP = {normalize(p['name']): p['id'] for p in rules.data['species']}; SPN = {v: k for k, v in SP.items()}
MV = {normalize(m['name']): m['id'] for m in rules.data['moves']}
lib = library(ROOT / 'build/pokemon/private_adapter_mirror.so')
lib.private_reset_mirror.argtypes = [C.c_void_p, C.c_uint64, C.c_int, C.c_void_p, C.c_void_p]; lib.private_reset_mirror.restype = C.c_int
h = lib.bench_create(os.path.abspath(glob.glob('reports/pokemon_experiments/20261027_giant/checkpoints/pokemon/*/0000003999793152.bin')[0]).encode(), 1)
def playable(mons):
    arr = (C.c_uint8 * 30)(*[v for m in mons for v in (m + [0, 0, 0, 0])[:5]])
    return lib.private_reset_mirror(h, 1, 0, arr, arr) >= 0 and lib.private_reset_mirror(h, 2, 1, arr, arr) >= 0
def setstr(mons): return 'sets:' + '/'.join(','.join(map(str, (m + [0, 0, 0, 0])[:5])) for m in mons)

# Corpus of real teams (species ids + move ids) for completion.
def parse(path):
    try: arr = list(load_team(Path(path).stem, rules, Path(path))[0])
    except Exception: return None
    return [arr[i:i + 5] for i in range(0, 30, 5)]
corpus = []
for s in ('hl_05_26', 'gl_05_26', 'modern_replays_v2', 'competitive'):
    for p in glob.glob(f'build/pokemon/metamon-cache/teams/{s}/gen1ou/*_team'):
        t = parse(p)
        if t: corpus.append(t)
print('corpus', len(corpus), 'teams', flush=True)
sets_by_species = collections.defaultdict(collections.Counter)
for t in corpus:
    for m in t: sets_by_species[m[0]][tuple(sorted(x for x in m[1:] if x))] += 1

def complete(revealed):  # revealed: {species_id: set(move ids)}
    best, score = None, -1
    for t in corpus:
        sp = {m[0] for m in t}
        k = len(sp & set(revealed))
        if k > score or (k == score and best is None): best, score = t, k
        if k == len(revealed): break
    mons = []
    for sid, moves in revealed.items():
        cands = [(n, s) for s, n in sets_by_species[sid].items() if moves <= set(s)]
        if cands: moveset = list(max(cands)[1])
        else:
            pool = sorted(sets_by_species[sid].items(), key=lambda x: -x[1])
            fill = [m for s, _ in pool for m in s if m not in moves]
            moveset = list(moves) + [m for m in dict.fromkeys(fill)][:max(0, 4 - len(moves))]
        mons.append([sid] + moveset[:4])
    for m in best:
        if len(mons) == 6: break
        if m[0] not in revealed: mons.append(list(m))
    return mons

counts = collections.OrderedDict(); banks = []; seen = set()
def add(source, name, mons, lead, weight=1, note=''):
    key = setstr(mons)
    if key in seen or not playable(mons): return 0
    seen.add(key)
    for w in range(weight): banks.append((f'{name}' + (f'.w{w}' if weight > 1 else ''), key, str(lead), f'{source}: {note}'))
    counts[source] = counts.get(source, 0) + 1
    return 1

# 1) giant pool
for name, team, lead in re.findall(r'\[bank\.\d+\]\s*name = (\S+)\s*team = (\S+)\s*lead = (\S+)', open('reports/pokemon_experiments/20261027_giant/pool.ini').read()):
    if team in seen: continue
    seen.add(team); banks.append((name, team, lead, 'giant run pool')); counts['giant run pool (Metamon sets, ours, Klitty)'] = counts.get('giant run pool (Metamon sets, ours, Klitty)', 0) + 1
# 2) Smogon pool (catalog-set teams are valid team strings as they are)
for i, team in enumerate(re.findall(r'^team = (\S+)', open('reports/pokemon_experiments/20261023_next/smogon_pool/pool.ini').read(), re.M)):
    if team in seen: continue
    seen.add(team); banks.append((f'smogon{i:03d}', team, 'None', 'Smogon OU sets')); counts['Smogon (256 random OU teams from Smogon sets)'] = counts.get('Smogon (256 random OU teams from Smogon sets)', 0) + 1
# 3) former held-out sets
for d, label in (('metamon_hl_05_26_sample28', 'held-out A (hl_05_26)'), ('metamon_modern_replays_v2_sample28', 'held-out B (modern_replays_v2)'),
                 ('metamon_gl_05_26_sample28', 'held-out C (gl_05_26)'), ('our_turf30', 'our turf')):
    for p in sorted(glob.glob(f'reports/pokemon_benchmarks/{d}/*')):
        if not p.endswith(('_team', '.paste.txt')): continue
        t = parse(p)
        if t: add(label, f'{d[:12]}.{Path(p).stem}', t, t[0][0], 1, p)
# 4) ladder opponents
ladder = json.load(open('reports/pokemon_experiments/20261027_showdown_vs_metamon/ladder_audit.json'))
raw = 0
for r in ladder:
    revealed = {}
    for sp, moves in r['team'].items():
        sid = SP.get(normalize(sp))
        if sid: revealed[sid] = {MV[normalize(m)] for m in moves if normalize(m) in MV}
    if len(revealed) < 3: continue
    raw += 1
    mons = complete(revealed)
    if len(mons) == 6: add('ladder opponents (completed, 10x)', f'ladder.{re.sub(r"[^a-z0-9]", "", r["opp"].lower())[:16]}.{raw}', mons, mons[0][0], 10, r['file'])
lines = ['# Ladder-run pool (2026-10-28): harvest_pool.py', '[native]', f'banks = {len(banks)}', '']
for i, (name, team, lead, note) in enumerate(banks):
    lines += [f'# {note}', f'[bank.{i}]', f'name = {name}', f'team = {team}', f'lead = {lead}', '']
(OUT / 'pool.ini').write_text('\n'.join(lines))
with open(OUT / 'pool_counts.txt', 'w') as f:
    f.write(f'ladder battles with a usable revealed team: {raw} (of {len(ladder)} logged)\n')
    for k, v in counts.items(): f.write(f'{k}: {v} unique teams\n')
    f.write(f'unique teams: {len(seen)}; pool slots (ladder weighted 10x): {len(banks)}\n')
print(open(OUT / 'pool_counts.txt').read())
