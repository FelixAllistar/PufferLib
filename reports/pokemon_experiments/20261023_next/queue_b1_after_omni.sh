#!/usr/bin/env bash
# B1 box, after omni_master_256 finishes training: free-pick from OMNI, plus the pilot-generality evals of
# OMNI's final checkpoint and the best specialist (B0 gar_egg_zap) on every team in pilot_teams.txt.
cd /workspace/PufferLib
N=reports/pokemon_experiments/20261023_next
until [ -f $N/omni_master_256/trained ]; do sleep 60; done
OMNI=$(ls $N/omni_master_256/checkpoints/pokemon/*/[0-9]*.bin | tail -1)
tmux new -d -s pilot_eval -c /workspace/PufferLib "bash $N/eval_teams.sh $OMNI $N/pilot_eval/omni_master_256; bash $N/eval_teams.sh /workspace/PufferLib/leagues/pokemon/rr20m_256x2_20261020/members/gar_egg_zap/r000026-a001/weights.bin $N/pilot_eval/b0_gar_egg_zap; exec bash"
bash $N/run_freepick_omni.sh
