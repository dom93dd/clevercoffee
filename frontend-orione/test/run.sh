#!/bin/bash
# Browser test of the Orione web page without hardware: starts two mock ESP32s (one answers every
# third GET /parameters with 503, like the request gate) and runs ui_test.mjs in Chromium.
#
#   ./run.sh [--only <name part>] [--headed]
#
# Needs node (nvm) and Playwright 1.62.1; the first run installs it into node_modules (from the npm
# cache when it is there) and the matching Chromium if it is missing. Screenshots: out/*.png
set -euo pipefail
cd "$(dirname "$0")"
PY=/opt/homebrew/bin/python3 # never /usr/bin/python3 on this Mac (CLAUDE.md)

if [ ! -d node_modules/playwright ]; then
    npm install --prefer-offline --no-audit --no-fund --silent
    npx playwright install chromium >/dev/null
fi

$PY mock_esp32.py --port 8099 >/dev/null & A=$!
$PY mock_esp32.py --port 8098 --busy-every 3 >/dev/null & B=$!
trap 'kill $A $B 2>/dev/null' EXIT
for _ in $(seq 50); do curl -sf http://127.0.0.1:8099/__state >/dev/null && curl -sf http://127.0.0.1:8098/__state >/dev/null && break; sleep 0.1; done

node ui_test.mjs --base http://127.0.0.1:8099 --busy http://127.0.0.1:8098 "$@"
