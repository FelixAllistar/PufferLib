#!/bin/bash
# One DAgger round: roll the current clone (student), relabel every reached
# state with the native expert (bc.bot), then retrain warm-started from the
# student. Re-run with the produced checkpoint as --student for the next round.
#
# Usage:
#   dagger_bot.sh thunder            # round 1 from *_full_clone.bin
#   dagger_bot.sh thunder 2 saved/kaggriculture_bc_v2/thunder_dagger1.bin
#
# Tunables:
#   KAG_BC_GAMES         games per round (default 200)
#   KAG_BC_EPOCHS        retrain epochs (default 600)
#   KAG_BC_BATCH         train games per batch (default 16)
#   KAG_BC_DAGGER_BATCH  parallel rollout games (default 16)
#   KAG_BC_LR            retrain learning rate (default 0.0005)
#   KAG_BC_BETA          expert action mixing fraction (default 0.25)
#   KAG_BC_STEPS         decision steps per episode (default 720)
#   KAG_BC_HIDDEN        policy width (default 128)
#   KAG_BC_LAYERS        policy depth (default 2)
set -euo pipefail

cd "$(dirname "$0")/../.."

kag_bc=./ocean/kaggriculture/build/kag_bc
kag_out=${KAG_BC_OUT_DIR:-saved/kaggriculture_bc_v2}
kag_games=${KAG_BC_GAMES:-200}
kag_epochs=${KAG_BC_EPOCHS:-600}
kag_batch=${KAG_BC_BATCH:-16}
kag_dagger_batch=${KAG_BC_DAGGER_BATCH:-16}
kag_lr=${KAG_BC_LR:-0.0005}
kag_beta=${KAG_BC_BETA:-0.25}
kag_steps=${KAG_BC_STEPS:-720}
kag_hidden=${KAG_BC_HIDDEN:-128}
kag_layers=${KAG_BC_LAYERS:-2}

declare -A kag_profile=(
    [rules]=0 [top]=1 [structured]=2 [pulse]=3 [frontier]=4
    [triad]=5 [thunder]=6 [lugovoy]=7 [thunder25]=8 [v20]=9
    [moon]=10 [hamburger]=11
)

kag_usage() {
    printf 'usage: %s NAME [ROUND] [STUDENT]\n' "$0" >&2
    printf 'available: rules top structured pulse frontier triad thunder lugovoy thunder25 v20 moon hamburger\n' >&2
}

kag_name=${1:-}
kag_round=${2:-1}
if [[ -z $kag_name ]]; then
    kag_usage
    exit 2
fi
kag_profile_id=${kag_profile[$kag_name]:-}
if [[ -z $kag_profile_id ]]; then
    printf 'unknown bot: %s\n' "$kag_name" >&2
    kag_usage
    exit 2
fi

kag_student=${3:-$kag_out/${kag_name}_clone_h${kag_hidden}_l${kag_layers}.bin}
if [[ ! -f $kag_student && $kag_hidden == 128 && $kag_layers == 2 ]]; then
    kag_student="$kag_out/${kag_name}_clone.bin"
fi
[[ -f $kag_student ]] || { echo "student not found: $kag_student" >&2; exit 1; }

kag_suffix="h${kag_hidden}_l${kag_layers}"
kag_data="$kag_out/${kag_name}_dagger${kag_round}_${kag_suffix}_data.bin"
kag_output="$kag_out/${kag_name}_dagger${kag_round}_${kag_suffix}.bin"

printf '== DAgger round %s on %s ==\n' "$kag_round" "$kag_name"
printf 'student=%s expert=%s beta=%.2f games=%d epochs=%d\n' \
    "$kag_student" "$kag_name" "$kag_beta" "$kag_games" "$kag_epochs"

"$kag_bc" bc.mode=gen_dagger bc.games="$kag_games" bc.steps="$kag_steps" \
    bc.bot="$kag_profile_id" bc.opponent=-1 bc.seat=-1 \
    bc.beta="$kag_beta" bc.dagger_batch="$kag_dagger_batch" \
    bc.student="$kag_student" bc.seed=2903 bc.data="$kag_data" \
    "policy.hidden_size=$kag_hidden" "policy.num_layers=$kag_layers"

"$kag_bc" bc.mode=train bc.data="$kag_data" \
    bc.epochs="$kag_epochs" bc.batch="$kag_batch" \
    bc.learning_rate="$kag_lr" bc.load_model_path="$kag_student" \
    bc.opening_steps=26 bc.opening_weight=1 bc.validation_games=30 \
    bc.zero_reset_source=0 bc.output="$kag_output" \
    "policy.hidden_size=$kag_hidden" "policy.num_layers=$kag_layers"

printf 'DAgger round %s complete: %s\n' "$kag_round" "$kag_output"
