#!/usr/bin/env python3
"""
Stand-in for the ESP32 web API of the Orione build, so the web page can be tested without hardware.
Serves frontend-orione/index.html and the endpoints the page uses, with the firmware's rules:
brew-by-time settings only in automatic mode, scale settings only with the scale on, blind toggles
for backflush and tare, a config upload that needs a restart, SSE live values.

    /opt/homebrew/bin/python3 mock_esp32.py [--port 8099] [--busy-every N]

--busy-every N answers every N-th GET /parameters with 503, like the request gate of the firmware.
Test hooks: GET /__posts (POST requests so far), GET /__state (values, restarts),
POST /__live (JSON merged into the SSE values), POST /__shot (log a shot), POST /__reset (fresh state).
"""

import argparse
import copy
import json
import re
import os
import threading
import time
import urllib.parse
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer

PAGE = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "index.html")

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
    "brew.presets": dict(type=4, value="25,36;30,45;45,80", min=0, max=48),
    "backflush.cycles": dict(type=0, value=5, min=2, max=20),
    "backflush.fill_time": dict(type=2, value=5.0, min=3, max=10),
    "backflush.flush_time": dict(type=2, value=10.0, min=5, max=20),
    "BACKFLUSH_ON": dict(type=1, value=0, min=0, max=1),
    "TARE_ON": dict(type=1, value=0, min=0, max=1),
    "standby.enabled": dict(type=1, value=0, min=0, max=1),
    "standby.time": dict(type=2, value=35, min=5, max=300),
    "display.language": dict(type=5, value=0, min=0, max=2, options=["Deutsch", "English", "Español"]),
    "display.blescale_brew_timer": dict(type=1, value=0, min=0, max=1),
    "display.post_brew_timer_duration": dict(type=2, value=3.0, min=0, max=60),
    "display.blinking.delta": dict(type=2, value=0.3, min=0, max=10),
    "system.hostname": dict(type=4, value="orione", min=0, max=32, reboot=True),
    "system.ota_password": dict(type=4, value="otapass", min=0, max=32),
    "system.log_level": dict(type=5, value=2, min=0, max=6, options=["TRACE", "DEBUG", "INFO", "WARNING", "ERROR", "FATAL", "SILENT"]),
    "hardware.sensors.watertank.enabled": dict(type=1, value=0, min=0, max=1, reboot=True),
    "hardware.sensors.watertank.mode": dict(type=5, value=1, min=0, max=1, options=["Normally Open", "Normally Closed"], reboot=True),
    "hardware.sensors.scale.enabled": dict(type=1, value=1, min=0, max=1, reboot=True),
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
        self.live = {}  # POST /__live: fields that override the SSE values (e.g. a running shot)
        self.temp = 22.0
        self.lock = threading.Lock()

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
        self.restarts += 1

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
        if url.path == "/shots":
            with S.lock:
                return self.send(200, json.dumps({"now": int(time.time()), "shots": S.shots[:5]}), "application/json")
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
                            "state": state, "brewTime": 0}
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
                S.posts.append({"path": url.path, "body": body.decode(errors="replace")[:200]})
        if url.path == "/parameters":
            form = urllib.parse.parse_qs(body.decode())
            with S.lock:
                for k, vals in form.items():
                    if k in S.p and S.shown(k):
                        d = S.p[k]
                        d["value"] = vals[0] if d["type"] == 4 else (int(float(vals[0])) if d["type"] in (1, 5) else float(vals[0]))
            return self.send(200, "OK")
        if url.path == "/toggleBackflush" or (url.path == "/toggleTareScale" and S.scale_at_boot):
            key = "BACKFLUSH_ON" if "Backflush" in url.path else "TARE_ON"
            with S.lock:
                S.p[key]["value"] = 0 if S.p[key]["value"] else 1  # blind toggle, as in the firmware
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
        if url.path == "/restart":
            with S.lock:
                S.restart()
            return self.send(200, "OK")
        if url.path in ("/wifireset", "/factoryreset"):
            return self.send(200, "OK")
        if url.path == "/__shot":  # JSON {"s": 25.3, "g": 36.1 or null, "at": UTC seconds or 0}
            with S.lock:
                S.shots.insert(0, json.loads(body))
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
