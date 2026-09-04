#!/usr/bin/env bash
set -euo pipefail

# Fine-tune a competitive mode-2 policy on sparse human opening decisions.
# Routine executor rows remain in the recurrent sequence but carry no loss.
# Each epoch candidate starts from the same parent; this is an ablation, not
# an accidental chain of progressively overfit continuations.

repo_root=$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)
python_bin=${KAG_OPENING_PYTHON:-/usr/bin/python3}
train_data=${KAG_OPENING_TRAIN_DATA:?set KAG_OPENING_TRAIN_DATA}
holdout_data=${KAG_OPENING_HOLDOUT_DATA:?set KAG_OPENING_HOLDOUT_DATA}
parent=${KAG_OPENING_PARENT:?set KAG_OPENING_PARENT}
output_root=${KAG_OPENING_OUTPUT_ROOT:-/workspace/elite_replays/macro_opening_teacher}
epochs_list=${KAG_OPENING_EPOCHS:-1 3 10}
learning_rate=${KAG_OPENING_LR:-0.00001}
anchor_l2=${KAG_OPENING_ANCHOR_L2:-0.01}
opening_steps=${KAG_OPENING_STEPS:-61}
batch=${KAG_OPENING_BATCH:-32}
hidden=${KAG_OPENING_HIDDEN:-256}
layers=${KAG_OPENING_LAYERS:-3}
seed=${KAG_OPENING_SEED:-2903}

for path in "$train_data" "$holdout_data" "$parent"; do
    [[ -s "$path" ]] || { echo "required input missing: $path" >&2; exit 1; }
done
mkdir -p "$output_root/data" "$output_root/models" "$output_root/evals" "$output_root/logs"

filter_one() {
    local source=$1 output=$2
    local manifest="${source}.players.tsv"
    local output_manifest="${output}.players.tsv"
    "$python_bin" "$repo_root/ocean/kaggriculture/filter_macro_decisions.py" \
        "$source" --output "$output" --opening-steps "$opening_steps" \
        --manifest "$manifest" --output-manifest "$output_manifest" \
        --report "${output}.decision_filter.json"
}

train_filtered="$output_root/data/train_opening_decisions.bc"
holdout_filtered="$output_root/data/holdout_opening_decisions.bc"
filter_one "$train_data" "$train_filtered" >"$output_root/logs/filter_train.log"
filter_one "$holdout_data" "$holdout_filtered" >"$output_root/logs/filter_holdout.log"

make -C "$repo_root/ocean/kaggriculture" build/kag_bc
printf 'epochs\tlearning_rate\tanchor_l2\tmodel\teval\n' >"$output_root/candidates.tsv"
for epochs in $epochs_list; do
    model="$output_root/models/opening_teacher_e${epochs}_lr${learning_rate}.bin"
    log="$output_root/logs/train_e${epochs}.log"
    KAG_ELITE_BC_DATA="$train_filtered" \
    KAG_ELITE_BC_OUTPUT="$model" \
    KAG_ELITE_BC_INIT="$parent" \
    KAG_ELITE_BC_HIDDEN="$hidden" KAG_ELITE_BC_LAYERS="$layers" \
    KAG_ELITE_BC_SEED="$seed" KAG_ELITE_BC_BATCH="$batch" \
    KAG_ELITE_BC_VALIDATION_GAMES=20 \
    KAG_ELITE_BC_OPENING_STEPS="$opening_steps" \
    KAG_ELITE_BC_OPENING_WEIGHT=1 KAG_ELITE_BC_ROOT_WEIGHT=1 \
    KAG_ELITE_BC_MACRO_CLASS_BALANCE=0 \
    KAG_ELITE_BC_ANCHOR_L2="$anchor_l2" KAG_ELITE_BC_LR="$learning_rate" \
    KAG_ELITE_BC_REPORT_INTERVAL=1 KAG_ELITE_BC_DETAILED_STATS=1 \
        "$repo_root/ocean/kaggriculture/train_elite_bc.sh" "$epochs" \
        >"$log" 2>&1
    evaluation="$output_root/evals/opening_teacher_e${epochs}.json"
    "$python_bin" "$repo_root/ocean/kaggriculture/evaluate_macro_clone.py" \
        "$holdout_filtered" "$model" \
        --manifest "${holdout_filtered}.players.tsv" --holdout-fraction 1 \
        --opening-steps "$opening_steps" --output "$evaluation" \
        >"$output_root/logs/eval_e${epochs}.log" 2>&1
    printf '%s\t%s\t%s\t%s\t%s\n' \
        "$epochs" "$learning_rate" "$anchor_l2" "$model" "$evaluation" \
        >>"$output_root/candidates.tsv"
done

"$python_bin" - "$output_root" "$parent" <<'PY'
import csv, json, pathlib, sys
root, parent = map(pathlib.Path, sys.argv[1:])
rows = list(csv.DictReader((root / "candidates.tsv").open(), delimiter="\t"))
for row in rows:
    report = json.loads(pathlib.Path(row["eval"]).read_text())
    row["macro_accuracy"] = report["accuracy"]["macro"]
    row["macro_top3"] = report["macro_top3"]
    row["signature_match"] = report["opening_signatures"]["exact_signature_match_rate"]
    row["predicted_macros"] = report["predicted_diversity"]["macro_ids"]
payload = {
    "format": "kaggriculture_macro_opening_teacher_v1",
    "parent": str(parent),
    "selection_warning": "Offline fidelity is not a promotion result; run closed-loop and league gates.",
    "candidates": rows,
}
(root / "summary.json").write_text(json.dumps(payload, indent=2, sort_keys=True) + "\n")
print(json.dumps(payload, indent=2, sort_keys=True))
PY

echo "MACRO OPENING TEACHER COMPLETE: $output_root"
