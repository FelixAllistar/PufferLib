#!/usr/bin/env bash
# Mirror test on "our turf" (reports/pokemon_benchmarks/our_turf30: our 18 teams with our 1B checkpoint's drafted sets,
# Princess Klitty's 2 teams, 10 random Smogon OU teams): identical teams both sides, every ordered pair on shared seeds,
# 2 seeds per pair (1800 games) vs Kakuna, TaurosV0 and SyntheticRLV2. The counterpart of the held-out tests: Metamon
# piloting teams from outside its data.
cd /workspace/PufferLib
R=reports/pokemon_experiments/20261026_metamon_teams_1b
PY=build/pokemon/metamon-eval-env-cu128/bin/python
CK=$(ls $R/checkpoints/pokemon/*/0000000999817216.bin)
mkdir -p $R/our_turf
for opp in Kakuna TaurosV0 SyntheticRLV2; do out=$R/our_turf/mirror_$opp
  [ -f $out/summary.json ] || $PY ocean/pokemon/benchmarks/evaluate_batched.py $CK --output $out --opponent $opp --mirror \
    --team-dir reports/pokemon_benchmarks/our_turf30 --games 2 --seed 20261029 --library build/pokemon/private_adapter_mirror.so \
    --batch 256 --workers 12 --threads 1 > $out.log 2>&1
  python3 -c "import json,sys,math;r=json.load(open(sys.argv[1]));s=r['score'];h=1.96*math.sqrt(s*(1-s)/r['games']);e=lambda p:400*math.log10(p/(1-p));print('our turf mirror vs',sys.argv[2],round(100*s,1),'+/-',round(100*h,1),r['games'],'games, Elo',round(e(s)),[round(e(s-h)),round(e(s+h))])" $out/summary.json $opp | tee -a $R/our_turf.txt
done
echo OUR_TURF_DONE >> $R/our_turf.txt
