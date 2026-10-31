#!/usr/bin/env bash
# Fetch the ROMs (2026-10-31) from the private repo FelixAllistar/puffer-roms into their gitignored places:
# ocean/retro/roms/smb1_ntsc.nes (Mario) and roms/pokered.gb (Gen 1 cartridge checks). They are never committed here.
# Access: PUFFER_ROMS_SSH_KEY in .env (a read-only deploy key of that repo), else the machine's own GitHub login (gh).
# Checks the SHA-1 of each file. Usage: scripts/fetch_roms.sh
set -euo pipefail
cd "$(dirname "$0")/.."
[ -f .env ] && set -a && . ./.env && set +a
REPO=FelixAllistar/puffer-roms
TMP=$(mktemp -d); trap 'rm -rf "$TMP"' EXIT
if [ -n "${PUFFER_ROMS_SSH_KEY:-}" ]; then
    GIT_SSH_COMMAND="ssh -i $PUFFER_ROMS_SSH_KEY -o IdentitiesOnly=yes -o StrictHostKeyChecking=accept-new" \
        git clone -q --depth 1 "git@github.com:$REPO.git" "$TMP/r"
else
    gh repo clone "$REPO" "$TMP/r" -- -q --depth 1
fi
check() { [ "$(sha1sum "$1" | cut -d' ' -f1)" = "$2" ] || { echo "SHA-1 mismatch: $1" >&2; exit 1; }; }
check "$TMP/r/smb1_ntsc.nes" 33d23c2f2cfa4c9efec87f7bc1321ce3ce6c89bd
check "$TMP/r/pokered.gb" ea9bcae617fdf159b045185467ae58b2e4a48b9a
mkdir -p ocean/retro/roms roms
cp "$TMP/r/smb1_ntsc.nes" ocean/retro/roms/smb1_ntsc.nes
cp "$TMP/r/pokered.gb" roms/pokered.gb
echo "ROMs in place: ocean/retro/roms/smb1_ntsc.nes, roms/pokered.gb"
