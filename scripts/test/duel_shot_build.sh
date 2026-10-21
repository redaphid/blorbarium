#!/bin/bash
# Builds tools/duel_shot.cpp with g++ against a lib/paint tree (default this
# one's), linked with the paint bench's blorb objects: a duel shot of another
# renderer, e.g. a commit's lib/paint exported with `git archive`, for before
# and after sheets.
#   bash scripts/test/duel_shot_build.sh <out-binary> [paint-dir]
set -euo pipefail
root="$(cd "$(dirname "$0")/../.." && pwd)"
out="$1" paint="${2:-$root/lib/paint}"
bash "$root/scripts/test/paint_bench.sh" 1 > /dev/null   # the blorb objects
obj="$out.obj"
mkdir -p "$obj"
pids=()
for src in "$paint"/src/*.cpp "$root/tools/duel_shot.cpp"; do
  g++ -std=c++17 -O2 -I"$paint/include" -I"$paint/src" -I"$root/lib/blorb/include" -I"$root/lib/blorb/src" -I"$root/pets" \
      -I"$root/pets/grungo" -I"$root/test/support" -c "$src" -o "$obj/$(basename "${src%.cpp}").o" &
  pids+=($!)
done
for p in "${pids[@]}"; do wait "$p"; done
g++ -o "$out" "$obj"/*.o "$root"/outputs/paint_bench/common/blorb_*.o
