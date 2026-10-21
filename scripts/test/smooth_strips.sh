#!/bin/bash
# Before and after frames between ticks, at the badge's 50 fps, from the sim:
# an idle pet and a hop, each recorded with --steps (each tick's own frame, as
# before) and without (paint/tween.h), then strips and side-by-side GIFs.
#   bash scripts/test/smooth_strips.sh          (pio run -e sim first)
# Writes outputs/smooth-motion/.
set -euo pipefail
root="$(cd "$(dirname "$0")/../.." && pwd)"
sim="$root/.pio/build/sim/program"
out="$root/outputs/smooth-motion"
mkdir -p "$out"

# record <name> <feed> <from ms> <for ms> [sim flags...]
record() {
  local name="$1" feed="$2" from="$3" until="$4"
  shift 4
  rm -rf "$out/$name"
  mkdir -p "$out/$name"
  "$sim" --headless --clock fixed --script "$root/tests/feeds/$feed.txt" --frame-ms 20 --record "$out/$name" --fps 50 \
    --from "$from" --for "$until" "$@" > /dev/null 2>&1 || true
}

record idle_before idle 16600 18600 --steps
record idle_after idle 16600 18600
record hop_before hop 16960 18200 --steps
record hop_after hop 16960 18200
record walk_before eating 2000 3200 --steps
record walk_after eating 2000 3200
species=(mossby puffkin gilly mothwick mudge fizz hushwing tinker octopus)
for sp in "${species[@]}"; do
  record "idle_${sp}_after" idle 16600 18600 --species "$sp"
done

strip="$root/scripts/test/film_strip.py"
python3 "$strip" "$out/idle_strip.png" "before (10 Hz steps)=$out/idle_before" "after (between ticks, x1.2)=$out/idle_after" \
  --cols 15 --scale 0.5 --gif "$out/idle_side_by_side.gif" --fps 50
python3 "$strip" "$out/hop_strip.png" "before=$out/hop_before" "after=$out/hop_after" --cols 20 --scale 0.5 \
  --gif "$out/hop_side_by_side.gif" --fps 50
python3 "$strip" "$out/walk_strip.png" "before=$out/walk_before" "after=$out/walk_after" --cols 15 --scale 0.5 \
  --gif "$out/walk_side_by_side.gif" --fps 50
rows=()
for sp in "${species[@]}"; do rows+=("$sp=$out/idle_${sp}_after"); done
python3 "$strip" "$out/species_idle_strip.png" "${rows[@]}" --every 5 --cols 21 --scale 0.5
python3 "$root/scripts/test/frame_motion.py" "$out"/*_before "$out"/*_after | tee "$out/motion.txt"
