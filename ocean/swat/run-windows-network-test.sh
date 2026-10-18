#!/usr/bin/env bash
set -euo pipefail

if [[ $# -lt 1 ]]; then
    printf 'usage: bash ocean/swat/run-windows-network-test.sh CANDIDATE.exe [arguments...]\n' >&2
    exit 2
fi
SWAT_TEST_CANDIDATE=$(realpath -- "$1")
shift
SWAT_TEST_TRANSIENT=0
case "$(basename -- "$SWAT_TEST_CANDIDATE")" in
    test_net.exe|test_net_*.exe|test_net-*.exe) SWAT_TEST_NAME=test_net.exe ;;
    test_frontend_input.exe) SWAT_TEST_NAME=test_net.exe; SWAT_TEST_TRANSIENT=1 ;;
    test_wall_support.exe|test_wall_support_*.exe|test_wall_support-*.exe) SWAT_TEST_NAME=test_wall_support.exe ;;
    *) printf 'Expected a native test_net, test_frontend_input or test_wall_support executable.\n' >&2; exit 2 ;;
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
SWAT_TEST_STAGE=
SWAT_TEST_BACKUP=
SWAT_TEST_REMOVE=0
swat_test_cleanup() {
    local result=$?
    [[ -z $SWAT_TEST_STAGE ]] || rm -f -- "$SWAT_TEST_STAGE"
    if [[ -n $SWAT_TEST_BACKUP ]]; then
        mv -f -- "$SWAT_TEST_BACKUP" "$SWAT_TEST_STABLE"
    elif [[ $SWAT_TEST_REMOVE == 1 ]]; then
        rm -f -- "$SWAT_TEST_STABLE"
    fi
    return "$result"
}
trap swat_test_cleanup EXIT
# Frontend transport checks share the approved network-test path temporarily.
# Restore its ordinary QA executable before releasing the same lock.
if [[ $SWAT_TEST_TRANSIENT == 1 ]]; then
    if [[ -f $SWAT_TEST_STABLE ]]; then
        SWAT_TEST_STAGE=$SWAT_TEST_STABLE.restore.$$
        cp -- "$SWAT_TEST_STABLE" "$SWAT_TEST_STAGE"
        SWAT_TEST_BACKUP=$SWAT_TEST_STAGE; SWAT_TEST_STAGE=
    else SWAT_TEST_REMOVE=1
    fi
fi
if [[ ! $SWAT_TEST_CANDIDATE -ef $SWAT_TEST_STABLE ]]; then
    SWAT_TEST_STAGE=$SWAT_TEST_STABLE.stage.$$
    cp -- "$SWAT_TEST_CANDIDATE" "$SWAT_TEST_STAGE"
    mv -- "$SWAT_TEST_STAGE" "$SWAT_TEST_STABLE"
    SWAT_TEST_STAGE=
fi
printf '[swat] Native network check: %s via build/swat/windows/%s\n' "$SWAT_TEST_CANDIDATE" "$SWAT_TEST_NAME" >&2
cd "$SWAT_TEST_ROOT"
# Arguments are forwarded unchanged, including already converted Windows paths.
"$SWAT_TEST_STABLE" "$@"
