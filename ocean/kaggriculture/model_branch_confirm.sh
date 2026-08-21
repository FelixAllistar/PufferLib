#!/usr/bin/env bash
# Matched-reward architecture confirmation for the Kaggriculture branches.
# This intentionally runs two short, pure-self-play continuations:
#   * 128x2 winner using its own reward recipe
#   * 512x3 winner using the same 128x2 reward recipe
# Every source config/checkpoint pair is checked before launch.
set -euo pipefail

root="$(cd "$(dirname "$0")/../.." && pwd)"
cd "$root"

steps=${KAG_CONFIRM_STEPS:-50000000}
stamp=${KAG_CONFIRM_STAMP:-$(date +%Y%m%d%H%M%S)}
dry_run=${KAG_CONFIRM_DRY_RUN:-0}
log="logs/kaggriculture/model_branch_confirm_${stamp}.log"
mkdir -p logs/kaggriculture

ini_value() {
    local file=$1 section=$2 key=$3
    awk -v wanted="[$section]" -v wanted_key="$key" '
        $0 ~ /^\[/ { inside = ($0 == wanted); next }
        inside && $0 ~ "^[[:space:]]*" wanted_key "[[:space:]]*=" {
            line = $0
            sub("^[[:space:]]*" wanted_key "[[:space:]]*=[[:space:]]*", "", line)
            sub("[[:space:]]+$", "", line)
            gsub(/^\047|\047$/, "", line)
            gsub(/^"|"$/, "", line)
            print line
            exit
        }
    ' "$file"
}

assert_pair() {
    local cfg=$1 checkpoint=$2 expected_hidden=$3 expected_layers=$4 expected_bytes=$5
    [[ -f $cfg ]] || { echo "missing config: $cfg" >&2; return 1; }
    [[ -f $checkpoint ]] || { echo "missing checkpoint: $checkpoint" >&2; return 1; }
    local hidden layers bytes
    hidden=$(ini_value "$cfg" policy hidden_size)
    layers=$(ini_value "$cfg" policy num_layers)
    bytes=$(stat -c %s "$checkpoint")
    [[ "$hidden" == "$expected_hidden" && "$layers" == "$expected_layers" ]] || {
        echo "architecture config mismatch: $cfg has ${hidden}x${layers}, expected ${expected_hidden}x${expected_layers}" >&2
        return 1
    }
    [[ "$bytes" == "$expected_bytes" ]] || {
        echo "checkpoint byte-size mismatch: $checkpoint has $bytes, expected $expected_bytes for ${expected_hidden}x${expected_layers}" >&2
        return 1
    }
}

# Copy the optimizer/capacity from the architecture's source config, but copy
# the reward block from reward_cfg. This makes the 512 transfer test explicit.
build_overrides() {
    local source_cfg=$1 reward_cfg=$2 checkpoint=$3 run_id=$4
    overrides=(
        "base.load_model_path=$checkpoint"
        "base.run_id=$run_id"
        "train.total_timesteps=$steps"
        "base.checkpoint_interval=20"
        "base.eval_agents=16"
        "vec.num_frozen_banks=1"
        "vec.frozen_bank_pct=0.5"
        "selfplay.enabled=1"
        "selfplay.max_size=1"
        "selfplay.opponent_pool=None"
        "selfplay.opponent_league=None"
        "selfplay.opponent_pool_weights=None"
        "selfplay.opponent_pool_prob=0"
        "selfplay.snapshot_interval=0"
        "selfplay.pfsp_mode=variance"
        "selfplay.pfsp_alpha=1"
        "selfplay.pfsp_uniform_mix=0.1"
        "selfplay.magnet_path=None"
        "env.bot_opponent_fraction=0"
        "env.bot_pass_fraction=0"
        "env.bot_first=0"
        "env.reward_differential_scale=0"
    )
    local key value
    for key in hidden_size num_layers; do
        value=$(ini_value "$source_cfg" policy "$key")
        [[ -n $value ]] && overrides+=("policy.$key=$value")
    done
    for key in total_agents frozen_bank_hidden_size frozen_bank_num_layers; do
        value=$(ini_value "$source_cfg" vec "$key")
        [[ -n $value ]] && overrides+=("vec.$key=$value")
    done
    for key in learning_rate anneal_lr min_lr_ratio gamma gae_lambda replay_ratio \
        clip_coef vf_coef vf_clip_coef max_grad_norm ent_coef emag_kl_coef \
        emag_tau emag_cutoff anneal_ent_coef momentum minibatch_size horizon \
        prio_alpha prio_beta0 epoch_sampling; do
        value=$(ini_value "$source_cfg" train "$key")
        [[ -n $value ]] && overrides+=("train.$key=$value")
    done
    for key in reward_potential_scale reward_win reward_seed_value reward_product_value \
        reward_crop_value reward_animal_value reward_land_value reward_neglect_discount \
        reward_liquidation_days reward_productive_action reward_margin_scale \
        reward_differential_scale reward_inactivity_threshold reward_inactivity \
        reward_neglect_death; do
        value=$(ini_value "$reward_cfg" env "$key")
        [[ -n $value ]] && overrides+=("env.$key=$value")
    done
}

tasks=(
  "branch128_r128|logs/kaggriculture/sweep_1787001437805_0029.ini|logs/kaggriculture/sweep_1787001437805_0029.ini|checkpoints/kaggriculture/sweep_1787001437805_0029/0000000049283072.bin|128|2|1347072"
  "branch512_r128|logs/kaggriculture/sweep_1786992955045_0132.ini|logs/kaggriculture/sweep_1787001437805_0029.ini|checkpoints/kaggriculture/sweep_1786992955045_0132/0000000029360128.bin|512|3|13252608"
)

printf 'task\trun_id\tarchitecture\tstart_checkpoint\tstatus\n' > "${log%.log}.tsv"
printf 'Matched branch confirmation: %d tasks, %s steps each\n' "${#tasks[@]}" "$steps" | tee "$log"

index=0
for row in "${tasks[@]}"; do
    IFS='|' read -r name source_cfg reward_cfg checkpoint hidden layers bytes <<< "$row"
    run_id="kag_${name}_${stamp}"
    assert_pair "$source_cfg" "$checkpoint" "$hidden" "$layers" "$bytes"
    printf '%s\t%s\t%sx%s\t%s\tqueued\n' "$index" "$run_id" "$hidden" "$layers" "$checkpoint" >> "${log%.log}.tsv"
    build_overrides "$source_cfg" "$reward_cfg" "$checkpoint" "$run_id"
    printf '\n=== task=%s run=%s arch=%sx%s ===\n' "$index" "$run_id" "$hidden" "$layers" | tee -a "$log"
    if ((dry_run)); then
        printf 'DRY: %s\n' "${overrides[*]}" | tee -a "$log"
    else
        ./puffer train kaggriculture "${overrides[@]}" 2>&1 | tee -a "$log"
        final=$(find "checkpoints/kaggriculture/$run_id" -maxdepth 1 -type f -name '*.bin' ! -name '*.emag' -printf '%T@ %p\n' 2>/dev/null | sort -nr | sed -n '1s/^[^ ]* //p')
        if [[ -n ${final:-} ]]; then
            final_bytes=$(stat -c %s "$final")
            [[ "$final_bytes" == "$bytes" ]] || { echo "final architecture guard failed: $final ($final_bytes)" | tee -a "$log"; exit 1; }
            printf 'FINAL task=%s run=%s checkpoint=%s\n' "$index" "$run_id" "$final" | tee -a "$log"
        else
            printf 'NO_FINAL task=%s run=%s\n' "$index" "$run_id" | tee -a "$log"
        fi
    fi
    index=$((index + 1))
done
printf '\n=== BRANCH CONFIRMATION COMPLETE ===\n' | tee -a "$log"
