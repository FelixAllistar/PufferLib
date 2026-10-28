#!/usr/bin/env bash
# Held-out mirror test: 28 high-ladder teams (Metamon hl_05_26 sample, never in any training pool), identical sets on
# both sides, every ordered pair, 2 shared seeds per pair (1568 games per model). The Metamon-teams run at 1B and
# OMNI-2 trial 11 (trained on neither set). Then mm_eval.py resumes the older checkpoints.
cd /workspace/PufferLib
R=reports/pokemon_experiments/20261026_metamon_teams_1b
PY=build/pokemon/metamon-eval-env-cu128/bin/python
M1B=$(ls $R/checkpoints/pokemon/*/0000000999817216.bin)
T11=$(ls reports/pokemon_experiments/20261025_omni2_sweep/checkpoints/pokemon/sweep_*_0011/0000000099876864.bin)
mkdir -p $R/heldout
for model in mm1B:$M1B t11:$T11; do name=${model%%:*}; ck=${model#*:}
  for opp in TaurosV0 SyntheticRLV2; do out=$R/heldout/${name}_$opp
    [ -f $out/summary.json ] || $PY ocean/pokemon/benchmarks/evaluate_batched.py $ck --output $out --opponent $opp --mirror \
      --team-dir reports/pokemon_benchmarks/metamon_hl_05_26_sample28 --games 2 --seed 20261027 \
      --library build/pokemon/private_adapter_mirror.so --batch 256 --workers 12 --threads 1 > $out.log 2>&1
    python3 -c "import json,sys,math;r=json.load(open(sys.argv[1]));s=r['score'];print(sys.argv[2],sys.argv[3],round(100*s,1),'+/-',round(196*math.sqrt(s*(1-s)/r['games']),1),r['games'],'games')" $out/summary.json $name $opp | tee -a $R/heldout.txt
  done; done
echo HELDOUT_DONE >> $R/heldout.txt
python3 $R/mm_eval.py $R >> $R/mm_eval.log 2>&1
