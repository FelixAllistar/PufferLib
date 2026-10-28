#!/usr/bin/env bash
# The 1B checkpoint vs Kakuna (Metamon's stated current best): unseen-teams mirror tests A (hl_05_26) and B
# (modern_replays_v2), 1568 games each, and the full game (zam_egg_cloy_don vs Metamon's 28 competitive teams, 448).
cd /workspace/PufferLib
R=reports/pokemon_experiments/20261026_metamon_teams_1b
PY=build/pokemon/metamon-eval-env-cu128/bin/python
CK=$(ls $R/checkpoints/pokemon/*/0000000999817216.bin)
B=reports/pokemon_benchmarks
mkdir -p $R/kakuna
line() { python3 -c "import json,sys,math;r=json.load(open(sys.argv[1]));s=r['score'];h=1.96*math.sqrt(s*(1-s)/r['games']);e=lambda p:400*math.log10(p/(1-p));print(sys.argv[2],round(100*s,1),'+/-',round(100*h,1),r['games'],'games, Elo',round(e(s)),[round(e(s-h)),round(e(s+h))])" $1 "$2" | tee -a $R/kakuna.txt; }
for set in A:metamon_hl_05_26_sample28:20261027 B:metamon_modern_replays_v2_sample28:20261028; do
  IFS=: read tag dir seed <<< "$set"; out=$R/kakuna/mirror_$tag
  [ -f $out/summary.json ] || $PY ocean/pokemon/benchmarks/evaluate_batched.py $CK --output $out --opponent Kakuna --mirror \
    --team-dir $B/$dir --games 2 --seed $seed --library build/pokemon/private_adapter_mirror.so --batch 256 --workers 12 \
    --threads 1 > $out.log 2>&1
  line $out/summary.json "unseen teams $tag mirror vs Kakuna"
done
F=$R/kakuna/full; mkdir -p $F; cp $CK $F/; python3 - $(dirname $CK)/config.ini $F/config.ini $R/pool.ini <<'PY'
import re, sys
src, dst, pool = sys.argv[1:]
team, lead = re.search(r'name = zam_egg_cloy_don\s*team = (\S+)\s*lead = (\S+)', open(pool).read()).groups()
out, sec = [], None
for line in open(src):
    s = line.strip()
    if s.startswith('['): sec = s[1:-1]
    k = s.split('=')[0].strip() if '=' in s else None
    if sec == 'env' and k == 'learner_team': line = f'learner_team = {team}\n'
    if sec == 'env' and k == 'learner_lead': line = f'learner_lead = {lead}\n'
    out.append(line)
open(dst, 'w').writelines(out)
PY
[ -f $F/eval/summary.json ] || $PY ocean/pokemon/benchmarks/evaluate_batched.py $F/$(basename $CK) --output $F/eval --opponent Kakuna \
  --team-dir $B/metamon_competitive_gen1ou --games 16 --seed 20261026 --batch 256 --workers 12 --threads 1 > $F/eval.log 2>&1
line $F/eval/summary.json "full game (zam_egg_cloy_don vs Metamon's 28 competitive teams) vs Kakuna"
rm -f $F/$(basename $CK)
echo KAKUNA_DONE >> $R/kakuna.txt
