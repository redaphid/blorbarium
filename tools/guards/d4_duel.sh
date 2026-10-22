#!/bin/bash
# Breaks each of unit D4's guards once and expects the named test to catch it.
# Works on a private WSL-local copy of the tree, so the checkout and its build
# mirror are never touched; each mutated file is put back on exit, whatever
# the build did. tools/duel_determinism.sh plants its own wrong hash.
#   MSYS_NO_PATHCONV=1 wsl -d survivor --exec bash /mnt/d/<this checkout>/tools/guards/d4_duel.sh
set -u
src="$(cd "$(dirname "$0")/../.." && pwd)"
work="$HOME/.cache/blorb-guards/d4-$(printf '%s' "$src" | md5sum | cut -c1-12)"
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
  elif out=$($pio test -e native -f test_duel_resolve 2>&1); then
    echo "SURVIVED     $name (expected: $expect)"; failed=1
  elif grep -qE "$expect.*FAILED|$expect.*: Failure" <<<"$out"; then
    echo "CAUGHT       $name by $expect"
  else
    echo "RED, BUT NOT BY $expect: $name"; failed=1
    grep -m3 -E 'error:|: Failure' <<<"$out" | sed 's/^/    /'
  fi
  restore
}

R=lib/blorb/src/duel_resolve.cpp P=lib/blorb/src/duel_powers.cpp M=lib/blorb/src/duel_mind.cpp
H=lib/blorb/include/blorb/duel.h D=lib/blorb/include/blorb/defs/powers.def B=tools/duel_balance.cpp
mutant "luck keyed by side, not lineage" $R 's/luck\[i\] = sideLuck(seed, t, card\[i\]->lineageId);/luck[i] = sideLuck(seed, t, i);/' \
  "Mirror.TenThousandRandomBattlesMirror"
mutant "a turn reads how long the logs are" $R 's/playTurn(o, card, seed, t, m);/playTurn(o, card, seed ^ known, t, m);/' \
  "Prefix.ResolvingMoreTurnsNeverChangesTheTurnsBefore"
mutant "hearts wrap below zero" $R 's/s\[i\].hearts = r.hit\[i\] >= s\[i\].hearts ? 0 : uint8_t(s\[i\].hearts - r.hit\[i\]);/s[i].hearts = uint8_t(s[i].hearts - r.hit[i]);/' \
  "Hearts.StayWithinZeroAndMaxEveryTurn"
mutant "a short meter commits anyway" $R 's/if (s.spark < cost || lands >= DuelRules::kMaxTurns) return false;/if (lands >= DuelRules::kMaxTurns) return false;/' \
  "Meter.AShortMeterPlaysPlain"
mutant "the verdict read from the loser's log" $R 's/\*log\[o.downed\[0\] ? 1 : 0\]/*log[o.downed[0] ? 0 : 1]/' \
  "Verdict.AMortalLoserIsDownedAndTheWinnersSlotDecides"
mutant "the verdict slot one late" $R 's/if (winner.count() > o.resolved) o.verdict = winner.at(o.resolved).verdict;/if (winner.count() > o.resolved + 1) o.verdict = winner.at(uint8_t(o.resolved + 1)).verdict;/' \
  "Verdict.ReadFromTheSixteenthSlotAfterADowningOnTurnFifteen"
mutant "gadget can draw itself" $R 's/% (kPowerCount - 1)/% kPowerCount/; s/if (p.id != power::gadget \&\& pick-- == 0)/if (pick-- == 0)/' \
  "Powers.GadgetDrawsEachOfTheOtherEightAndNeverItself"
mutant "a dazzle never misses" $R 's/(draw(luck\[w\], Purpose::Dazzle) \& 1) != 0/false/' \
  "Powers.LanternDazzlesTheFriendNextTurn"
mutant "a zap leaves the strike's mark" $R 's/return Scar::Kind::Bite;/return Scar::Kind::Claw;/' \
  "TurnHits.HeartsKindAndLuckPerTurn"
mutant "hush is gone by the next turn" $P 's/c.side\[mine(c)\].hushTurns = 2;/c.side[mine(c)].hushTurns = 1;/' \
  "Powers.HushWhiffsTheNextSpecialAndCancelsTheNextLostClash"
mutant "dash is gone by the next turn" $P 's/c.side\[mine(c)\].bonkTurns = 2;/c.side[mine(c)].bonkTurns = 1;/' \
  "Powers.DashWinsTheNextHeadBonk"
mutant "adopt takes a conflicting prefix" $H 's/      if (packed\[i\] != packed_\[i\]) return Merge::Conflict;/      (void)packed;/' \
  "MoveLog.AdoptKeepsTheLongestConsistentPrefix"
mutant "duelSeed depends on who is A" $H 's/const uint32_t lo = nonceA < nonceB ? nonceA : nonceB, hi = nonceA < nonceB ? nonceB : nonceA;/const uint32_t lo = nonceA, hi = nonceB;/' \
  "Seed.SymmetricInTheNoncesAndNamedByRematchAndStakes"
mutant "the pet learns the wrong way round" $M 's/beats(Stance(s), Stance(them)) ? kOne/beats(Stance(them), Stance(s)) ? kOne/' \
  "Mind.ThePetLearnsToCounterAFriendsHabit"
mutant "grungo reads one turn like the rest" $M 's/seer.species == kGrungo ? 2 : 1/1/' \
  "Foretell.GrungoReadsATwoTurnPatternTheOthersMiss"
mutant "zap at three pips tips the wheel" $D 's/BLORB_POWER(6, zap,     6,/BLORB_POWER(6, zap,     3,/' \
  "Balance.TheWheelHoldsAndMatchesTheProbe"
mutant "the parent stops reading" $B 's/const Stance s = counterTheirHabit(rng, o, me);/const Stance s = Stance(rng.below(kCycle));/' \
  "Balance.AReaderBeatsAHabitKid"
mutant "a heavy winds up one turn, as the probe had it" $R 's/const uint8_t lands = uint8_t(t + uint8_t(m.windup));/const uint8_t lands = uint8_t(t + 1);/' \
  "Special.AHeavyBracesTwoTurnsThenLandsForThree"
mutant "a stagger leaves a heavy at full strength" $R 's/s\[l\].queued = s\[l\].queued == Windup::Heavy ? Windup::Quick : Windup::None;/s[l].queued = s[l].queued == Windup::Heavy ? Windup::Heavy : Windup::None;/' \
  "Special.AStaggeredHeavyDropsToQuickStrength"
mutant "a riposted special still fires" $R 's/    r.special\[i\] = Special::Riposted;/    r.special[i] = Special::Riposted; r.powered[i] = true; acting = r.fired[i] = fires;/' \
  "Special.ACounteredLandingIsRipostedAndThePowerDoesNotFire"
mutant "a tie goes to the friend" $R 's/!r.bracing\[o\] \&\& beats(r.shown\[o\], r.shown\[i\]);/!r.bracing[o] \&\& !beats(r.shown[i], r.shown[o]);/' \
  "Special.ATieGoesToTheSpecial"
mutant "any stance staggers a bracer" $R 's/if (r.bracing\[b\]) return r.shown\[a\] == Stance::Trick;/if (r.bracing[b]) return true;/' \
  "Special.AQuickBracesOneTurnThenLandsForTwo"
mutant "a special lands in its sleep" $R '/else if (r.dozed\[i\]) s.queued = Windup::None;/d' \
  "Special.AsleepOnItsLandingTurnTheSpecialIsLost"
mutant "foresee is riposted like the rest" $P 's/constexpr bool seesTheRiposte(PowerId p) { return p == power::foresee; }/constexpr bool seesTheRiposte(PowerId) { return false; }/' \
  "Powers.ForeseeWhiffsWhereOthersAreRiposted"
mutant "the commit leaks its landing" $H 's/append(Move{Stance::Pounce, false, byPet, Verdict::None, w});/append(Move{landing, false, byPet, Verdict::None, w});/' \
  "MoveLog.AppendSpecialWritesTheCommitAndRefusesALandingPastTheEnd"
exit $failed
