#!/usr/bin/env bash
set -euo pipefail

# Download recent official top-episode datasets, convert each archive without
# unpacking it, merge the section-major BC shards, and discard raw archives.
# The default three days are all Kaggriculture 1.32.7 data.

repo_root=$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)
data_root=${KAG_ELITE_DATA_ROOT:-/workspace/elite_replays}
kaggle_bin=${KAGGLE_BIN:-/root/.local/bin/kaggle}
keep_shards=${KAG_ELITE_KEEP_SHARDS:-0}
python_bin=${KAG_ELITE_PYTHON:-}

if [[ -z "$python_bin" ]]; then
    for candidate in /venv/main/bin/python python3; do
        if "$candidate" -c 'import numpy' >/dev/null 2>&1; then
            python_bin=$candidate
            break
        fi
    done
fi
if [[ -z "$python_bin" ]]; then
    echo "no Python interpreter with NumPy found; set KAG_ELITE_PYTHON" >&2
    exit 1
fi

if (($#)); then
    slugs=("$@")
else
    slugs=(
        kaggriculture-episodes-2026-08-19
        kaggriculture-episodes-2026-08-20
        kaggriculture-episodes-2026-08-21
    )
fi

mkdir -p "$data_root/raw" "$data_root/shards" "$data_root/logs"

run_shard() {
    local slug=$1
    local download_dir="$data_root/raw/$slug"
    local output="$data_root/shards/$slug.bc"
    local archive="$download_dir/$slug.zip"
    mkdir -p "$download_dir"
    if [[ -s "$output" && -s "$output.audit.json" ]]; then
        echo "REUSE $output"
        return 0
    fi
    if [[ ! -s "$archive" ]]; then
        echo "DOWNLOAD kaggle/$slug"
        "$kaggle_bin" datasets download "kaggle/$slug" -p "$download_dir"
    fi
    if [[ ! -s "$archive" ]]; then
        archive=$(find "$download_dir" -maxdepth 1 -type f -name '*.zip' -print -quit)
    fi
    if [[ ! -s "$archive" ]]; then
        echo "archive not found after downloading kaggle/$slug" >&2
        return 1
    fi
    echo "IMPORT $archive"
    "$python_bin" "$repo_root/ocean/kaggriculture/import_elite_replays.py" \
        --output "$output" --minimum-version 1.32.7 --players both "$archive"
    # Raw downloads are reproducible and large; keep the compact BC shard and
    # its audit/manifest instead.
    rm -f -- "$archive"
    rmdir "$download_dir" 2>/dev/null || true
    echo "DONE $output"
}

pids=()
for slug in "${slugs[@]}"; do
    run_shard "$slug" >"$data_root/logs/$slug.log" 2>&1 &
    pids+=("$!")
done

failed=0
for index in "${!pids[@]}"; do
    if ! wait "${pids[$index]}"; then
        echo "FAILED ${slugs[$index]} (see $data_root/logs/${slugs[$index]}.log)" >&2
        failed=1
    else
        tail -n 14 "$data_root/logs/${slugs[$index]}.log"
    fi
done
if ((failed)); then
    exit 1
fi

shards=()
for slug in "${slugs[@]}"; do
    shards+=("$data_root/shards/$slug.bc")
done
merged="$data_root/kaggriculture_elite_1.32.7.bc"
"$python_bin" "$repo_root/ocean/kaggriculture/merge_bc_datasets.py" \
    "$merged" "${shards[@]}"

# Preserve trajectory order alongside the merged section-major dataset. This
# sidecar supplies final-money targets for the future-value fit.
merged_manifest="$merged.players.tsv"
manifest_tmp=$(mktemp "$data_root/.elite-players.XXXXXX")
first=1
for shard in "${shards[@]}"; do
    shard_manifest="$shard.players.tsv"
    if [[ ! -s "$shard_manifest" ]]; then
        echo "missing player manifest: $shard_manifest" >&2
        rm -f -- "$manifest_tmp"
        exit 1
    fi
    if ((first)); then
        awk '1' "$shard_manifest" >>"$manifest_tmp"
        first=0
    else
        awk 'NR > 1' "$shard_manifest" >>"$manifest_tmp"
    fi
done
mv -- "$manifest_tmp" "$merged_manifest"

if [[ "$keep_shards" == 0 ]]; then
    for shard in "${shards[@]}"; do
        rm -f -- "$shard"
    done
fi

echo "ELITE DATASET READY: $merged"
du -h "$merged" "$merged_manifest" "$data_root" | tail -n 3
