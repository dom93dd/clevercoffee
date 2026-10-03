# OrioneScaleBLE

AcaiaArduinoBLE v4.0.2 ([rancilio-pid/AcaiaArduinoBLE](https://github.com/rancilio-pid/AcaiaArduinoBLE),
commit 9afd589, MIT, see `LICENSE`) with three fixes for the slim Orione build. Every change is marked
`Orione:` in `src/AcaiaArduinoBLE.cpp`; the files keep the upstream names and CRLF line endings so
`diff` against upstream shows only the fixes.

1. **No NimBLE stack reset while no scale is around.** A scan that found no scale within 15 s went to
   `FAILED`, and `FAILED` counted an attempt on every call (every 250 ms), so the count passed 8 while
   waiting for the first backoff and every failure ran `NimBLEDevice::deinit(true)` + `init()`: with
   the scale enabled but switched off, every ~21 s. Each reset loses ~50 bytes of heap for good and
   needs the whole BLE stack (~33 KB) at once; the free heap sank by several KB per hour and once fell
   to 1.2 KB during a page load. Now a scan without a scale just restarts (loses nothing), and `FAILED`
   counts one attempt per failure, so the reset only follows 9 real connection failures in a row.
   Measured with `simulator/esp32-bench` env `ble_leak`: stack down/up ~50 B per cycle with and
   without WiFi, scan restarts 0 B, the unfixed library ~50 B per scan timeout.
2. **Clients are handed back to NimBLE** (`NimBLEDevice::deleteClient`) instead of only forgetting the
   pointer. The Orione build allows one client (`CONFIG_BT_NIMBLE_MAX_CONNECTIONS=1`), so after a failed
   or lost connection `createClient()` returned `nullptr` until the stack reset.
3. **Callbacks are detached before they are deleted** (scan and client), so a late event cannot call
   into freed memory.
