#!/usr/bin/env bash
set -euo pipefail

# Restartable exact-player BC factory. It can refresh recent official daily
# datasets, fit the positive economic coefficients, then train independent
# 128x2/256x2/512x2 clones for every sufficiently represented named player.

repo_root=$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)
data_root=${KAG_ELITE_DATA_ROOT:-/workspace/elite_replays}
data=${KAG_CLONE_DATA:-$data_root/kaggriculture_elite_1.32.7.bc}
manifest=${KAG_CLONE_MANIFEST:-$data.players.tsv}
factory_root=${KAG_CLONE_ROOT:-$data_root/clone_factory}
model_root=${KAG_CLONE_MODEL_ROOT:-saved/kaggriculture_elite_clones}
python_bin=${KAG_ELITE_PYTHON:-/usr/bin/python3}
kaggle_bin=${KAGGLE_BIN:-/root/.local/bin/kaggle}
widths=${KAG_CLONE_WIDTHS:-"128 256 512"}
layers=${KAG_CLONE_LAYERS:-2}
epochs=${KAG_CLONE_EPOCHS:-50}
minimum_trajectories=${KAG_CLONE_MIN_TRAJECTORIES:-40}
minimum_sources=${KAG_CLONE_MIN_SOURCES:-2}
minimum_money=${KAG_CLONE_MIN_MONEY:-60000}
maximum_agents=${KAG_CLONE_MAX_AGENTS:-0}
maximum_behavior_jsd=${KAG_CLONE_MAX_BEHAVIOR_JSD:-0.02}
refresh=${KAG_CLONE_REFRESH:-0}
refresh_days=${KAG_CLONE_REFRESH_DAYS:-6}
probe_days=${KAG_CLONE_PROBE_DAYS:-14}
exact_version=${KAG_ELITE_EXACT_VERSION:-1.32.7}
fit_values=${KAG_CLONE_FIT_VALUES:-1}

mkdir -p "$factory_root/datasets" "$factory_root/logs" "$repo_root/$model_root"

if [[ "$refresh" == 1 ]]; then
    echo "DISCOVER latest official Kaggriculture daily datasets"
    listing="$factory_root/kaggle_datasets.json"
    "$kaggle_bin" datasets list --user kaggle -s kaggriculture-episodes \
        --page-size 200 --format json >"$listing"
    mapfile -t candidates < <("$python_bin" -c '
import json, re, sys
rows=json.load(open(sys.argv[1]))
values=[]
for row in rows:
    ref=str(row.get("ref", ""))
    match=re.fullmatch(r"kaggle/kaggriculture-episodes-(\d{4}-\d{2}-\d{2})", ref)
    if match:
        values.append((match.group(1), ref.split("/", 1)[1]))
for _, slug in sorted(values, reverse=True)[:int(sys.argv[2])]:
    print(slug)
' "$listing" "$probe_days")
    if ((${#candidates[@]} == 0)); then
        echo "no official daily datasets discovered" >&2
        exit 1
    fi
    slugs=()
    for slug in "${candidates[@]}"; do
        download_dir="$data_root/raw/$slug"
        archive="$download_dir/$slug.zip"
        mkdir -p "$download_dir"
        if [[ ! -s "$archive" ]]; then
            echo "PROBE-DOWNLOAD kaggle/$slug"
            "$kaggle_bin" datasets download "kaggle/$slug" -p "$download_dir"
        fi
        if [[ ! -s "$archive" ]]; then
            archive=$(find "$download_dir" -maxdepth 1 -type f -name '*.zip' \
                -print -quit)
        fi
        if [[ ! -s "$archive" ]]; then
            echo "archive not found for kaggle/$slug" >&2
            exit 1
        fi
        version=$("$python_bin" -c '
import json, sys, zipfile
with zipfile.ZipFile(sys.argv[1]) as archive:
    names=[name for name in archive.namelist() if name.endswith(".json")]
    if not names:
        raise SystemExit("archive contains no replay JSON")
    with archive.open(names[0]) as stream:
        print(json.load(stream).get("module_version", ""))
' "$archive")
        if [[ "$version" == "$exact_version" ]]; then
            slugs+=("$slug")
            echo "ACCEPT $slug version=$version"
            if ((${#slugs[@]} >= refresh_days)); then
                break
            fi
        else
            echo "SKIP $slug version=$version target=$exact_version"
        fi
    done
    if ((${#slugs[@]} < refresh_days)); then
        echo "found only ${#slugs[@]} exact-version days; wanted $refresh_days" >&2
        exit 1
    fi
    printf 'REFRESH-EXACT %s\n' "${slugs[*]}"
    KAG_ELITE_KEEP_SHARDS=1 KAG_ELITE_KEEP_ARCHIVES=1 \
        KAG_ELITE_DATA_ROOT="$data_root" \
        KAG_ELITE_EXACT_VERSION="$exact_version" \
        "$repo_root/ocean/kaggriculture/build_elite_bc_dataset.sh" "${slugs[@]}"
fi

if [[ ! -s "$data" || ! -s "$manifest" ]]; then
    echo "missing merged BC data or manifest: $data" >&2
    exit 1
fi

if [[ "$fit_values" == 1 ]]; then
    mapfile -t archives < <(find "$data_root/raw" -type f -name '*.zip' -print | sort)
    if ((${#archives[@]})); then
        echo "FIT positive economic values from ${#archives[@]} retained archives"
        "$python_bin" "$repo_root/ocean/kaggriculture/fit_elite_economic_values.py" \
            --output "$factory_root/elite_economic_values.json" \
            --ini-output "$factory_root/elite_economic_values.ini" \
            --sweep-output "$factory_root/elite_economic_values.sweep.ini" \
            --minimum-version "$exact_version" \
            --exact-version "$exact_version" \
            --minimum-final-money "$minimum_money" \
            "${archives[@]}" \
            >"$factory_root/logs/economic_fit.log" 2>&1
        tail -n 30 "$factory_root/logs/economic_fit.log"
    else
        echo "FIT skipped: no retained replay archives (set KAG_CLONE_REFRESH=1)"
    fi
fi

plan="$factory_root/plan.tsv"
plan_args=(
    "$manifest" --output "$plan"
    --minimum-trajectories "$minimum_trajectories"
    --minimum-sources "$minimum_sources"
    --minimum-final-money "$minimum_money"
    --exact-version "$exact_version"
    --maximum-behavior-jsd "$maximum_behavior_jsd"
)
if [[ "$maximum_agents" != 0 ]]; then
    plan_args+=(--maximum-agents "$maximum_agents")
fi
"$python_bin" "$repo_root/ocean/kaggriculture/plan_elite_clones.py" \
    "${plan_args[@]}"
if ! awk -F '\t' 'NR > 1 && $1 == "Ryo Hasegawa" {found=1} END {exit !found}' \
        "$plan"; then
    echo "Ryo Hasegawa did not meet clone sufficiency thresholds" >&2
    exit 1
fi

results="$factory_root/models.tsv"
if [[ ! -e "$results" ]]; then
    printf 'agent\tslug\tdataset_id\thidden\tlayers\tepochs\ttrajectories\tdata\tmodel\tstatus\n' \
        >"$results"
fi

tail -n +2 "$plan" | while IFS=$'\t' read -r agent slug dataset_id \
        trajectories sources \
        source_names source_counts versions behavior_jsd excluded wins \
        money_min money_mean money_max; do
    subset="$factory_root/datasets/${slug}_${dataset_id}.bc"
    if [[ ! -s "$subset" ]]; then
        echo "SUBSET agent=$agent trajectories=$trajectories sources=$sources"
        subset_args=(
            "$data" "$subset" --agent "$agent"
            --minimum-final-money "$minimum_money"
        )
        IFS=',' read -r -a stable_sources <<<"$source_names"
        for source in "${stable_sources[@]}"; do
            subset_args+=(--source "$source")
        done
        "$python_bin" "$repo_root/ocean/kaggriculture/subset_bc_dataset.py" \
            "${subset_args[@]}" \
            >"$factory_root/logs/${slug}_subset.log" 2>&1
    fi
    for hidden in $widths; do
        relative_model="$model_root/$slug/${slug}_${dataset_id}_${hidden}x${layers}_e${epochs}.bin"
        model="$repo_root/$relative_model"
        log="$factory_root/logs/${slug}_${hidden}x${layers}_e${epochs}.log"
        if [[ -s "$model" ]]; then
            echo "REUSE agent=$agent arch=${hidden}x${layers} model=$relative_model"
            continue
        fi
        echo "TRAIN agent=$agent arch=${hidden}x${layers} trajectories=$trajectories"
        mkdir -p "$(dirname "$model")"
        if KAG_ELITE_BC_DATA="$subset" \
                KAG_ELITE_BC_OUTPUT="$relative_model" \
                KAG_ELITE_BC_HIDDEN="$hidden" \
                KAG_ELITE_BC_LAYERS="$layers" \
                KAG_ELITE_BC_VALIDATION_GAMES=20 \
                KAG_ELITE_BC_REPORT_INTERVAL=5 \
                KAG_ELITE_BC_DETAILED_STATS=0 \
                "$repo_root/ocean/kaggriculture/train_elite_bc.sh" "$epochs" \
                >"$log" 2>&1; then
            status=ready
            tail -n 8 "$log"
        else
            status=failed
            tail -n 30 "$log" >&2
        fi
        printf '%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\n' \
            "$agent" "$slug" "$dataset_id" "$hidden" "$layers" "$epochs" \
            "$trajectories" "$subset" "$relative_model" "$status" \
            >>"$results"
    done
done

echo "CLONE FACTORY COMPLETE plan=$plan models=$results"
