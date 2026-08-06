#!/bin/bash
set -euo pipefail

cd "$(dirname "$0")/../.."

kag_games=20
kag_jobs=4
kag_output=logs/kaggriculture/policy_profile
kag_stage=4
kag_preunlocked_land=0
kag_inputs=()

kag_usage() {
    printf '%s\n' \
        "Usage: $0 [options] [CHECKPOINT_OR_DIRECTORY ...]" \
        "" \
        "Profile native policy behavior against the rules bot without touching training." \
        "The report is bench-only: no hot-path counters are added to the trainer." \
        "" \
        "Options:" \
        "  --games N          Even total games per policy (default 20)" \
        "  --jobs N           Parallel policy profiles (default 4)" \
        "  --output PREFIX    Output TSV (default logs/kaggriculture/policy_profile)" \
        "  --stage N          Evaluator curriculum stage 1..6 (default 4)" \
        "  --preunlocked-land 0|1|2  Normal, paid 50-tile, or NE-only drill" \
        "" \
        "If no input is given, saved/kaggriculture_league_v3 is profiled." \
        "A league manifest is joined automatically for role and meta weight." \
        "Examples:" \
        "  $0 --games 50 saved/kaggriculture_league_v3" \
        "  $0 --output logs/kaggriculture/psro_X_profile saved/kaggriculture_league_v3"
}

while (($#)); do
    case "$1" in
        --games) kag_games=$2; shift 2 ;;
        --jobs) kag_jobs=$2; shift 2 ;;
        --output) kag_output=$2; shift 2 ;;
        --stage) kag_stage=$2; shift 2 ;;
        --preunlocked-land) kag_preunlocked_land=$2; shift 2 ;;
        -h|--help) kag_usage; exit 0 ;;
        --*) printf 'Unknown option: %s\n' "$1" >&2; kag_usage >&2; exit 2 ;;
        *) kag_inputs+=("$1"); shift ;;
    esac
done

[[ $kag_games =~ ^[0-9]+$ ]] && ((kag_games >= 2 && kag_games % 2 == 0)) \
    || { printf '%s\n' '--games must be an even integer of at least 2' >&2; exit 2; }
[[ $kag_jobs =~ ^[0-9]+$ ]] && ((kag_jobs >= 1)) \
    || { printf '%s\n' '--jobs must be a positive integer' >&2; exit 2; }
[[ $kag_stage =~ ^[1-6]$ ]] \
    || { printf '%s\n' '--stage must be an integer from 1 through 6' >&2; exit 2; }
[[ $kag_preunlocked_land =~ ^[012]$ ]] \
    || { printf '%s\n' '--preunlocked-land must be 0, 1, or 2' >&2; exit 2; }
[[ -x ./kaggriculture ]] \
    || { printf '%s\n' 'Build the native evaluator first: bash build.sh kaggriculture --fast' >&2; exit 1; }

if ((${#kag_inputs[@]} == 0)); then
    kag_inputs=(saved/kaggriculture_league_v3)
fi

declare -A kag_seen=() kag_paths=() kag_roles=() kag_weights=()
kag_order=()
kag_add() {
    local kag_path=$1 kag_base kag_name
    [[ -f $kag_path && $kag_path == *.bin ]] || return
    kag_base=${kag_path##*/}
    kag_name=${kag_base%.bin}
    [[ $kag_name =~ ^[0-9]{16}$ ]] && return
    [[ -n ${kag_seen["$kag_path"]+x} ]] && return
    kag_seen["$kag_path"]=1
    kag_paths["$kag_name"]=$kag_path
    kag_order+=("$kag_name")
}
for kag_input in "${kag_inputs[@]}"; do
    if [[ -f $kag_input ]]; then
        kag_add "$kag_input"
    elif [[ -d $kag_input ]]; then
        while IFS= read -r kag_path; do kag_add "$kag_path"; done \
            < <(find "$kag_input" -type f -name '*.bin' -print | sort)
        if [[ -f $kag_input/manifest.tsv ]]; then
            while IFS=$'\t' read -r kag_policy kag_role kag_weight _; do
                [[ $kag_policy == policy ]] && continue
                kag_roles["$kag_policy"]=$kag_role
                kag_weights["$kag_policy"]=$kag_weight
            done < "$kag_input/manifest.tsv"
        fi
    else
        printf 'Input does not exist: %s\n' "$kag_input" >&2
        exit 1
    fi
done

(( ${#kag_order[@]} >= 1 )) || { printf '%s\n' 'No named .bin policies found' >&2; exit 1; }
(( ${#kag_order[@]} <= 64 )) || { printf 'Refusing %d policies\n' "${#kag_order[@]}" >&2; exit 1; }

kag_steps=$((kag_games / 2 * 720))
kag_tmp=$(mktemp -d)
trap 'rm -r "$kag_tmp"' EXIT
mkdir -p "$(dirname "$kag_output")"

kag_parse_action() {
    local kag_text=$1 kag_side=$2 kag_label=$3
    printf '%s\n' "$kag_text" | awk -v side="$kag_side" -v label="$kag_label" '
        $1 == side && $2 == label {
            for (i = 3; i <= NF; i++) {
                split($i, a, "=")
                if (a[2] != "") value[a[1]] = a[2]
                else if (i < NF) value[a[1]] = $(++i)
            }
            printf "%s %s %s %s %s %s %s %s %s %s %s %s %s %s %s %s %s %s %s %s %s %s %s %s\n",
                value["pass"], value["move"], value["plant"], value["water"],
                value["harvest"], value["build"], value["dig"], value["inv"],
                value["buy"], value["sell"], value["hire"], value["land"],
                value["animal_harvest"], value["fertilize"], value["coop"],
                value["pasture"], value["animal_place"], value["feed"],
                value["care"], value["fert_collect"], value["seed_buy"],
                value["product_buy"], value["animal_buy"], value["fert_sell"]
            exit
        }'
}

kag_parse_summary() {
    awk '/agent-steps\/s/ {
        for (i=1; i<=NF; i++) {
            if ($i ~ /^score=/) {x=$i; sub(/^score=/,"",x)}
            if ($i ~ /^draw=/) {d=$i; sub(/^draw=/,"",d)}
            if ($i ~ /^avg_money=/) {
                a=$i; sub(/^avg_money=\(/,"",a); gsub(/,/,"",a)
                b=$(i+1); gsub(/[(),]/,"",b)
            }
        }
        print x, d, a, b
        exit
    }'
}

kag_profile_one() {
    local kag_label=$1 kag_path=${kag_paths["$1"]}
    local kag_forward kag_reverse kag_fstats kag_rstats kag_fsummary kag_rsummary
    kag_forward=$(KAG_STAGE="$kag_stage" \
        KAG_PREUNLOCKED_LAND="$kag_preunlocked_land" ./kaggriculture bench \
        "$kag_steps" "$kag_path" rules 2>&1)
    kag_reverse=$(KAG_STAGE="$kag_stage" \
        KAG_PREUNLOCKED_LAND="$kag_preunlocked_land" ./kaggriculture bench \
        "$kag_steps" rules "$kag_path" 2>&1)
    kag_fstats=$(kag_parse_action "$kag_forward" P0 "$kag_path")
    kag_rstats=$(kag_parse_action "$kag_reverse" P1 "$kag_path")
    kag_fsummary=$(printf '%s\n' "$kag_forward" | kag_parse_summary)
    kag_rsummary=$(printf '%s\n' "$kag_reverse" | kag_parse_summary)
    [[ -n $kag_fstats && -n $kag_rstats && -n $kag_fsummary && -n $kag_rsummary ]] \
        || { printf 'Profile parse failed for %s\n' "$kag_label" >&2; return 1; }
    awk -v label="$kag_label" -v role="${kag_roles["$kag_label"]:-unassigned}" \
        -v weight="${kag_weights["$kag_label"]:-0}" \
        -v games="$((kag_games / 2))" \
        -v fs="$kag_fstats" -v rs="$kag_rstats" \
        -v fa="$kag_fsummary" -v ra="$kag_rsummary" 'BEGIN {
        n=split(fs,f," "); m=split(rs,r," "); split(fa,fp," "); split(ra,rp," ")
        printf "%s\t%s\t%s\t%d", label, role, weight, games
        for (i=1; i<=n; i++) printf "\t%.4f", (f[i]+r[i])/2
        printf "\t%.4f\t%.4f\t%.1f\n", (fp[1]+1-rp[1])/2, (fp[2]+rp[2])/2, (fp[3]+rp[4])/2
    }' > "$kag_tmp/$kag_label.tsv"
}

kag_wait_slot() {
    while :; do
        (( $(jobs -rp | wc -l) < kag_jobs )) && return
        wait -n || true
    done
}
printf 'Profiling %d policies: %d games/policy (%d/seat), %d jobs\n' \
    "${#kag_order[@]}" "$kag_games" "$((kag_games / 2))" "$kag_jobs"
for kag_label in "${kag_order[@]}"; do
    kag_wait_slot
    kag_profile_one "$kag_label" &
done
wait

{
    printf 'policy\trole\tbase_weight\tgames_per_seat\tpass_pct\tmove_pct\tplant_pct\twater_pct\tharvest_pct\tbuild_pct\tdig_pct\tinv_pct\tbuy_orders\tsell_orders\thire_orders\tland_orders\tanimal_harvest\tfertilize\tcoop_build\tpasture_build\tanimal_place\tfeed\tcare\tfert_collect\tseed_buy\tproduct_buy\tanimal_buy\tfert_sell\twin_rate\tdraw_rate\tmean_money\n'
    for kag_label in "${kag_order[@]}"; do cat "$kag_tmp/$kag_label.tsv"; done
} > "${kag_output}.tsv"
printf 'Wrote %s.tsv\n' "$kag_output"
