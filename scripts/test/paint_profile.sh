#!/bin/bash
# Where the bench's time goes, by function (gprof): the perf climb's first look
# at each step (docs/notes/perf-climb.md). Small helpers stay out of line so they show.
#   bash scripts/test/paint_profile.sh [reps=3] [lines=40]
set -euo pipefail
root="$(cd "$(dirname "$0")/../.." && pwd)"
out="$root/outputs/paint_profile"
mkdir -p "$out"
g++ -std=c++17 -O2 -pg -fno-inline-small-functions -fno-inline-functions-called-once \
    -I"$root/lib/blorb/include" -I"$root/lib/blorb/src" -I"$root/lib/paint/include" -I"$root/lib/paint/src" \
    -I"$root/pets" -I"$root/pets/grungo" -I"$root/test/support" \
    "$root/scripts/test/paint_bench.cpp" "$root"/lib/paint/src/*.cpp "$root"/lib/blorb/src/*.cpp -o "$out/paint_bench_pg"
(cd "$out" && ./paint_bench_pg "${1:-3}" && gprof -b -p paint_bench_pg gmon.out | head -"${2:-40}")
