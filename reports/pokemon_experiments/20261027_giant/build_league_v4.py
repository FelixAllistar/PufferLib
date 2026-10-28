#!/usr/bin/env python3
"""LeagueV4 (2026-10-27): the 2B long run's best checkpoint (1875M) piloting LeagueV3's 16 teams (8 of Metamon's
competitive teams, Princess Klitty's Jynx version, 7 of ours), plus 8 of LeagueV3's banks (the 1B checkpoint) for policy
variety: 24 banks. The 1875M checkpoint trained on the final sweep's pool, so it may pilot any of these teams (pk_pool_has).
Usage (box repo root): build_league_v4.py"""
import glob, re, shutil
from pathlib import Path
V3 = Path('leagues/pokemon/league_v3_mm1B'); OUT = Path('leagues/pokemon/league_v4_2b1875')
ck = Path(glob.glob('reports/pokemon_experiments/20261026_long2b/checkpoints/pokemon/*/0000001875640320.bin')[0])
v3 = re.findall(r'\[bank\.\d+\]\s*name = (\S+)\s*path = (\S+)\s*team = (\S+)\s*lead = (\S+)', (V3 / 'league.ini').read_text())
assert len(v3) == 16
(OUT / 'b1875').mkdir(parents=True, exist_ok=True)
shutil.copy(ck, OUT / 'b1875/weights.bin')
shutil.copy(ck.with_name('config.ini'), OUT / 'b1875/config.ini')
new = Path.cwd() / OUT / 'b1875/weights.bin'
sha = re.search(r'rules_sha = (\S+)', ck.with_name('config.ini').read_text()).group(1)
assert sha in (V3 / 'league.ini').read_text(), 'rules changed since LeagueV3'
banks = [(n + '.b1875', str(new), t, l, f'2B long run 1875M ({ck})') for n, _, t, l in v3] + \
        [(n + '.mm1B', p, t, l, 'LeagueV3 bank (Metamon-teams 1B)') for n, p, t, l in v3[::2]]
lines = ['[native]', 'version = 3', f'banks = {len(banks)}', 'hidden_size = 1024', 'num_layers = 2', f'rules_sha = {sha}',
         f'opponents = {Path.cwd() / OUT}/opponents.txt', '']
for i, (name, path, team, lead, note) in enumerate(banks):
    lines += [f'# {note} piloting {name}', f'[bank.{i}]', f'name = {name}', f'path = {path}', f'team = {team}', f'lead = {lead}', '']
(OUT / 'league.ini').write_text('\n'.join(lines))
(OUT / 'opponents.txt').write_text(''.join(f'{b[1]}\n' for b in banks))
print(len(banks), 'banks')
