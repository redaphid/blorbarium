#!/bin/bash
# Breaks each of the founding's guards (unit D10, then the badge table) once and expects the named test to catch
# it. Works on a private WSL-local copy of the tree, so the checkout and its
# build mirror are never touched; each mutated file is put back on exit,
# whatever the build did.
#   MSYS_NO_PATHCONV=1 wsl -d survivor --exec bash /mnt/d/<this checkout>/tools/guards/d10_founding.sh
set -u
src="$(cd "$(dirname "$0")/../.." && pwd)"
work="$HOME/.cache/blorb-guards/$(printf '%s' "$src" | md5sum | cut -c1-12)"
mkdir -p "$work"
rsync -a --delete --exclude .pio --exclude .git --exclude outputs "$src/" "$work/"
cd "$work"
# One build at a time: the flock wrapper when this machine has it, else the penv pio.
pio=${BLORB_PIO:-$HOME/Worktrees/blorbarium/pio}; [ -x "$pio" ] || pio=~/.platformio/penv/bin/pio
pending=""
restore() { if [ -n "$pending" ]; then mv "$pending.orig" "$pending"; pending=""; fi; }
trap restore EXIT

failed=0
mutant() {   # mutant <name> <file> <sed expression> <test that must catch it>
  local name="$1" file="$2" expr="$3" expect="$4" out
  cp "$file" "$file.orig"
  pending="$file"
  sed -i "$expr" "$file"
  if cmp -s "$file" "$file.orig"; then
    echo "NOT APPLIED  $name"; failed=1
  elif out=$($pio test -e native -f test_founding 2>&1); then
    echo "SURVIVED     $name (expected: $expect)"; failed=1
  elif grep -qF "$expect" <<<"$out"; then
    echo "CAUGHT       $name by $expect"
  else
    echo "RED, BUT NOT BY $expect: $name"; failed=1
    grep -m3 -E 'error:|: Failure' <<<"$out" | sed 's/^/    /'
  fi
  restore
}

F=lib/blorb/src/founding.cpp V=src/debug_verbs.h
H=lib/blorb/include/blorb/founding.h
mutant "the table is ignored" $F 's/  if (std::optional<SpeciesId> s = tableSpecies(mac)) return \*s;//'   "Table.NineAssignedDevicesKeepTheirSpeciesOnceEach"
mutant "an unknown board is a grungo" $F 's/  return castSpecies(fnv1a(mac, 6));/  return kGrungo;/'   "Table.AnyOtherBoardTakesARegularSpeciesFromItsMac"
mutant "the octopus joins the regular cast" $H 's/return s != species::octopus;/return true;/'   "Founding.TheOctopusComesOnlyByName"
mutant "a rescued slot is no pet" $F 's/"rescue\/snap.a.0", //' \
  "NeedsFounding.AnyKeepsakeOrLineageMeansAPetLivesHere"
mutant "the lineage alone is no pet" $F 's/"lineage.log",//' \
  "NeedsFounding.APetThatLivesIsNeverFoundedAgain"
mutant "the pin is ignored" $F 's/if (std::optional<SpeciesId> pin = readFoundPin(s)) return \*pin;//' \
  "Founding.ThePinIsTheOwnersOverrideOverTheTable"
mutant "a pin past the roster is read" $F 's/if (b\[5\] >= kSpeciesCount) return std::nullopt;//' \
  "Pin.AnythingButAWholePinIsNoPin"
mutant "a pin of any length is read" $F 's/\*n != kPinLen || //' \
  "Pin.AnythingButAWholePinIsNoPin"
mutant "off leaves the pin" $F 's/|| s.rename(kPinFile, kPinAside)/|| true/' \
  "Pin.OffGivesTheTableBackAndDeletesNothing"
mutant "the verb writes on a pet's flash" $V 's/if (!blorb::needsFounding(store)) return "a pet lives here: a pin only seeds a fresh founding";//' \
  "NeedsFounding.APetThatLivesIsNeverFoundedAgain"
mutant "the verb pins a species with no row" $V 's/ || s.v >= blorb::kSpeciesCount//' \
  "Pin.ANameOrNumberWithNoRowIsRefusedAndWritesNothing"
exit $failed
