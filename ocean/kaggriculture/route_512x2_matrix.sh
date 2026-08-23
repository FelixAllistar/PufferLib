#!/usr/bin/env bash
# Controlled 512x2 route-discovery matrix. The live kaggriculture.ini is only
# used for unswept simulator constants; architecture, opponents, optimizer,
# and every reward coefficient are pinned here so unrelated config edits cannot
# leak into the experiment.
set -euo pipefail

root="$(cd "$(dirname "$0")/../.." && pwd)"
cd "$root"

steps=${KAG_ROUTE_STEPS:-100000000}
stamp=${KAG_ROUTE_STAMP:-$(date +%Y%m%d%H%M%S)}
league=saved/kaggriculture_league_512x2_elite_v1/league.ini
manifest="logs/kaggriculture/route_512x2_${stamp}.tsv"
queue_log="logs/kaggriculture/route_512x2_${stamp}.log"
expected_bytes=11081728

[[ -f $league ]] || { printf 'Missing league: %s\n' "$league" >&2; exit 1; }
mkdir -p logs/kaggriculture
printf 'variant\trun_id\tsource\tlr\tcrop_scale\tanimal_scale\tland_scale\tterminal_cash_scale\tsteps\tcheckpoint\n' > "$manifest"

common=(
    base.checkpoint_interval=24
    base.eval_deterministic=0
    base.load_enemy_model_path=None
    vec.total_agents=8192
    vec.num_frozen_banks=4
    vec.frozen_bank_pct=0.75
    vec.frozen_bank_hidden_size=512
    vec.frozen_bank_num_layers=2
    policy.hidden_size=512
    policy.num_layers=2
    selfplay.enabled=1
    selfplay.max_size=16
    selfplay.snapshot_interval=50000000
    selfplay.opponent_pool=None
    "selfplay.opponent_league=$league"
    selfplay.opponent_pool_weights=None
    selfplay.opponent_pool_prob=1
    selfplay.eval_pool_size=8
    selfplay.eval_metric=money
    selfplay.magnet_path=None
    env.reward_potential_scale=0
    env.reward_potential_gamma=0.99970
    env.reward_cash_scale=0
    env.reward_money_scale=0
    env.reward_progress_scale=1
    env.reward_progress_win_scale=1
    env.reward_progress_liquidation_days=6
    env.reward_progress_seed_scale=1
    env.reward_progress_product_scale=1
    env.reward_progress_maintenance_scale=0
    env.reward_progress_health_ratio=0.9
    env.reward_progress_crop_wheat_units=3.174477
    env.reward_progress_crop_carrot_units=2.375688
    env.reward_progress_crop_tomato_units=6.217698
    env.reward_progress_crop_strawberry_units=6.836367
    env.reward_progress_crop_melon_units=5.972984
    env.reward_progress_seed_wheat_realization=1
    env.reward_progress_seed_carrot_realization=1
    env.reward_progress_seed_tomato_realization=1
    env.reward_progress_seed_strawberry_realization=1
    env.reward_progress_seed_melon_realization=1
    env.reward_progress_animal_goose_units_per_event=1.894787
    env.reward_progress_animal_cow_units_per_event=3.219676
    env.reward_progress_animal_sheep_units_per_event=4.133789
    env.reward_progress_animal_goose_realization=1
    env.reward_progress_animal_cow_realization=1
    env.reward_progress_animal_sheep_realization=1
    env.reward_progress_product_wheat_realization=0.653793
    env.reward_progress_product_carrot_realization=0.888549
    env.reward_progress_product_tomato_realization=0.905649
    env.reward_progress_product_strawberry_realization=0.718436
    env.reward_progress_product_melon_realization=0.209642
    env.reward_progress_product_egg_realization=0.836119
    env.reward_progress_product_milk_realization=0.773441
    env.reward_progress_product_wool_realization=0.704319
    env.reward_progress_product_fertilizer_realization=0.98
    env.bot_opponent_fraction=0.333333
    env.bot_pass_fraction=0
    env.bot_first=0
    env.bot_top_fraction=0
    env.bot_rules_fraction=0.25
    env.bot_script_fraction=0.3
    env.bot_adaptive_fraction=0.7
    train.total_timesteps="$steps"
    train.anneal_lr=1
    train.min_lr_ratio=0.1
    train.gamma=0.99970
    train.gae_lambda=1
    train.replay_ratio=1
    train.reward_clip=0
    train.ent_coef=0.000334
    train.emag_kl_coef=0
    train.emag_tau=0
    train.emag_cutoff=1
    train.anneal_ent_coef=0
    train.minibatch_size=4096
    train.horizon=128
    train.epoch_sampling=1
)

run_variant() {
    local variant=$1 source=$2 lr=$3 crop=$4 animal=$5 land=$6 terminal_cash=$7
    local run_id="route512_${variant}_${stamp}"
    local run_dir="checkpoints/kaggriculture/$run_id"
    local final_checkpoint actual_bytes

    if [[ $source != None ]]; then
        [[ -f $source ]] || { printf 'Missing source: %s\n' "$source" >&2; exit 1; }
        actual_bytes=$(stat -c %s "$source")
        [[ $actual_bytes == "$expected_bytes" ]] || {
            printf 'Source architecture mismatch: %s (%s bytes)\n' "$source" "$actual_bytes" >&2
            exit 1
        }
    fi

    printf '\nRUN variant=%s source=%s lr=%s crop=%s animal=%s land=%s terminal_cash=%s steps=%s\n' \
        "$variant" "$source" "$lr" "$crop" "$animal" "$land" "$terminal_cash" "$steps" | tee -a "$queue_log"
    ./puffer train kaggriculture "${common[@]}" \
        "base.run_id=$run_id" \
        "base.load_model_path=$source" \
        "train.learning_rate=$lr" \
        "env.reward_progress_crop_scale=$crop" \
        "env.reward_progress_animal_scale=$animal" \
        "env.reward_progress_land_scale=$land" \
        "env.reward_progress_terminal_money_scale=$terminal_cash" \
        2>&1 | tee -a "$queue_log"

    final_checkpoint=$(find "$run_dir" -maxdepth 1 -type f -regextype posix-extended \
        -regex '.*/[0-9]{16}\.bin' -print | sort | tail -n 1)
    [[ -n $final_checkpoint ]] || { printf 'No checkpoint for %s\n' "$run_id" >&2; exit 1; }
    actual_bytes=$(stat -c %s "$final_checkpoint")
    [[ $actual_bytes == "$expected_bytes" ]] || { printf 'Final architecture mismatch: %s\n' "$final_checkpoint" >&2; exit 1; }
    printf '%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\n' \
        "$variant" "$run_id" "$source" "$lr" "$crop" "$animal" "$land" "$terminal_cash" "$steps" "$final_checkpoint" >> "$manifest"
}

# One continuation preserves the known animal route. The other starts and
# coefficients deliberately probe crop, land, and balanced routes.
run_variant animal_anchor saved/kaggriculture_league_512x2_elite_v1/future_cash_49m.bin 0.000188 1.00 1.00 1.00 0.25
run_variant crop_dusta saved/kaggriculture_league_512x2_elite_v1/clone_crop_dusta.bin 0.000400 1.50 0.50 1.00 0.35
run_variant peikopon saved/kaggriculture_league_512x2_elite_v1/clone_peikopon.bin 0.000400 1.50 0.25 1.25 0.35
run_variant ryo_balanced saved/kaggriculture_league_512x2_elite_v1/clone_ryo.bin 0.000300 1.25 0.75 1.00 0.50
run_variant animal_to_crop saved/kaggriculture_league_512x2_elite_v1/future_cash_49m.bin 0.000300 1.50 0.50 1.00 0.35
run_variant cold_crop None 0.000400 1.50 0.50 1.00 0.35

printf '\nROUTE MATRIX COMPLETE manifest=%s\n' "$manifest" | tee -a "$queue_log"
