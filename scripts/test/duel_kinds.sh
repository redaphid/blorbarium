#!/bin/bash
# What each duel prop kind costs on the host (scripts/test/duel_kinds.cpp),
# linked against the paint bench's objects (scripts/test/paint_bench.sh builds them).
#   bash scripts/test/duel_kinds.sh [reps=15] [cpu=3]
set -euo pipefail
root="$(cd "$(dirname "$0")/../.." && pwd)"
out="$root/outputs/paint_bench"
bash "$root/scripts/test/paint_bench.sh" 1 > /dev/null
g++ -std=c++17 -O2 -I"$root/lib/blorb/include" -I"$root/lib/blorb/src" -I"$root/lib/paint/include" -I"$root/lib/paint/src" \
    -I"$root/pets" -I"$root/pets/grungo" -I"$root/test/support" "$root/scripts/test/duel_kinds.cpp" \
    "$out"/common/blorb_*.o "$out"/paint/*.o -o "$out/duel_kinds"
taskset -c "${2:-3}" "$out/duel_kinds" "${1:-15}"
