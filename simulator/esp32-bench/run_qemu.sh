#!/bin/sh
# Builds the bench, runs it in the Espressif QEMU emulator (no ESP32 needed) and prints an
# estimate of the drawing time per screen on a real ESP32 at 240 MHz.
#
# QEMU: https://github.com/espressif/qemu/releases (qemu-xtensa-softmmu-…-apple-darwin.tar.xz),
# needs Homebrew's libgcrypt, pixman and sdl2. Default location as ESP-IDF installs it:
#   ~/.espressif/tools/qemu-xtensa/<version>/qemu/bin/qemu-system-xtensa   (or set QEMU=…)
#
# With -icount shift=0 every guest instruction advances the guest clock by exactly 1 ns, so the
# microseconds the bench prints are instruction counts / 1000. QEMU knows nothing about caches,
# flash wait states or pipeline stalls; the estimate therefore gives a range of 1.0 to 1.6 cycles
# per instruction (own assumption, to be replaced by the measurement on the real chip).
set -e
cd "$(dirname "$0")"

QEMU="${QEMU:-$(ls -d "$HOME"/.espressif/tools/qemu-xtensa/*/qemu/bin/qemu-system-xtensa 2>/dev/null | tail -1)}"
[ -x "$QEMU" ] || { echo "qemu-system-xtensa not found, see the comment at the top" >&2; exit 1; }

pio run -e bench -s
B=.pio/build/bench
APP0="$HOME/.platformio/packages/framework-arduinoespressif32/tools/partitions/boot_app0.bin"
pio pkg exec -p tool-esptoolpy -- esptool.py --chip esp32 merge_bin --fill-flash-size 4MB -o "$B/flash.bin" \
    0x1000 "$B/bootloader.bin" 0x8000 "$B/partitions.bin" 0xe000 "$APP0" 0x10000 "$B/firmware.bin" > /dev/null

OUT="$B/qemu.log"
"$QEMU" -nographic -machine esp32 -icount shift=0 -drive file="$B/flash.bin",if=mtd,format=raw -serial file:"$OUT" -monitor none &
PID=$!
trap 'kill $PID 2>/dev/null || true' EXIT

# Wait for the end of the run (a few minutes: QEMU emulates every instruction)
for i in $(seq 1 1200); do
    grep -q "BENCH END\|BENCH ERROR" "$OUT" 2>/dev/null && break
    sleep 0.5
done

kill $PID 2>/dev/null || true
[ "$1" = "--check" ] || grep "PRIM" "$OUT" || true
RC=0
grep "BENCH" "$OUT" | "${PYTHON:-python3}" report.py "$@" || RC=$?
exit $RC
