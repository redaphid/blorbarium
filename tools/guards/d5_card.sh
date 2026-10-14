#!/bin/bash
# Breaks each of unit D5's guards once and expects the named test to catch
# it. Works on a private WSL-local copy of the tree, so the checkout and its
# build mirror are never touched; each mutated file is put back on exit,
# whatever the build did.
#   MSYS_NO_PATHCONV=1 wsl -d survivor --exec bash /mnt/d/<this checkout>/tools/guards/d5_card.sh
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
  elif out=$($pio test -e native -f test_duel_card 2>&1); then
    echo "SURVIVED     $name (expected: $expect)"; failed=1
  elif grep -qF "$expect" <<<"$out"; then
    echo "CAUGHT       $name by $expect"
  else
    echo "RED, BUT NOT BY $expect: $name"; failed=1
    grep -m3 -E 'error:|: Failure' <<<"$out" | sed 's/^/    /'
  fi
  restore
}

C=lib/blorb/src/duel_card.cpp
mutant "power from the body, not the power gene" $C \
  's/card.power = c.phenotype().species.power;/card.power = speciesInfo(c.phenotype().species.body).power;/' \
  "Card.AHybridBringsItsBodyAndItsOwnPower"
mutant "knack ignores the gene" $C 's/Knack{c.phenotype().species.knackOrder}/Knack{0}/' \
  "Card.EachStarterBringsItsSpeciesPowerKnackAndTheBookAsGiven"
mutant "hatchlings drawn full size" $C 's/l.scalePct = kHatchlingScalePct;/l.scalePct = 100;/' \
  "Card.TheLookIsWhatPresentDrawsForEveryStarterAtEveryStage"
mutant "the egg's shell keeps no skin tint" $C '/if (!shellGene) l.regions\[region::shell.v\]/d' \
  "Card.TheLookIsWhatPresentDrawsForEveryStarterAtEveryStage"
mutant "tints ignore their bound chemical" $C '/if (paint.hasBound) t.val = /d' \
  "Card.TheLookIsWhatPresentDrawsForEveryStarterAtEveryStage"
mutant "the hue_shift locus is ignored" $C 's/const int shift = scaled(chem.locus\[locus::hue_shift.v\], kHueShiftSteps);/const int shift = 0;/' \
  "Card.TheLookTurnsWithTheHueShiftLocusAsPresentDoes"
mutant "nerve forgets dread" $C 's/ - c.chem\[chem::dread.v\]//' \
  "Mood.FearAndDreadLowerNerveAndMettleRaisesIt"
mutant "nerve forgets mettle" $C 's/c.chem\[chem::mettle.v\] - //' \
  "Mood.FearAndDreadLowerNerveAndMettleRaisesIt"
mutant "nerve forgets fear" $C 's/ - d(drive::fear))/)/' \
  "Mood.FearAndDreadLowerNerveAndMettleRaisesIt"
mutant "pep forgets boredom" $C 's/ + d(drive::boredom))/)/' \
  "Mood.EnergyAndBoredomRaisePep"
mutant "pep centred on half" $C 's/ - kEnergyAtRest + / - kHalf + /' \
  "Mood.ARestedStarterLeansOnlyGentlyTowardHisKid"
mutant "spirit forgets pain" $C 's/ + d(drive::pain)))/))/' \
  "Mood.SleepinessAndPainLowerSpirit"
mutant "trust forgets loneliness" $C 's/kHalf - d(drive::loneliness) - /kHalf - /' \
  "Mood.CompanyRaisesTrustAndLonelinessLowersIt"
mutant "a pet already gone may duel" $C 's/if (!c || c->dead())/if (!c)/' \
  "Readiness.AnEggAClutchAndAPetAlreadyGoneHaveNothingToDuelWith"
mutant "an asleep pet is woken for it" $C '/if (c->body().asleep) return Decline::Asleep;/d' \
  "Readiness.AnAsleepPetSleepsOn"
mutant "sleepy only past 0.8" $C 's/drive(drive::sleepiness) >= kDeclineSleepy/drive(drive::sleepiness) > kDeclineSleepy/' \
  "Readiness.AVerySleepyPetDeclines"
mutant "a hungry pet may duel" $C '/return Decline::Hungry;/d' \
  "Readiness.AVeryHungryPetWantsHisBowlFirst"
mutant "a faint rests no longer" $C 's/(faintedLast ? kRestAfterFaintTicks : kRestTicks)/kRestTicks/' \
  "Readiness.HeRestsTwoMinutesAfterADuelAndFiveAfterAFaint"
mutant "a cuddle only after four" $C 's/lossesInARow >= kLossesBeforeACuddle/lossesInARow > kLossesBeforeACuddle/' \
  "Readiness.ThreeLossesInARowAndHeWantsACuddle"
mutant "any stage may enter any tier" $C 's/if (uint8_t(c.stage()) < uint8_t(r.youngestStage))/if (false)/' \
  "Ceiling.EachStageMayEnterTheTiersItIsOldEnoughFor"
mutant "a wounded pet may fight" $C 's/ \&\& wounded) why = Decline::Wounded;/ \&\& false) why = Decline::Wounded;/' \
  "Ceiling.AWoundedPetStillSparsButDoesNotFight"
mutant "the forecast refuses nothing" $C 's/forecast(c, s).skull == 255/false/' \
  "Ceiling.ATierWhoseForecastSkullIsFullIsRefused"
mutant "the forecast refuses a spar" $C 's/else if (r.injuryCapPerDuel > Fx::zero() \&\& forecast/else if (forecast/' \
  "Ceiling.ATierWhoseForecastSkullIsFullIsRefused"
mutant "the device's ceiling is ignored" $C 's/else if (uint8_t(s) > uint8_t(deviceCeiling))/else if (false)/' \
  "Ceiling.TheDevicesCeilingHoldsAHealthyAdultDown"
exit $failed
