#!/bin/bash
# Breaks each of unit D10's guards once and expects the named test to catch
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
pio=~/.platformio/penv/bin/pio
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

F=lib/blorb/src/founding.cpp C=lib/blorb/src/clutch.cpp V=src/debug_verbs.h
mutant "a hold does not keep" $C 's/if (s == stim::button_hold || s == stim::double_knock) return cursor;/if (s == stim::double_knock) return cursor;/' \
  "Chooser.APressLightsTheNextEggAndAHoldKeepsTheLitOne"
mutant "a double knock does not keep" $C 's/if (s == stim::button_hold || s == stim::double_knock) return cursor;/if (s == stim::button_hold) return cursor;/' \
  "Chooser.AKnockLightsTheNextEggAndADoubleKnockKeepsTheLitOne"
mutant "a press does not light the next egg" $C 's/if (s == stim::button || s == stim::knock) cursor/if (s == stim::knock) cursor/' \
  "Chooser.APressLightsTheNextEggAndAHoldKeepsTheLitOne"
mutant "one egg waits for a choice" $C 's/if (count <= 1) return uint8_t(0);//' \
  "Chooser.OneEggNeedsNoChoice"
mutant "the timeout keeps the lit egg" $F 's/kept = 0;/kept = lit_;/' \
  "Chooser.LightingAnotherEggStartsTheMinuteAgainAndTheTimeoutStillKeepsTheFirst"
mutant "lighting an egg does not restart the minute" $F 's/if (lit_ != was) idleSinceMs_ = nowMs;//' \
  "Chooser.LightingAnotherEggStartsTheMinuteAgainAndTheTimeoutStillKeepsTheFirst"
mutant "the timeout comes early" $F 's/nowMs - idleSinceMs_ >= kTimeoutMs/nowMs - idleSinceMs_ + 1000 >= kTimeoutMs/' \
  "Chooser.NobodyChoosingForAMinuteKeepsTheFirstEgg"
mutant "a nest is chosen twice" $F 's/if (done_) return std::nullopt;//' \
  "Chooser.APressLightsTheNextEggAndAHoldKeepsTheLitOne"
mutant "a rescued slot is no pet" $F 's/"rescue\/snap.a.0", //' \
  "NeedsFounding.AnyKeepsakeOrLineageMeansAPetLivesHere"
mutant "the lineage alone is no pet" $F 's/"lineage.log",//' \
  "NeedsFounding.TheGiftDevicesGrungoIsNeverFoundedAgain"
mutant "a pin re-founds a living pet" $F 's/if (!needsFounding(s)) return std::nullopt;//' \
  "NeedsFounding.TheGiftDevicesGrungoIsNeverFoundedAgain"
mutant "the pin is ignored" $F 's/if (std::optional<SpeciesId> pin = readFoundPin(s)) return Nest{{\*pin}, 1};//' \
  "Pin.MakesTheNestOneEggAtAFreshFounding"
mutant "a pin past the roster is read" $F 's/if (b\[5\] >= kSpeciesCount) return std::nullopt;//' \
  "Pin.AnythingButAWholePinIsNoPin"
mutant "a pin of any length is read" $F 's/\*n != kPinLen || //' \
  "Pin.AnythingButAWholePinIsNoPin"
mutant "off leaves the pin" $F 's/|| s.rename(kPinFile, kPinAside)/|| true/' \
  "Pin.OffGivesTheThreeEggsBackAndDeletesNothing"
mutant "the verb writes on a pet's flash" $V 's/if (!blorb::needsFounding(store)) return "a pet lives here: a pin only seeds a fresh founding";//' \
  "NeedsFounding.TheGiftDevicesGrungoIsNeverFoundedAgain"
mutant "the verb pins a species with no row" $V 's/ || s.v >= blorb::kSpeciesCount//' \
  "Pin.ANameOrNumberWithNoRowIsRefusedAndWritesNothing"
exit $failed
