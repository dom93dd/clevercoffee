#!/usr/bin/env python3
"""
Stand-in for the ESP32 web API of the Orione build, so the web page can be tested without hardware.
Serves frontend-orione/index.html and the endpoints the page uses, with the firmware's rules:
brew-by-time settings only in automatic mode, scale settings only with the scale on, blind toggles
for backflush and tare, a config upload that needs a restart, SSE live values.

    /opt/homebrew/bin/python3 mock_esp32.py [--port 8099] [--busy-every N]

--busy-every N answers every N-th GET /parameters with 503, like the request gate of the firmware.
Test hooks: GET /__posts (POST requests so far), GET /__state (values, restarts),
POST /__live (JSON merged into the SSE values; {"noBeans": true}: GET /beans answers 404), POST /__shot (log a shot),
POST /__bf (backflush counter), POST /__param (JSON {name: value}: a value the machine changed itself, e.g. the learned
drop time), POST /__care (JSON merged into GET /care), POST /__reset (fresh state).
"""

import argparse
import copy
import json
import re
import sys
import os
import threading
import time
import urllib.parse
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer

PAGE = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "index.html")
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), ".."))
import assets  # noqa: E402  (manifest and icons, as the build puts them next to the page)
ASSETS = assets.files()

# type: 0 int, 1 bool (uint8 0/1), 2 double, 4 string, 5 enum -- EditableKind in src/Parameter.h
BASE = {
    "pid.enabled": dict(type=1, value=1, min=0, max=1),
    "pid.use_ponm": dict(type=1, value=0, min=0, max=1),
    "pid.ema_factor": dict(type=2, value=0.6, min=0, max=1),
    "pid.regular.kp": dict(type=2, value=62, min=0, max=200),
    "pid.regular.tn": dict(type=2, value=52, min=0, max=999),
    "pid.regular.tv": dict(type=2, value=11.5, min=0, max=999),
    "pid.regular.i_max": dict(type=2, value=55, min=0, max=999),
    "brew.setpoint": dict(type=2, value=95.0, min=20, max=110),
    "brew.temp_offset": dict(type=2, value=0.0, min=-20, max=20),
    "brew.mode": dict(type=5, value=0, min=0, max=1, options=["Manual", "Automatic"]),
    "brew.by_time.enabled": dict(type=1, value=1, min=0, max=1),
    "brew.by_time.target_time": dict(type=2, value=25.0, min=0, max=120),
    "brew.by_weight.enabled": dict(type=1, value=0, min=0, max=1),
    "brew.by_weight.target_weight": dict(type=2, value=36.0, min=0, max=500),
    "brew.by_weight.auto_tare": dict(type=1, value=1, min=0, max=1),
    "brew.by_weight.lag": dict(type=2, value=1.0, min=0, max=3),
    "brew.heat_boost": dict(type=2, value=60.0, min=0, max=100),
    "brew.by_weight.learn": dict(type=1, value=1, min=0, max=1),
    "brew.presets": dict(type=4, value="25,36;30,45;45,80", min=0, max=48),
    "brew.dose": dict(type=2, value=18.0, min=5, max=30),
    "brew.grind": dict(type=4, value="12", min=0, max=9),
    "brew.grinder": dict(type=4, value="", min=0, max=40),
    "brew.beans": dict(type=4, value="", min=0, max=40),
    "brew.grind_scale": dict(type=5, value=0, min=0, max=2, options=["Not known", "Higher is finer", "Higher is coarser"]),
    "brew.pre_infusion.enabled": dict(type=1, value=0, min=0, max=1),
    "brew.pre_infusion.time": dict(type=2, value=2.0, min=0, max=10),
    "brew.pre_infusion.pause": dict(type=2, value=4.0, min=0, max=15),
    "backflush.remind_after": dict(type=0, value=50, min=0, max=500),
    "backflush.cycles": dict(type=0, value=5, min=2, max=20),
    "backflush.fill_time": dict(type=2, value=5.0, min=3, max=10),
    "backflush.flush_time": dict(type=2, value=10.0, min=5, max=20),
    "BACKFLUSH_ON": dict(type=1, value=0, min=0, max=1),
    "TARE_ON": dict(type=1, value=0, min=0, max=1),
    "standby.enabled": dict(type=1, value=0, min=0, max=1),
    "standby.time": dict(type=2, value=35, min=5, max=300),
    "display.language": dict(type=5, value=0, min=0, max=2, options=["Deutsch", "English", "Español"]),
    "display.blescale_brew_timer": dict(type=1, value=0, min=0, max=1),
    "display.post_brew_timer_duration": dict(type=2, value=10.0, min=0, max=60),
    "display.blinking.delta": dict(type=2, value=0.3, min=0, max=10),
    "system.hostname": dict(type=4, value="orione", min=0, max=32, reboot=True),
    "system.ota_password": dict(type=4, value="otapass", min=0, max=32),
    "system.log_level": dict(type=5, value=2, min=0, max=6, options=["TRACE", "DEBUG", "INFO", "WARNING", "ERROR", "FATAL", "SILENT"]),
    "hardware.sensors.watertank.enabled": dict(type=1, value=0, min=0, max=1, reboot=True),
    "brew.warmup_flush": dict(type=1, value=1, min=0, max=1),
    "hardware.sensors.watertank.mode": dict(type=5, value=1, min=0, max=1, options=["Normally Open", "Normally Closed"], reboot=True),
    "hardware.sensors.scale.enabled": dict(type=1, value=1, min=0, max=1, reboot=True),
    "schedule.enabled": dict(type=1, value=0, min=0, max=1),
    "schedule.days": dict(type=0, value=127, min=0, max=127),
    "schedule.on": dict(type=0, value=390, min=0, max=1439),
    "schedule.off": dict(type=0, value=1440, min=0, max=1440),
    "descale.litres": dict(type=2, value=40.0, min=0, max=300),
    "brew.temp_end": dict(type=2, value=0.0, min=-5, max=5),
}


class State:
    def __init__(self):
        self.p = copy.deepcopy(BASE)
        self.scale_at_boot = self.p["hardware.sensors.scale.enabled"]["value"]  # the firmware registers scale settings at boot
        self.posts = []
        self.pending_upload = None
        self.restarts = 0
        self.page_loads = 0
        self.shots = []  # newest first, as GET /shots of the firmware (src/shotHistory.h)
        self.bf = 0      # shots since the last backflush
        self.live = {}  # POST /__live: fields that override the SSE values (e.g. a running shot)
        # the Bluetooth scale chosen in the settings (GET /scale): connected; select/forget/discover change it
        self.scale = {"address": "c8:2e:18:aa:01:02", "name": "BOOKOO_SC U 1234", "connect_at": 0.0, "search_from": 0.0}
        self.temp = 22.0
        self.beans = []         # recipes per bean as src/beanProfiles.h keeps them: {"n","d","m","tw","t","lg","used"}
        self.bean_live = None   # the current bean's values as last seen
        self.bean_clock = 0
        # src/machineCare.h: counters and the water through the thermoblock since descaling (ml)
        self.care = {"today": 3, "week": 12, "total": 245, "coffee": 4410.0, "water": 12300, "descaledAt": 0}
        self.reference = None  # src/shotHistory.h: {"shot": {...}, "curve": {...}} or None
        self.clean = 0         # cleaning with detergent: orione::CleaningProgram::Phase
        self.lock = threading.Lock()

    # ---- beans: the firmware's loop (src/beanProfiles.h), run here before each request instead of every 0.5 s
    RECIPE = {"d": "brew.dose", "m": "brew.grind", "tw": "brew.by_weight.target_weight", "t": "brew.setpoint", "lg": "brew.by_weight.lag"}

    @staticmethod
    def bean_key(name):
        return re.sub(r"[A-Z]", lambda m: m.group(0).lower(), str(name or "").strip())

    def recipe(self):
        r = {k: self.p[n]["value"] for k, n in self.RECIPE.items()}
        r["n"] = self.p["brew.beans"]["value"]
        return r

    def bean_find(self, name):
        key = self.bean_key(name)
        return next((i for i, b in enumerate(self.beans) if key and self.bean_key(b["n"]) == key), None)

    def bean_put(self, r):
        if not self.bean_key(r["n"]):
            return
        self.bean_clock += 1
        r = dict(r, used=self.bean_clock)
        i = self.bean_find(r["n"])
        if i is None and len(self.beans) >= 8:  # full: the one not used for the longest time makes room
            i = min(range(len(self.beans)), key=lambda k: self.beans[k]["used"])
        if i is None:
            self.beans.append(r)
        else:
            self.beans[i] = r

    def check_beans(self):
        now, live = self.recipe(), self.bean_live
        if live is None:
            self.bean_live = now
            if self.bean_find(now["n"]) is None:
                self.bean_put(now)
            return
        if self.bean_key(now["n"]) != self.bean_key(live["n"]):
            self.bean_put(live)  # the old bean keeps its values
            i = self.bean_find(now["n"])
            if i is not None:
                back = dict(self.beans[i], n=now["n"])
                for k, n in self.RECIPE.items():
                    if back[k] or k in ("m", "lg"):
                        self.p[n]["value"] = back[k]
            self.bean_put(self.recipe())
        self.bean_live = self.recipe()

    def beans_json(self):
        cur = self.bean_key(self.bean_live["n"]) if self.bean_live else ""
        out = []
        for b in sorted(self.beans, key=lambda b: -b["used"]):
            b = self.bean_live if cur and self.bean_key(b["n"]) == cur else b
            out.append({"n": b["n"], "d": b["d"] or None, "m": b["m"], "tw": b["tw"] or None, "t": b["t"] or None, "lg": round(b["lg"], 2)})
        return {"now": self.bean_live["n"] if self.bean_live else "", "beans": out}

    def scale_state(self):
        """0 off, 1 chosen but not (yet) connected, 2 connected, 3 none chosen: as the firmware"""
        if not self.scale_at_boot:
            return 0
        if not self.scale["address"]:
            return 3
        return 2 if time.time() >= self.scale["connect_at"] else 1

    def shown(self, name):
        """registered and shouldShow() in src/ParameterRegistry.cpp (scale settings exist only if the scale was on at boot)"""
        v = lambda n: self.p[n]["value"]
        if name.startswith("brew.by_time."):
            return v("brew.mode") == 1
        if name.startswith("brew.by_weight."):
            return bool(self.scale_at_boot) and v("brew.mode") == 1
        if name == "TARE_ON":
            return bool(self.scale_at_boot)
        return True

    def restart(self):
        if self.pending_upload is not None:  # the firmware takes an uploaded config over at the next start
            for k, val in self.pending_upload.items():
                if k in self.p:
                    self.p[k]["value"] = val
            self.pending_upload = None
        self.scale_at_boot = self.p["hardware.sensors.scale.enabled"]["value"]
        self.p["BACKFLUSH_ON"]["value"] = 0
        self.p["TARE_ON"]["value"] = 0
        self.clean = 0
        self.restarts += 1

    def care_json(self):
        limit = self.p["descale.litres"]["value"]
        standby = 23 if self.p["standby.enabled"]["value"] else -1
        return {**self.care, "descaleL": limit, "due": limit > 0 and self.care["water"] >= limit * 1000, "standby": standby, "rssi": self.care.get("rssi", -71)}

    @staticmethod
    def curve(shot):
        """a curve as src/shotHistory.h writes it, made up from the shot"""
        n = int((shot["s"] + 4) / 0.5) + 1
        stop = int(shot["s"] / 0.5) + 1
        def grams(k):
            t = k * 0.5
            if t < 6: return 0
            return round(min(t - 6, shot["s"] - 6) / (shot["s"] - 6) * shot["g"] * 10 + (4 if t > shot["s"] else 0))
        w = None if shot.get("g") is None else [grams(k) for k in range(n)]
        f = None if w is None else [max(0, (w[min(k + 1, n - 1)] - w[max(k - 1, 0)]) * 10) for k in range(n)]  # hundredths of g/s
        temp = [round((93.5 - 1.6 * (1 if 4 < k * 0.5 < shot["s"] else 0) * min(1, (k * 0.5 - 4) / 6)) * 10) for k in range(n)]
        return {"dt": 500, "stop": stop, "w": w, "t": temp, "f": f}

    def json_param(self, name):
        d = self.p[name]
        out = {"type": d["type"], "name": name, "displayName": name, "section": 0, "position": 0, "hasHelpText": False,
               "show": True, "reboot": d.get("reboot", False), "value": d["value"], "min": d["min"], "max": d["max"]}
        if d["type"] == 5:
            out["options"] = [{"value": i, "label": l} for i, l in enumerate(d["options"])]
        return out


S = State()
BUSY_EVERY = 0
counter = {"get": 0}


class Handler(BaseHTTPRequestHandler):
    protocol_version = "HTTP/1.1"

    def log_message(self, *a):
        pass

    def send(self, code, body=b"", ctype="text/plain", extra=None):
        if isinstance(body, str):
            body = body.encode()
        self.send_response(code)
        self.send_header("Content-Type", ctype)
        self.send_header("Content-Length", str(len(body)))
        self.send_header("Connection", "close")
        for k, v in (extra or {}).items():
            self.send_header(k, v)
        self.end_headers()
        self.wfile.write(body)

    def do_GET(self):
        url = urllib.parse.urlparse(self.path)
        q = urllib.parse.parse_qs(url.query)
        if not url.path.startswith("/__"):
            with S.lock:
                S.check_beans()
        if url.path in ("/", "/index.html"):
            S.page_loads += 1
            with open(PAGE, "rb") as f:
                return self.send(200, f.read(), "text/html; charset=utf-8", {"Cache-Control": "no-cache"})
        if url.path == "/parameters":
            counter["get"] += 1
            if BUSY_EVERY and counter["get"] % BUSY_EVERY == 0:
                return self.send(503, "busy, retry", extra={"Retry-After": "2"})
            names = q.get("names", [""])[0].split(",") if "names" in q else list(S.p)
            limit = int(q.get("limit", ["5"])[0]); offset = int(q.get("offset", ["0"])[0])
            with S.lock:
                hits = [n for n in S.p if n in names and S.shown(n)]
                page = [S.json_param(n) for n in hits[offset:offset + limit]]
            return self.send(200, json.dumps({"parameters": page, "offset": offset, "limit": limit, "returned": len(page)}), "application/json")
        if url.path == "/timeseries":
            n = 30
            return self.send(200, json.dumps({"currentTemps": [22 + i * 2 for i in range(n)], "targetTemps": [95] * n, "heaterPowers": [100] * n}), "application/json")
        if url.path.lstrip("/") in ASSETS:
            name = url.path.lstrip("/")
            kind = "application/manifest+json" if name.endswith(".json") else "font/woff2" if name.endswith(".woff2") else "image/png"
            return self.send(200, ASSETS[name], kind)
        if url.path == "/shots":
            with S.lock:
                shots = [{"d": None, "m": "", "r": 0, "t0": None, "fd": None, "tw": None, "sw": None, "ld": 0, "lg": None, "fs": None, "pi": None, "ch": False, **x} for x in S.shots[:5]]
                return self.send(200, json.dumps({"now": int(time.time()), "bf": S.bf, "shots": shots}), "application/json")
        if url.path == "/shot":  # curve as src/shotHistory.h writes it, made up from the shot
            i = int(q.get("i", ["0"])[0])
            with S.lock:
                shot = S.shots[i] if 0 <= i < min(5, len(S.shots)) else None
            if not shot or shot.get("nocurve"):
                return self.send(404, "no curve")
            return self.send(200, json.dumps(S.curve(shot)), "application/json")
        if url.path == "/reference":
            with S.lock:
                return self.send(200, json.dumps(S.reference), "application/json")
        if url.path == "/care":
            with S.lock:
                return self.send(200, json.dumps(S.care_json()), "application/json")
        if url.path == "/beans":
            if S.live.get("noBeans"):  # firmware without bean profiles
                return self.send(404, "not found")
            with S.lock:
                return self.send(200, json.dumps(S.beans_json()), "application/json")
        if url.path == "/scale":
            with S.lock:
                st, since = S.scale_state(), time.time() - S.scale["search_from"]
                found = [{"name": "BOOKOO_SC U 1234", "address": "c8:2e:18:aa:01:02", "rssi": -58},
                         {"name": "BOOKOO_SC 5678", "address": "c8:2e:18:aa:03:04", "rssi": -81}] if 1 < since < 30 and not S.live.get("noScales") else []
                body = {"state": st, "address": S.scale["address"], "name": S.scale["name"], "searching": since < 3, "found": found}
                if st == 2:
                    body["battery"] = 76
            return self.send(200, json.dumps(body), "application/json")
        if url.path == "/boot":  # src/orioneMachine.h: why it last started, uptime in seconds
            return self.send(200, json.dumps({"reason": S.live.get("bootReason", "power"), "uptime": 7380, "fw": S.live.get("fw", "ok"), "updateFailed": bool(S.live.get("updateFailed"))}), "application/json")
        if url.path == "/log":  # src/orioneLog.h: the last lines, also from before a restart
            return self.send(200, "--- start: power ---\n[06:16:42] I Started after: power\n[06:16:42] I Round display ready, free heap 135224 bytes\n", "text/plain; charset=utf-8")
        if url.path == "/version":
            return self.send(200, "4.0.4+mock")
        if url.path == "/download/config":
            with S.lock:
                return self.send(200, json.dumps({k: v["value"] for k, v in S.p.items()}), "application/json")
        if url.path == "/events":
            return self.events()
        if url.path == "/__posts":
            return self.send(200, json.dumps(S.posts), "application/json")
        if url.path == "/__state":
            with S.lock:
                return self.send(200, json.dumps({"values": {k: v["value"] for k, v in S.p.items()}, "restarts": S.restarts, "pageLoads": S.page_loads,
                                                  "scaleAtBoot": S.scale_at_boot}), "application/json")
        return self.send(404, "not found")

    def events(self):
        self.send_response(200)
        self.send_header("Content-Type", "text/event-stream")
        self.send_header("Cache-Control", "no-cache")
        self.end_headers()
        try:
            while True:
                with S.lock:
                    pid = S.p["pid.enabled"]["value"]
                    target = S.p["brew.setpoint"]["value"]
                    S.temp += (target - S.temp) * 0.08 if pid else (22 - S.temp) * 0.02
                    state = 10 if pid else 60
                    data = {"currentTemp": round(S.temp, 2), "targetTemp": target, "heaterPower": 100 if pid and S.temp < target - 1 else 20 if pid else 0,
                            "state": state, "brewTime": 0, "scale": S.scale_state(), "weight": 0.0 if S.scale_state() == 2 else None, "flow": None,
                            "battery": 76 if S.scale_state() == 2 else None, "warmup": 0, "pulse": 0, "cup": None, "held": False, "sw": False, "steam": 0, "pi": 0, "fp": False, "bfd": False,
                            "clean": S.clean}
                    data.update(S.live)
                self.wfile.write(f"event: new_temps\ndata: {json.dumps(data)}\n\n".encode())
                self.wfile.flush()
                time.sleep(0.5)
        except (BrokenPipeError, ConnectionResetError):
            return

    def do_POST(self):
        url = urllib.parse.urlparse(self.path)
        n = int(self.headers.get("Content-Length") or 0)
        body = self.rfile.read(n) if n else b""
        if not url.path.startswith("/__"):
            with S.lock:
                S.check_beans()  # the firmware's loop has seen the values before this request (it looks every 0.5 s)
                S.posts.append({"path": url.path, "query": url.query, "body": body.decode(errors="replace")[:200]})
        if url.path == "/parameters":
            form = urllib.parse.parse_qs(body.decode(), keep_blank_values=True)
            with S.lock:
                for k, vals in form.items():
                    if vals[0] == "" and k not in ("brew.grind", "brew.grinder", "brew.beans"):
                        continue  # the firmware ignores empty values, except for these
                    if k in S.p and S.shown(k):
                        d = S.p[k]
                        d["value"] = vals[0] if d["type"] == 4 else (int(float(vals[0])) if d["type"] in (0, 1, 5) else float(vals[0]))
            return self.send(200, "OK")
        if url.path == "/toggleBackflush" and "cleaner" in url.query:  # cleaning with detergent (src/orioneMachine.h)
            with S.lock:
                S.p["BACKFLUSH_ON"]["value"] = 1
                S.clean = 1
            return self.send(202, "ok")
        if url.path == "/toggleBackflush" or (url.path == "/toggleTareScale" and S.scale_at_boot):
            key = "BACKFLUSH_ON" if "Backflush" in url.path else "TARE_ON"
            with S.lock:
                S.p[key]["value"] = 0 if S.p[key]["value"] else 1  # blind toggle, as in the firmware
                if key == "BACKFLUSH_ON" and not S.p[key]["value"]:
                    S.clean = 0  # backflush mode off ends the cleaning
            return self.send(302, "", extra={"Location": "/"})
        if url.path == "/upload/config":
            m = re.search(rb"\{.*\}", body, re.S)  # the JSON file inside the multipart form
            try:
                cfg = json.loads(m.group(0)) if m else None
            except ValueError:
                cfg = None
            if not isinstance(cfg, dict):
                return self.send(400, json.dumps({"success": False, "message": "invalid"}), "application/json")
            with S.lock:
                S.pending_upload = cfg
            return self.send(200, json.dumps({"success": True, "message": "ok", "restart": True}), "application/json")
        if url.path in ("/scale/discover", "/scale/select", "/scale/forget"):
            q = urllib.parse.parse_qs(url.query)
            with S.lock:
                if not S.scale_at_boot:
                    return self.send(409, "scale off")
                if url.path == "/scale/discover":
                    S.scale["search_from"] = time.time()  # found after 1 s, searching for 3 s (the firmware: 12 s)
                elif url.path == "/scale/select":
                    S.scale.update(address=q.get("address", [""])[0].lower(), name=q.get("name", [""])[0], connect_at=time.time() + 1.5)
                else:
                    S.scale.update(address="", name="")
            return self.send(202, "ok")
        if url.path == "/flush":  # warm-up flush by hand: the test drives the live values itself
            if not S.p["hardware.sensors.watertank.enabled"]["value"]:
                return self.send(409, "no water level sensor")
            return self.send(202, "ok")
        if url.path == "/restart":
            with S.lock:
                S.restart()
            return self.send(200, "OK")
        if url.path in ("/wifireset", "/factoryreset"):
            return self.send(200, "OK")
        if url.path == "/shot/rate":
            q = urllib.parse.parse_qs(url.query)
            i, t = int(q.get("i", ["-1"])[0]), int(q.get("t", ["-1"])[0])
            with S.lock:
                if not (0 <= i < min(5, len(S.shots)) and 0 <= t <= 3):
                    return self.send(400, "bad rating")
                S.shots[i]["r"] = t
            return self.send(200, "OK")
        if url.path == "/beans/delete":  # as src/beanProfiles.h: not the current one
            name = urllib.parse.parse_qs(url.query).get("n", [""])[0]
            with S.lock:
                S.check_beans()
                i = S.bean_find(name)
                if i is None or S.bean_key(name) == S.bean_key(S.bean_live["n"]):
                    return self.send(409, "not that bean")
                del S.beans[i]
            return self.send(200, "OK")
        if url.path == "/shot/delete":  # as src/embeddedWebserver.h: by place, checked against time and seconds
            q = urllib.parse.parse_qs(url.query)
            i = int(q.get("i", ["-1"])[0])
            at, sec = int(q.get("at", ["-1"])[0]), float(q.get("s", ["-1"])[0])
            with S.lock:
                shots = S.shots[:5]
                if not (0 <= i < len(shots) and int(shots[i].get("at", 0)) == at and abs(shots[i]["s"] - sec) < 0.06):
                    return self.send(409, "not that shot")
                del S.shots[i]
            return self.send(200, "OK")
        if url.path == "/care/descaled":
            with S.lock:
                S.care.update(water=0, descaledAt=int(time.time()))
            return self.send(200, "OK")
        if url.path == "/shot/reference":  # as src/embeddedWebserver.h: ?clear=1, or by place checked against time and seconds
            q = urllib.parse.parse_qs(url.query)
            with S.lock:
                if "clear" in q:
                    S.reference = None
                    return self.send(200, "OK")
                i = int(q.get("i", ["-1"])[0])
                at, sec = int(q.get("at", ["-1"])[0]), float(q.get("s", ["-1"])[0])
                shots = S.shots[:5]
                if not (0 <= i < len(shots) and int(shots[i].get("at", 0)) == at and abs(shots[i]["s"] - sec) < 0.06):
                    return self.send(409, "not that shot")
                x = {"d": None, "m": "", "r": 0, "t0": None, "fd": None, "tw": None, "sw": None, "ld": 0, "lg": None, "fs": None, "pi": None, "ch": False, **shots[i]}
                S.reference = {"shot": x, "curve": S.curve(x)}
            return self.send(200, "OK")
        if url.path == "/__care":
            with S.lock:
                S.care.update(json.loads(body))
            return self.send(200, "OK")
        if url.path == "/__bf":  # JSON {"bf": shots since the last backflush}
            with S.lock:
                S.bf = json.loads(body)["bf"]
            return self.send(200, "OK")
        if url.path == "/__shot":  # JSON {"s": 25.3, "g": 36.1 or null, "at": UTC seconds or 0}
            with S.lock:
                S.shots.insert(0, json.loads(body))
            return self.send(200, "OK")
        if url.path == "/__param":
            with S.lock:
                for k, val in json.loads(body).items():
                    S.p[k]["value"] = val
            return self.send(200, "OK")
        if url.path == "/__live":
            with S.lock:
                S.live = json.loads(body or b"{}")
            return self.send(200, "OK")
        if url.path == "/__reset":
            with S.lock:
                lock = S.lock
                S.__init__()
                S.lock = lock
            return self.send(200, "OK")
        return self.send(404, "not found")


class Server(ThreadingHTTPServer):
    daemon_threads = True

    def handle_error(self, request, client_address):  # the browser drops keep-alive and SSE sockets: not an error
        import sys
        if not isinstance(sys.exc_info()[1], (ConnectionError, TimeoutError)):
            super().handle_error(request, client_address)


def main():
    global BUSY_EVERY
    ap = argparse.ArgumentParser()
    ap.add_argument("--port", type=int, default=8099)
    ap.add_argument("--busy-every", type=int, default=0)
    a = ap.parse_args()
    BUSY_EVERY = a.busy_every
    print(f"mock ESP32 on http://127.0.0.1:{a.port}", flush=True)
    Server(("127.0.0.1", a.port), Handler).serve_forever()


if __name__ == "__main__":
    main()
