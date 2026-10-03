#!/bin/bash
# Builds the simulator and holds every frame golden, in WSL survivor:
#   wsl -d survivor --exec bash /mnt/d/<this checkout>/tools/sim_film.sh [case ...]
#   wsl -d survivor --exec env UPDATE_GOLDEN=1 bash /mnt/d/<this checkout>/tools/sim_film.sh
# The tree is mirrored to WSL-local disk first, as wsl_test.sh does. What the
# run writes comes back: outputs/ (frames and misses) always, test/golden/
# when blessing.
set -euo pipefail
src="$(cd "$(dirname "$0")/.." && pwd)"
dst="$HOME/.cache/blorbarium-build/$(printf '%s' "$src" | md5sum | cut -c1-12)"
mkdir -p "$dst"
rsync -a --delete --exclude .pio --exclude .git --exclude outputs "$src/" "$dst/"
cd "$dst"
~/.platformio/penv/bin/pio run -e sim -s
status=0
python3 tests/film.py "$@" || status=$?
mkdir -p "$src/outputs"
rsync -a --delete outputs/ "$src/outputs/"
if [ -n "${UPDATE_GOLDEN:-}" ]; then rsync -a test/golden/ "$src/test/golden/"; fi
exit $status
