#!/usr/bin/env bash
# B1 box: Smogon-pool OMNI, then free-pick from it with three annealing strategies, one after another.
cd /workspace/PufferLib
N=reports/pokemon_experiments/20261023_next
bash $N/run_omni_smogon.sh
for s in fast none slow; do bash $N/run_freepick_smogon.sh $s; done
echo SMOGON_QUEUE_DONE
