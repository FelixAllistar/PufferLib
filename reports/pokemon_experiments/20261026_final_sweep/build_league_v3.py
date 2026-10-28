#!/usr/bin/env python3
"""LeagueV3 (2026-10-26): the Metamon-teams 1B checkpoint piloting 16 diverse teams of its own training pool (allowed by
pk_pool_has: a pool-trained checkpoint may pilot any team of its pool): 8 of Metamon's competitive teams, Princess
Klitty's Jynx version (exact sets) and 7 of ours (species fixed). Weights and config.ini are copied into the league dir.
Usage (box repo root): build_league_v3.py"""
import glob, re, shutil
from pathlib import Path
M = Path('reports/pokemon_experiments/20261026_metamon_teams_1b')
OUT = Path('leagues/pokemon/league_v3_mm1B')
ck = Path(glob.glob(str(M / 'checkpoints/pokemon/*/0000000999817216.bin'))[0])
pool = re.findall(r'\[bank\.\d+\]\s*name = (\S+)\s*team = (\S+)\s*lead = (\S+)', (M / 'pool.ini').read_text())
comp = [b for b in pool if '.mm' in b[0]]
ours = ['gar_egg_zap', 'zam_egg_cloy_don', 'mie_egg_zap', 'jynx_mie_don', 'zam_cha_egg_lap_tau_zap.s1',
        'zam_cha_lap_lax_mie_tau.s1', 'zam_cha_gol_lap_tau_zap.s1']
banks = comp[::3][:8] + [b for b in pool if b[0] == 'zam_cha_egg_cloy_don_jynx.klitty'] + [b for b in pool if b[0] in ours]
assert len(banks) == 16, len(banks)
(OUT / 'mm1B').mkdir(parents=True, exist_ok=True)
shutil.copy(ck, OUT / 'mm1B/weights.bin')
shutil.copy(ck.with_name('config.ini'), OUT / 'mm1B/config.ini')
weights = Path.cwd() / OUT / 'mm1B/weights.bin'
sha = re.search(r'rules_sha = (\S+)', (ck.with_name('config.ini')).read_text()).group(1)
lines = ['[native]', 'version = 3', f'banks = {len(banks)}', 'hidden_size = 1024', 'num_layers = 2', f'rules_sha = {sha}',
         f'opponents = {Path.cwd() / OUT}/opponents.txt', '']
for i, (name, team, lead) in enumerate(banks):
    lines += [f'# Metamon-teams 1B ({ck}) piloting {name}', f'[bank.{i}]', f'name = {name}', f'path = {weights}',
              f'team = {team}', f'lead = {lead}', '']
(OUT / 'league.ini').write_text('\n'.join(lines))
(OUT / 'opponents.txt').write_text(''.join(f'{weights}\n' for _ in banks))
print(len(banks), 'banks:', ', '.join(b[0] for b in banks))
