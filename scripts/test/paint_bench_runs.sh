#!/bin/bash
# The perf climb's measuring stick (docs/notes/perf-climb.md): builds the bench
# with scripts/test/paint_bench.sh, then runs it RUNS times pinned to one core
# and prints the median and the spread of each, with the hash (which must not move).
#   bash scripts/test/paint_bench_runs.sh [runs=7] [reps=3] [cpu=2]
set -euo pipefail
root="$(cd "$(dirname "$0")/../.." && pwd)"
runs="${1:-7}" reps="${2:-3}" cpu="${3:-2}"
bash "$root/scripts/test/paint_bench.sh" 1 > /dev/null
bin="$root/outputs/paint_bench/paint_bench"
dish=() duel=() hashes=()
for _ in $(seq "$runs"); do
  line="$(taskset -c "$cpu" "$bin" "$reps" | grep dish_frames)"
  dish+=("$(sed -E 's/.*dish_frames=[0-9]+ us_per_frame=([0-9.]+).*/\1/' <<< "$line")")
  duel+=("$(sed -E 's/.*duel_frames=[0-9]+ us_per_frame=([0-9.]+).*/\1/' <<< "$line")")
  hashes+=("$(sed -E 's/.*hash=([0-9a-f]+).*/\1/' <<< "$line")")
done
stats() { printf '%s\n' "$@" | sort -n | awk '{v[NR]=$1} END {printf "median=%.1f min=%.1f max=%.1f", v[int((NR+1)/2)], v[1], v[NR]}'; }
echo "dish us/frame $(stats "${dish[@]}")"
echo "duel us/frame $(stats "${duel[@]}")"
echo "hash $(printf '%s\n' "${hashes[@]}" | sort -u | tr '\n' ' ')"
