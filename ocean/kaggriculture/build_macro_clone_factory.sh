#!/usr/bin/env bash
set -euo pipefail

# Build mode-2 replay datasets and size-matched clones for exact named agents.
# Raw archives are inputs only; this script never deletes them.  Run the
# identity scanner first and pass exact --agent-name values to avoid aliases.
#
# Example:
#   KAG_MACRO_CLONE_ROOT=/workspace/elite_replays/clone_factory_macro2 \
#   ./ocean/kaggriculture/build_macro_clone_factory.sh Yuan800 peikopon

repo_root=$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)
data_root=${KAG_ELITE_DATA_ROOT:-/workspace/elite_replays}
factory_root=${KAG_MACRO_CLONE_ROOT:-$data_root/clone_factory_macro2}
raw_glob=${KAG_MACRO_RAW_GLOB:-$data_root/raw/*/*.zip}
minimum_version=${KAG_ELITE_MIN_VERSION:-1.32.7}
exact_version=${KAG_ELITE_EXACT_VERSION:-$minimum_version}
cutoff=${KAG_MACRO_CUTOFF:-latest}
train_until=${KAG_MACRO_TRAIN_UNTIL:-latest}
skip_train=${KAG_MACRO_SKIP_TRAIN:-0}
seed=${KAG_MACRO_CLONE_SEED:-2903}
epochs=${KAG_MACRO_CLONE_EPOCHS:-25}
opening_steps=${KAG_MACRO_OPENING_STEPS:-61}
opening_weight=${KAG_MACRO_OPENING_WEIGHT:-2}
root_weight=${KAG_MACRO_ROOT_WEIGHT:-2}
validation_games=${KAG_MACRO_VALIDATION_GAMES:-20}
min_512_trajectories=${KAG_MACRO_MIN_512_TRAJECTORIES:-200}
max_episodes=${KAG_MACRO_MAX_EPISODES:-0}
newest_first=${KAG_MACRO_NEWEST_FIRST:-1}
artifact_suffix=${KAG_MACRO_ARTIFACT_SUFFIX:-}
python_bin=${KAG_ELITE_PYTHON:-}

if [[ -z "$python_bin" ]]; then
    for candidate in /venv/main/bin/python /usr/bin/python3 python3; do
        if "$candidate" -c 'import numpy' >/dev/null 2>&1; then
            python_bin=$candidate
            break
        fi
    done
fi

if (($# == 0)); then
    echo "pass one or more exact agent names (for example: Yuan800)" >&2
    exit 2
fi
if [[ ! -x "$python_bin" ]]; then
    echo "Python with NumPy not found: $python_bin" >&2
    exit 1
fi
mapfile -t archives < <(compgen -G "$raw_glob" | sort)
if ((${#archives[@]} == 0)); then
    echo "no raw archives match: $raw_glob" >&2
    exit 1
fi
mkdir -p "$factory_root/datasets" "$factory_root/models" "$factory_root/logs"

if [[ "$cutoff" == latest ]]; then
    cutoff=$($python_bin - "$factory_root" "${archives[@]}" <<'PY'
import re, sys
values=[]
for path in sys.argv[2:]:
    values.extend(re.findall(r"20\d\d-\d\d-\d\d", path))
print(max(values) if values else "unknown")
PY
)
fi
if [[ "$train_until" != latest ]]; then
    mapfile -t archives < <(printf '%s\n' "${archives[@]}" | \
        awk -v until="$train_until" 'match($0, /20[0-9][0-9]-[0-9][0-9]-[0-9][0-9]/) {day=substr($0,RSTART,RLENGTH); if (day <= until) print}')
    if ((${#archives[@]} == 0)); then
        echo "no raw archives at or before train cutoff: $train_until" >&2
        exit 1
    fi
    cutoff=$train_until
fi
if [[ "$newest_first" == 1 ]]; then
    # A bounded import should represent the freshest available replay days;
    # full imports are order-independent but also benefit from recent progress
    # being visible first in the log.
    mapfile -t archives < <(printf '%s\n' "${archives[@]}" | sort -r)
fi

manifest="$factory_root/factory_manifest.tsv"
if [[ ! -e "$manifest" ]]; then
    printf 'agent\tcutoff\tversion\tdata\tmodel\thidden\tlayers\tepochs\tseed\ttrajectories\tstatus\n' >"$manifest"
fi

for agent in "$@"; do
    slug=$($python_bin - "$agent" <<'PY'
import re, sys, unicodedata
value = unicodedata.normalize("NFKC", sys.argv[1]).strip()
slug = re.sub(r"[^A-Za-z0-9._-]+", "_", value).strip("._-")
print(slug or "agent")
PY
)
    dataset="$factory_root/datasets/${slug}_cutoff-${cutoff}_v${exact_version}_macro2${artifact_suffix}.bc"
    audit="$dataset.audit.json"
    players="$dataset.players.tsv"
    if [[ ! -s "$dataset" || ! -s "$audit" || ! -s "$players" ]]; then
        echo "IMPORT exact agent=$agent cutoff=$cutoff"
        import_args=(
            --output "$dataset" --audit-json "$audit" --manifest "$players"
            --minimum-version "$minimum_version" --exact-version "$exact_version"
            --players both --macro-mode structured --display-name "$agent"
        )
        if ((max_episodes > 0)); then
            import_args+=(--limit "$max_episodes")
        fi
        "$python_bin" "$repo_root/ocean/kaggriculture/import_elite_replays.py" \
            "${import_args[@]}" \
            "${archives[@]}" >"$factory_root/logs/${slug}_import.log" 2>&1
    else
        echo "REUSE dataset=$dataset"
    fi
    trajectories=$($python_bin - "$players" <<'PY'
import csv, sys
with open(sys.argv[1], encoding="utf-8", newline="") as stream:
    print(sum(1 for _ in csv.DictReader(stream, delimiter="\t")))
PY
)
    if ((trajectories < 1)); then
        echo "agent has no exact trajectories: $agent" >&2
        exit 1
    fi
    for hidden in 128 256; do
        model="$factory_root/models/${slug}_cutoff-${cutoff}_v${exact_version}_macro2${artifact_suffix}_h${hidden}x2_s${seed}_e${epochs}.bin"
        log="$factory_root/logs/${slug}_h${hidden}x2.log"
        status=ready
        if [[ "$skip_train" == 1 ]]; then
            status=deferred
            echo "DEFER TRAIN exact agent=$agent hidden=$hidden (KAG_MACRO_SKIP_TRAIN=1)"
        elif [[ ! -s "$model" ]]; then
            echo "TRAIN exact agent=$agent hidden=$hidden trajectories=$trajectories"
            if ! KAG_ELITE_BC_DATA="$dataset" \
                KAG_ELITE_BC_OUTPUT="$model" \
                KAG_ELITE_BC_HIDDEN="$hidden" \
                KAG_ELITE_BC_LAYERS=2 \
                KAG_ELITE_BC_SEED="$seed" \
                KAG_ELITE_BC_VALIDATION_GAMES="$validation_games" \
                KAG_ELITE_BC_OPENING_STEPS="$opening_steps" \
                KAG_ELITE_BC_OPENING_WEIGHT="$opening_weight" \
                KAG_ELITE_BC_ROOT_WEIGHT="$root_weight" \
                KAG_ELITE_BC_REPORT_INTERVAL=5 \
                KAG_ELITE_BC_DETAILED_STATS=1 \
                "$repo_root/ocean/kaggriculture/train_elite_bc.sh" "$epochs" \
                >"$log" 2>&1; then
                status=failed
                tail -n 50 "$log" >&2 || true
            fi
        else
            echo "REUSE model=$model"
        fi
        printf '%s\t%s\t%s\t%s\t%s\t%s\t2\t%s\t%s\t%s\t%s\n' \
            "$agent" "$cutoff" "$exact_version" "$dataset" "$model" \
            "$hidden" "$epochs" "$seed" "$trajectories" "$status" >>"$manifest"
    done
    if ((trajectories >= min_512_trajectories)); then
        hidden=512
        model="$factory_root/models/${slug}_cutoff-${cutoff}_v${exact_version}_macro2${artifact_suffix}_h512x2_s${seed}_e${epochs}.bin"
        log="$factory_root/logs/${slug}_h512x2.log"
        status=ready
        if [[ "$skip_train" == 1 ]]; then
            status=deferred
            echo "DEFER TRAIN exact agent=$agent hidden=512 (KAG_MACRO_SKIP_TRAIN=1)"
        elif [[ ! -s "$model" ]]; then
            echo "TRAIN exact agent=$agent hidden=512 trajectories=$trajectories"
            if ! KAG_ELITE_BC_DATA="$dataset" \
                KAG_ELITE_BC_OUTPUT="$model" KAG_ELITE_BC_HIDDEN=512 \
                KAG_ELITE_BC_LAYERS=2 KAG_ELITE_BC_SEED="$seed" \
                KAG_ELITE_BC_VALIDATION_GAMES="$validation_games" \
                KAG_ELITE_BC_OPENING_STEPS="$opening_steps" \
                KAG_ELITE_BC_OPENING_WEIGHT="$opening_weight" \
                KAG_ELITE_BC_ROOT_WEIGHT="$root_weight" \
                KAG_ELITE_BC_REPORT_INTERVAL=5 KAG_ELITE_BC_DETAILED_STATS=1 \
                "$repo_root/ocean/kaggriculture/train_elite_bc.sh" "$epochs" \
                >"$log" 2>&1; then
                status=failed
                tail -n 50 "$log" >&2 || true
            fi
        else
            echo "REUSE model=$model"
        fi
        printf '%s\t%s\t%s\t%s\t%s\t512\t2\t%s\t%s\t%s\t%s\n' \
            "$agent" "$cutoff" "$exact_version" "$dataset" "$model" \
            "$epochs" "$seed" "$trajectories" "$status" >>"$manifest"
    else
        echo "SKIP 512 agent=$agent trajectories=$trajectories threshold=$min_512_trajectories"
    fi
done

# JSON is useful for transfer/deletion audits; it contains hashes of all
# datasets/models but no raw replay bytes.
report="$factory_root/factory_manifest.json"
"$python_bin" - "$manifest" "$report" <<'PY'
import csv, hashlib, json, os, pathlib, sys
manifest, output = sys.argv[1:]
rows = list(csv.DictReader(open(manifest, encoding="utf-8", newline=""), delimiter="\t"))
for row in rows:
    for field in ("data", "model"):
        path = pathlib.Path(row[field])
        if not path.is_file():
            row[field + "_sha256"] = None
            row[field + "_bytes"] = 0
            continue
        digest = hashlib.sha256()
        with path.open("rb") as stream:
            while chunk := stream.read(16 * 1024 * 1024):
                digest.update(chunk)
        row[field + "_sha256"] = digest.hexdigest()
        row[field + "_bytes"] = path.stat().st_size
payload = {"macro_mode": 2, "observation_bytes": 1280,
           "heads": 47, "mask_bits": 1058, "rows": rows}
pathlib.Path(output).write_text(json.dumps(payload, indent=2, sort_keys=True) + "\n", encoding="utf-8")
PY
echo "MACRO CLONE FACTORY COMPLETE manifest=$manifest report=$report"
