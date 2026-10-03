#!/bin/sh
# Checks the firmware side of the round display branch:
#  1. esp32_usb, esp32_round_usb and esp32_round_ota build
#  2. the round display build leaves at least MIN_FREE bytes of the app partition free
#  3. drawing time of every screen on the ESP32, measured in the QEMU emulator
#     (esp32-bench; skipped if QEMU is not installed, see esp32-bench/run_qemu.sh)
#  4. nothing of the round display / Orione build (CC_ORIONE) gets into the stock esp32_usb build
#     (symbol check). Since 02.10.2026 the branch carries upstream 4.0.4 plus irrwisch1's frontend
#     on purpose, so the stock build is no longer byte-equal to upstream: its size difference to
#     upstream/master is only reported (builds master in a temporary git worktree; skip with --quick)
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

echo "== Drawing time on the ESP32 (QEMU)"
if ls "$HOME"/.espressif/tools/qemu-xtensa/*/qemu/bin/qemu-system-xtensa >/dev/null 2>&1 || [ -n "$QEMU" ]; then
    RC=0
    REPORT=$(simulator/esp32-bench/run_qemu.sh --check 2>/dev/null) || RC=$?
    echo "$REPORT" | grep -E "slowest frame|TOO SLOW|ERROR" | sed 's/^/   /'
    [ $RC -eq 0 ] || fail "a screen draws too slowly on the ESP32 (see simulator/esp32-bench/run_qemu.sh)"
else
    echo "   skipped: QEMU not installed (see simulator/esp32-bench/run_qemu.sh)"
fi

echo "== Stock firmware free of round display / Orione code"
NM=$(ls ~/.platformio/packages/toolchain-xtensa-esp32/bin/xtensa-esp32-elf-nm)
LEAK=$("$NM" -C .pio/build/esp32_usb/firmware.elf | grep -E 'web_gate::|round_timing::|[ (]rd::|RoundUi|roundDisplay|loopGuard|pruneUnknownKeys|fixedValue|freeConfigDefs|TempSensorFake|shot_history|orione_portal|AcaiaArduinoBLE::releaseClient' | head -3)
[ -z "$LEAK" ] || fail "round/Orione code in the stock build: $LEAK"
echo "OK"

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
echo "   upstream/master ${BASE%"${BASE#???????}"}: $1 bytes, this branch: $STOCK bytes ($(( STOCK - $1 )) bytes, irrwisch1's frontend and fixes)"
echo "OK"
