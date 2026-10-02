#!/usr/bin/env python3
"""
Loads the CleverCoffee web interface the way a browser does, to check that the ESP32 survives it
(test plan B2/B3: heap, crashes, display stutter while pages load).

Single-page frontend (bundle): the first visit fetches the page and its four files in parallel
(a browser opens up to 6 connections), then the home view's data. Each later "click" switches a
tab: the view pages through /parameters (5 per request, one after another as app.js does);
home also fetches /timeseries. Files stay cached in a real browser, so clicks do not fetch them.

    /opt/homebrew/bin/python3 web_load.py 192.168.178.59 [--clicks 20] [--parallel 6] [--first-visits 1]

Prints one line per view and a summary; exit code 1 if a request failed.
"""

import argparse
import concurrent.futures
import sys
import time
import urllib.request

FILES = ["/", "/css/bundle.css", "/js/bundle.js", "/img/logo.png", "/manifest.json"]
ORIONE_FILES = ["/"]  # the Orione build serves one file (frontend-orione/index.html)
VIEWS = {
    "home": "names=pid.enabled,brew.setpoint,STEAM_MODE,BACKFLUSH_ON,TARE_ON,CALIBRATION_ON",
    "settings": "filter=behavior",
    "hardware": "filter=hardware",
}


def fetch(base, path):
    req = urllib.request.Request(base + path, headers={"Accept-Encoding": "gzip"})
    start = time.time()

    try:
        with urllib.request.urlopen(req, timeout=10) as r:
            body = r.read()
            return path, r.status, body, time.time() - start
    except urllib.error.HTTPError as e:
        return path, e.code, b"", time.time() - start
    except Exception as e:  # timeout, connection reset: the ESP32 crashed or ran out of memory
        return path, type(e).__name__, b"", time.time() - start


def parameter_pages(base, query):
    """Pages through /parameters like app.js fetchParameters(): 5 at a time, one after another"""
    results, offset = [], 0

    while True:
        r = fetch(base, f"/parameters?offset={offset}&limit=5&{query}")
        results.append(r)

        if r[1] != 200 or r[2].count(b'"name"') < 5:
            return results

        offset += 5


def view(pool, base, name, first):
    results = list(pool.map(lambda p: fetch(base, p), FILES)) if first else []

    if name == "home":
        timeseries = pool.submit(fetch, base, "/timeseries")
        results += parameter_pages(base, VIEWS[name])
        results.append(timeseries.result())
    else:
        results += parameter_pages(base, VIEWS[name])

    bad = [(p, s) for p, s, _, _ in results if s != 200]
    slowest = max(r[3] for r in results)
    return len(results), bad, slowest


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("host")
    ap.add_argument("--clicks", type=int, default=20)
    ap.add_argument("--parallel", type=int, default=6)
    ap.add_argument("--first-visits", type=int, default=1, help="fresh page loads (all files) before the clicks")
    ap.add_argument("--orione", action="store_true", help="Orione page: one file instead of the stock bundle")
    args = ap.parse_args()
    base = "http://" + args.host
    if args.orione:
        FILES[:] = ORIONE_FILES
    failed = 0
    order = ["home", "settings", "hardware"]

    with concurrent.futures.ThreadPoolExecutor(max_workers=args.parallel) as pool:
        steps = [("home", True)] * args.first_visits + [(order[(i + 1) % 3], False) for i in range(args.clicks)]

        for i, (name, first) in enumerate(steps):
            n, bad, slowest = view(pool, base, name, first)
            failed += len(bad)
            what = "erster Besuch" if first else "Reiter"
            print(f"{i:2d} {what:13s} {name:9s} {n:2d} Anfragen, langsamste {slowest:5.2f} s" + (f", FEHLER: {bad}" if bad else ""), flush=True)

    print(f"Fertig: {failed} fehlgeschlagene Anfragen")
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
