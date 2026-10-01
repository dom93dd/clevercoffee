#!/bin/sh
# Checks the firmware side of the round display branch:
#  1. esp32_usb, esp32_round_usb and esp32_round_ota build
#  2. the round display build leaves at least MIN_FREE bytes of the app partition free
#  3. the stock esp32_usb build has exactly the size of upstream/master, i.e. the branch
#     does not change the normal firmware (builds master in a temporary git worktree;
#     skip with --quick)
#
# Usage: simulator/check_firmware.sh [--quick]
set -e
cd "$(dirname "$0")/.."

MIN_FREE=16384
QUICK=0
[ "$1" = "--quick" ] && QUICK=1

# Flash bytes used by an env ("Flash: [...] (used N bytes from M bytes)"), $2 = project dir
used() {
    pio run -e "$1" -d "${2:-.}" 2>&1 | sed -n 's/.*used \([0-9]*\) bytes from \([0-9]*\) bytes.*/\1 \2/p' | tail -1
}

fail() {
    echo "FAILED: $1"
    exit 1
}

echo "== Builds"
for env in esp32_usb esp32_round_usb esp32_round_ota; do
    set -- $(used "$env")
    [ -n "$1" ] || fail "$env does not build"
    echo "   $env: $1 of $2 bytes, $(( $2 - $1 )) free"

    case "$env" in
        esp32_usb) STOCK=$1 ;;
        esp32_round_*) [ $(( $2 - $1 )) -ge $MIN_FREE ] || fail "$env leaves less than $MIN_FREE bytes free" ;;
    esac
done

if [ $QUICK -eq 1 ]; then
    echo "== Skipped comparison with upstream/master (--quick)"
    echo "OK"
    exit 0
fi

echo "== Stock firmware compared with upstream/master"
BASE=$(git merge-base HEAD upstream/master 2>/dev/null || git merge-base HEAD origin/master)
BRANCH=$(git branch --show-current)
TMP=$(mktemp -d)
trap 'git worktree remove --force "$TMP/base" >/dev/null 2>&1; rm -rf "$TMP"' EXIT
git worktree add --detach "$TMP/base" "$BASE" >/dev/null 2>&1

# Same branch name in the version string, so only code differences change the size
set -- $(GITHUB_REF_NAME="$BRANCH" used esp32_usb "$TMP/base")
[ -n "$1" ] || fail "upstream/master ($BASE) does not build"
echo "   upstream/master ${BASE%"${BASE#???????}"}: $1 bytes, this branch: $STOCK bytes"
[ "$1" -eq "$STOCK" ] || fail "the stock firmware differs from upstream/master by $(( STOCK - $1 )) bytes"
echo "OK"
