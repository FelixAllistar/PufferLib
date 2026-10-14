#!/usr/bin/env bash
# Native CLI/worker contracts. Build bomberman with --float on pre-Ampere GPUs.
# Usage: bash tests/check_sweep_steps.sh BINARY NEW_REPORT_DIRECTORY
set -euo pipefail
cd "$(dirname "$0")/.."
if [[ $# != 2 || ! -x $1 || -e $2 ]]; then
    printf 'Usage: %s BOMBERMAN_BINARY NEW_REPORT_DIRECTORY\n' "$0" >&2
    exit 1
fi
step_binary=$(realpath "$1")
step_report=$(realpath -m "$2")
mkdir -p "$step_report"
ulimit -c 0

step_common=(
    --base.load_model_path=None --base.eval_episodes=0 --base.checkpoint_interval=0
    --base.async=0 --base.cudagraphs=-1 --selfplay.enabled=0
    --selfplay.eval_games=0 --selfplay.eval_bot_games=0
    --vec.total_agents=32 --vec.num_buffers=1 --vec.num_threads=1 --vec.num_policies=1
    --policy.hidden_size=32 --policy.num_layers=1
    --train.gpus=1 --train.total_timesteps=512 --train.horizon=8 --train.minibatch_size=64
    --train.learning_rate=0 --train.anneal_lr=0 --train.anneal_ent_coef=0 --train.ent_coef=.005
    --train.replay_ratio=1 --env.max_ticks=8
    --sweep.max_runs=4 --sweep.gpus=1 --sweep.downsample=1
)
declare -A step_values
for step_arg in "${step_common[@]}"; do
    step_key=${step_arg%%=*}
    step_values[${step_key#--}]=${step_arg#*=}
done
step_bounds=()
# Resolve inherited dimensions, then pin all except entropy for these tiny runs.
while IFS=$'\t' read -r step_key step_default; do
    step_value=${step_values[$step_key]-$step_default}
    step_bounds+=("--sweep.$step_key.min=$step_value" "--sweep.$step_key.max=$step_value")
done < <(awk '
    function trim(s) { sub(/^[ \t]+/, "", s); sub(/[ \t]+$/, "", s); return s }
    { sub(/[#;].*$/, "") }
    /^\[/ { section=$0; gsub(/[\[\]]/, "", section); next }
    /=/ {
        at=index($0, "="); key=trim(substr($0, 1, at-1)); value=trim(substr($0, at+1))
        values[section "." key]=value
        if (section ~ /^sweep\./) ranges[substr(section, 7)]=1
    }
    END { for (key in ranges) print key "\t" values[key] }
' config/default.ini config/bomberman.ini)
step_space=(--sweep.train.ent_coef.distribution=uniform
    --sweep.train.ent_coef.min=.001 --sweep.train.ent_coef.max=.009)

run_case() {
    local step_case=$1
    shift
    mkdir -p "$step_report/$step_case/config"
    cp config/default.ini "$step_report/$step_case/config/default.ini"
    # Optional CLI keys must exist in the INI schema. Use private test fixtures;
    # do not weaken the parser or edit the repository's real configurations.
    awk -v declared="$step_case" '{print} END {if (declared!="omitted")
        print "\n[sweep.train.ent_coef]\nstep = 0"}' config/bomberman.ini \
        > "$step_report/$step_case/config/bomberman.ini"
    (cd "$step_report/$step_case"
    "$step_binary" sweep "${step_common[@]}" "${step_bounds[@]}" "${step_space[@]}" \
        --base.checkpoint_dir="$step_report/$step_case/checkpoints" \
        --base.log_dir="$step_report/$step_case/logs" "$@") > "$step_report/$step_case.log" 2>&1
    [[ $(rg -c '^sweep run=' "$step_report/$step_case.log") == 4 ]]
    while IFS= read -r step_ini; do
        awk '/^\[train\]/{active=1;next} /^\[/{active=0}
            active && $1=="ent_coef" {print $3}' "$step_ini"
    done < <(rg --files --no-ignore "$step_report/$step_case/logs/bomberman" -g '*.ini' | sort) \
        > "$step_report/$step_case.values"
    [[ $(wc -l < "$step_report/$step_case.values") == 4 ]]
}

run_case grid --sweep.train.ent_coef.step=.002
awk 'BEGIN {ok=1} {
    n=($1-.001)/.002; nearest=int(n+.5); error=n-nearest
    if (error<0) error=-error
    if ($1<.001-1e-8 || $1>.009+1e-8 || error>1e-5) ok=0
    seen[nearest]=1
} END {for (n in seen) count++; exit !(ok && count>1)}' "$step_report/grid.values"
run_case omitted
run_case zero --sweep.train.ent_coef.step=0
cmp "$step_report/omitted.values" "$step_report/zero.values"
# A fixed stepped dimension remains fixed while another dimension is searched.
run_case fixed --sweep.train.ent_coef.step=.002 \
    --sweep.train.ent_coef.min=.005 --sweep.train.ent_coef.max=.005 \
    --train.vf_coef=1 --sweep.train.vf_coef.min=.5 --sweep.train.vf_coef.max=2
awk '{error=$1-.005; if (error<0) error=-error; if (error>1e-8) exit 1}' \
    "$step_report/fixed.values"
printf 'PASS grid values reach workers; omitted/zero match; fixed dimension stays fixed\n'

step_bad=(
    --sweep.train.ent_coef.step=-.1
    --sweep.train.ent_coef.step=nan
    --sweep.train.ent_coef.step=inf
    --sweep.train.ent_coef.step=word
    --sweep.train.ent_coef.step=0.1,0.2
    --sweep.train.ent_coef.step=1e-50
    --sweep.train.ent_coef.step=1e-30
    --sweep.train.ent_coef.step=.003
    --sweep.train.ent_coef.distribution=int_uniform
    --sweep.train.ent_coef.distribution=uniform_pow2
    --sweep.train.ent_coef.distribution=log_normal
    --sweep.train.ent_coef.distribution=logit_normal
)
step_errors=(
    'sweep step must be finite' 'sweep step must be finite' 'sweep step must be finite'
    'puf_ini_parse_val' 'puf_ini_parse_val' 'sweep step must be finite'
    'sweep step must separate' 'whole number of steps'
    'sweep step requires uniform' 'sweep step requires uniform'
    'sweep step requires uniform' 'sweep step requires uniform'
)
mkdir -p "$step_report/invalid/config"
cp "$step_report/grid/config/"*.ini "$step_report/invalid/config/"
for step_index in "${!step_bad[@]}"; do
    if { (cd "$step_report/invalid"
        "$step_binary" sweep "${step_common[@]}" "${step_bounds[@]}" "${step_space[@]}" \
        --base.checkpoint_dir="$step_report/invalid/checkpoints" \
        --base.log_dir="$step_report/invalid/logs" --sweep.train.ent_coef.step=.002 \
        "${step_bad[$step_index]}"); } > "$step_report/invalid_$step_index.log" 2>&1; then
        printf 'FAIL accepted %s\n' "${step_bad[$step_index]}" >&2
        exit 1
    fi
    rg -q "${step_errors[$step_index]}" "$step_report/invalid_$step_index.log"
done
[[ ! -e $step_report/invalid/checkpoints && ! -e $step_report/invalid/logs ]]
printf 'PASS %s malformed/unsupported grids rejected before workers launch\n' "${#step_bad[@]}"
