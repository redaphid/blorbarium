#!/bin/bash
# Builds learnsim against this checkout and runs the whole scenario matrix, in
# WSL survivor:
#   wsl -d survivor --exec bash tools/learnsim/run.sh <label> [seeds]
# Outputs land in ~/.cache/learnsim/<label>/out, and score.py prints the
# success-criteria table from them. Every run is deterministic, so a label
# rerun on the same tree reproduces its numbers exactly.
set -euo pipefail
label=${1:?usage: run.sh <label> [seeds]}
seeds=${2:-6}
here="$(cd "$(dirname "$0")" && pwd)"
bash "$here/build.sh" "$label"
cd "$HOME/.cache/learnsim/$label"
rm -rf out && mkdir out
jobs=()
add() { jobs+=("$*"); }   # scenario env style days
for s in $(seq 1 "$seeds"); do
  for st in rich doting quiet trainer; do add life "FEED=demand" "$st" 14 "$s"; done
  add neglect "FEED=demand" neglect 8 "$s"
  add punish "FEED=demand" punisher 6 "$s"
  add cue "FEED=demand" cuetrainer 6 "$s"
  add cue "FEED=demand" cuecontrol 6 "$s"
  add rot "FEED=demand" sloppy 8 "$s"
  add time "FEED=routine" rich 8 "$s"
done
for s in $(seq 1 $(( seeds < 4 ? seeds : 4 ))); do
  for st in doting rough punisher trainer quiet; do add lineage "FEED=demand,GARDEN=3" "$st" 40 "$s"; done
done
printf '%s\n' "${jobs[@]}" | xargs -P "$(nproc)" -I{} bash -c '
  set -- {}
  out="out/$1_$3_$5"
  env ${2//,/ } WOUT="$out.w" ./life "$3" "$5" "$4" > "$out.txt" 2>&1 || echo "FAIL $out" >&2
'
python3 "$here/score.py" out
