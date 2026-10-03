#!/bin/bash
# Re-blesses every pinned value in one run, after a change that moves them on
# purpose (the starter genome, the engine's seeding). Run in WSL survivor:
#   wsl -d survivor --exec bash tools/rebless.sh
#
#  1. The native suites' pinned literals: a verbose run, then tools/rebless.py
#     rewrites each expected string or hash constant that moved. A failure that
#     is not a pinned literal stops the run unblessed.
#  2. The frame goldens: tools/sim_film.sh with UPDATE_GOLDEN=1.
#  3. Everything again, which must now pass.
#
# Nothing here decides that a move is right: read the diff before committing.
set -uo pipefail
repo="$(cd "$(dirname "$0")/.." && pwd)"
log="$(mktemp)"
trap 'rm -f "$log"' EXIT

for pass in 1 2; do
  bash "$repo/tools/wsl_test.sh" "*" -v >"$log" 2>&1 && break
  python3 "$repo/tools/rebless.py" "$log" "$repo" || { echo "rebless: a failure that is not a pinned literal; nothing more blessed"; exit 1; }
done
UPDATE_GOLDEN=1 bash "$repo/tools/sim_film.sh" || exit 1
bash "$repo/tools/wsl_test.sh" >"$log" 2>&1 || { tail -40 "$log"; echo "rebless: still failing"; exit 1; }
bash "$repo/tools/sim_film.sh" || exit 1
echo "rebless: every pinned literal and frame golden matches this tree"
