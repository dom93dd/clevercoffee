#!/usr/bin/env python3
"""
Turns the BENCH lines of the bench (stdin) into a table and checks the drawing time budget.

From QEMU (-icount shift=0: 1 us = 1000 instructions) the time on the ESP32 at 240 MHz is estimated
with 1.0 to 1.6 cycles per instruction (own assumption; the upper value is used for the check).
With --real the microseconds come from the real chip and are taken as they are.

The firmware draws one band per loop() iteration. A band blocks loop() for its drawing time plus
the wait for the previous band's SPI transfer (5.7 ms per 240x40 band at the default 27 MHz).

  --check   exit code 1 if a screen breaks the budget:
            slowest band <= 12 ms (loop() never stops longer for drawing),
            whole frame <= 50 ms of drawing (animations at 20 pictures per second or more)
"""

import sys

CPI = (1.0, 1.6)
MHZ = 240.0
SPI_HZ = 27e6
SEND_MS = 240 * 40 * 16 / SPI_HZ * 1000
BANDS = 6
MAX_BAND_MS = 12.0
MAX_FRAME_MS = 50.0

real = "--real" in sys.argv
check = "--check" in sys.argv
rows = []

for line in sys.stdin:
    parts = line.split()
    if len(parts) < 2 or parts[0] != "BENCH":
        continue
    if parts[1] in ("START", "END", "ERROR"):
        print(line.strip())
        continue
    values = dict(zip(parts[2::2], parts[3::2]))
    rows.append((parts[1], int(values["us"]), int(values["band_us"]), values.get("crc", "")))

if not rows:
    sys.exit("no BENCH lines")


def ms(us, cpi):
    return us / 1000.0 if real else us * 1000.0 * cpi / (MHZ * 1e6) * 1000.0


print(f"{'screen':18} {'drawing ms':>15} {'slowest band ms':>16} {'frame incl. SPI':>16}  crc")
problems = []
slowest_frame = 0.0

for name, us, band_us, crc in rows:
    lo, hi = ms(us, CPI[0]), ms(us, CPI[1])
    blo, bhi = ms(band_us, CPI[0]), ms(band_us, CPI[1])
    frame = max(hi / BANDS, SEND_MS) * (BANDS - 1) + hi / BANDS + SEND_MS
    slowest_frame = max(slowest_frame, frame)
    span = f"{lo:6.1f} – {hi:6.1f}" if not real else f"{lo:15.1f}"
    bspan = f"{blo:7.1f} – {bhi:6.1f}" if not real else f"{blo:16.1f}"
    print(f"{name:18} {span} {bspan} {frame:16.1f}  {crc}")

    if bhi > MAX_BAND_MS:
        problems.append(f"{name}: slowest band {bhi:.1f} ms > {MAX_BAND_MS:.0f} ms")
    if hi > MAX_FRAME_MS:
        problems.append(f"{name}: frame {hi:.1f} ms > {MAX_FRAME_MS:.0f} ms")

print(f"slowest frame incl. SPI at {SPI_HZ / 1e6:.0f} MHz: {slowest_frame:.1f} ms -> {1000.0 / slowest_frame:.0f} pictures per second at least")

if check:
    for p in problems:
        print("TOO SLOW " + p)
    sys.exit(1 if problems else 0)
