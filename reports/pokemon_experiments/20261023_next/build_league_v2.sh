#!/usr/bin/env bash
# LeagueV2 (2026-10-25): the frozen league is OMNI-1024 alone, piloting each of the 13 league teams as its own bank
# (same names, teams and leads as the upgraded league). Allowed because an OMNI checkpoint may pilot any team of
# its own learner_team_pool (pk_pool_has in ocean/pokemon/pokemon_base.h). The weights and config.ini are copied
# into the league dir so the league stays fixed. Usage: build_league_v2.sh [STEP] (default the 2B checkpoint).
cd /workspace/PufferLib
STEP=${1:-0000001999896576}
N=reports/pokemon_experiments/20261023_next
OLD=reports/pokemon_experiments/20261022_freepick_league/league/league.ini
OUT=leagues/pokemon/league_v2_omni1024_$STEP
CK=$(ls $N/omni_master_1024/checkpoints/pokemon/*/$STEP.bin)
mkdir -p $OUT/omni1024
cp $CK $OUT/omni1024/weights.bin
cp $(dirname $CK)/config.ini $OUT/omni1024/config.ini
W=/workspace/PufferLib/$OUT/omni1024/weights.bin
python3 - $OLD $OUT $W $CK <<'PY'
import sys, re
old, out, weights, source = sys.argv[1:]
text = open(old).read()
banks = re.findall(r'\[bank\.(\d+)\]\s*name = (\S+)\s*path = \S+\s*team = (\S+)\s*lead = (\S+)', text)
assert len(banks) == 13, banks
head = re.search(r'rules_sha = (\S+)', text).group(1)
lines = ['[native]', 'version = 3', f'banks = {len(banks)}', 'hidden_size = 1024', 'num_layers = 2', f'rules_sha = {head}',
         f'opponents = /workspace/PufferLib/{out}/opponents.txt', '']
for i, name, team, lead in banks:
    lines += [f'# OMNI-1024 ({source}) piloting {name}', f'[bank.{i}]', f'name = {name}', f'path = {weights}',
              f'team = {team}', f'lead = {lead}', '']
open(f'{out}/league.ini', 'w').write('\n'.join(lines))
open(f'{out}/opponents.txt', 'w').write(''.join(weights + '\n' for _ in banks))
print(f'{len(banks)} banks -> {out}/league.ini')
PY
