#!/usr/bin/env bash
set -euo pipefail

cd "$(dirname "$0")/../.."

if (($# < 1 || $# > 3)); then
    echo "Usage: $0 TASK_DATA.bc [RUN_ID] [MODE2_LEAGUE.ini]" >&2
    exit 2
fi

kag_data=$1
kag_run=${2:-kag_task_256x3_v1}
kag_league=${3:-saved/kaggriculture_league_macro_256x3_v1/league.ini}
kag_hidden=${KAG_TASK_HIDDEN:-256}
kag_layers=${KAG_TASK_LAYERS:-3}
kag_frozen_hidden=${KAG_TASK_FROZEN_HIDDEN:-256}
kag_frozen_layers=${KAG_TASK_FROZEN_LAYERS:-3}
kag_anchor=${KAG_TASK_ANCHOR:-saved/kaggriculture_task_bc_1280_${kag_hidden}x${kag_layers}.bin}
kag_bc_epochs=${KAG_TASK_BC_EPOCHS:-20}
kag_bc_batch=${KAG_TASK_BC_BATCH:-8}
kag_bc_lr=${KAG_TASK_BC_LR:-0.00005}
kag_steps=${KAG_TASK_PPO_STEPS:-500000000}
kag_agents=${KAG_TASK_AGENTS:-512}
kag_horizon=${KAG_TASK_HORIZON:-256}
kag_minibatch=${KAG_TASK_MINIBATCH:-2048}
kag_lr=${KAG_TASK_PPO_LR:-0.0007}

[[ -s $kag_data ]] || { echo "Task BC dataset not found: $kag_data" >&2; exit 1; }

if [[ ${KAG_TASK_SKIP_BUILD:-0} != 1 ]]; then
    make -C ocean/kaggriculture build/kag_bc
fi

if ((kag_bc_epochs > 0)); then
    mkdir -p "$(dirname "$kag_anchor")"
    KAG_ELITE_BC_DATA="$kag_data" \
    KAG_ELITE_BC_OUTPUT="$kag_anchor" \
    KAG_ELITE_BC_HIDDEN="$kag_hidden" \
    KAG_ELITE_BC_LAYERS="$kag_layers" \
    KAG_ELITE_BC_BATCH="$kag_bc_batch" \
    KAG_ELITE_BC_VALIDATION_GAMES="${KAG_TASK_BC_VALIDATION_GAMES:-400}" \
    KAG_ELITE_BC_OPENING_STEPS="${KAG_TASK_BC_OPENING_STEPS:-300}" \
    KAG_ELITE_BC_OPENING_WEIGHT="${KAG_TASK_BC_OPENING_WEIGHT:-2}" \
    KAG_ELITE_BC_ROOT_WEIGHT="${KAG_TASK_BC_ROOT_WEIGHT:-2}" \
    KAG_ELITE_BC_LR="$kag_bc_lr" \
    KAG_ELITE_BC_TASK_MODE=1 \
    KAG_ELITE_BC_TASK_CLASS_BALANCE="${KAG_TASK_CLASS_BALANCE:-0.75}" \
    KAG_ELITE_BC_TASK_CLASS_WEIGHT_CAP="${KAG_TASK_CLASS_WEIGHT_CAP:-8}" \
    ./ocean/kaggriculture/train_elite_bc.sh "$kag_bc_epochs"
fi

[[ -s $kag_anchor ]] || {
    echo "Task BC checkpoint not found: $kag_anchor" >&2
    echo "Set KAG_TASK_BC_EPOCHS>0 or KAG_TASK_ANCHOR to an existing task model." >&2
    exit 1
}

if [[ ${KAG_TASK_BC_ONLY:-0} == 1 ]]; then
    echo "Task BC complete: $kag_anchor"
    exit 0
fi

[[ -s $kag_league ]] || { echo "Mode-2 league not found: $kag_league" >&2; exit 1; }

if [[ ${KAG_TASK_SKIP_BUILD:-0} != 1 ]]; then
    CUDA_HOME=${CUDA_HOME:-/usr/local/cuda} bash build.sh kaggriculture --gpu
fi

# Policy zero uses the new task ABI. Every nonzero frozen bank retains the
# structured mode-2 observation/mask/decoder it was trained with. The BC
# anchor supplies mechanics and the weak fixed EMAG penalty prevents PPO from
# instantly erasing them while final-cash/self-play returns improve strategy.
exec ./puffer train kaggriculture \
    "base.run_id=$kag_run" \
    "base.load_model_path=$kag_anchor" \
    base.load_enemy_model_path=None \
    base.checkpoint_interval="${KAG_TASK_CHECKPOINT_INTERVAL:-192}" \
    policy.hidden_size="$kag_hidden" policy.num_layers="$kag_layers" \
    vec.total_agents="$kag_agents" vec.num_buffers=1 \
    vec.num_frozen_banks="${KAG_TASK_LEAGUE_BANKS:-4}" \
    vec.frozen_bank_hidden_size="$kag_frozen_hidden" \
    vec.frozen_bank_num_layers="$kag_frozen_layers" \
    vec.frozen_bank_pct="${KAG_TASK_FROZEN_PCT:-0.75}" \
    selfplay.enabled=1 selfplay.max_size=8 \
    "selfplay.opponent_league=$kag_league" \
    selfplay.opponent_pool=None selfplay.opponent_pool_weights=None \
    selfplay.opponent_pool_prob=1 \
    selfplay.pfsp_uniform_mix="${KAG_TASK_PFSP_UNIFORM_MIX:-0.25}" \
    selfplay.snapshot_interval="${KAG_TASK_SNAPSHOT_INTERVAL:-50000000}" \
    "selfplay.magnet_path=$kag_anchor" \
    env.macro_mode=3 env.frozen_macro_mode=2 \
    env.macro_decision_interval=1 \
    env.reset_state_prob="${KAG_TASK_RESET_STATE_PROB:-0}" \
    env.curriculum_enabled=0 \
    env.bot_opponent_fraction="${KAG_TASK_BOT_FRACTION:-0.25}" \
    train.total_timesteps="$kag_steps" train.horizon="$kag_horizon" \
    train.minibatch_size="$kag_minibatch" train.learning_rate="$kag_lr" \
    train.anneal_lr=0 \
    train.emag_kl_coef="${KAG_TASK_EMAG_KL:-0.002}" \
    train.emag_tau=0 train.emag_cutoff="${KAG_TASK_EMAG_CUTOFF:-0.25}"
