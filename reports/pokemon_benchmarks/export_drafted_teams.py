#!/usr/bin/env python3
"""Export teams with the exact sets a checkpoint drafts: for each species-fixed team of a pool manifest, the
checkpoint (argmax) drafts its moves against a fixed opponent team, and the result is written as a Showdown export
(the drafted order, lead first), so other pilots (e.g. Metamon) can play our teams with our sets in a mirror test.
Usage (box repo root, PYTHONPATH=ocean/pokemon/benchmarks): export_drafted_teams.py CHECKPOINT POOL OUT_DIR"""
import ctypes as C, os, re, shutil, sys, tempfile
from pathlib import Path
from evaluate_private import load_team, library
from private_view import PublicMechanics, ROOT

ck, pool, out = sys.argv[1:]
rules = PublicMechanics()
species = {p['id']: p['name'] for p in rules.data['species']}
moves = {m['id']: m['name'] for m in rules.data['moves']}
lib = library(ROOT / 'build/pokemon/private_adapter_mirror.so')
lib.bench_team.argtypes = [C.c_void_p, C.c_void_p]; lib.bench_team.restype = None
opponent = load_team('team_1001', rules, ROOT / 'reports/pokemon_benchmarks/metamon_competitive_gen1ou/team_1001.gen1ou_team')[0]
os.makedirs(out, exist_ok=True)
teams = [(n, t, l) for n, t, l in re.findall(r'\[bank\.\d+\]\s*name = (\S+)\s*team = (\S+)\s*lead = (\S+)', open(pool).read())
         if t.startswith('species:')]
for name, team, lead in teams:
    tmp = Path(tempfile.mkdtemp())
    shutil.copy(ck, tmp / 'w.bin')
    with open(Path(ck).with_name('config.ini')) as src, open(tmp / 'config.ini', 'w') as dst:
        sec = None
        for line in src:
            s = line.strip()
            if s.startswith('['): sec = s[1:-1]
            k = s.split('=')[0].strip() if '=' in s else None
            if sec == 'env' and k == 'learner_team': line = f'learner_team = {team}\n'
            if sec == 'env' and k == 'learner_lead': line = f'learner_lead = {lead}\n'
            dst.write(line)
    h = lib.bench_create(str(tmp / 'w.bin').encode(), 1)
    assert lib.bench_reset(h, 20261029, 0, opponent) == 0, name
    drafted = (C.c_uint8 * 30)(); lib.bench_team(h, drafted); lib.bench_destroy(h)
    shutil.rmtree(tmp)
    mons = [list(drafted)[i:i + 5] for i in range(0, 30, 5)]
    text = '\n\n'.join(species[m[0]] + ''.join(f'\n- {moves[x]}' for x in m[1:] if x) for m in mons) + '\n'
    (Path(out) / f'{name}.paste.txt').write_text(text)
    print(name, '|', '; '.join(species[m[0]] + ': ' + '/'.join(moves[x] for x in m[1:] if x) for m in mons))
