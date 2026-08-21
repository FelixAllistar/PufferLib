#!/usr/bin/env bash
# Architecture-guarded long continuation matrix for the model-size branches.
# The default matrix continues the matched-reward 512x3 confirmation run from
# three checkpoints and repeats the accidental 128 experiment's three opponent
# regimes: pure self-play, mixed bots, and pass-only bots.
set -euo pipefail

root="$(cd "$(dirname "$0")/../.." && pwd)"
cd "$root"

steps=${KAG_BRANCH_CONT_STEPS:-1000000000}
stamp=${KAG_BRANCH_CONT_STAMP:-$(date +%Y%m%d%H%M%S)}
start_index=${KAG_BRANCH_CONT_START_INDEX:-0}
dry_run=${KAG_BRANCH_CONT_DRY_RUN:-0}
log="logs/kaggriculture/branch_continue_${stamp}.log"
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

assert_checkpoint() {
    local cfg=$1 checkpoint=$2
    local h n bytes
    [[ -f $cfg && -f $checkpoint ]] || { echo "missing source: $cfg or $checkpoint" >&2; exit 1; }
    h=$(ini_value "$cfg" policy hidden_size)
    n=$(ini_value "$cfg" policy num_layers)
    bytes=$(stat -c %s "$checkpoint")
    [[ "$h" == 512 && "$n" == 3 && "$bytes" == 13252608 ]] || {
        echo "REFUSING non-512x3 source: cfg=${h}x${n} checkpoint_bytes=$bytes path=$checkpoint" >&2
        exit 1
    }
}

build_overrides() {
    local source_cfg=$1 checkpoint=$2 run_id=$3 variant=$4
    overrides=(
        "base.load_model_path=$checkpoint"
        "base.run_id=$run_id"
        "train.total_timesteps=$steps"
        "base.checkpoint_interval=20"
        "base.eval_agents=16"
        "policy.hidden_size=512"
        "policy.num_layers=3"
        "vec.total_agents=4096"
        "vec.num_frozen_banks=1"
        "vec.frozen_bank_pct=0.5"
        "vec.frozen_bank_hidden_size=512"
        "vec.frozen_bank_num_layers=3"
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
        "env.reward_potential_scale=0.000535061059"
        "env.reward_win=0.138636678"
        "env.reward_margin_scale=0.567313671"
        "env.reward_seed_value=0.323051393"
        "env.reward_product_value=0.299563348"
        "env.reward_crop_value=0.193032458"
        "env.reward_animal_value=0.617953598"
        "env.reward_land_value=0"
        "env.reward_neglect_discount=0.0230635703"
        "env.reward_liquidation_days=7.06268167"
        "env.reward_productive_action=0"
        "env.reward_differential_scale=0"
        "env.reward_inactivity_threshold=500"
        "env.reward_inactivity=0"
        "env.reward_neglect_death=0"
    )
    local key value
    for key in learning_rate anneal_lr min_lr_ratio gamma gae_lambda replay_ratio \
        clip_coef vf_coef vf_clip_coef max_grad_norm ent_coef emag_kl_coef \
        emag_tau emag_cutoff anneal_ent_coef momentum minibatch_size horizon \
        prio_alpha prio_beta0 epoch_sampling; do
        value=$(ini_value "$source_cfg" train "$key")
        [[ -n $value ]] && overrides+=("train.$key=$value")
    done
    case "$variant" in
        selfplay)
            overrides+=("vec.frozen_bank_pct=0.5" "env.bot_opponent_fraction=0" "env.bot_pass_fraction=0" "env.bot_first=0")
            ;;
        mixedbots)
            overrides+=("vec.frozen_bank_pct=0.5" "env.bot_opponent_fraction=0.5" "env.bot_pass_fraction=0" "env.bot_first=0" "env.bot_top_fraction=0.2" "env.bot_rules_fraction=0.2" "env.bot_script_fraction=0.4" "env.bot_adaptive_fraction=0.6")
            ;;
        passonly)
            overrides+=("vec.frozen_bank_pct=1" "env.bot_opponent_fraction=1" "env.bot_pass_fraction=1" "env.bot_first=0" "env.bot_top_fraction=0" "env.bot_rules_fraction=0" "env.bot_script_fraction=0" "env.bot_adaptive_fraction=0")
            ;;
        *) echo "unknown variant: $variant" >&2; exit 2 ;;
    esac
}

source_cfg=logs/kaggriculture/sweep_1786992955045_0132.ini
checkpoint_dir=checkpoints/kaggriculture/kag_branch512_r128_20260818033022
candidates=(
  "r20|$checkpoint_dir/0000000020971520.bin|selfplay"
  "r20|$checkpoint_dir/0000000020971520.bin|mixedbots"
  "r20|$checkpoint_dir/0000000020971520.bin|passonly"
  "r42|$checkpoint_dir/0000000041943040.bin|selfplay"
  "r42|$checkpoint_dir/0000000041943040.bin|mixedbots"
  "r42|$checkpoint_dir/0000000041943040.bin|passonly"
  "r49|$checkpoint_dir/0000000049283072.bin|selfplay"
  "r49|$checkpoint_dir/0000000049283072.bin|mixedbots"
  "r49|$checkpoint_dir/0000000049283072.bin|passonly"
)

printf 'task\tseed\tvariant\trun_id\tstart_checkpoint\tstatus\n' > "${log%.log}.tsv"
printf '512x3 continuation matrix: %d tasks, %s additional steps/task\n' "${#candidates[@]}" "$steps" | tee "$log"

index=0
for row in "${candidates[@]}"; do
    IFS='|' read -r seed checkpoint variant <<< "$row"
    if ((index < start_index)); then index=$((index + 1)); continue; fi
    assert_checkpoint "$source_cfg" "$checkpoint"
    run_id="kag_branch512_${seed}_${variant}_${stamp}"
    printf '%s\t%s\t%s\t%s\t%s\tqueued\n' "$index" "$seed" "$variant" "$run_id" "$checkpoint" >> "${log%.log}.tsv"
    build_overrides "$source_cfg" "$checkpoint" "$run_id" "$variant"
    printf '\n=== START task=%s seed=%s variant=%s run=%s ===\n' "$index" "$seed" "$variant" "$run_id" | tee -a "$log"
    if ((dry_run)); then
        printf 'DRY: %s\n' "${overrides[*]}" | tee -a "$log"
    else
        ./puffer train kaggriculture "${overrides[@]}" 2>&1 | tee -a "$log"
        final=$(find "checkpoints/kaggriculture/$run_id" -maxdepth 1 -type f -name '*.bin' ! -name '*.emag' -printf '%T@ %p\n' 2>/dev/null | sort -nr | sed -n '1s/^[^ ]* //p')
        [[ -n ${final:-} ]] || { echo "NO_FINAL task=$index run=$run_id" | tee -a "$log"; exit 1; }
        bytes=$(stat -c %s "$final")
        [[ "$bytes" == 13252608 ]] || { echo "FINAL ARCHITECTURE FAILURE $final bytes=$bytes" | tee -a "$log"; exit 1; }
        printf 'FINAL task=%s run=%s checkpoint=%s\n' "$index" "$run_id" "$final" | tee -a "$log"
    fi
    index=$((index + 1))
done
printf '\n=== 512x3 CONTINUATION MATRIX COMPLETE ===\n' | tee -a "$log"
