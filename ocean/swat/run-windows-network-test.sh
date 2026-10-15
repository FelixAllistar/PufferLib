#!/usr/bin/env bash
set -euo pipefail

if [[ $# -lt 1 ]]; then
    printf 'usage: bash ocean/swat/run-windows-network-test.sh CANDIDATE.exe [arguments...]\n' >&2
    exit 2
fi
SWAT_TEST_CANDIDATE=$(realpath -- "$1")
shift
case "$(basename -- "$SWAT_TEST_CANDIDATE")" in
    test_net.exe|test_net_*.exe|test_net-*.exe) SWAT_TEST_NAME=test_net.exe ;;
    test_wall_support.exe|test_wall_support_*.exe|test_wall_support-*.exe) SWAT_TEST_NAME=test_wall_support.exe ;;
    *) printf 'Expected a native test_net or test_wall_support executable.\n' >&2; exit 2 ;;
esac
[[ -f $SWAT_TEST_CANDIDATE && -x $SWAT_TEST_CANDIDATE ]] || {
    printf 'Native test executable is missing or not executable: %s\n' "$SWAT_TEST_CANDIDATE" >&2
    exit 2
}
SWAT_TEST_ROOT=$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)
SWAT_TEST_BUILD=$SWAT_TEST_ROOT/build/swat/windows
mkdir -p "$SWAT_TEST_BUILD"

# Firewall approvals follow the executable path. Keep one stable path per
# network check, and serialize replacement/execution of each candidate there.
exec 9> "$SWAT_TEST_BUILD/.$SWAT_TEST_NAME.lock"
flock 9
SWAT_TEST_STABLE=$SWAT_TEST_BUILD/$SWAT_TEST_NAME
if [[ ! $SWAT_TEST_CANDIDATE -ef $SWAT_TEST_STABLE ]]; then
    SWAT_TEST_STAGE=$SWAT_TEST_STABLE.stage.$$
    trap 'rm -f -- "$SWAT_TEST_STAGE"' EXIT
    cp -- "$SWAT_TEST_CANDIDATE" "$SWAT_TEST_STAGE"
    mv -- "$SWAT_TEST_STAGE" "$SWAT_TEST_STABLE"
    trap - EXIT
fi
printf '[swat] Native network check: %s via build/swat/windows/%s\n' "$SWAT_TEST_CANDIDATE" "$SWAT_TEST_NAME" >&2
cd "$SWAT_TEST_ROOT"
# Arguments are forwarded unchanged, including already converted Windows paths.
"$SWAT_TEST_STABLE" "$@"
