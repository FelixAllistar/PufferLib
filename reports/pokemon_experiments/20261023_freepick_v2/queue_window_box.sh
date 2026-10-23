#!/usr/bin/env bash
# Free-pick v2 (2026-10-23), queued on the window box after the window self-play run.
# Same as free-pick v1 (from scratch, upgraded 13-bank league at 93.75% of games, eps 0.9 -> 0 at
# half of training, 2B steps, MinGRU), plus env.force_core_until = 0.5: the forced-core share falls
# from 0.75 to 0 by mid-training, so the second half is all free drafts with its own team.
# Compare: free-pick v1 (constant 75% forced cores) plateaued near 37 / 38 on SyntheticRLV2 / TaurosV0.
cd /workspace/PufferLib
until [ -f reports/pokemon_experiments/20261023_window/selfplay_zam/trained ]; do sleep 60; done
mv puffer_fp2 puffer
R=reports/pokemon_experiments/20261023_freepick_v2/run
mkdir -p $R
tmux new -d -s fp2v2_eval "bash reports/pokemon_experiments/20261023_freepick_v2/eval_dir.sh $R; exec bash"
bash reports/pokemon_experiments/20261022_freepick_league/run_freepick.sh $R --env.expert_random_start=0.9 --env.expert_random_until=0.5 --env.force_core_until=0.5
echo FREEPICK_V2_DONE
