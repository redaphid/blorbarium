#!/bin/bash
# Builds and runs scripts/test/paint_bench.cpp: host time per frame and the
# hash of every frame drawn. Run it before and after a change to lib/paint;
# the hash must not move.
#   bash scripts/test/paint_bench.sh [reps]
#   REF=main bash scripts/test/paint_bench.sh    also builds it with main's lib/paint/src, and compares
set -euo pipefail
root="$(cd "$(dirname "$0")/../.." && pwd)"
out="$root/outputs/paint_bench"
inc=(-I"$root/lib/blorb/include" -I"$root/lib/blorb/src" -I"$root/lib/paint/include" -I"$root/lib/paint/src"
     -I"$root/pets" -I"$root/pets/grungo" -I"$root/test/support")

# objects <dir> <sources...>: one object per source, rebuilt only when its source is newer, all at once.
objects() {
  local dir="$1"; shift
  mkdir -p "$dir"
  local pids=() src obj
  for src in "$@"; do
    obj="$dir/$(basename "$(dirname "$(dirname "$src")")")_$(basename "${src%.cpp}").o"
    if [ ! -f "$obj" ] || [ "$src" -nt "$obj" ]; then
      g++ -std=c++17 -O2 "${inc[@]}" -c "$src" -o "$obj" &
      pids+=($!)
    fi
  done
  for p in "${pids[@]}"; do wait "$p"; done
}

objects "$out/common" "$root/scripts/test/paint_bench.cpp" "$root"/lib/blorb/src/*.cpp
objects "$out/paint" "$root"/lib/paint/src/*.cpp
g++ -o "$out/paint_bench" "$out"/common/*.o "$out"/paint/*.o
echo "this tree: $("$out/paint_bench" "$@")"

if [ -n "${REF:-}" ]; then
  ref="$out/ref-$(git -C "$root" rev-parse --short "$REF")"
  mkdir -p "$ref/src"
  for f in $(git -C "$root" ls-tree --name-only "$REF" lib/paint/src/); do
    git -C "$root" show "$REF:$f" > "$ref/src/$(basename "$f").new"
    cmp -s "$ref/src/$(basename "$f").new" "$ref/src/$(basename "$f")" || mv "$ref/src/$(basename "$f").new" "$ref/src/$(basename "$f")"
    rm -f "$ref/src/$(basename "$f").new"
  done
  objects "$ref/obj" "$ref"/src/*.cpp
  g++ -o "$ref/paint_bench" "$out"/common/*.o "$ref"/obj/*.o
  echo "$REF:      $("$ref/paint_bench" "$@")"
fi
