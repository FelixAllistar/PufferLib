#!/usr/bin/env bash
# FP-4 (2026-10-25): free-pick from OMNI-1024 2B (the eval matrix's pick) against LeagueV2 (frozen OMNI-1024 2B
# piloting each of the 13 league teams; build_league_v2.sh). Same as FP-3 otherwise: no forcing, expert_fraction
# 0.3125 (81.5% of the learner's games are self-play), 1B, scored every 125M by eval_dir.sh. Also the live test of
# the LeagueV2 env change (the log must show 13 vs_<team>_score lines).
cd /workspace/PufferLib
R=reports/pokemon_experiments/20261023_next/freepick_league_v2
L=leagues/pokemon/league_v2_omni1024_0000001999896576
INIT=$(ls reports/pokemon_experiments/20261023_next/omni_master_1024/checkpoints/pokemon/*/0000001999896576.bin)  # config.ini is beside it
mkdir -p $R
tmux new -d -s freepick_league_v2_eval -c /workspace/PufferLib "bash reports/pokemon_experiments/20261023_freepick_v2/eval_dir.sh $R; exec bash"
bash reports/pokemon_experiments/20261022_freepick_league/run_freepick.sh $R \
  --base.load_model_path=$INIT --train.total_timesteps=1000000000 --base.checkpoint_interval=477 \
  --env.expert_fraction=0.3125 --env.force_core_prob=0 --env.force_move_pairs_prob=0 \
  --env.expert_random_start=0.3 --env.expert_random_until=0.2 --policy.hidden_size=1024 \
  --env.native_league=$L/league.ini --selfplay.initial_opponents=/workspace/PufferLib/$L/opponents.txt \
  --vec.hist_policy_hidden_size=1024
echo FREEPICK_LEAGUE_V2_DONE
