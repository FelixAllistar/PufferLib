#!/usr/bin/env bash
# Second held-out mirror test: 28 modern_replays_v2 teams (never trained on), the 1B checkpoint, 1568 games per model.
cd /workspace/PufferLib
R=reports/pokemon_experiments/20261026_metamon_teams_1b
PY=build/pokemon/metamon-eval-env-cu128/bin/python
CK=$(ls $R/checkpoints/pokemon/*/0000000999817216.bin)
mkdir -p $R/heldout2
for opp in TaurosV0 SyntheticRLV2; do out=$R/heldout2/mm1B_$opp
  [ -f $out/summary.json ] || $PY ocean/pokemon/benchmarks/evaluate_batched.py $CK --output $out --opponent $opp --mirror \
    --team-dir reports/pokemon_benchmarks/metamon_modern_replays_v2_sample28 --games 2 --seed 20261028 \
    --library build/pokemon/private_adapter_mirror.so --batch 256 --workers 12 --threads 1 > $out.log 2>&1
  python3 -c "import json,sys,math;r=json.load(open(sys.argv[1]));s=r['score'];print(sys.argv[2],round(100*s,1),'+/-',round(196*math.sqrt(s*(1-s)/r['games']),1),r['games'],'games')" $out/summary.json $opp | tee -a $R/heldout2.txt
done
echo HELDOUT2_DONE >> $R/heldout2.txt
