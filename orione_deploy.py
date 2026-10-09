#!/usr/bin/env python3
"""
Orione build (CC_ORIONE): update the machine over WiFi, as safely as over USB.

    /opt/homebrew/bin/python3 orione_deploy.py --host 192.168.178.59            # firmware (with the web page)
    /opt/homebrew/bin/python3 orione_deploy.py --host 192.168.178.59 --log      # only show GET /log
    /opt/homebrew/bin/python3 orione_deploy.py --host 192.168.178.59 --decode   # GET /log, crash backtraces as source lines

1. Asks the machine what runs and refuses while a shot, flush or backflush runs (the firmware also
   ignores an update then).
2. Backs up settings, shots, beans, boot info and log into --backup-dir/<time>/ (outside git).
3. Builds and sends the firmware (env esp32_round_ota, OTA password from the machine's settings); the
   web page is part of it (src/flashAssets.h). Waits for the new version and for the firmware to
   confirm itself (src/firmwareGuard.h: a firmware that fails at the start makes the ESP32 go back to
   the one before; that is reported). The settings in LittleFS are not touched.
4. Compares settings, shots and beans with the backup and checks that the page loads.
5. Keeps the firmware's ELF per version (--backup-dir/elf/<commit>.elf): --decode turns the backtrace a
   crash leaves in GET /log (src/panicLog.h) into functions and source lines.
"""

import argparse
import json
import os
import shutil
import subprocess
import sys
import time
import urllib.error
import urllib.request

ROOT = os.path.dirname(os.path.abspath(__file__))
ENV = "esp32_round_ota"
BUSY = {20: "Bezug", 25: "Spülen", 50: "Backflush"}


def get(host, path, timeout=6, raw=False):
    # the machine answers 503 while its request gate is full or the heap low (src/webRequestGate.h): try again
    # a few times (09.10.2026: the backup and the comparison broke off on it with a weak WiFi)
    for attempt in range(6):
        try:
            with urllib.request.urlopen(f"http://{host}{path}", timeout=timeout) as r:
                body = r.read()
                return body if raw else json.loads(body) if r.headers.get_content_type() == "application/json" else body.decode(errors="replace")
        except urllib.error.HTTPError as e:
            if e.code != 503 or attempt == 5:
                raise
            time.sleep(2 + attempt * 2)
        except (urllib.error.URLError, OSError):  # reset by the gate's emergency brake, a timeout over a weak link
            if attempt == 5:
                raise
            time.sleep(2 + attempt * 2)


def try_get(host, path, timeout=4, raw=False):
    try:
        return get(host, path, timeout, raw)
    except (urllib.error.URLError, OSError, ValueError):
        return None


def live_state(host):
    """The machine state from the first live value (GET /events)"""
    try:
        with urllib.request.urlopen(urllib.request.Request(f"http://{host}/events", headers={"Accept": "text/event-stream"}), timeout=8) as r:
            for _ in range(40):
                line = r.readline().decode(errors="replace").strip()
                if line.startswith("data:") and "{" in line:
                    return json.loads(line[5:])
    except (urllib.error.URLError, OSError, ValueError):
        pass
    return None


def flat(o, prefix=""):
    out = {}
    for k, v in o.items():
        if isinstance(v, dict):
            out.update(flat(v, prefix + k + "."))
        else:
            out[prefix + k] = v
    return out


def wait_online(host, expect=None, timeout=120):
    """Until the machine answers again (with the expected version suffix, if given); returns the version or None"""
    end = time.time() + timeout
    while time.time() < end:
        v = try_get(host, "/version", 3)
        if v and (expect is None or v.strip().endswith(expect)):
            return v.strip()
        time.sleep(2)
    return try_get(host, "/version", 3)


def wait_restarted(host, since, timeout=150):
    """Until the machine runs again after a restart that came after `since` (time.time()); returns /boot or None"""
    end = time.time() + timeout
    while time.time() < end:
        b = try_get(host, "/boot", 3)
        if b and b.get("uptime") is not None and b["uptime"] < time.time() - since + 5:
            return b
        time.sleep(2)
    return None


def addr2line():
    for root in [os.path.expanduser("~/.platformio/packages")]:
        for dirpath, _, names in os.walk(root):
            if "xtensa-esp32-elf-addr2line" in names:
                return os.path.join(dirpath, "xtensa-esp32-elf-addr2line")
    return None


def decode(host, backup_dir, elf=None):
    """Prints GET /log with every backtrace turned into functions and lines"""
    log = get(host, "/log", 10)
    version = (try_get(host, "/version") or "").strip()
    elf = elf or os.path.join(backup_dir, "elf", version.rsplit(".", 1)[-1] + ".elf")
    tool = addr2line()
    for line in log.splitlines():
        print(line)
        if line.startswith("Backtrace:"):
            if not os.path.exists(elf) or not tool:
                print(f"    (keine ELF-Datei {elf} oder kein addr2line: Adressen nicht übersetzbar)")
                continue
            addrs = line.split()[1:]
            out = subprocess.run([tool, "-pfiaC", "-e", elf] + addrs, capture_output=True, text=True).stdout
            for l in out.strip().splitlines():
                print("    " + l.replace(ROOT + "/", ""))
    return 0


def pio(target, host, password, extra_env=None, environment=ENV):
    env = dict(os.environ, PLATFORMIO_UPLOAD_FLAGS=f"--auth={password}", **(extra_env or {}))
    cmd = ["pio", "run", "-e", environment, "-t", target, "--upload-port", host]
    print("  $ " + " ".join(cmd))
    p = subprocess.run(cmd, cwd=ROOT, env=env, capture_output=True, text=True)
    tail = [l for l in (p.stdout + p.stderr).splitlines() if any(k in l for k in ("Uploading", "Error", "error", "Authenticat", "SUCCESS", "FAILED", "Sending"))]
    for l in tail[-6:]:
        print("    " + l.strip())
    return p.returncode == 0


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--host", default=os.environ.get("ORIONE_HOST", "orione.local"))
    ap.add_argument("--force", action="store_true", help="also while the machine is busy (the firmware still refuses during a shot)")
    ap.add_argument("--log", action="store_true", help="only print GET /log")
    ap.add_argument("--decode", action="store_true", help="print GET /log with crash backtraces as source lines")
    ap.add_argument("--elf", help="ELF for --decode (default: the one kept for the version that runs)")
    ap.add_argument("--env", default=ENV, help="build environment to send (default esp32_round_ota)")
    ap.add_argument("--backup-dir", default=os.path.join(ROOT, ".device-backups"))
    a = ap.parse_args()
    host = a.host

    if a.log:
        print(get(host, "/log", 10))
        return 0

    if a.decode:
        return decode(host, a.backup_dir, a.elf)

    # 1. what runs now
    version = try_get(host, "/version")
    if not version:
        print(f"Maschine unter {host} nicht erreichbar.")
        return 1
    boot = try_get(host, "/boot") or {}
    live = live_state(host) or {}
    print(f"Maschine {host}: {version.strip()} · läuft seit {boot.get('uptime', '?')} s · Firmware {boot.get('fw', '?')} · Zustand {live.get('state', '?')}")
    if live.get("state") in BUSY and not a.force:
        print(f"Abbruch: {BUSY[live['state']]} läuft gerade. Danach noch einmal (oder --force).")
        return 1

    # 2. backup
    stamp = time.strftime("%Y%m%d-%H%M%S")
    folder = os.path.join(a.backup_dir, stamp)
    os.makedirs(folder, exist_ok=True)
    config = get(host, "/download/config", 10)
    if isinstance(config, str):
        config = json.loads(config)
    shots, beans = try_get(host, "/shots", 10), try_get(host, "/beans", 10)
    for name, data in [("config.json", config), ("shots.json", shots), ("beans.json", beans), ("boot.json", boot)]:
        if data is not None:
            with open(os.path.join(folder, name), "w") as f:
                json.dump(data, f, indent=1, ensure_ascii=False)
    log = try_get(host, "/log", 10)
    if isinstance(log, str):
        with open(os.path.join(folder, "log.txt"), "w") as f:
            f.write(log)
    print(f"Sicherung: {folder}")
    password = flat(config).get("system.ota_password") or "otapass"
    before = flat(config)

    # 3. firmware (with the page)
    head = subprocess.run(["git", "rev-parse", "--short", "HEAD"], cwd=ROOT, capture_output=True, text=True).stdout.strip()
    dirty = subprocess.run(["git", "status", "--porcelain", "--untracked-files=no"], cwd=ROOT, capture_output=True, text=True).stdout.strip()
    print(f"Firmware {head}{' (mit nicht committeten Änderungen)' if dirty else ''} per WLAN …")
    since = time.time()
    if not pio("upload", host, password, environment=a.env):
        print("Firmware-Update fehlgeschlagen; auf der Maschine läuft weiter die alte.")
        return 1
    elf_dir = os.path.join(a.backup_dir, "elf")
    os.makedirs(elf_dir, exist_ok=True)
    shutil.copyfile(os.path.join(ROOT, ".pio", "build", a.env, "firmware.elf"), os.path.join(elf_dir, head + (".crashtest" if "crashtest" in a.env else "") + ".elf"))
    if not wait_restarted(host, since):
        print("Nach dem Update startet die Maschine nicht wieder im WLAN (USB nötig?).")
        return 1
    now = wait_online(host, "." + head, 10)
    if not now or not now.endswith("." + head):
        b = try_get(host, "/boot") or {}
        print(f"Nach dem Update meldet sie {now or 'nichts'}" + (" – die neue Firmware ist nicht gestartet, der ESP32 hat die alte wieder gestartet." if b.get("updateFailed") else "."))
        return 1
    print(f"Neue Firmware läuft: {now}")
    # confirmed = "fw" ok AND still the new version: a crash before the confirmation starts the old one, which is "ok" too
    end = time.time() + 120
    confirmed = False
    while time.time() < end:
        b = try_get(host, "/boot") or {}
        v = (try_get(host, "/version") or "").strip()
        if v and not v.endswith("." + head):
            print(f"Die neue Firmware ist vor der Bestätigung abgestürzt; der ESP32 hat die vorige gestartet ({v}). GET /log zeigt warum (--decode).")
            return 1
        if b.get("fw") == "ok" and v.endswith("." + head):
            confirmed = True
            print("Firmware hat sich bestätigt (läuft mit WLAN).")
            break
        time.sleep(3)
    if not confirmed:
        print("Warnung: Firmware hat sich noch nicht bestätigt. Ein Neustart vorher würde die alte starten.")

    # 4. compare
    time.sleep(2)
    after_cfg = get(host, "/download/config", 10)
    after = flat(json.loads(after_cfg) if isinstance(after_cfg, str) else after_cfg)
    changed = [k for k in sorted(set(before) | set(after)) if k in before and k in after and before[k] != after[k]]
    added = [k for k in after if k not in before]
    dropped = [k for k in before if k not in after]
    for k in changed:
        print(f"  Einstellung geändert: {k}: {before[k]!r} -> {after[k]!r}")
    if added:
        print(f"  Neue Einstellungen (Standardwert): {', '.join(sorted(added))}")
    if dropped:
        print(f"  Entfallene Einstellungen: {', '.join(sorted(dropped))}")
    s2, b2 = try_get(host, "/shots", 10), try_get(host, "/beans", 10)
    n1, n2 = len((shots or {}).get("shots", [])), len((s2 or {}).get("shots", []))
    print(f"  Bezüge: {n1} -> {n2}" + ("" if n1 == n2 else "  ← ANDERS"))
    if beans is not None and b2 is not None:
        same = [x["n"] for x in beans.get("beans", [])] == [x["n"] for x in b2.get("beans", [])]
        print(f"  Bohnen: {len(beans.get('beans', []))} -> {len(b2.get('beans', []))}" + ("" if same else "  ← ANDERS"))
    page = try_get(host, "/", 10, raw=True)
    print(f"  Seite: {'lädt (' + str(len(page)) + ' Bytes)' if page else 'lädt NICHT'}")
    b = try_get(host, "/boot") or {}
    print(f"  Start: {b.get('reason')} · Heap {b.get('heap')} (tiefster {b.get('heapMin')}) · Stack frei tcp {b.get('stackUnused', {}).get('tcp')} · Firmware {b.get('fw')}")
    bad = changed or n1 != n2 or not page
    print("Fertig." if not bad else "Fertig, aber bitte die markierten Punkte ansehen.")
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
