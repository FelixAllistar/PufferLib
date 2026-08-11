#!/bin/bash
set -euo pipefail

cd "$(dirname "$0")/../.."

kag_dir=${1:-saved/kaggriculture_bc_v2}
kag_open_games=${KAG_BC_OPEN_GAMES:-400}
kag_full_games=${KAG_BC_FULL_GAMES:-400}
kag_open_epochs=${KAG_BC_OPEN_EPOCHS:-500}
kag_full_epochs=${KAG_BC_FULL_EPOCHS:-1000}
mkdir -p "$kag_dir"

make -C ocean/kaggriculture build/kag_bc

kag_bc=./ocean/kaggriculture/build/kag_bc
kag_open_data=$kag_dir/opening_data.bin
kag_open_model=$kag_dir/opening_anchor.bin
kag_full_data=$kag_dir/recovery_96_data.bin
kag_full_model=$kag_dir/top_clone.bin

# Phase A: exact recurrent opening imitation. No rollout perturbation here;
# this phase must reproduce the canonical 26-action prefix from either seat.
"$kag_bc" bc.mode=gen bc.games="$kag_open_games" bc.steps=26 \
    bc.bot=1 bc.opponent=-1 bc.seat=-1 bc.rollout_noise=0 \
    bc.seed=1701 bc.data="$kag_open_data"
"$kag_bc" bc.mode=train bc.data="$kag_open_data" \
    bc.epochs="$kag_open_epochs" bc.batch=64 bc.learning_rate=0.001 \
    bc.opening_steps=26 bc.opening_weight=1 bc.validation_games=80 \
    bc.zero_reset_source=0 policy.hidden_size=128 policy.num_layers=2 \
    bc.output="$kag_open_model"
./ocean/kaggriculture/bc_gate.sh opening "$kag_open_model" 64

# Phase B: opening handoff plus early recovery labels. The expert action is saved
# before a 5% rollout perturbation; later observations therefore teach the
# expert's response to displaced workers and missing economic bundles.
"$kag_bc" bc.mode=gen bc.games="$kag_full_games" bc.steps=96 \
    bc.bot=1 bc.opponent=-1 bc.seat=-1 bc.rollout_noise=0.05 \
    bc.seed=2903 bc.data="$kag_full_data"
"$kag_bc" bc.mode=train bc.data="$kag_full_data" \
    bc.epochs="$kag_full_epochs" bc.batch=32 bc.learning_rate=0.0002 \
    bc.opening_steps=26 bc.opening_weight=3 bc.validation_games=80 \
    bc.zero_reset_source=0 bc.load_model_path="$kag_open_model" \
    policy.hidden_size=128 policy.num_layers=2 bc.output="$kag_full_model"
./ocean/kaggriculture/bc_gate.sh recovery "$kag_full_model" 64

cat > "$kag_dir/ppo_overrides.txt" <<EOF
base.load_model_path=$kag_full_model
selfplay.magnet_path=$kag_full_model
train.emag_tau=0
train.emag_kl_coef=0.01
train.emag_cutoff=0.134
env.opening_turns=0
env.reset_opening_turns=0
env.reset_opening_prob=0
selfplay.enabled=1
selfplay.opponent_league=saved/kaggriculture_league_v6/league.ini
selfplay.opponent_pool_prob=0.75
selfplay.pfsp_uniform_mix=0.25
env.bot_opponent_fraction=0.50
env.bot_top_fraction=0.70
env.bot_rules_fraction=0.50
env.bot_script_fraction=0.40
env.bot_adaptive_fraction=0.60
env.reward_productive_action=0
env.reward_inactivity=0
env.reward_neglect_death=0
EOF

echo "Faithful BC clone: $kag_full_model"
echo "Frozen-KL PPO overrides: $kag_dir/ppo_overrides.txt"
echo "Next: ./ocean/kaggriculture/train_bc_ppo.sh $kag_full_model"
