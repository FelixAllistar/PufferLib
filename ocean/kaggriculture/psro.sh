#!/bin/bash
set -euo pipefail

cd "$(dirname "$0")/../.."

kag_mode=iterate
if (($#)) && [[ $1 =~ ^(iterate|analyze|runs)$ ]]; then
    kag_mode=$1
    shift
fi

kag_games=10
kag_confirm_games=50
kag_jobs=1
kag_gpu_agents=${KAG_GPU_AGENTS:-64}
kag_range=0:100:12
kag_shortlist=4
kag_prescreen_games=20
kag_prescreen_agents=16384
kag_run=
kag_run_explicit=0
kag_output=
kag_reuse=
kag_max_admit=2
kag_min_weight=0.01
kag_max_league=8
kag_jsd_steps=720
kag_jsd_seeds=2
kag_profile_games=0
kag_quality_gap=0.15
kag_eval_deterministic=1
kag_meta_share=0.70
kag_diversity_share=0.25
kag_exploration_share=0.05
kag_fixed=none
kag_league=saved/kaggriculture_league_v6
kag_config=config/kaggriculture.ini
kag_archive_root=saved/kaggriculture_league_v6_archive

kag_usage() {
    printf '%s\n' \
        "Usage: $0 [iterate|analyze|runs] [options]" \
        "" \
        "With no subcommand, run one PSRO iteration against the newest run." \
        "  iterate             Evaluate, solve, admit support policies, update config" \
        "  analyze             Evaluate and solve without changing the active league" \
        "  runs                List non-sweep runs, checkpoint counts, and step ranges" \
        "" \
        "Options:" \
        "  --run DIRECTORY     Candidate run (default newest non-sweep run)" \
        "  --games N           Games per shortlisted pairing, balanced by seat (default 10)" \
        "  --confirm-games N   Games for the screened support set (default 50; 0 disables)" \
        "  --jobs N            Legacy/fixed evaluator processes (default 1; matrix eval is persistent)" \
        "  --gpu-agents N      CUDA agents per evaluator (default 64)" \
        "  --range A:B:N       Coarse sample N stages from A through B percent (default 0:100:12)" \
        "  --shortlist N       Local peaks retained for the full matrix (default 4)" \
        "  --prescreen-games N Games/candidate against each active league member (default 20)" \
        "  --prescreen-agents N Minimum native screen batch; surplus runs unique games (default 4096)" \
        "  --output PREFIX     Result prefix under logs/kaggriculture" \
        "  --reuse PREFIX      Reuse an existing screen instead of evaluating again" \
        "  --max-admit N       Maximum new support policies admitted (default 2)" \
        "  --min-weight X      Minimum solved mass for admission (default 0.01)" \
        "  --max-league N      Prune lowest-mass members above N (2..8; default 8)" \
        "  --jsd-steps N       Shared-state behavior probe steps per seed (default 720)" \
        "  --jsd-seeds N       Independent probe seeds, each a full trajectory (default 2)" \
        "  --profile-games N   GPU behavior profile games per active policy (default 0)" \
        "  --quality-gap X     Max score gap for diversity candidates (default 0.15)" \
        "  --stochastic       Sample masked actions throughout screening/confirmation" \
        "  --fixed LIST        Optional fixed eval sides (default none)" \
        "  --league DIRECTORY  Active league (default saved/kaggriculture_league_v6)" \
        "  --config FILE       INI updated by iterate (default config/kaggriculture.ini)" \
        "  --archive DIRECTORY League snapshot root" \
        "  -h, --help          Show this help"
}

while (($#)); do
    case "$1" in
        --run) kag_run=$2; kag_run_explicit=1; shift 2 ;;
        --games) kag_games=$2; shift 2 ;;
        --confirm-games) kag_confirm_games=$2; shift 2 ;;
        --jobs) kag_jobs=$2; shift 2 ;;
        --gpu-agents) kag_gpu_agents=$2; shift 2 ;;
        --range) kag_range=$2; shift 2 ;;
        --shortlist) kag_shortlist=$2; shift 2 ;;
        --prescreen-games) kag_prescreen_games=$2; shift 2 ;;
        --prescreen-agents) kag_prescreen_agents=$2; shift 2 ;;
        --output) kag_output=$2; shift 2 ;;
        --reuse) kag_reuse=$2; shift 2 ;;
        --max-admit) kag_max_admit=$2; shift 2 ;;
        --min-weight) kag_min_weight=$2; shift 2 ;;
        --max-league) kag_max_league=$2; shift 2 ;;
        --jsd-steps) kag_jsd_steps=$2; shift 2 ;;
        --jsd-seeds) kag_jsd_seeds=$2; shift 2 ;;
        --profile-games) kag_profile_games=$2; shift 2 ;;
        --quality-gap) kag_quality_gap=$2; shift 2 ;;
        --stochastic) kag_eval_deterministic=0; shift ;;
        --fixed) kag_fixed=$2; shift 2 ;;
        --league) kag_league=$2; shift 2 ;;
        --config) kag_config=$2; shift 2 ;;
        --archive) kag_archive_root=$2; shift 2 ;;
        -h|--help) kag_usage; exit 0 ;;
        *) printf 'Unknown argument: %s\n' "$1" >&2; kag_usage >&2; exit 2 ;;
    esac
done
kag_eval_mode_args=()
((kag_eval_deterministic)) || kag_eval_mode_args+=(--stochastic)

kag_latest_run() {
    find checkpoints/kaggriculture -mindepth 2 -maxdepth 2 -type f \
        -regextype posix-extended -regex '.*/[0-9]{16}\.bin' \
        ! -path '*/sweep_*/*' -printf '%T@\t%h\n' | sort -nr \
        | awk -F '\t' 'NR == 1 {print $2}'
}

kag_run_stats() {
    local kag_dir kag_first kag_last kag_count kag_marker
    local kag_newest
    kag_newest=$(kag_latest_run)
    printf 'run\tcheckpoints\tfirst_steps\tlast_steps\tnewest\n'
    while IFS= read -r kag_dir; do
        mapfile -t kag_files < <(find "$kag_dir" -maxdepth 1 -type f \
            -regextype posix-extended -regex '.*/[0-9]{16}\.bin' -printf '%f\n' | sort)
        kag_count=${#kag_files[@]}
        ((kag_count)) || continue
        kag_first=${kag_files[0]%.bin}
        kag_last=${kag_files[kag_count-1]%.bin}
        kag_marker=
        [[ $kag_dir == "$kag_newest" ]] && kag_marker=yes
        printf '%s\t%d\t%d\t%d\t%s\n' "${kag_dir##*/}" "$kag_count" \
            "$((10#$kag_first))" "$((10#$kag_last))" "$kag_marker"
    done < <(find checkpoints/kaggriculture -mindepth 1 -maxdepth 1 -type d \
        ! -name 'sweep_*' -print | sort)
}

if [[ $kag_mode == runs ]]; then
    if command -v column >/dev/null 2>&1; then
        kag_run_stats | column -t -s $'\t'
    else
        kag_run_stats
    fi
    exit 0
fi

[[ $kag_games =~ ^[0-9]+$ ]] && ((kag_games >= 2 && kag_games % 2 == 0)) \
    || { printf '%s\n' '--games must be an even integer of at least 2' >&2; exit 2; }
[[ $kag_confirm_games =~ ^[0-9]+$ ]] \
    && ((kag_confirm_games == 0 || (kag_confirm_games >= 2 && kag_confirm_games % 2 == 0))) \
    || { printf '%s\n' '--confirm-games must be zero or an even integer of at least 2' >&2; exit 2; }
[[ $kag_jobs =~ ^[0-9]+$ ]] && ((kag_jobs >= 1)) \
    || { printf '%s\n' '--jobs must be a positive integer' >&2; exit 2; }
[[ $kag_gpu_agents =~ ^[0-9]+$ ]] && ((kag_gpu_agents >= 4)) \
    || { printf '%s\n' '--gpu-agents must be at least 4' >&2; exit 2; }
[[ $kag_shortlist =~ ^[0-9]+$ ]] && ((kag_shortlist >= 1 && kag_shortlist <= 4)) \
    || { printf '%s\n' '--shortlist must be an integer from 1 through 4' >&2; exit 2; }
[[ $kag_prescreen_games =~ ^[0-9]+$ ]] \
    && ((kag_prescreen_games >= 2 && kag_prescreen_games % 2 == 0)) \
    || { printf '%s\n' '--prescreen-games must be an even integer of at least 2' >&2; exit 2; }
[[ $kag_prescreen_agents =~ ^[0-9]+$ ]] && ((kag_prescreen_agents >= 0)) \
    || { printf '%s\n' '--prescreen-agents must be a nonnegative integer' >&2; exit 2; }
if ! [[ $kag_range =~ ^([0-9]+):([0-9]+):([0-9]+)$ ]]; then
    printf '%s\n' '--range must have the form START_PERCENT:END_PERCENT:COUNT' >&2
    exit 2
fi
kag_range_start=${BASH_REMATCH[1]}
kag_range_end=${BASH_REMATCH[2]}
kag_range_count=${BASH_REMATCH[3]}
((kag_range_start <= kag_range_end && kag_range_end <= 100 && kag_range_count >= 2)) \
    || { printf '%s\n' '--range requires 0 <= START <= END <= 100 and COUNT >= 2' >&2; exit 2; }
[[ $kag_max_admit =~ ^[0-9]+$ ]] \
    || { printf '%s\n' '--max-admit must be a nonnegative integer' >&2; exit 2; }
[[ $kag_max_league =~ ^[0-9]+$ ]] \
    && ((kag_max_league >= 2 && kag_max_league <= 8)) \
    || { printf '%s\n' '--max-league must be an integer from 2 through 8' >&2; exit 2; }
[[ $kag_jsd_steps =~ ^[0-9]+$ ]] && ((kag_jsd_steps >= 720)) \
    || { printf '%s\n' '--jsd-steps must be at least 720' >&2; exit 2; }
[[ $kag_jsd_seeds =~ ^[0-9]+$ ]] && ((kag_jsd_seeds >= 1 && kag_jsd_seeds <= 32)) \
    || { printf '%s\n' '--jsd-seeds must be an integer from 1 through 32' >&2; exit 2; }
[[ $kag_profile_games =~ ^[0-9]+$ ]] \
    && ((kag_profile_games == 0 || (kag_profile_games >= 2 && kag_profile_games % 2 == 0))) \
    || { printf '%s\n' '--profile-games must be zero or an even integer of at least 2' >&2; exit 2; }
awk -v x="$kag_min_weight" 'BEGIN {exit !(x >= 0 && x <= 1)}' \
    || { printf '%s\n' '--min-weight must be a probability' >&2; exit 2; }
awk -v x="$kag_quality_gap" 'BEGIN {exit !(x >= 0 && x <= 1)}' \
    || { printf '%s\n' '--quality-gap must be in [0,1]' >&2; exit 2; }

if (( ! kag_run_explicit )) && [[ -n $kag_reuse \
        && -f ${kag_reuse}_manifest.tsv ]]; then
    kag_reuse_checkpoint=$(awk -F '\t' '
        NR > 1 && $3 ~ /checkpoints\/kaggriculture\/[^\/]+\/[0-9]{16}\.bin$/ {
            print $3; exit
        }
    ' "${kag_reuse}_manifest.tsv")
    [[ -n $kag_reuse_checkpoint ]] && kag_run=${kag_reuse_checkpoint%/*}
fi
if [[ -z $kag_run ]]; then kag_run=$(kag_latest_run); fi
if [[ -z $kag_run || ! -d $kag_run ]]; then
    printf 'Candidate run not found: %s\n' "$kag_run" >&2
    exit 1
fi
if [[ ! -d $kag_league ]]; then
    printf 'Active league not found: %s\n' "$kag_league" >&2
    exit 1
fi
if [[ ! -f $kag_config ]]; then
    printf 'Config not found: %s\n' "$kag_config" >&2
    exit 1
fi

kag_reference_checkpoint=$(find "$kag_league" -maxdepth 1 -type f \
    -name '*.bin' -print -quit)
if [[ -z $kag_reference_checkpoint ]]; then
    printf 'No active checkpoints in %s\n' "$kag_league" >&2
    exit 1
fi
kag_reference_bytes=$(stat -c %s "$kag_reference_checkpoint")
mapfile -t kag_run_files_all < <(find "$kag_run" -maxdepth 1 -type f \
    -regextype posix-extended -regex '.*/[0-9]{16}\.bin' -print | sort)
kag_run_files=()
for kag_path in "${kag_run_files_all[@]}"; do
    [[ $(stat -c %s "$kag_path") == "$kag_reference_bytes" ]] \
        && kag_run_files+=("$kag_path")
done
if ((${#kag_run_files[@]} != ${#kag_run_files_all[@]})); then
    printf 'Ignoring %d architecture-incompatible checkpoints in %s\n' \
        "$(( ${#kag_run_files_all[@]} - ${#kag_run_files[@]} ))" "$kag_run"
fi
if ((${#kag_run_files[@]} < 2)); then
    printf 'Need at least two checkpoints in %s\n' "$kag_run" >&2
    exit 1
fi
kag_run_id=${kag_run##*/}
kag_last_name=${kag_run_files[${#kag_run_files[@]}-1]##*/}
kag_last_step=$((10#${kag_last_name%.bin}))
if [[ -n $kag_reuse ]]; then
    kag_output=$kag_reuse
elif [[ -z $kag_output ]]; then
    kag_output="logs/kaggriculture/psro_${kag_run_id}_${kag_last_step}"
fi
kag_tmp=$(mktemp -d)
trap 'rm -r "$kag_tmp"' EXIT

printf 'PSRO %s: run=%s checkpoints=%d through %.2fM range=%s games=%d\n' \
    "$kag_mode" "$kag_run_id" "${#kag_run_files[@]}" \
    "$(awk -v n="$kag_last_step" 'BEGIN {print n/1000000}')" "$kag_range" "$kag_games"

if [[ -z $kag_reuse ]]; then
    if [[ ! -x ./puffer || src/pufferl.cu -nt ./puffer \
            || ocean/kaggriculture/kaggriculture.c -nt ./puffer ]]; then
        CUDA_HOME="${CUDA_HOME:-/usr/local/cuda}" bash build.sh kaggriculture
    fi

    kag_coarse_manifest="${kag_output}_coarse_manifest.tsv"
    kag_opponent_manifest="${kag_output}_active_manifest.tsv"
    kag_prescreen_raw="${kag_output}_prescreen_raw.tsv"
    kag_prescreen_ranking="${kag_output}_prescreen_ranking.tsv"
    printf 'id\tpolicy\tcheckpoint\n' > "$kag_coarse_manifest"
    declare -A kag_coarse_seen=()
    kag_coarse_count=0
    kag_coarse_first=$(((kag_range_start * (${#kag_run_files[@]} - 1) + 50) / 100))
    kag_coarse_last=$(((kag_range_end * (${#kag_run_files[@]} - 1) + 50) / 100))
    for ((kag_pick=0; kag_pick<kag_range_count; kag_pick++)); do
        kag_idx=$((kag_coarse_first + kag_pick * (kag_coarse_last - kag_coarse_first) / (kag_range_count - 1)))
        kag_path=${kag_run_files[kag_idx]}
        [[ -n ${kag_coarse_seen["$kag_path"]+x} ]] && continue
        kag_coarse_seen["$kag_path"]=1
        kag_step_name=${kag_path##*/}
        kag_step=$((10#${kag_step_name%.bin}))
        kag_policy=$(awk -v n="$kag_step" -v run="$kag_run_id" \
            'BEGIN {printf "%s@%.2fM", run, n/1000000}')
        printf '%d\t%s\t%s\n' "$kag_coarse_count" "$kag_policy" "$kag_path" \
            >> "$kag_coarse_manifest"
        kag_coarse_count=$((kag_coarse_count + 1))
    done

    printf 'id\tpolicy\tweight\tcheckpoint\n' > "$kag_opponent_manifest"
    kag_active_paths=()
    while IFS= read -r kag_path; do
        kag_policy=${kag_path##*/}
        kag_policy=${kag_policy%.bin}
        kag_weight=1
        if [[ -f $kag_league/manifest.tsv ]]; then
            kag_manifest_weight=$(awk -F '\t' -v policy="$kag_policy" \
                'NR>1 && $1==policy {print $3; exit}' \
                "$kag_league/manifest.tsv")
            [[ -z $kag_manifest_weight ]] || kag_weight=$kag_manifest_weight
        fi
        printf '%d\t%s\t%s\t%s\n' "${#kag_active_paths[@]}" "$kag_policy" \
            "$kag_weight" "$kag_path" \
            >> "$kag_opponent_manifest"
        kag_active_paths+=("$kag_path")
    done < <(find "$kag_league" -maxdepth 1 -type f -name '*.bin' -print | sort)
    if ((${#kag_active_paths[@]} < 2 || ${#kag_active_paths[@]} > 8)); then
        printf 'Fast PSRO requires 2..8 active league policies; found %d in %s\n' \
            "${#kag_active_paths[@]}" "$kag_league" >&2
        exit 1
    fi

    printf 'Coarse screen: candidates=%d active=%d requested_games=%d min_agents=%d\n' \
        "$kag_coarse_count" "${#kag_active_paths[@]}" \
        "$kag_prescreen_games" "$kag_prescreen_agents"
    ./puffer league kaggriculture \
        league.mode=screen \
        "league.candidate_manifest=$kag_coarse_manifest" \
        "league.opponent_manifest=$kag_opponent_manifest" \
        "league.output=$kag_prescreen_raw" \
        "league.games=$kag_prescreen_games" \
        "league.min_agents=$kag_prescreen_agents" \
        "base.eval_deterministic=$kag_eval_deterministic" \
        base.seed=6100

    kag_prescreen_scores="$kag_tmp/prescreen_scores.tsv"
    awk -F '\t' -v count="$kag_coarse_count" '
        NR==FNR {
            if (FNR>1) weight[$1]=$3
            next
        }
        {sum[$1]+=$3*weight[$2]; mass[$1]+=weight[$2]}
        END {
            for (i=0; i<count; i++) score[i]=mass[i] ? sum[i]/mass[i] : 0
            for (i=0; i<count; i++) {
                left=(i==0 || score[i]>=score[i-1])
                right=(i==count-1 || score[i]>=score[i+1])
                printf "%d\t%.9f\t%d\n", i,score[i],left&&right
            }
        }
    ' "$kag_opponent_manifest" "$kag_prescreen_raw" \
        > "$kag_prescreen_scores"
    {
        printf 'rank\tpolicy\tmean_score\tlocal_peak\tcheckpoint\n'
        sort -t$'\t' -k2,2gr "$kag_prescreen_scores" | while IFS=$'\t' \
                read -r kag_id kag_score kag_peak; do
            IFS=$'\t' read -r _ kag_policy kag_path \
                < <(awk -F '\t' -v id="$kag_id" 'NR>1 && $1==id {print; exit}' \
                    "$kag_coarse_manifest")
            printf '%s\t%s\t%s\t%s\n' "$kag_policy" "$kag_score" "$kag_peak" "$kag_path"
        done | awk -F '\t' 'BEGIN{OFS="\t"} {print NR,$0}'
    } > "$kag_prescreen_ranking"

    kag_selected_ids=()
    declare -A kag_selected_id=()
    while IFS=$'\t' read -r kag_id _; do
        ((${#kag_selected_ids[@]} < kag_shortlist)) || break
        kag_selected_ids+=("$kag_id")
        kag_selected_id["$kag_id"]=1
    done < <(awk -F '\t' '$3==1 {print $1"\t"$2}' "$kag_prescreen_scores" \
        | sort -t$'\t' -k2,2gr)
    while IFS=$'\t' read -r kag_id _; do
        ((${#kag_selected_ids[@]} < kag_shortlist)) || break
        [[ -n ${kag_selected_id["$kag_id"]+x} ]] && continue
        kag_selected_ids+=("$kag_id")
        kag_selected_id["$kag_id"]=1
    done < <(sort -t$'\t' -k2,2gr "$kag_prescreen_scores")

    kag_selected_paths=()
    printf '%s\n' 'Shortlisted checkpoint peaks:'
    for kag_id in "${kag_selected_ids[@]}"; do
        IFS=$'\t' read -r _ kag_policy kag_path \
            < <(awk -F '\t' -v id="$kag_id" 'NR>1 && $1==id {print; exit}' \
                "$kag_coarse_manifest")
        kag_selected_paths+=("$kag_path")
        kag_score=$(awk -F '\t' -v id="$kag_id" '$1==id {print $2}' \
            "$kag_prescreen_scores")
        printf '  %s score=%s\n' "$kag_policy" "$kag_score"
    done

    ./ocean/kaggriculture/eval_population.sh \
        --games "$kag_games" --jobs "$kag_jobs" --gpu-agents "$kag_gpu_agents" \
        --fixed "$kag_fixed" \
        --cache "$kag_league/payoffs.tsv" \
        --focal-count "${#kag_selected_paths[@]}" \
        --output "$kag_output" "${kag_eval_mode_args[@]}" \
        "${kag_selected_paths[@]}" "$kag_league"
else
    for kag_required in manifest matrix ranking fixed; do
        [[ -f ${kag_output}_${kag_required}.tsv ]] || {
            printf 'Missing reused result: %s_%s.tsv\n' "$kag_output" "$kag_required" >&2
            exit 1
        }
    done
    printf 'Reusing screen %s\n' "$kag_output"
fi

kag_solver=ocean/kaggriculture/build/kaggriculture_metagame
if [[ ! -x $kag_solver || ocean/kaggriculture/metagame.c -nt $kag_solver ]]; then
    make -C ocean/kaggriculture metagame
fi
if [[ ! -x ./kaggriculture || ocean/kaggriculture/kaggriculture.c -nt ./kaggriculture ]]; then
    CUDA_HOME="${CUDA_HOME:-/usr/local/cuda}" bash build.sh kaggriculture --fast
fi
kag_print_meta() {
    local kag_label=$1 kag_path=$2
    printf '%s\n' "$kag_label"
    {
        printf 'policy\tweight\tresponse\n'
        awk -F '\t' 'NR > 1 && $1 !~ /^#/ {print}' "$kag_path" \
            | sort -t$'\t' -k2,2gr | sed -n '1,12p'
    } | if command -v column >/dev/null 2>&1; then column -t -s $'\t'; else cat; fi
    awk -F '\t' '$1 == "# exploitability" {printf "meta_exploitability=%s\n", $2}' "$kag_path"
}

kag_screen_meta="${kag_output}_screen_metagame.tsv"
"$kag_solver" "${kag_output}_matrix.tsv" > "$kag_screen_meta"
kag_print_meta 'Screened meta-strategy:' "$kag_screen_meta"

kag_behavior_jsd="${kag_output}_behavior_jsd.tsv"
kag_diversity="${kag_output}_diversity.tsv"
kag_selected="${kag_output}_selected.tsv"
kag_candidate_prefix="${kag_run_id}@"
kag_jsd_specs=()
while IFS=$'\t' read -r _ kag_policy kag_path; do
    [[ $kag_policy == policy ]] && continue
    kag_jsd_specs+=("${kag_policy}=${kag_path}")
done < "${kag_output}_manifest.tsv"
printf 'Probing masked recurrent behavior JSD: policies=%d steps=%d seeds=%d\n' \
    "${#kag_jsd_specs[@]}" "$kag_jsd_steps" "$kag_jsd_seeds"
KAG_JSD_SEEDS="$kag_jsd_seeds" \
    ./kaggriculture jsd "$kag_jsd_steps" \
    "${kag_jsd_specs[@]}" > "$kag_behavior_jsd"
"$kag_solver" diversity "${kag_output}_matrix.tsv" "$kag_behavior_jsd" \
    "$kag_candidate_prefix" 16 "$kag_quality_gap" > "$kag_diversity"

kag_economy_policy=$(awk -F '\t' -v prefix="$kag_candidate_prefix" \
    -v gap="$kag_quality_gap" '
    NR == FNR {
        if (FNR > 1) {
            quality[$2]=$3
            if (index($2, prefix) == 1 && $3 > best) best=$3
        }
        next
    }
    FNR > 1 && $2 == "rules" && index($1, prefix) == 1 \
        && quality[$1] >= best-gap && $5 > money {
            money=$5; policy=$1
        }
    END {print policy}
' "${kag_output}_ranking.tsv" "${kag_output}_fixed.tsv")

{
    printf 'policy\trole\tquality\tpayoff_jsd\tbehavior_jsd\tcombined_jsd\n'
    awk -F '\t' -v economy="$kag_economy_policy" -v limit="$kag_max_admit" '
        FNR == NR {
            if (FNR > 1) {
                line[$1]=$0
                order[++n]=$1
            }
            next
        }
        END {
            count=0
            if (n && !seen[order[1]]++) {
                split(line[order[1]], f, "\t")
                printf "%s\tmeta\t%s\t%s\t%s\t%s\n", f[1],f[2],f[3],f[4],f[5]
                count++
            }
            if (economy != "" && count < limit && !seen[economy]++) {
                split(line[economy], f, "\t")
                printf "%s\teconomy\t%s\t%s\t%s\t%s\n", f[1],f[2],f[3],f[4],f[5]
                count++
            }
            for (i=2; i<=n && count<limit; i++) if (!seen[order[i]]++) {
                split(line[order[i]], f, "\t")
                printf "%s\tdiversity\t%s\t%s\t%s\t%s\n", f[1],f[2],f[3],f[4],f[5]
                count++
            }
        }
    ' "$kag_diversity" /dev/null
} > "$kag_selected"
printf '%s\n' 'Quality-gated diverse candidates:'
if command -v column >/dev/null 2>&1; then
    column -t -s $'\t' "$kag_selected"
else
    cat "$kag_selected"
fi

kag_meta=$kag_screen_meta
kag_meta_manifest="${kag_output}_manifest.tsv"
if ((kag_confirm_games)); then
    kag_support_all="$kag_tmp/support_all.tsv"
    awk -F '\t' '
        NR == FNR {
            if (FNR > 1 && $1 !~ /^#/) weight[$1] = $2
            next
        }
        FNR > 1 {printf "%.9f\t%s\t%s\n", weight[$2], $2, $3}
    ' "$kag_screen_meta" "${kag_output}_manifest.tsv" \
        | sort -t$'\t' -k1,1gr > "$kag_support_all"
    kag_support="$kag_tmp/support.tsv"
    awk -F '\t' '
        NR == FNR {if (FNR > 1) selected[$1]=1; next}
        FNR <= 2 || (FNR <= 8 && $1 >= 0.005) || selected[$2] {
            if (!seen[$3]++) print
        }
    ' "$kag_selected" "$kag_support_all" > "$kag_support"
    kag_support_paths=()
    while IFS=$'\t' read -r _ _ kag_path; do
        kag_support_paths+=("$kag_path")
    done < "$kag_support"
    kag_confirm_output="${kag_output}_confirm"
    printf 'Confirming %d support policies with %d games/pair\n' \
        "${#kag_support_paths[@]}" "$kag_confirm_games"
    ./ocean/kaggriculture/eval_population.sh --games "$kag_confirm_games" \
        --jobs "$kag_jobs" --gpu-agents "$kag_gpu_agents" --fixed none \
        --output "$kag_confirm_output" "${kag_eval_mode_args[@]}" \
        "${kag_support_paths[@]}"
    kag_meta="${kag_output}_metagame.tsv"
    "$kag_solver" "${kag_confirm_output}_matrix.tsv" > "$kag_meta"
    kag_meta_manifest="${kag_confirm_output}_manifest.tsv"
    kag_print_meta 'Confirmed meta-strategy:' "$kag_meta"
else
    kag_meta="${kag_output}_metagame.tsv"
    cp "$kag_screen_meta" "$kag_meta"
fi

# The learner is selected from the confirmed meta-strategy, restricted to the
# candidate run.  This is deliberately different from "last checkpoint": a
# late checkpoint can be behaviorally novel while being strategically weak.
kag_learner_policy=$(awk -F '\t' -v prefix="$kag_candidate_prefix" '
    NR > 1 && $1 !~ /^#/ && index($1, prefix) == 1 {
        if (($2 + 0) > best) {best = $2 + 0; policy = $1}
    }
    END {print policy}
' "$kag_meta")
if [[ -z $kag_learner_policy ]]; then
    kag_learner_policy=$(awk -F '\t' -v prefix="$kag_candidate_prefix" '
        NR > 1 && index($2, prefix) == 1 {print $2; exit}
    ' "${kag_output}_ranking.tsv")
fi
kag_learner_weight=$(awk -F '\t' -v policy="$kag_learner_policy" \
    'NR > 1 && $1 == policy {print $2; exit}' "$kag_meta")
kag_learner_source=$(awk -F '\t' -v policy="$kag_learner_policy" \
    'NR > 1 && $2 == policy {print $3; exit}' "$kag_meta_manifest")
if [[ -n $kag_learner_source ]]; then
    printf 'Auto-selected learner: run=%s policy=%s weight=%s source=%s\n' \
        "$kag_run_id" "$kag_learner_policy" "${kag_learner_weight:-0}" \
        "$kag_learner_source"
fi

if [[ $kag_mode == analyze ]]; then
    printf 'Analysis only; active league unchanged. Meta-strategy: %s\n' "$kag_meta"
    exit 0
fi

declare -A kag_meta_weight=() kag_selected_role=()
while IFS=$'\t' read -r kag_policy kag_weight _; do
    [[ $kag_policy == policy || $kag_policy == \#* ]] && continue
    kag_meta_weight["$kag_policy"]=$kag_weight
done < "$kag_meta"
while IFS=$'\t' read -r kag_policy kag_role _; do
    [[ $kag_policy == policy ]] && continue
    kag_selected_role["$kag_policy"]=$kag_role
done < "$kag_selected"

kag_candidates="$kag_tmp/candidates.tsv"
while IFS=$'\t' read -r _ kag_policy kag_source; do
    [[ $kag_policy == policy || $kag_source != "$kag_run/"* ]] && continue
    kag_weight=${kag_meta_weight["$kag_policy"]:-0}
    kag_role=${kag_selected_role["$kag_policy"]:-}
    if [[ -z $kag_role ]]; then
        awk -v x="$kag_weight" -v y="$kag_min_weight" \
            'BEGIN {exit !(x >= y)}' || continue
        kag_role=meta
    fi
    printf '%s\t%s\t%s\t%s\n' "$kag_role" "$kag_weight" \
        "$kag_policy" "$kag_source" >> "$kag_candidates"
done < "$kag_meta_manifest"

# The confirm manifest is not weight-sorted; admission below is capped by
# max_admit, so sort by confirmed meta weight first. Otherwise a capped
# admission can skip the strongest support policy and fall back to a weaker
# active league member for the next learner.
sort -t $'\t' -k2,2gr "$kag_candidates" -o "$kag_candidates"

kag_archive="$kag_archive_root/iteration_$(date +%Y%m%d_%H%M%S)"
mkdir -p "${kag_archive%/*}"
cp -a "$kag_league" "$kag_archive"
printf 'Archived previous league at %s\n' "$kag_archive"

declare -A kag_policy_label=() kag_policy_role=() kag_policy_pinned=()
if [[ -f $kag_league/manifest.tsv ]]; then
    while IFS=$'\t' read -r kag_policy kag_role _; do
        [[ $kag_policy == policy ]] && continue
        if [[ $kag_role == *divers* || $kag_role == *econom* ]]; then
            kag_policy_role["$kag_policy"]=diversity
        elif [[ $kag_role == champion ]]; then
            kag_policy_pinned["$kag_policy"]=1
        fi
    done < "$kag_league/manifest.tsv"
fi
for kag_path in "$kag_league"/*.bin; do
    kag_policy=${kag_path##*/}
    kag_policy=${kag_policy%.bin}
    kag_policy_label["$kag_policy"]=$kag_policy
    : "${kag_policy_role["$kag_policy"]:=exploration}"
    while IFS=$'\t' read -r _ kag_label kag_source; do
        [[ $kag_label == policy ]] && continue
        if cmp -s "$kag_path" "$kag_source"; then
            kag_policy_label["$kag_policy"]=$kag_label
            break
        fi
    done < "$kag_meta_manifest"
done

kag_admitted=0
while IFS=$'\t' read -r kag_role kag_weight kag_policy kag_source; do
    ((kag_admitted < kag_max_admit)) || break
    kag_duplicate=
    for kag_existing in "$kag_league"/*.bin; do
        if cmp -s "$kag_source" "$kag_existing"; then
            kag_duplicate=$kag_existing
            break
        fi
    done
    if [[ -n $kag_duplicate ]]; then
        kag_existing_policy=${kag_duplicate##*/}
        kag_existing_policy=${kag_existing_policy%.bin}
        kag_policy_label["$kag_existing_policy"]=$kag_policy
        if [[ $kag_role != meta ]]; then
            kag_policy_role["$kag_existing_policy"]=diversity
        fi
        continue
    fi
    kag_source_name=${kag_source##*/}
    kag_source_step=${kag_source_name%.bin}
    kag_dest_policy="run_${kag_run_id}_${kag_source_step}"
    kag_dest="$kag_league/$kag_dest_policy.bin"
    cp "$kag_source" "$kag_dest"
    kag_policy_label["$kag_dest_policy"]=$kag_policy
    kag_policy_role["$kag_dest_policy"]=$([[ $kag_role == meta ]] \
        && printf meta || printf diversity)
    printf 'Admitted %s role=%s meta_weight=%s from %s\n' \
        "$kag_dest_policy" "$kag_role" "$kag_weight" "$kag_source"
    kag_admitted=$((kag_admitted + 1))
done < "$kag_candidates"

kag_active_raw="$kag_tmp/active_raw.tsv"
for kag_path in "$kag_league"/*.bin; do
    kag_policy=${kag_path##*/}
    kag_policy=${kag_policy%.bin}
    kag_label=${kag_policy_label["$kag_policy"]:-$kag_policy}
    kag_signal=${kag_meta_weight["$kag_label"]:-0}
    kag_role=${kag_policy_role["$kag_policy"]:-exploration}
    if awk -v x="$kag_signal" -v y="$kag_min_weight" \
            'BEGIN {exit !(x >= y)}'; then
        kag_role=meta
    elif [[ $kag_role != diversity ]]; then
        kag_role=exploration
    fi
    printf '%s\t%s\t%s\n' "$kag_policy" "$kag_role" "$kag_signal" \
        >> "$kag_active_raw"
done

kag_keep="$kag_tmp/keep.tsv"
kag_allocate_weights() {
    awk -F '\t' -v meta_share="$kag_meta_share" \
        -v diversity_share="$kag_diversity_share" \
        -v exploration_share="$kag_exploration_share" '
        {policy[NR]=$1; role[NR]=$2; signal[NR]=$3; count[$2]++; sum[$2]+=$3}
        END {
            available=(count["meta"] ? meta_share : 0) \
                +(count["diversity"] ? diversity_share : 0) \
                +(count["exploration"] ? exploration_share : 0)
            for (i=1; i<=NR; i++) {
                share=role[i]=="meta" ? meta_share \
                    : role[i]=="diversity" ? diversity_share : exploration_share
                share/=available
                if (role[i]=="meta" && sum[role[i]]>0) weight=share*signal[i]/sum[role[i]]
                else weight=share/count[role[i]]
                printf "%s\t%s\t%.12f\t%.12f\n", policy[i],role[i],signal[i],weight
            }
        }
    '
}
kag_weighted_all="$kag_tmp/weighted_all.tsv"
kag_allocate_weights < "$kag_active_raw" | sort -t$'\t' -k4,4gr \
    > "$kag_weighted_all"
sed -n "1,${kag_max_league}p" "$kag_weighted_all" > "$kag_keep"
kag_pinned_file="$kag_tmp/pinned.tsv"
: > "$kag_pinned_file"
for kag_policy in "${!kag_policy_pinned[@]}"; do
    printf '%s\n' "$kag_policy" >> "$kag_pinned_file"
done
for kag_policy in "${!kag_policy_pinned[@]}"; do
    awk -F '\t' -v policy="$kag_policy" '$1==policy {found=1} END {exit !found}' \
        "$kag_keep" && continue
    kag_remove=$(awk -F '\t' '
        NR==FNR {pinned[$1]=1; next}
        !pinned[$1] {candidate=$1}
        END {print candidate}
    ' "$kag_pinned_file" "$kag_keep")
    [[ -n $kag_remove ]] || continue
    kag_keep_tmp="$kag_tmp/keep_replace.tsv"
    awk -F '\t' -v remove="$kag_remove" '$1!=remove' "$kag_keep" \
        > "$kag_keep_tmp"
    awk -F '\t' -v policy="$kag_policy" '$1==policy' "$kag_weighted_all" \
        >> "$kag_keep_tmp"
    mv "$kag_keep_tmp" "$kag_keep"
    printf 'Pinned champion %s replaced %s in active league\n' \
        "$kag_policy" "$kag_remove"
done

declare -A kag_keep_policy=()
while IFS=$'\t' read -r kag_policy _; do kag_keep_policy["$kag_policy"]=1; done < "$kag_keep"
for kag_path in "$kag_league"/*.bin; do
    kag_policy=${kag_path##*/}
    kag_policy=${kag_policy%.bin}
    if [[ -z ${kag_keep_policy["$kag_policy"]+x} ]]; then
        rm "$kag_path"
        printf 'Pruned %s (recoverable from %s)\n' "$kag_policy" "$kag_archive"
    fi
done

kag_weights="$kag_tmp/weights.tsv"
cut -f1-3 "$kag_keep" | kag_allocate_weights \
    | sort -t$'\t' -k4,4gr > "$kag_weights"

kag_manifest_tmp=$(mktemp "$kag_league/.manifest.XXXXXX")
{
    printf 'policy\trole\tbase_weight\tsource\n'
    while IFS=$'\t' read -r kag_policy kag_role _ kag_weight; do
        if [[ -n ${kag_policy_pinned["$kag_policy"]+x} ]]; then
            kag_role=champion
        fi
        printf '%s\t%s\t%s\t%s/%s.bin\n' \
            "$kag_policy" "$kag_role" "$kag_weight" "$kag_league" "$kag_policy"
    done < "$kag_weights"
} > "$kag_manifest_tmp"
mv "$kag_manifest_tmp" "$kag_league/manifest.tsv"

kag_league_ini_tmp=$(mktemp "$kag_league/.league.XXXXXX")
{
    printf '[league]\nmax_active = %d\n\n' "$kag_max_league"
    while IFS=$'\t' read -r kag_policy kag_role _ kag_weight; do
        if [[ -n ${kag_policy_pinned["$kag_policy"]+x} ]]; then
            kag_role=champion
        fi
        printf '[policy.%s]\n' "$kag_policy"
        printf "path = '%s/%s.bin'\n" "$kag_league" "$kag_policy"
        printf "role = '%s'\n" "$kag_role"
        printf 'train_weight = %s\n' "$kag_weight"
        printf 'enabled = 1\n\n'
    done < "$kag_weights"
} > "$kag_league_ini_tmp"
mv "$kag_league_ini_tmp" "$kag_league/league.ini"

kag_pool=$(awk -F '\t' -v root="$kag_league" \
    'BEGIN {ORS=""} {sep = NR > 1 ? "," : ""; printf "%s%s/%s.bin", sep, root, $1}' "$kag_weights")
kag_learner_active=
if [[ -n $kag_learner_source && -f $kag_learner_source ]]; then
    for kag_path in "$kag_league"/*.bin; do
        if cmp -s "$kag_learner_source" "$kag_path"; then
            kag_learner_active=$kag_path
            break
        fi
    done
fi
if [[ -z $kag_learner_active ]]; then
    # A quality-gated candidate can be admitted and then lose the active-league
    # prune. Never resume from that rejected branch: continue from the
    # highest-weight policy that actually survived in the active league.
    kag_fallback_policy=$(awk -F '\t' 'NR==1 {print $1}' "$kag_weights")
    kag_learner_active="$kag_league/$kag_fallback_policy.bin"
    printf 'Candidate learner was not retained; using active policy %s\n' \
        "$kag_fallback_policy"
fi
kag_config_dir=${kag_config%/*}
[[ $kag_config_dir == "$kag_config" ]] && kag_config_dir=.
kag_config_tmp=$(mktemp "$kag_config_dir/.kaggriculture.XXXXXX")
awk -v league="$kag_league/league.ini" -v learner="$kag_learner_active" '
    /^load_model_path =/ && learner != "" {
        printf "load_model_path = \047%s\047\n", learner; found=1; next
    }
    /^opponent_pool =/ {print "opponent_pool = \047None\047"; next}
    /^opponent_league =/ {
        printf "opponent_league = \047%s\047\n", league; next
    }
    # Per-policy base weights now live in league.ini; PFSP multiplies them by
    # the online matchup priority instead of discarding the solved allocation.
    /^opponent_pool_weights =/ {print "opponent_pool_weights = \047None\047"; next}
    {print}
' "$kag_config" > "$kag_config_tmp"
mv "$kag_config_tmp" "$kag_config"

if [[ -n $kag_learner_active ]]; then
    kag_learner_marker="$kag_league/learner.tsv"
    {
        printf 'run_id\tcandidate_policy\tconfirmed_meta_weight\tsource_checkpoint\tactive_checkpoint\n'
        printf '%s\t%s\t%s\t%s\t%s\n' "$kag_run_id" "$kag_learner_policy" \
            "${kag_learner_weight:-0}" "$kag_learner_source" "$kag_learner_active"
    } > "$kag_learner_marker"
    printf 'Updated %s: load_model_path=%s\n' "$kag_config" "$kag_learner_active"
    printf 'Learner marker: %s\n' "$kag_learner_marker"
fi

if ((kag_profile_games)); then
    ./ocean/kaggriculture/profile_population_gpu.sh \
        --games "$kag_profile_games" --jobs "$kag_jobs" --gpu-agents "$kag_gpu_agents" \
        --output "${kag_output}_profile" "$kag_league"
fi

printf 'PSRO iteration complete: admitted=%d active=%d\n' "$kag_admitted" "$(wc -l < "$kag_weights")"
printf 'Future training will use learner=%s and the updated active league; an already-running trainer is unchanged.\n' \
    "${kag_learner_active:-unchanged}"
printf 'Next command: ./puffer train kaggriculture\n'
