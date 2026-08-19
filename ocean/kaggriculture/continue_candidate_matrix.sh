#!/usr/bin/env bash
# Continue the nine completed 200M candidate runs for an additional long
# horizon.  Each row keeps the original reward/training config and only changes
# the starting checkpoint, run id, total continuation steps, and opponent mix.
set -euo pipefail

root="$(cd "$(dirname "$0")/../.." && pwd)"
cd "$root"

steps=${KAG_CONT_STEPS:-1000000000}
stamp=${KAG_CONT_STAMP:-$(date +%Y%m%d%H%M%S)}
start_index=${KAG_CONT_START_INDEX:-0}
dry_run=${KAG_CONT_DRY_RUN:-0}
queue_log="logs/kaggriculture/candidate_continue_${stamp}.log"
task_file="logs/kaggriculture/candidate_continue_${stamp}.tsv"
mkdir -p logs/kaggriculture

# name|source sweep config|200M checkpoint
# Keep run34 selfplay first: it was the strongest member of the 9-policy
# pairwise matrix and showed the best maintenance behavior.
candidates=(
  "run34_selfplay|logs/kaggriculture/sweep_1787001616597_0034.ini|checkpoints/kaggriculture/kag_matrix_run34_selfplay_20260817224531/0000000199229440.bin|selfplay"
  "run34_mixedbots|logs/kaggriculture/sweep_1787001616597_0034.ini|checkpoints/kaggriculture/kag_matrix_run34_mixedbots_20260817224531/0000000199229440.bin|mixedbots"
  "run34_passonly|logs/kaggriculture/sweep_1787001616597_0034.ini|checkpoints/kaggriculture/kag_matrix_run34_passonly_20260817224531/0000000199229440.bin|passonly"
  "run40_selfplay|logs/kaggriculture/sweep_1787001831568_0040.ini|checkpoints/kaggriculture/kag_matrix_run40_selfplay_20260817224531/0000000199229440.bin|selfplay"
  "run40_mixedbots|logs/kaggriculture/sweep_1787001831568_0040.ini|checkpoints/kaggriculture/kag_matrix_run40_mixedbots_20260817224531/0000000199229440.bin|mixedbots"
  "run40_passonly|logs/kaggriculture/sweep_1787001831568_0040.ini|checkpoints/kaggriculture/kag_matrix_run40_passonly_20260817224531/0000000199229440.bin|passonly"
  "run44_selfplay|logs/kaggriculture/sweep_1787001975000_0044.ini|checkpoints/kaggriculture/kag_matrix_run44_selfplay_20260817224531/0000000199229440.bin|selfplay"
  "run44_mixedbots|logs/kaggriculture/sweep_1787001975000_0044.ini|checkpoints/kaggriculture/kag_matrix_run44_mixedbots_20260817224531/0000000199229440.bin|mixedbots"
  "run44_passonly|logs/kaggriculture/sweep_1787001975000_0044.ini|checkpoints/kaggriculture/kag_matrix_run44_passonly_20260817224531/0000000199229440.bin|passonly"
)

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

append_common() {
    local cfg=$1 checkpoint=$2 run_id=$3
    local key value
    overrides=(
        "base.load_model_path=$checkpoint"
        "base.run_id=$run_id"
        "train.total_timesteps=$steps"
        "base.checkpoint_interval=20"
        "base.eval_agents=16"
        "vec.num_frozen_banks=1"
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
        "env.reward_differential_scale=0"
    )
    for key in hidden_size num_layers; do
        value=$(ini_value "$cfg" policy "$key")
        [[ -n $value ]] && overrides+=("policy.$key=$value")
    done
    for key in total_agents frozen_bank_pct frozen_bank_hidden_size frozen_bank_num_layers; do
        value=$(ini_value "$cfg" vec "$key")
        [[ -n $value ]] && overrides+=("vec.$key=$value")
    done
    for key in learning_rate anneal_lr min_lr_ratio gamma gae_lambda replay_ratio \
        clip_coef vf_coef vf_clip_coef max_grad_norm ent_coef emag_kl_coef \
        emag_tau emag_cutoff anneal_ent_coef momentum minibatch_size horizon \
        prio_alpha prio_beta0 epoch_sampling; do
        value=$(ini_value "$cfg" train "$key")
        [[ -n $value ]] && overrides+=("train.$key=$value")
    done
    for key in reward_potential_scale reward_win reward_seed_value reward_product_value \
        reward_crop_value reward_animal_value reward_land_value reward_neglect_discount \
        reward_liquidation_days reward_productive_action reward_margin_scale \
        reward_differential_scale reward_inactivity_threshold reward_inactivity \
        reward_neglect_death; do
        value=$(ini_value "$cfg" env "$key")
        [[ -n $value ]] && overrides+=("env.$key=$value")
    done
    value=$(ini_value "$cfg" selfplay magnet_path)
    [[ -n $value ]] && overrides+=("selfplay.magnet_path=$value")
}

append_variant() {
    local cfg=$1 variant=$2
    case "$variant" in
        selfplay)
            overrides+=(
                "vec.frozen_bank_pct=0.5"
                "env.bot_opponent_fraction=0"
                "env.bot_pass_fraction=0"
                "env.bot_first=0"
            )
            ;;
        mixedbots)
            overrides+=(
                "vec.frozen_bank_pct=0.5"
                "env.bot_opponent_fraction=0.5"
                "env.bot_pass_fraction=0"
                "env.bot_first=0"
                "env.bot_top_fraction=$(ini_value "$cfg" env bot_top_fraction)"
                "env.bot_rules_fraction=$(ini_value "$cfg" env bot_rules_fraction)"
                "env.bot_script_fraction=$(ini_value "$cfg" env bot_script_fraction)"
                "env.bot_adaptive_fraction=$(ini_value "$cfg" env bot_adaptive_fraction)"
            )
            ;;
        passonly)
            overrides+=(
                "vec.frozen_bank_pct=1"
                "env.bot_opponent_fraction=1"
                "env.bot_pass_fraction=1"
                "env.bot_first=0"
                "env.bot_top_fraction=0"
                "env.bot_rules_fraction=0"
                "env.bot_script_fraction=0"
                "env.bot_adaptive_fraction=0"
            )
            ;;
        *) printf 'Unknown variant: %s\n' "$variant" >&2; return 2 ;;
    esac
}

printf 'task\tname\tvariant\tstatus\trun_id\tstart_checkpoint\n' > "$task_file"
for row in "${candidates[@]}"; do
    IFS='|' read -r name cfg checkpoint variant <<< "$row"
    run_id="kag_long_${name}_${stamp}"
    printf '%s\t%s\t%s\tqueued\t%s\t%s\n' \
        "$name" "$name" "$variant" "$run_id" "$checkpoint" >> "$task_file"
done

printf 'Kaggriculture continuation matrix: %d tasks, %s additional steps/task\n' \
    "${#candidates[@]}" "$steps" | tee "$queue_log"
printf 'Task list: %s\nQueue log: %s\n' "$task_file" "$queue_log" | tee -a "$queue_log"

task_index=0
for row in "${candidates[@]}"; do
    IFS='|' read -r name cfg checkpoint variant <<< "$row"
    if ((task_index < start_index)); then
        task_index=$((task_index + 1))
        continue
    fi
    [[ -f $cfg ]] || { printf 'Missing config: %s\n' "$cfg" | tee -a "$queue_log"; exit 1; }
    [[ -f $checkpoint ]] || { printf 'Missing checkpoint: %s\n' "$checkpoint" | tee -a "$queue_log"; exit 1; }
    run_id="kag_long_${name}_${stamp}"
    printf '\n=== START task=%s name=%s variant=%s run=%s ===\n' \
        "$task_index" "$name" "$variant" "$run_id" | tee -a "$queue_log"
    overrides=()
    append_common "$cfg" "$checkpoint" "$run_id"
    append_variant "$cfg" "$variant"
    if ((dry_run)); then
        printf 'DRY task=%s run=%s\n  %s\n' \
            "$task_index" "$run_id" "${overrides[*]}" | tee -a "$queue_log"
    else
        ./puffer train kaggriculture "${overrides[@]}" 2>&1 | tee -a "$queue_log"
        final=$(find "checkpoints/kaggriculture/$run_id" -maxdepth 1 -type f \
            -name '*.bin' ! -name '*.emag' -printf '%T@ %p\n' 2>/dev/null \
            | sort -nr | sed -n '1s/^[^ ]* //p')
        if [[ -n ${final:-} ]]; then
            printf 'FINAL task=%s run=%s checkpoint=%s\n' \
                "$task_index" "$run_id" "$final" | tee -a "$queue_log"
            tmp_profile=$(mktemp -d)
            cp "$final" "$tmp_profile/${run_id}.bin"
            ./ocean/kaggriculture/profile_population_gpu.sh \
                --games 20 --jobs 2 --gpu-agents 64 \
                --output "logs/kaggriculture/${run_id}_profile" "$tmp_profile" \
                2>&1 | tee -a "$queue_log" || true
            rm -rf "$tmp_profile"
        else
            printf 'NO_FINAL task=%s run=%s\n' "$task_index" "$run_id" | tee -a "$queue_log"
        fi
    fi
    task_index=$((task_index + 1))
done

printf '\n=== CONTINUATION MATRIX COMPLETE ===\n' | tee -a "$queue_log"
