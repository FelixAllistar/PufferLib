#!/usr/bin/env bash
set -euo pipefail

# Convert several exact identities concurrently.  The importer is CPU-bound
# (it mirrors the native mode-2 legality/runtime path), while the separate
# factory script is intentionally sequential when it trains models.  This
# helper only builds datasets; run the factory with KAG_MACRO_SKIP_TRAIN=1
# afterward to emit its combined manifest.

repo_root=$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)
data_root=${KAG_ELITE_DATA_ROOT:-/workspace/elite_replays}
factory_root=${KAG_MACRO_CLONE_ROOT:-$data_root/clone_factory_macro2}
raw_glob=${KAG_MACRO_RAW_GLOB:-$data_root/raw/*/*.zip}
minimum_version=${KAG_ELITE_MIN_VERSION:-1.32.7}
exact_version=${KAG_ELITE_EXACT_VERSION:-$minimum_version}
limit=${KAG_MACRO_MAX_EPISODES:-0}
cutoff=${KAG_MACRO_TRAIN_UNTIL:-latest}
python_bin=${KAG_ELITE_PYTHON:-}

if [[ -z "$python_bin" ]]; then
    for candidate in /venv/main/bin/python /usr/bin/python3 python3; do
        if "$candidate" -c 'import numpy' >/dev/null 2>&1; then
            python_bin=$candidate
            break
        fi
    done
fi
if [[ ! -x "$python_bin" ]]; then
    echo "Python with NumPy not found: $python_bin" >&2
    exit 1
fi
if (($# == 0)); then
    echo "pass exact agent names" >&2
    exit 2
fi
mapfile -t archives < <(compgen -G "$raw_glob" | sort -r)
if ((${#archives[@]} == 0)); then
    echo "no raw archives match: $raw_glob" >&2
    exit 1
fi
if [[ "$cutoff" != latest ]]; then
    mapfile -t archives < <(printf '%s\n' "${archives[@]}" | \
        awk -v until="$cutoff" 'match($0, /20[0-9][0-9]-[0-9][0-9]-[0-9][0-9]/) {day=substr($0,RSTART,RLENGTH); if (day <= until) print}')
fi
mkdir -p "$factory_root/datasets" "$factory_root/logs"

slug_for() {
    "$python_bin" - "$1" <<'PY'
import re, sys, unicodedata
value = unicodedata.normalize("NFKC", sys.argv[1]).strip()
print(re.sub(r"[^A-Za-z0-9._-]+", "_", value).strip("._-") or "agent")
PY
}

run_agent() {
    local agent=$1 slug=$2
    local dataset="$factory_root/datasets/${slug}_cutoff-${cutoff}_v${exact_version}_macro2.bc"
    local audit="$dataset.audit.json"
    local players="$dataset.players.tsv"
    if [[ -s "$dataset" && -s "$audit" && -s "$players" ]]; then
        echo "REUSE dataset=$dataset"
        return 0
    fi
    local -a args=(
        --output "$dataset" --audit-json "$audit" --manifest "$players"
        --minimum-version "$minimum_version" --exact-version "$exact_version"
        --players both --macro-mode structured --display-name "$agent"
    )
    if ((limit > 0)); then
        args+=(--limit "$limit")
    fi
    echo "IMPORT exact agent=$agent cutoff=$cutoff"
    "$python_bin" "$repo_root/ocean/kaggriculture/import_elite_replays.py" \
        "${args[@]}" "${archives[@]}" \
        >"$factory_root/logs/${slug}_import.log" 2>&1
}

declare -a pids=()
declare -a names=()
for agent in "$@"; do
    slug=$(slug_for "$agent")
    run_agent "$agent" "$slug" &
    pids+=("$!")
    names+=("$agent")
done

failed=0
for index in "${!pids[@]}"; do
    if ! wait "${pids[index]}"; then
        echo "FAILED exact agent=${names[index]}" >&2
        failed=1
    fi
done
exit "$failed"
