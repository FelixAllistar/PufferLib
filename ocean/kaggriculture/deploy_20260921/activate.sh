#!/usr/bin/env bash
# User-authorized, recoverable in-place promotion. Run in the user's tmux.
set -euo pipefail
kag_active=/workspace/PufferLib
kag_stage=/workspace/PufferLib-multi-intent
[[ $(realpath "$kag_active") == /workspace/PufferLib ]]
[[ $(realpath "$kag_stage") == /workspace/PufferLib-multi-intent ]]
if pgrep -x puffer >/dev/null || pgrep -x kag_bc >/dev/null; then
    echo 'Refusing to update while a trainer is running.' >&2
    exit 1
fi
[[ -f "$kag_stage/qualification/baseline_v2_checks/PASS.txt" ]]
[[ -x "$kag_stage/ocean/kaggriculture/build/qualification_20260921/kag_bc" ]]
[[ -f "$kag_active/ocean/kaggriculture/state_bank/diverse_20260913/full.kgb" ]]
cd "$kag_active"
mkdir -p /workspace/PufferLib-backups
kag_backup=$(mktemp -d /workspace/PufferLib-backups/pre-multi-intent.XXXXXX)
exec > >(tee "$kag_backup/upgrade.log") 2>&1
printf 'Active install: %s\nRecoverable backup: %s\n' "$kag_active" "$kag_backup"
echo 'Saving old runtime/config/source. Existing checkpoints, logs and reset banks stay in place.'
tar --exclude='ocean/kaggriculture/state_bank' \
    --exclude='ocean/kaggriculture/reference' \
    --exclude='ocean/kaggriculture/_reset_deps' \
    --exclude='*/__pycache__' \
    -czf "$kag_backup/old-runtime.tar.gz" \
    src config build.sh puffer kaggriculture ocean/kaggriculture
tar -tzf "$kag_backup/old-runtime.tar.gz" > "$kag_backup/archive-files.txt"
sha256sum config/kaggriculture.ini puffer kaggriculture \
    > "$kag_backup/old-launcher-sha256.txt"
cp -a config/kaggriculture.ini "$kag_backup/old-kaggriculture.ini"
echo 'Overlaying qualified sources without deleting remote-only files or experiment data.'
rsync -a "$kag_stage/src/" "$kag_active/src/"
rsync -a --exclude=build --exclude=reference --exclude=__pycache__ \
    --exclude=state_bank --exclude=_reset_deps \
    "$kag_stage/ocean/kaggriculture/" "$kag_active/ocean/kaggriculture/"
mkdir -p ocean/pokemon
rsync -a "$kag_stage/ocean/pokemon/" ocean/pokemon/
cp -a "$kag_stage/build.sh" build.sh
cp -a "$kag_stage/config/default.ini" config/default.ini
mkdir -p data qualification/production_20260921
rsync -a "$kag_stage/data/entity_2_2_policy5_baseline_v2."* data/
rsync -a "$kag_stage/ocean/kaggriculture/build/qualification_20260921/" \
    ocean/kaggriculture/build/qualification_20260921/
cp -a "$kag_stage/qualification/baseline_v2_checks" qualification/
printf '%s\n' "$kag_backup" > qualification/production_20260921/backup_path.txt
echo 'Building in /workspace/PufferLib, with normal Raylib dependencies.'
echo 'The old ./puffer and its config remain active until this compile succeeds.'
NVCC_ARCH=sm_120 NATIVE_OUTPUT_NAME=build/puffer-multi-intent-20260921 \
    bash build.sh kaggriculture --gpu
./build/puffer-multi-intent-20260921 check kaggriculture \
    env.macro_executor_version=2 env.land_buy_min_days=0 vec.action_mask_size=1978
if pgrep -x puffer >/dev/null || pgrep -x kag_bc >/dev/null; then
    echo 'A trainer started during the build; publishing deferred.' >&2
    exit 1
fi
echo 'Publishing the new ./puffer, BC trainer and active 2/2 config.'
cp -a ocean/kaggriculture/deploy_20260921/active_2_2.ini config/kaggriculture.ini.multi-next
mv -f config/kaggriculture.ini.multi-next config/kaggriculture.ini
mv -f build/puffer-multi-intent-20260921 puffer
cp -a ocean/kaggriculture/build/qualification_20260921/kag_bc \
    ocean/kaggriculture/build/kag_bc.multi-next
mv -f ocean/kaggriculture/build/kag_bc.multi-next ocean/kaggriculture/build/kag_bc
./puffer check kaggriculture
ocean/kaggriculture/build/kag_bc bc.verify_only=1
sha256sum puffer config/kaggriculture.ini ocean/kaggriculture/build/kag_bc \
    data/entity_2_2_policy5_baseline_v2.bc > qualification/production_20260921/active-sha256.txt
printf 'INSTALLED: /workspace/PufferLib is now 2/2, H512/L3, policy ABI 5.\nBackup: %s\n' "$kag_backup"
