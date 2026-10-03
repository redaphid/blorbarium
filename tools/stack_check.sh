#!/bin/bash
# Fails if any engine or renderer function needs more stack than the budget,
# measured with the ESP32-S3 compiler and -fstack-usage. Run in WSL survivor:
#   MSYS_NO_PATHCONV=1 wsl -d survivor --exec bash tools/stack_check.sh
# wsl_test.sh runs it before the native suite.
#
# The budget: setup() and loop() run on the Arduino loop task, whose stack is
# 8,192 B (arduino-esp32's default). 2,560 B is under a third of it. One
# expressed Phenotype (2,216 B on Xtensa) fits under it, as presentEgg and
# portrait hold. Two do not, and nor does a Brain (5,560 B), a Creature
# (10,196 B) or a Snapshot (10,424 B) held by value.
set -euo pipefail
budget=${STACK_BUDGET:-2560}
repo="$(cd "$(dirname "$0")/.." && pwd)"
xt=~/.platformio/packages/toolchain-xtensa-esp32s3/bin/xtensa-esp32s3-elf-g++
inc=(-I"$repo/lib/blorb/include" -I"$repo/lib/paint/include")
out="$(mktemp -d)"
trap 'rm -rf "$out"' EXIT
pids=()
for f in "$repo"/lib/blorb/src/*.cpp "$repo"/lib/paint/src/*.cpp; do
  "$xt" -std=gnu++17 -O2 -mlongcalls "${inc[@]}" -fstack-usage -c "$f" -o "$out/$(basename "$f" .cpp).o" &
  pids+=($!)
done
for p in "${pids[@]}"; do wait "$p"; done
frames="$out/frames.txt"
cat "$out"/*.su | awk -F'\t' '{ printf "%7d  %-8s  %s\n", $2, $3, $1 }' | sort -n -r > "$frames"
over=$(awk -v b="$budget" '$1 > b' "$frames")
echo "largest frames (budget $budget B):"
head -${STACK_TOP:-12} "$frames"
if [ -n "$over" ]; then
  echo "FAIL: $(echo "$over" | wc -l) frame(s) over $budget B:"
  echo "$over"
  exit 1
fi
echo "ok: every frame is within $budget B"
