#!/usr/bin/env bash
set -euo pipefail

root="$(cd "$(dirname "$0")/../.." && pwd)"
cd "$root"

state_root=saved/kaggriculture_v2_population
crop_league=saved/kaggriculture_league_v2_crop
land_league=saved/kaggriculture_league_v2_land
full_league=saved/kaggriculture_league_v2_full
gate=ocean/kaggriculture/build/checkpoint_gate
champion=saved/kaggriculture_v2/champion_crop_real_opening.bin

usage() {
    cat >&2 <<'EOF'
Usage:
  population_v2.sh setup|status
  population_v2.sh train crop|drill|acreage|land|full [STEPS] [CHECKPOINT]
  population_v2.sh eval crop|drill|acreage|land|full [RUN_DIRECTORY]
  population_v2.sh psro crop|drill|acreage|land|full [RUN_DIRECTORY]
  population_v2.sh promote land [CROP_CHECKPOINT]
  population_v2.sh promote full LAND_CHECKPOINT

The wrapper fixes architecture, opponent league, and run identity.
PSRO defaults to the latest run launched by this wrapper for that mode.
EOF
    exit 2
}

mode_league() {
    case "$1" in
        crop) printf %s "$crop_league";; drill|acreage|land) printf %s "$land_league";;
        full) printf %s "$full_league";; *) usage;;
    esac
}
mode_default_checkpoint() {
    case "$1" in
        crop) printf %s "$champion";; drill|acreage|land) printf %s "$state_root/land_seed.bin";;
        full) printf %s "$state_root/full_seed.bin";; *) usage;;
    esac
}
mode_default_steps() { case "$1" in crop) printf 75000000;; drill) printf 20000000;; acreage) printf 30000000;; land) printf 100000000;; full) printf 150000000;; *) usage;; esac; }
mode_entropy() { case "$1" in crop) printf 0.002;; drill|acreage|land|full) printf 0.003;; *) usage;; esac; }
mode_bot_fraction() { case "$1" in crop) printf 0.50;; drill|acreage|land|full) printf 0.75;; *) usage;; esac; }
mode_land_value() { case "$1" in crop|drill) printf 1.0;; acreage|land|full) printf 0.25;; *) usage;; esac; }

league_pool() {
    local league=$1 path pool=
    while IFS= read -r path; do pool+="${pool:+,}$path"; done \
        < <(find "$league" -maxdepth 1 -type f -name '*.bin' -print | sort)
    [[ -n $pool ]] || { printf 'No checkpoints in league: %s\n' "$league" >&2; exit 1; }
    printf %s "$pool"
}

build_gate() {
    mkdir -p ocean/kaggriculture/build
    cc -O3 -std=c17 -Wall -Wextra -Werror \
        ocean/kaggriculture/checkpoint_gate.c -o "$gate"
}

seed_crop_league() {
    mkdir -p "$crop_league"
    declare -a specs=(
        "champion:$champion"
        "real_opening_50m:saved/kaggriculture_v2/real_opening_50m.bin"
        "hyper_robust_31:checkpoints/kaggriculture/sweep_1785783457849_0031/0000000024969216.bin"
        "hyper_alternate_35:checkpoints/kaggriculture/sweep_1785783985294_0035/0000000024969216.bin"
        "reward_robust_33:checkpoints/kaggriculture/sweep_1785808482870_0033/0000000024969216.bin"
        "reward_economy_13:checkpoints/kaggriculture/sweep_1785805320517_0013/0000000024969216.bin"
    )
    local spec name source destination
    for spec in "${specs[@]}"; do
        name=${spec%%:*}; source=${spec#*:}; destination="$crop_league/$name.bin"
        [[ -f $source ]] || { printf 'Missing v2 seed: %s\n' "$source" >&2; exit 1; }
        [[ -f $destination ]] || cp "$source" "$destination"
    done
    if [[ ! -f $crop_league/manifest.tsv ]]; then
        {
            printf 'policy\trole\tbase_weight\tsource\n'
            printf 'champion\tmeta\t0.700000000000\t%s/champion.bin\n' "$crop_league"
            for name in real_opening_50m hyper_robust_31 hyper_alternate_35 reward_robust_33 reward_economy_13; do
                printf '%s\tdiversity\t0.060000000000\t%s/%s.bin\n' "$name" "$crop_league" "$name"
            done
        } > "$crop_league/manifest.tsv"
    fi
}

gate_league() {
    local source_league=$1 destination_league=$2 gate_mode=$3 force=${4:-0}
    mkdir -p "$destination_league"
    local source name destination count=0
    for source in "$source_league"/*.bin; do
        [[ -f $source ]] || continue
        name=${source##*/}; destination="$destination_league/$name"
        if [[ $force == 1 || ! -f $destination ]]; then
            "$gate" "$source" "$destination" "$gate_mode" 32 2
        fi
        count=$((count + 1))
    done
    ((count)) || { printf 'No policies in %s\n' "$source_league" >&2; exit 1; }
    if [[ -f $source_league/manifest.tsv && ! -f $destination_league/manifest.tsv ]]; then
        sed "s#${source_league}/#${destination_league}/#g" \
            "$source_league/manifest.tsv" > "$destination_league/manifest.tsv"
    fi
}

setup_all() {
    mkdir -p "$state_root"
    build_gate
    seed_crop_league
    [[ -f $state_root/land_seed.bin ]] || \
        "$gate" "$champion" "$state_root/land_seed.bin" land-safe 32 2
    gate_league "$crop_league" "$land_league" land-safe
    printf 'V2 population ready: crop=%s land=%s\n' "$crop_league" "$land_league"
}

promote() {
    local mode=$1 source=${2:-}
    setup_all
    if [[ $mode == land ]]; then
        source=${source:-$champion}
        [[ -f $source ]] || { printf 'Checkpoint not found: %s\n' "$source" >&2; exit 1; }
        "$gate" "$source" "$state_root/land_seed.bin" land-safe 32 2
        gate_league "$crop_league" "$land_league" land-safe 1
        printf 'Land seed: %s\n' "$state_root/land_seed.bin"
    elif [[ $mode == full ]]; then
        [[ -n $source && -f $source ]] || usage
        "$gate" "$source" "$state_root/full_seed.bin" full-safe 32 2
        gate_league "$land_league" "$full_league" full-safe 1
        printf 'Full-game seed: %s\n' "$state_root/full_seed.bin"
    else
        usage
    fi
}

train_mode() {
    local mode=$1 steps=${2:-} checkpoint=${3:-}
    setup_all
    local league pool entropy bot_fraction land_value run_id
    league=$(mode_league "$mode")
    steps=${steps:-$(mode_default_steps "$mode")}
    checkpoint=${checkpoint:-$(mode_default_checkpoint "$mode")}
    entropy=$(mode_entropy "$mode"); bot_fraction=$(mode_bot_fraction "$mode")
    land_value=$(mode_land_value "$mode")
    [[ $steps =~ ^[0-9]+$ && $steps -gt 0 ]] || usage
    [[ -f $checkpoint ]] || { printf 'Missing %s seed: %s\n' "$mode" "$checkpoint" >&2; exit 1; }
    [[ -d $league ]] || { printf 'Missing league: %s\n' "$league" >&2; exit 1; }
    pool=$(league_pool "$league")
    run_id="v2_${mode}_$(date +%s%3N)"
    printf '%s\n' "checkpoints/kaggriculture/$run_id" > "$state_root/latest_${mode}_run.txt"
    printf 'Starting %s run %s from %s against %s\n' "$mode" "$run_id" "$checkpoint" "$league"
    exec ./puffer train kaggriculture \
        "base.run_id=$run_id" "base.load_model_path=$checkpoint" \
        policy.hidden_size=32 policy.num_layers=2 \
        vec.frozen_bank_hidden_size=32 vec.frozen_bank_num_layers=2 \
        selfplay.enabled=1 "selfplay.opponent_pool=$pool" \
        selfplay.opponent_pool_prob=0.75 selfplay.snapshot_interval=5000000 \
        selfplay.eval_pool_size=8 selfplay.eval_games=100 \
        env.episode_steps=720 \
        "env.bot_opponent_fraction=$bot_fraction" \
        "env.reward_land_value=$land_value" \
        train.learning_rate=0.0002 "train.ent_coef=$entropy" \
        "train.total_timesteps=$steps"
}

resolve_run() {
    local mode=$1 run=${2:-} marker="$state_root/latest_${mode}_run.txt"
    if [[ -z $run && -f $marker ]]; then read -r run < "$marker"; fi
    [[ -n $run && -d $run ]] || { printf 'No recorded %s run; pass its directory.\n' "$mode" >&2; exit 1; }
    printf %s "$run"
}

eval_mode() {
    local mode=$1 run league output
    run=$(resolve_run "$mode" "${2:-}"); league=$(mode_league "$mode")
    output="logs/kaggriculture/${run##*/}_${mode}_confirm"
    exec ./ocean/kaggriculture/eval_population.sh --games 100 --jobs 4 \
        --fixed rules \
        --range 0:100:12 --output "$output" "$league" "$run"
}

psro_mode() {
    local mode=$1 run league
    run=$(resolve_run "$mode" "${2:-}"); league=$(mode_league "$mode")
    exec ./ocean/kaggriculture/psro.sh iterate --run "$run" \
        --league "$league" --archive "saved/kaggriculture_league_v2_${mode}_archive" \
        --config config/kaggriculture.ini
}

status() {
    printf 'mode\tleague\tpolicies\tlatest_run\n'
    local mode league count latest marker
    for mode in crop drill acreage land full; do
        league=$(mode_league "$mode"); count=0
        [[ -d $league ]] && count=$(find "$league" -maxdepth 1 -type f -name '*.bin' | wc -l)
        marker="$state_root/latest_${mode}_run.txt"; latest=-
        [[ -f $marker ]] && read -r latest < "$marker"
        printf '%s\t%s\t%s\t%s\n' "$mode" "$league" "$count" "$latest"
    done
}

command=${1:-status}
case "$command" in
    setup) setup_all;; status) status;;
    train) (($# >= 2 && $# <= 4)) || usage; train_mode "$2" "${3:-}" "${4:-}";;
    eval) (($# >= 2 && $# <= 3)) || usage; eval_mode "$2" "${3:-}";;
    psro) (($# >= 2 && $# <= 3)) || usage; psro_mode "$2" "${3:-}";;
    promote) (($# >= 2 && $# <= 3)) || usage; promote "$2" "${3:-}";;
    *) usage;;
esac
