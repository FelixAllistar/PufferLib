#!/usr/bin/env bash
set -euo pipefail
SWAT_WRAPPER_SOURCE=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)/run-windows-network-test.sh
SWAT_WRAPPER_TEST_ROOT=$(mktemp -d "${TMPDIR:-/tmp}/swat-wrapper.XXXXXX")
trap 'rm -rf -- "$SWAT_WRAPPER_TEST_ROOT"' EXIT
mkdir -p "$SWAT_WRAPPER_TEST_ROOT/ocean/swat" "$SWAT_WRAPPER_TEST_ROOT/build/swat/windows" "$SWAT_WRAPPER_TEST_ROOT/candidates"
cp -- "$SWAT_WRAPPER_SOURCE" "$SWAT_WRAPPER_TEST_ROOT/ocean/swat/"
SWAT_WRAPPER_RUN=$SWAT_WRAPPER_TEST_ROOT/ocean/swat/run-windows-network-test.sh
SWAT_WRAPPER_STABLE=$SWAT_WRAPPER_TEST_ROOT/build/swat/windows/test_net.exe
SWAT_WRAPPER_CANDIDATE=$SWAT_WRAPPER_TEST_ROOT/candidates/test_frontend_input.exe
# Linux shell stand-ins test path/exit/restore behavior without starting Windows
# programs, requesting firewall permission or touching the real approved path.
printf 'original network QA bytes\n' > "$SWAT_WRAPPER_TEST_ROOT/original"
cp -- "$SWAT_WRAPPER_TEST_ROOT/original" "$SWAT_WRAPPER_STABLE"
printf '#!/usr/bin/env bash\nexit "${1:-0}"\n' > "$SWAT_WRAPPER_CANDIDATE"
chmod +x "$SWAT_WRAPPER_CANDIDATE"
bash "$SWAT_WRAPPER_RUN" "$SWAT_WRAPPER_CANDIDATE" 0 > /dev/null 2>&1
cmp "$SWAT_WRAPPER_TEST_ROOT/original" "$SWAT_WRAPPER_STABLE"
printf 'PASS network wrapper: transient success restores original QA bytes\n'
if bash "$SWAT_WRAPPER_RUN" "$SWAT_WRAPPER_CANDIDATE" 23 > /dev/null 2>&1; then exit 1; else SWAT_WRAPPER_RESULT=$?; fi
[[ $SWAT_WRAPPER_RESULT == 23 ]]
cmp "$SWAT_WRAPPER_TEST_ROOT/original" "$SWAT_WRAPPER_STABLE"
for SWAT_WRAPPER_LEFTOVER in "$SWAT_WRAPPER_STABLE".stage.* "$SWAT_WRAPPER_STABLE".restore.*; do [[ ! -e $SWAT_WRAPPER_LEFTOVER ]]; done
printf 'PASS network wrapper: transient failure preserves exit status and restores original QA bytes\n'
rm -- "$SWAT_WRAPPER_STABLE"
bash "$SWAT_WRAPPER_RUN" "$SWAT_WRAPPER_CANDIDATE" 0 > /dev/null 2>&1
[[ ! -e $SWAT_WRAPPER_STABLE ]]
printf 'PASS network wrapper: transient use removes an alias that had no original QA tool\n'
cp -- "$SWAT_WRAPPER_CANDIDATE" "$SWAT_WRAPPER_TEST_ROOT/candidates/test_net-candidate.exe"
bash "$SWAT_WRAPPER_RUN" "$SWAT_WRAPPER_TEST_ROOT/candidates/test_net-candidate.exe" 0 > /dev/null 2>&1
cmp "$SWAT_WRAPPER_TEST_ROOT/candidates/test_net-candidate.exe" "$SWAT_WRAPPER_STABLE"
printf 'PASS network wrapper: ordinary network candidates remain installed at the approved path\n'
