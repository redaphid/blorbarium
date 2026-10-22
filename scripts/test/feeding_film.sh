#!/bin/bash
# Films a feeding (test/test_feeding_film) on this checkout ("after") and on
# the commit before the feeding show ("before", default the merge base with
# origin/main), and makes PNGs and before/after sheets in outputs/feeding/.
#   bash scripts/test/feeding_film.sh [before-commit]
# The "before" tree is that commit with only the film and the shell's pure
# body path (feeding_badge.h, src/hw/feel.h, and the knocks' and sample's
# new fields it reads) laid over it, so the two films are the same knocks.
set -euo pipefail
cd "$(dirname "$0")/../.."
here="$PWD"
base="${1:-$(git merge-base HEAD origin/main)}"
out="$here/outputs/feeding"
work="$here/.pio/feeding-before"
mkdir -p "$out/ppm_before" "$out/ppm_after" "$work"
export PLATFORMIO_RUN_JOBS=3
# Through the laptop's build lock when it has one (one compile at a time).
pio="${PIO:-$HOME/Worktrees/blorbarium/pio}"
[ -x "$pio" ] || pio=~/.platformio/penv/bin/pio

"$pio" test -e native -f test_feeding_film --without-testing > /dev/null
FEEDING_DUMP="$out/ppm_after" .pio/build/native/program --gtest_filter='Film.*' > /dev/null

rm -rf "$work/src" "$work/lib" "$work/test" "$work/pets"
git archive "$base" | tar -x -C "$work"
for f in test/support/feeding_badge.h test/test_feeding_film/test_main.cpp src/hw/feel.h \
         lib/blorb/include/blorb/knocks.h lib/blorb/include/blorb/senses.h; do
  mkdir -p "$work/$(dirname "$f")"
  cp "$f" "$work/$f"
done
(cd "$work" && "$pio" test -e native -f test_feeding_film --without-testing > /dev/null)
FEEDING_DUMP="$out/ppm_before" "$work/.pio/build/native/program" --gtest_filter='Film.*' > /dev/null

python3 scripts/test/feeding_sheet.py "$out"
echo "before: $base"
