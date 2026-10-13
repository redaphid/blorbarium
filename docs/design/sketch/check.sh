#!/usr/bin/env bash
# Compiles every sketch header alone and usage.cpp against the engine headers
# on this branch, then plants three bad registry rows and expects each to fail.
# WSL: wsl -d survivor --exec bash -lc 'cd /mnt/d/Projects/_wt/blorb-battles && docs/design/sketch/check.sh'
set -u
here="$(cd "$(dirname "$0")" && pwd)"
repo="$(cd "$here/../../.." && pwd)"
# The engine's own headers come first: once a unit lands a header for real
# (species.h, stakes.h), the sketch's copy is the history of the contract, and
# every other sketch must compile against what was built.
flags=(-std=c++17 -fsyntax-only -Wall -Wextra -Werror -Werror=narrowing -Wno-pragma-once-outside-header
       -I"$repo/lib/blorb/include" -I"$here")
fail=0
for f in "$here"/blorb/*.h "$here"/hw/*.h "$here"/test/*.h "$here"/tools/*.h "$here"/usage.cpp; do
  if [ -f "$repo/lib/blorb/include/blorb/$(basename "$f")" ] && [ "$(dirname "$f")" = "$here/blorb" ]; then
    echo "real ${f#$here/} (landed in lib/blorb; the sketch is superseded)"; continue
  fi
  if g++ "${flags[@]}" -x c++ "$f"; then echo "ok   ${f#$here/}"; else echo "FAIL ${f#$here/}"; fail=1; fi
done
plant() {   # plant <label> <sed expression on species.def or powers.def> <file> <expected message>
  # Plant into a copy of the ENGINE's tables: species.h has landed in lib/blorb.
  local tmp; tmp="$(mktemp -d)"; cp -r "$repo/lib/blorb/include/blorb" "$tmp/"
  sed -i "$2" "$tmp/blorb/defs/$3"
  if out=$(g++ -std=c++17 -fsyntax-only -Wall -Wextra -Werror -I"$tmp" -x c++ "$tmp/blorb/species.h" 2>&1); then
    echo "FAIL planted $1 compiled"; fail=1
  elif grep -q "$4" <<<"$out"; then echo "ok   planted $1 refused: $4"
  else echo "FAIL planted $1 refused for another reason"; echo "$out" | head -5; fail=1; fi
  rm -rf "$tmp"
}
plant "two species on one wheel cell" 's/BLORB_SPECIES(1, mossby,   Sprout, Guard/BLORB_SPECIES(1, mossby,   Sprout, Trick/' species.def "every (element, style) pair exactly once"
plant "a shared power" 's/BLORB_SPECIES(8, tinker,   Spark,  Guard,  gadget/BLORB_SPECIES(8, tinker,   Spark,  Guard,  zap/' species.def "a shared power"
plant "a cost the meter cannot hold" 's/BLORB_POWER(6, zap,     6,/BLORB_POWER(6, zap,     9,/' powers.def "a cost the meter cannot hold"
g++ -std=c++17 -I"$repo/lib/blorb/include" -I"$here" "$here/tools/sizes.cpp" -o /tmp/blorb_sketch_sizes && /tmp/blorb_sketch_sizes || fail=1
exit $fail
