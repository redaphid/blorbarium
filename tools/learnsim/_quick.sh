#!/bin/bash
# Throwaway: foresee share under starter-genome variants. Not committed.
set -euo pipefail
src="$(cd "$(dirname "$0")/../.." && pwd)"
variant() {
  local name=$1; shift
  local out="$HOME/.cache/learnsim/q-$name"
  mkdir -p "$out/tree"
  rsync -a --delete --exclude .git --exclude .pio --exclude outputs "$src/" "$out/tree/"
  for expr in "$@"; do sed -i "$expr" "$out/tree/lib/blorb/src/starter_genome.cpp" "$out/tree/lib/blorb/include/blorb/defs/actions.def"; done
  (cd "$out" && g++ -std=c++17 -O2 -Itree/lib/blorb/include -Itree/test/support tree/lib/blorb/src/*.cpp tree/tools/learnsim/life.cpp -o life)
  for st in rich doting quiet; do for s in 1 2 3; do (cd "$out" && FEED=demand ./life $st $s 4 > $st$s.txt) & done; done
  wait
  python3 - "$out" "$name" <<'EOF'
import glob, sys
tot = fore = bored = n = 0
for p in glob.glob(sys.argv[1] + "/*.txt"):
    for l in open(p):
        f = l.split()
        if f[:2] == ["M", "ticks"] and int(f[2]) >= 1:
            v = list(map(int, f[3:])); tot += sum(v); fore += v[4]
        if f[:3] == ["M", "drive", "boredom"]:
            bored += float(f[4]); n += 1
print("%-10s foresee share of waking time %.3f   boredom<0.02 %.2f" % (sys.argv[2], fore / tot, bored / n))
EOF
}
variant hold25 's/BLORB_ACTION( 4, foresee,     Foresee,     40, self_foresaw)/BLORB_ACTION( 4, foresee,     Foresee,     25, self_foresaw)/'
