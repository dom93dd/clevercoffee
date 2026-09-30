#!/bin/sh
# Builds the simulator and starts it. Arguments are passed on, e.g.
#   ./run.sh --scale 3
#   ./run.sh --gallery gallery.png
set -e
cd "$(dirname "$0")"
pio run -s
exec .pio/build/sim/program "$@"
