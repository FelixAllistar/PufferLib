#!/usr/bin/env bash
# Complete battery vs Metamon for the 1B checkpoint, filling the missing cells after our_turf.sh: Abra on every test
# (unseen teams A and B, our turf, Metamon's competitive teams in mirror, full game) and Kakuna on the competitive mirror.
# Mirror tests: identical teams both sides, every ordered pair on shared seeds, 2 seeds per pair. Full game:
# zam_egg_cloy_don (our policy drafts its moves) vs Metamon's 28 competitive teams, 16 games each.
cd /workspace/PufferLib
R=reports/pokemon_experiments/20261026_metamon_teams_1b
PY=build/pokemon/metamon-eval-env-cu128/bin/python
B=reports/pokemon_benchmarks
CK=$(ls $R/checkpoints/pokemon/*/0000000999817216.bin)
until grep -q OUR_TURF_DONE $R/our_turf.txt 2>/dev/null; do sleep 60; done
mkdir -p $R/battery
mirror() { out=$R/battery/$1_$2; [ -f $out/summary.json ] || $PY ocean/pokemon/benchmarks/evaluate_batched.py $CK --output $out \
  --opponent $1 --mirror --team-dir $B/$3 --games 2 --seed $4 --library build/pokemon/private_adapter_mirror.so --batch 256 \
  --workers 12 --threads 1 > $out.log 2>&1; }
mirror Kakuna competitive metamon_competitive_gen1ou 20261026
mirror Abra unseenA metamon_hl_05_26_sample28 20261027
mirror Abra unseenB metamon_modern_replays_v2_sample28 20261028
mirror Abra ourturf our_turf30 20261029
mirror Abra competitive metamon_competitive_gen1ou 20261026
F=$R/kakuna/full
cp $CK $F/
[ -f $R/battery/Abra_full/summary.json ] || $PY ocean/pokemon/benchmarks/evaluate_batched.py $F/$(basename $CK) --output $R/battery/Abra_full \
  --opponent Abra --team-dir $B/metamon_competitive_gen1ou --games 16 --seed 20261026 --batch 256 --workers 12 --threads 1 \
  > $R/battery/Abra_full.log 2>&1
rm -f $F/$(basename $CK)
echo BATTERY_DONE > $R/battery/done
