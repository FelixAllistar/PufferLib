#!/usr/bin/env bash
# Continue corrected top-bot BC clones under matched 128x3/512x3 settings.
set -euo pipefail
root="$(cd "$(dirname "$0")/../.." && pwd)"
cd "$root"

steps=${KAG_CLONE_CONT_STEPS:-1000000000}
stamp=${KAG_CLONE_CONT_STAMP:-$(date +%Y%m%d%H%M%S)}
start=${KAG_CLONE_CONT_START_INDEX:-0}
log="logs/kaggriculture/bc_clone_continuation_${stamp}.log"
mkdir -p logs/kaggriculture

clone_dir=saved/kaggriculture_bc_512x3_top
declare -a tasks=(
  "128x3|$clone_dir/top_clone_h128_l3.bin|selfplay"
  "128x3|$clone_dir/top_clone_h128_l3.bin|mixedbots"
  "128x3|$clone_dir/top_clone_h128_l3.bin|passonly"
  "512x3|$clone_dir/top_clone_h512_l3.bin|selfplay"
  "512x3|$clone_dir/top_clone_h512_l3.bin|mixedbots"
  "512x3|$clone_dir/top_clone_h512_l3.bin|passonly"
)

printf 'task\tarch\tvariant\trun_id\tstatus\n' > "${log%.log}.tsv"
printf 'BC clone continuation matrix: %d tasks, %s steps/task\n' "${#tasks[@]}" "$steps" | tee "$log"

index=0
for task in "${tasks[@]}"; do
    IFS='|' read -r arch clone variant <<< "$task"
    if ((index < start)); then index=$((index + 1)); continue; fi
    [[ -f "$clone" ]] || { echo "missing clone: $clone" >&2; exit 1; }
    if [[ $arch == 128x3 ]]; then hidden=128; layers=3; else hidden=512; layers=3; fi
    run="kag_clone_${arch}_${variant}_${stamp}"
    printf '%s\t%s\t%s\t%s\tqueued\n' "$index" "$arch" "$variant" "$run" >> "${log%.log}.tsv"
    case "$variant" in
      selfplay)
        bot_frac=0; pass_frac=0; first=0; top=0; rules=0; script=0; adaptive=0 ;;
      mixedbots)
        bot_frac=0.5; pass_frac=0; first=0; top=0.2; rules=0.2; script=0.4; adaptive=0.6 ;;
      passonly)
        bot_frac=1; pass_frac=1; first=0; top=0; rules=0; script=0; adaptive=0 ;;
      *) echo "unknown variant $variant" >&2; exit 2 ;;
    esac
    printf '\n=== START task=%s arch=%s variant=%s run=%s ===\n' "$index" "$arch" "$variant" "$run" | tee -a "$log"
    ./puffer train kaggriculture \
      "base.load_model_path=$clone" "base.run_id=$run" \
      train.total_timesteps="$steps" base.checkpoint_interval=20 base.eval_agents=16 \
      policy.hidden_size="$hidden" policy.num_layers="$layers" \
      vec.total_agents=4096 vec.num_frozen_banks=1 vec.frozen_bank_pct=0.5 \
      vec.frozen_bank_hidden_size="$hidden" vec.frozen_bank_num_layers="$layers" \
      selfplay.enabled=1 selfplay.max_size=1 selfplay.opponent_pool=None \
      selfplay.opponent_league=None selfplay.opponent_pool_weights=None \
      selfplay.opponent_pool_prob=0 selfplay.snapshot_interval=0 \
      selfplay.magnet_path="$clone" train.emag_kl_coef=0.01 train.emag_tau=0 train.emag_cutoff=0.134 \
      env.reward_potential_scale=0.000753463246 env.reward_win=0.163425595 \
      env.reward_margin_scale=0.890816748 env.reward_seed_value=0.264302313 \
      env.reward_product_value=0.782155633 env.reward_crop_value=0 \
      env.reward_animal_value=0 env.reward_land_value=0 \
      env.reward_neglect_discount=0.595858634 env.reward_liquidation_days=4.85993576 \
      env.reward_productive_action=0 env.reward_differential_scale=0 \
      env.reward_inactivity_threshold=500 env.reward_inactivity=0 env.reward_neglect_death=0 \
      env.bot_opponent_fraction="$bot_frac" env.bot_pass_fraction="$pass_frac" \
      env.bot_first="$first" env.bot_top_fraction="$top" env.bot_rules_fraction="$rules" \
      env.bot_script_fraction="$script" env.bot_adaptive_fraction="$adaptive" 2>&1 | tee -a "$log"
    final=$(find "checkpoints/kaggriculture/$run" -maxdepth 1 -type f -name '*.bin' ! -name '*.emag' -printf '%T@ %p\n' 2>/dev/null | sort -nr | sed -n '1s/^[^ ]* //p')
    [[ -n "$final" ]] || { echo "NO_FINAL task=$index run=$run" | tee -a "$log"; exit 1; }
    printf 'FINAL task=%s arch=%s variant=%s run=%s checkpoint=%s\n' "$index" "$arch" "$variant" "$run" "$final" | tee -a "$log"
    index=$((index + 1))
done
printf '\n=== BC CLONE CONTINUATION MATRIX COMPLETE ===\n' | tee -a "$log"
