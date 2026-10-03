#!/usr/bin/env bash
# Compiles the v2 final sketch against origin/engine's headers, each header
# alone, plants four bad rows and confirms each fails with its own message,
# then prints the measured sizes. Run in WSL `survivor`:
#   wsl -d survivor --exec bash <this dir>/check.sh
set -euo pipefail
HERE="$(cd "$(dirname "$0")" && pwd)"
REPO="${BLORB_REPO:-/mnt/d/Projects/blorbarium}"
WORK="$(mktemp -d)"
trap 'rm -rf "$WORK"' EXIT

mkdir -p "$WORK/ref"
git -c safe.directory='*' -C "$REPO" archive origin/engine lib/blorb/include | tar -x -C "$WORK/ref"
echo "engine headers: origin/engine $(git -c safe.directory='*' -C "$REPO" rev-parse --short origin/engine)"

FLAGS=(-std=c++17 -fsyntax-only -Wall -Wextra -Werror=narrowing)
inc() { echo "-I$1 -I$WORK/ref/lib/blorb/include"; }

g++ "${FLAGS[@]}" $(inc "$HERE") "$HERE/usage_check.cpp"
echo "usage_check.cpp: ok"
for h in v2_registry genes_v2 oracle world games; do
  printf '#include "blorb/%s.h"\n' "$h" > "$WORK/one.cpp"
  g++ "${FLAGS[@]}" $(inc "$HERE") "$WORK/one.cpp"
  echo "$h.h alone: ok"
done

plant() {   # plant <name> <def file> <row> <expected message>
  local dir="$WORK/plant-$1"
  mkdir -p "$dir/blorb/defs"
  cp "$HERE/blorb/defs/$2" "$dir/blorb/defs/$2"
  echo "$3" >> "$dir/blorb/defs/$2"
  if g++ "${FLAGS[@]}" -I"$dir" $(inc "$HERE") "$HERE/usage_check.cpp" 2> "$dir/log"; then
    echo "planted $1: NOT caught"; exit 1
  fi
  grep -q "$4" "$dir/log" && echo "planted $1: caught ($4)" || { echo "planted $1: failed for another reason"; cat "$dir/log"; exit 1; }
}
plant duplicate_route gestures.def "BLORB_ROUTE(Live, button, knock)" "a (mode, stimulus) pair has two routes"
plant routed_hold     gestures.def "BLORB_ROUTE(Wheel, button_hold, none)" "BOOT hold feeds him in every mode"
plant improv_omen     omens.def    "BLORB_OMEN(16, azerbaijan, rain, luck, moon)" "puts an omen on an improv topic"
plant no_station      actions.def  "BLORB_ACTION(14, hoard, Hoard, 30, none)" "every action needs exactly one station"
plant shake_capture   games.def    "BLORB_GAME(6, peek, Snap, stimBit(stim::shake), 60, 0, 4, 2, lid)" "a game captures BOOT hold, a drop, a shake or the lid"

g++ -std=c++17 -DSIZES $(inc "$HERE") "$HERE/usage_check.cpp" -o "$WORK/sizes"
"$WORK/sizes"
