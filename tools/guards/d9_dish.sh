#!/bin/bash
# Breaks each of unit D9's guards once and expects the named test to catch it.
# Works on a private WSL-local copy of the tree, so the checkout and its build
# mirror are never touched; each mutated file is put back on exit, whatever
# the build did.
#   MSYS_NO_PATHCONV=1 wsl -d survivor --exec bash /mnt/d/<this checkout>/tools/guards/d9_dish.sh
set -u
src="$(cd "$(dirname "$0")/../.." && pwd)"
work="$HOME/.cache/blorb-guards/d9-$(printf '%s' "$src" | md5sum | cut -c1-12)"
mkdir -p "$work"
rsync -a --delete --exclude .pio --exclude .git --exclude outputs "$src/" "$work/"
cd "$work"
pio=~/.platformio/penv/bin/pio
pending=""
restore() { if [ -n "$pending" ]; then mv "$pending.orig" "$pending"; pending=""; fi; }
trap restore EXIT

failed=0
mutant() {   # mutant <name> <file> <sed expression> <suite> <test that must catch it>
  local name="$1" file="$2" expr="$3" suite="$4" expect="$5" out
  cp "$file" "$file.orig"
  pending="$file"
  sed -i "$expr" "$file"
  if cmp -s "$file" "$file.orig"; then
    echo "NOT APPLIED  $name"; failed=1
  elif out=$($pio test -e native -f "$suite" 2>&1); then
    echo "SURVIVED     $name (expected: $expect)"; failed=1
  elif grep -qE "$expect.*FAILED|$expect.*: Failure" <<<"$out"; then
    echo "CAUGHT       $name by $expect"
  else
    echo "RED, BUT NOT BY $expect: $name"; failed=1
    grep -m3 -E 'error:|: Failure' <<<"$out" | sed 's/^/    /'
  fi
  restore
}

D=lib/blorb/src/dish.cpp U=lib/blorb/src/dish_duel.cpp B=lib/blorb/src/senses.cpp T=test_two_dishes
mutant "the phone's stimuli count as the body's" $D 's/void Dish::fire(StimId s) { pending_.fireRemote(s); }/void Dish::fire(StimId s) { pending_.fire(s); }/' \
  $T "Consent.ThePhoneCanNeitherOpenADuelNorLockAMove"
mutant "a captured knock still reaches the pet" $U 's/  drop(raw, fx.captured);/  (void)fx.captured;/' \
  $T "TwoDishes.ASparRunsWholeAndChangesNothingAWoundWould"
mutant "a wound waits for the timed save" $D '/\.save)$/{n;s/^    save();/    (void)0;/}' \
  $T "AtOnce.ACablePulledMidScrapKeepsEveryWoundSoFar"
mutant "the graft runs at every open" $U 's/  if (feelsDuels(c.genome())) return false;/  (void)feelsDuels;/' \
  $T "Graft.APreDuelLineGetsTheDuelGenesOnceAsOneCheckpoint"
mutant "friend.egg outlives the birth" $D 's/  if (friendEgg \&\& !FriendEggStore::consume(store_)) return false;//' \
  $T "FriendEgg.APowerCutAtAnyStepNeverHatchesItTwice"
mutant "a duel's wound skips the clamp" lib/blorb/src/creature.cpp 's/fxMin(fxMax(injury, Fx::zero()), woundRoom(\*this))/fxMax(injury, Fx::zero()) * Fx::ratio(4, 1)/' \
  $T "TwoDishes.NoDuelEndsALifeAtAnyTier"
mutant "the swoon never naps" $U 's/      c->force(action::sleep);.*/      (void)0;/' \
  $T "TwoDishes.TheLoserOfASparSwoonsNapsAndWakes"
mutant "no double press" $B 's/out.fire(second_ ? stim::button_double : stim::button);/out.fire(stim::button);/' \
  $T "Gestures.TwoQuickPressesAreADoubleAndASlowSecondIsAPress"
mutant "leaving the friends screen asks a rematch" $U 's/    if (both \&\& fromBody(raw, stim::button)) {/    if (false) {/' \
  $T "BootRows.LeavingTheFriendsScreenNeverAsksARematch"
mutant "a cuddle is never written" $U 's/book_.recordCuddle(tick);/(void)0;/' \
  $T "Readiness.ThreeLossesInARowWantACuddleAndACuddleIsEnough"
mutant "a double where it means nothing is lost" $D 's/  if (m.doublePress == BootAct::Nothing \&\& fired(out, stim::button_double)) {/  if (false) {/' \
  $T "BootRows.AsleepADoublePressIsTwoPellets"
mutant "a lobby that found nobody starts the rest" $U 's/  if (fx.radioClosed \&\& battled_) {/  if (fx.radioClosed) {/' \
  $T "Readiness.ThreeLossesInARowWantACuddleAndACuddleIsEnough"
mutant "the hint drifts from the table" $U 's/  h.glyph = m.hint;/  h.glyph = BootGlyph::Pellet;/' \
  $T "BootRows.EveryDuelRowShowsItsOwnHint"
exit $failed
