#!/bin/bash
# Runs the end-to-end learning scenarios (test/test_learning) in WSL survivor:
#   MSYS_NO_PATHCONV=1 wsl -d survivor --exec bash tools/e2e_learning.sh
#   wsl -d survivor --exec env E2E_OUT=/mnt/c/... E2E_SEEDS=40 bash tools/e2e_learning.sh
# E2E_OUT, when set, receives each scenario's numbers (<name>.json) and the
# probe window's frames for one representative seed (frames/<name>/...), which
# tools/e2e_sheets.py turns into contact sheets and videos.
set -euo pipefail
src="$(cd "$(dirname "$0")/.." && pwd)"
dst="$HOME/.cache/blorbarium-build/$(printf '%s' "$src" | md5sum | cut -c1-12)"
mkdir -p "$dst"
rsync -a --delete --exclude .pio --exclude .git --exclude outputs "$src/" "$dst/"
cd "$dst"
args=(test -e e2e)
if [ $# -gt 0 ]; then args+=(-f "$1"); shift; fi
exec ~/.platformio/penv/bin/pio "${args[@]}" "$@"
