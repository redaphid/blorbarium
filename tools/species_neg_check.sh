#!/bin/bash
# Proves species.h's compile-time guards refuse bad rows. Each plant copies the
# engine headers, breaks one thing, and expects species.h to fail to compile
# with that guard's message and no other error, so a guard cannot pass by
# another one tripping. Then checks the ESP32-S3 compiler and the host agree on
# rosterHash, since two badges duel only if their hashes match. Run in WSL survivor:
#   MSYS_NO_PATHCONV=1 wsl -d survivor --exec bash tools/species_neg_check.sh
set -u
repo="$(cd "$(dirname "$0")/.." && pwd)"
inc="$repo/lib/blorb/include"
flags=(-std=c++17 -fsyntax-only -Wall -Wextra -Werror=narrowing)
fail=0

if ! out=$(echo '#include "blorb/species.h"' | g++ "${flags[@]}" -I"$inc" -x c++ - 2>&1); then
  echo "FAIL species.h does not compile unplanted"; echo "$out" | head -20; exit 1
fi

plant() {   # plant <label> <file under include/blorb> <sed expression> <the static_assert's whole message>
  local tmp; tmp="$(mktemp -d)"; cp -r "$inc/blorb" "$tmp/"
  sed -i "$3" "$tmp/blorb/$2"
  if cmp -s "$inc/blorb/$2" "$tmp/blorb/$2"; then
    echo "FAIL planted $1: the sed matched nothing in $2"; fail=1
  elif out=$(echo '#include "blorb/species.h"' | g++ "${flags[@]}" -I"$tmp" -x c++ - 2>&1); then
    echo "FAIL planted $1 compiled"; fail=1
  elif ! grep -q "static assertion failed: $4\$" <<<"$out"; then
    echo "FAIL planted $1 refused for another reason"; echo "$out" | grep "error:" | head -5; fail=1
  elif grep "error:" <<<"$out" | grep -vq "static assertion failed: $4\$"; then
    echo "FAIL planted $1 also tripped another guard"; echo "$out" | grep "error:" | grep -v "$4" | head -5; fail=1
  else
    echo "ok   planted $1 refused: $4"
  fi
  rm -rf "$tmp"
}

# The three rows docs/design/sketch/check.sh plants, verbatim.
plant "two species on one wheel cell" defs/species.def 's/BLORB_SPECIES(1, mossby,   Sprout, Guard/BLORB_SPECIES(1, mossby,   Sprout, Trick/' "species.def: every (element, style) pair exactly once, nine rows"
plant "a shared power" defs/species.def 's/BLORB_SPECIES(8, tinker,   Spark,  Guard,  gadget/BLORB_SPECIES(8, tinker,   Spark,  Guard,  zap/' "species.def: a shared power"
plant "a cost the meter cannot hold" defs/powers.def 's/BLORB_POWER(6, zap,     6,/BLORB_POWER(6, zap,     9,/' "powers.def: a cost the meter cannot hold"

plant "a free power" defs/powers.def 's/BLORB_POWER(5, dash,    3,/BLORB_POWER(5, dash,    0,/' "powers.def: a cost the meter cannot hold"
plant "a row out of order" defs/species.def 's/BLORB_SPECIES(8, tinker/BLORB_SPECIES(9, tinker/' "species.def / powers.def ids must be 0..n-1 in order"
plant "grungo without foresee" defs/species.def 's/Trick,  foresee,/Trick,  shell,  /; s/Guard,  shell,  /Guard,  foresee,/' "slot 0 is grungo, and grungo foresees"
plant "a broken stance cycle" species.h 's/Pounce = 0, Guard = 1, Trick = 2/Pounce = 0, Guard = 2, Trick = 1/' "pounce beats trick beats guard beats pounce"
plant "counterOf naming the loser" species.h 's/Stance((uint8_t(s) + 1) % kCycle)/Stance((uint8_t(s) + 2) % kCycle)/' "counterOf names the winner"

bin="$(mktemp -d)"
printf '#include <cstdio>\n#include "blorb/species.h"\nint main() { std::printf("%%u\\n", unsigned(blorb::rosterHash())); }\n' > "$bin/hash.cpp"
if g++ -std=c++17 -I"$inc" "$bin/hash.cpp" -o "$bin/hash" && host=$("$bin/hash"); then
  xt=~/.platformio/packages/toolchain-xtensa-esp32s3/bin/xtensa-esp32s3-elf-g++
  if [ ! -x "$xt" ]; then
    echo "skip rosterHash on the S3 compiler: $xt is missing"
  elif printf '#include "blorb/species.h"\nstatic_assert(blorb::rosterHash() == %su, "");\n' "$host" |
       "$xt" -std=gnu++17 -fsyntax-only -I"$inc" -x c++ - ; then
    echo "ok   rosterHash $host on the host and the S3 compiler"
  else
    echo "FAIL rosterHash differs on the S3 compiler (host says $host)"; fail=1
  fi
else
  echo "FAIL could not build the rosterHash printer"; fail=1
fi
rm -rf "$bin"
exit $fail
