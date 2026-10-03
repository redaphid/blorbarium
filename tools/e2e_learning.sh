#!/bin/bash
# Runs the end-to-end learning scenarios (test/test_learning) in WSL survivor:
#   MSYS_NO_PATHCONV=1 wsl -d survivor --exec bash tools/e2e_learning.sh
#   wsl -d survivor --exec env E2E_OUT=~/e2e E2E_SEEDS=40 bash tools/e2e_learning.sh test_learning -a --gtest_filter='Scenarios/*'
# With E2E_OUT set, each scenario writes its numbers (<name>.json) and its
# clip's frames there, and tools/e2e_sheets.py then makes a contact sheet and
# a side-by-side video per scenario in E2E_SHEETS and E2E_VIDEOS (default:
# under E2E_OUT), with E2E_PYTHON (default python3; one with imageio-ffmpeg
# gives MP4 rather than GIF). Keep E2E_OUT on WSL-local disk: frames over /mnt
# are slow.
set -euo pipefail
src="$(cd "$(dirname "$0")/.." && pwd)"
dst="$HOME/.cache/blorbarium-build/$(printf '%s' "$src" | md5sum | cut -c1-12)"
mkdir -p "$dst"
rsync -a --delete --exclude .pio --exclude .git --exclude outputs "$src/" "$dst/"
cd "$dst"
args=(test -e e2e)
if [ $# -gt 0 ]; then args+=(-f "$1"); shift; fi
status=0
~/.platformio/penv/bin/pio "${args[@]}" "$@" || status=$?
if [ -n "${E2E_OUT:-}" ]; then
  "${E2E_PYTHON:-python3}" tools/e2e_sheets.py "$E2E_OUT" "${E2E_SHEETS:-$E2E_OUT/sheets}" "${E2E_VIDEOS:-$E2E_OUT/videos}"
fi
exit $status
