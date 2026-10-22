#!/bin/bash
# Films a death to the next hatch (test/test_clutch_film): the vigil, the
# clutch, choosing an egg, the chosen moment and the hatch, on this checkout
# ("after", the tilt picker) and on the commit before it ("before", default
# the merge base with origin/main, driven the old way: a knock knock for the
# next egg, a side hold to keep), and makes PNGs and film strips in
# outputs/egg-ux/.
#   bash scripts/test/clutch_film.sh [before-commit]
# The "before" tree is that commit with only the film and its harness
# (test/support/feeding_badge.h) laid over it. Builds go through the
# laptop's one-at-a-time pio ($PIO, default ~/Worktrees/blorbarium/pio).
set -euo pipefail
cd "$(dirname "$0")/../.."
here="$PWD"
base="${1:-$(git merge-base HEAD origin/main)}"
out="$here/outputs/egg-ux"
work="$here/.pio/clutch-before"
pio="${PIO:-$HOME/Worktrees/blorbarium/pio}"
[ -x "$pio" ] || pio=~/.platformio/penv/bin/pio
mkdir -p "$out/ppm_before" "$out/ppm_after" "$work"
rm -f "$out"/ppm_before/*.ppm "$out"/ppm_after/*.ppm
export PLATFORMIO_RUN_JOBS=3

"$pio" test -e native -f test_clutch_film --without-testing > /dev/null
CLUTCH_DUMP="$out/ppm_after" .pio/build/native/program --gtest_filter='Film.*' > /dev/null

rm -rf "$work/src" "$work/lib" "$work/test" "$work/pets"
git archive "$base" | tar -x -C "$work"
for f in test/support/feeding_badge.h test/test_clutch_film/test_main.cpp; do
  mkdir -p "$work/$(dirname "$f")"
  cp "$f" "$work/$f"
done
(cd "$work" && "$pio" test -e native -f test_clutch_film --without-testing > /dev/null)
CLUTCH_DUMP="$out/ppm_before" CLUTCH_GESTURES=old "$work/.pio/build/native/program" --gtest_filter='Film.*' > /dev/null

python3 scripts/test/clutch_sheet.py "$out"
echo "before: $base"
