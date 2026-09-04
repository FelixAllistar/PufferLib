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
epochs_list=${KAG_OPENING_EPOCHS:-1 3}
# A competitive PPO parent can put the expert macro far below its current
# argmax.  One tiny learning rate can lower CE without ever changing the
# selected intent, so test a small, bounded ladder from the same parent.
learning_rates=${KAG_OPENING_LRS:-${KAG_OPENING_LR:-0.00003 0.0001 0.0003}}
anchor_l2=${KAG_OPENING_ANCHOR_L2:-0.01}
decision_through=${KAG_DECISION_THROUGH:-201}
opening_steps=${KAG_OPENING_STEPS:-61}
opening_weight=${KAG_OPENING_WEIGHT:-2}
macro_class_balance=${KAG_OPENING_MACRO_CLASS_BALANCE:-0.5}
macro_class_weight_cap=${KAG_OPENING_MACRO_CLASS_WEIGHT_CAP:-4}
argmax_margin=${KAG_OPENING_ARGMAX_MARGIN:-0.25}
opening_argmax_coef=${KAG_OPENING_ARGMAX_COEF:-0.1}
batch=${KAG_OPENING_BATCH:-32}
hidden=${KAG_OPENING_HIDDEN:-256}
layers=${KAG_OPENING_LAYERS:-3}
seed=${KAG_OPENING_SEED:-2903}
rollout_games=${KAG_OPENING_ROLLOUT_GAMES:-20}

for path in "$train_data" "$holdout_data" "$parent"; do
    [[ -s "$path" ]] || { echo "required input missing: $path" >&2; exit 1; }
done
mkdir -p "$output_root/data" "$output_root/models" "$output_root/evals" "$output_root/logs"

filter_one() {
    local source=$1 output=$2
    local manifest="${source}.players.tsv"
    local output_manifest="${output}.players.tsv"
    "$python_bin" "$repo_root/ocean/kaggriculture/filter_macro_decisions.py" \
        "$source" --output "$output" --opening-steps "$decision_through" \
        --manifest "$manifest" --output-manifest "$output_manifest" \
        --report "${output}.decision_filter.json"
}

train_filtered="$output_root/data/train_opening_decisions.bc"
holdout_filtered="$output_root/data/holdout_opening_decisions.bc"
filter_one "$train_data" "$train_filtered" >"$output_root/logs/filter_train.log"
filter_one "$holdout_data" "$holdout_filtered" >"$output_root/logs/filter_holdout.log"

make -C "$repo_root/ocean/kaggriculture" build/kag_bc
parent_evaluation="$output_root/evals/parent.json"
"$python_bin" "$repo_root/ocean/kaggriculture/evaluate_macro_clone.py" \
    "$holdout_filtered" "$parent" \
    --manifest "${holdout_filtered}.players.tsv" --holdout-fraction 1 \
    --opening-steps "$opening_steps" --output "$parent_evaluation" \
    >"$output_root/logs/eval_parent.log" 2>&1
printf 'epochs\tlearning_rate\tanchor_l2\tmodel\teval\n' >"$output_root/candidates.tsv"
rollout_models=("$parent")
for learning_rate in $learning_rates; do
  for epochs in $epochs_list; do
    model="$output_root/models/opening_teacher_e${epochs}_lr${learning_rate}.bin"
    log="$output_root/logs/train_e${epochs}_lr${learning_rate}.log"
    # Training supervision extends through the decision filter, rather than
    # stopping at the 61-turn reporting window. Crop Dusta's land labels begin
    # after turn 60; the old setting excluded them from balance/margin losses.
    KAG_ELITE_BC_DATA="$train_filtered" \
    KAG_ELITE_BC_OUTPUT="$model" \
    KAG_ELITE_BC_INIT="$parent" \
    KAG_ELITE_BC_HIDDEN="$hidden" KAG_ELITE_BC_LAYERS="$layers" \
    KAG_ELITE_BC_SEED="$seed" KAG_ELITE_BC_BATCH="$batch" \
    KAG_ELITE_BC_VALIDATION_GAMES=20 \
    KAG_ELITE_BC_OPENING_STEPS="$decision_through" \
    KAG_ELITE_BC_OPENING_WEIGHT="$opening_weight" KAG_ELITE_BC_ROOT_WEIGHT="$opening_weight" \
    KAG_ELITE_BC_MACRO_CLASS_BALANCE="$macro_class_balance" \
    KAG_ELITE_BC_MACRO_CLASS_WEIGHT_CAP="$macro_class_weight_cap" \
    KAG_ELITE_BC_ARGMAX_MARGIN="$argmax_margin" \
    KAG_ELITE_BC_OPENING_ARGMAX_COEF="$opening_argmax_coef" \
    KAG_ELITE_BC_ANCHOR_L2="$anchor_l2" KAG_ELITE_BC_LR="$learning_rate" \
    KAG_ELITE_BC_REPORT_INTERVAL=1 KAG_ELITE_BC_DETAILED_STATS=1 \
        "$repo_root/ocean/kaggriculture/train_elite_bc.sh" "$epochs" \
        >"$log" 2>&1
    evaluation="$output_root/evals/opening_teacher_e${epochs}_lr${learning_rate}.json"
    "$python_bin" "$repo_root/ocean/kaggriculture/evaluate_macro_clone.py" \
        "$holdout_filtered" "$model" \
        --manifest "${holdout_filtered}.players.tsv" --holdout-fraction 1 \
        --opening-steps "$opening_steps" --output "$evaluation" \
        >"$output_root/logs/eval_e${epochs}_lr${learning_rate}.log" 2>&1
    printf '%s\t%s\t%s\t%s\t%s\n' \
        "$epochs" "$learning_rate" "$anchor_l2" "$model" "$evaluation" \
        >>"$output_root/candidates.tsv"
    rollout_models+=("$model")
  done
done

if ((rollout_games > 0)); then
    "$repo_root/ocean/kaggriculture/eval_population.sh" \
        --games "$rollout_games" --jobs 1 --gpu-agents 16 \
        --fixed pass,rules,top --hidden-size "$hidden" --num-layers "$layers" \
        --output "$output_root/evals/closed_loop" "${rollout_models[@]}" \
        >"$output_root/logs/closed_loop.log" 2>&1
fi

"$python_bin" - "$output_root" "$parent" "$parent_evaluation" <<'PY'
import csv, json, pathlib, sys
root, parent, parent_evaluation = map(pathlib.Path, sys.argv[1:])
rows = list(csv.DictReader((root / "candidates.tsv").open(), delimiter="\t"))
parent_report = json.loads(parent_evaluation.read_text())
for row in rows:
    report = json.loads(pathlib.Path(row["eval"]).read_text())
    row["macro_accuracy"] = report["accuracy"]["macro"]
    row["macro_top3"] = report["macro_top3"]
    row["signature_match"] = report["opening_signatures"]["exact_signature_match_rate"]
    row["predicted_macros"] = report["predicted_diversity"]["macro_ids"]
payload = {
    "format": "kaggriculture_macro_opening_teacher_v1",
    "parent": str(parent),
    "parent_offline": {
        "macro_accuracy": parent_report["accuracy"]["macro"],
        "macro_top3": parent_report["macro_top3"],
        "signature_match": parent_report["opening_signatures"]["exact_signature_match_rate"],
        "predicted_macros": parent_report["predicted_diversity"]["macro_ids"],
    },
    "closed_loop_prefix": str(root / "evals" / "closed_loop"),
    "selection_warning": "Offline fidelity is not a promotion result; run closed-loop and league gates.",
    "candidates": rows,
}
(root / "summary.json").write_text(json.dumps(payload, indent=2, sort_keys=True) + "\n")
print(json.dumps(payload, indent=2, sort_keys=True))
PY

echo "MACRO OPENING TEACHER COMPLETE: $output_root"
