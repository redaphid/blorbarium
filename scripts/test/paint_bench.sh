#!/bin/bash
# Builds and runs scripts/test/paint_bench.cpp: host time per frame and the
# hash of every frame drawn. Run it before and after a change to lib/paint;
# the hash must not move.
#   bash scripts/test/paint_bench.sh [reps] [threads|reverse]   (the renderer's halves on a second thread, or reversed)
#   REF=main bash scripts/test/paint_bench.sh    also builds it with main's lib/paint (its sources and its
#                                                own headers), and compares
set -euo pipefail
root="$(cd "$(dirname "$0")/../.." && pwd)"
out="$root/outputs/paint_bench"
common=(-I"$root/lib/blorb/include" -I"$root/lib/blorb/src" -I"$root/pets" -I"$root/pets/grungo" -I"$root/test/support")

# objects <dir> <paint dir> <sources...>: one object per source, against that
# lib/paint's headers, rebuilt when its source or any header is newer, all at once.
objects() {
  local dir="$1" paint="$2"; shift 2
  mkdir -p "$dir"
  local pids=() src obj
  for src in "$@"; do
    obj="$dir/$(basename "$(dirname "$(dirname "$src")")")_$(basename "${src%.cpp}").o"
    if [ ! -f "$obj" ] || [ "$src" -nt "$obj" ] ||
       [ -n "$(find "$paint" "$root/lib/blorb" "$root/scripts/test" -name '*.h' -newer "$obj" | head -1)" ]; then
      g++ -std=c++17 -O2 -I"$paint/include" -I"$paint/src" "${common[@]}" -c "$src" -o "$obj" &
      pids+=($!)
    fi
  done
  for p in "${pids[@]}"; do wait "$p"; done
}

objects "$out/common" "$root/lib/paint" "$root/scripts/test/paint_bench.cpp" "$root"/lib/blorb/src/*.cpp
objects "$out/paint" "$root/lib/paint" "$root"/lib/paint/src/*.cpp
g++ -pthread -o "$out/paint_bench" "$out"/common/*.o "$out"/paint/*.o
echo "this tree: $("$out/paint_bench" "$@")"

if [ -n "${REF:-}" ]; then
  ref="$out/ref-$(git -C "$root" rev-parse --short "$REF")"
  for f in $(git -C "$root" ls-tree -r --name-only "$REF" lib/paint/); do
    dst="$ref/${f#lib/}"
    mkdir -p "$(dirname "$dst")"
    git -C "$root" show "$REF:$f" > "$dst.new"
    cmp -s "$dst.new" "$dst" || mv "$dst.new" "$dst"
    rm -f "$dst.new"
  done
  # A renderer from before paint/phases.h or paint/parallel.h: its phases read
  # zero, and it draws on one core whatever the bench lends it.
  stub=()
  for hook in "phases.h:PhaseClock phaseClock" "parallel.h:Parallel parallel"; do
    if ! grep -q "${hook#*:};" "$ref"/paint/src/*.cpp; then
      mkdir -p "$ref/stub"
      name="${hook%%.h:*}"
      printf '#include "paint/%s"\nnamespace paint { %s; }\n' "${hook%%:*}" "${hook#*:}" > "$ref/stub/$name.cpp"
      [ -f "$ref/stub/$name.o" ] || g++ -std=c++17 -O2 -I"$root/lib/paint/include" -c "$ref/stub/$name.cpp" -o "$ref/stub/$name.o"
      stub+=("$ref/stub/$name.o")
    fi
  done
  objects "$ref/obj" "$ref/paint" "$ref"/paint/src/*.cpp
  g++ -pthread -o "$ref/paint_bench" "$out"/common/*.o "$ref"/obj/*.o "${stub[@]}"
  echo "$REF:      $("$ref/paint_bench" "$@")"
fi
