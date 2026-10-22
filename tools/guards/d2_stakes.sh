#!/bin/bash
# Breaks each of unit D2's guards once and expects the named test or
# static_assert to catch it. Works on a private WSL-local copy of the tree, so
# the checkout and its build mirror are never touched; each mutated file is put
# back on exit, whatever the build did.
#   MSYS_NO_PATHCONV=1 wsl -d survivor --exec bash /mnt/d/<this checkout>/tools/guards/d2_stakes.sh
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
mutant() {   # mutant <name> <file> <sed expression> <test or static_assert message that must catch it>
  local name="$1" file="$2" expr="$3" expect="$4" out
  cp "$file" "$file.orig"
  pending="$file"
  sed -i "$expr" "$file"
  if cmp -s "$file" "$file.orig"; then
    echo "NOT APPLIED  $name"; failed=1
  elif out=$($pio test -e native -f test_stakes 2>&1); then
    echo "SURVIVED     $name (expected: $expect)"; failed=1
  elif grep -qF "$expect" <<<"$out"; then
    echo "CAUGHT       $name by $expect"
  else
    echo "RED, BUT NOT BY $expect: $name"; failed=1
    grep -m3 -E 'error:|: Failure' <<<"$out" | sed 's/^/    /'
  fi
  restore
}

C=lib/blorb/src/creature.cpp S=lib/blorb/src/stakes.cpp H=lib/blorb/include/blorb/stakes.h P=lib/blorb/src/protocol.cpp
mutant "stimulus step skips duelFilter" $C 's/duel ? duelFilter(duelStakes_, r.chem\[k\], r.amount\[k\]) : r.amount\[k\]/r.amount[k]/' \
  "Filter.APlantedBonkToInjuryGeneChangesNothing"
mutant "wound ignores woundRoom" $C 's/fxMin(fxMax(injury, Fx::zero()), woundRoom(\*this))/fxMax(injury, Fx::zero())/' \
  "Wounds.CreatureWoundIsClampedByWoundRoom"
mutant "addScar keeps a fainter scar" $C '/if (scar.fade < faintest->fade) return false;/d' \
  "Scars.AFifthReplacesTheFaintestAndAFainterOneIsRefused"
mutant "fall records old age" $C 's/DeathCause::Injured)/DeathCause::OldAge)/' \
  "Fall.AFallRunsTheOrdinaryDeathPathOnce"
mutant "woundRoom forgets the drop" $S 's/lethalInjury(c) - dropInjury(c) - kWoundMargin - now/lethalInjury(c) - kWoundMargin - now/' \
  "Wounds.NoWoundsPlusADropKillAStarterOrAThousandDescendants"
mutant "woundRoom forgets the margin" $S 's/lethalInjury(c) - dropInjury(c) - kWoundMargin - now/lethalInjury(c) - dropInjury(c) - now/' \
  "Wounds.TheStartersRoomIsHisLethalLevelLessADropAndTheMargin"
mutant "nothing is lethal" $S 's/if (dieAt(c, Fx::ratio(k, kLethalSteps)) >= Fx::ratio(1, 2)) return/if (false) return/' \
  "Wounds.NoWoundsPlusADropKillAStarterOrAThousandDescendants"
mutant "woundFor ignores the cap" $S 's/fxMin(fxMin(r.injuryPerHeart \* Fx::ratio(hit.hearts, 1), capLeft), /fxMin(r.injuryPerHeart * Fx::ratio(hit.hearts, 1), /' \
  "Wounds.EachHeartCostsTheTiersInjuryUpToItsCapPerDuel"
mutant "woundFor ignores the room" $S 's/, fxMax(roomNow, Fx::zero()));/, Fx::one());/' \
  "Wounds.AWoundNeverExceedsTheRoomGiven"
mutant "every hurting turn scars" $S 's/hit.hearts > 0 \&\& scarred < 2;/hit.hearts > 0;/' \
  "Wounds.TheFirstTwoTurnsThatCostHeartsLeaveScarsOfWhatHitHim"
mutant "filter trusts unnamed chems" $H 's/^  return Fx::zero();$/  return delta;/' \
  "wounds are the engine's, never a gene's"
mutant "a spar may hurt" $H '/if (c == driveChem(drive::pain) \&\& s == Stakes::Spar) return Fx::zero();/d' \
  "a spar never hurts"
mutant "traits move in a spar" $H 's/return rule(s).traits ? fxMin(delta, kDuelDriveRiseCap) : Fx::zero();/return fxMin(delta, kDuelDriveRiseCap);/' \
  "a spar leaves nothing lasting"
mutant "die readable from any chem" $H 's/if (!vital) return false;/(void)vital;/' \
  "pain alone never kills"
mutant "SCHEMA loses a source name" $P 's/, "duel"};/};/' \
  "kSources needs a name for every StimSource"
exit $failed
