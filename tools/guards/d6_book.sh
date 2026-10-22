#!/bin/bash
# Breaks each of unit D6's guards once and expects the named test to catch it.
# Works on a private WSL-local copy of the tree, so the checkout and its build
# mirror are never touched; each mutated file is put back on exit, whatever
# the build did.
#   MSYS_NO_PATHCONV=1 wsl -d survivor --exec bash /mnt/d/<this checkout>/tools/guards/d6_book.sh
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
  elif out=$($pio test -e native -f test_duel_book 2>&1); then
    echo "SURVIVED     $name (expected: $expect)"; failed=1
  elif grep -F "$expect" <<<"$out" | grep -q FAILED; then
    echo "CAUGHT       $name by $expect"
  else
    echo "RED, BUT NOT BY $expect: $name"; failed=1
    grep -m3 -E 'error:|FAILED' <<<"$out" | sed 's/^/    /'
  fi
  restore
}

B=lib/blorb/src/duel_book.cpp L=lib/blorb/src/lineage.cpp
mutant "a fizzle earns a draw's XP" $B 's/m.fizzled ? 1u : won ? 3u : 2u/won ? 3u : 2u/' \
  "Fold.XpIsThreeAWinTwoALossOrDrawOneAFizzleAndEachLifeStartsAtZero"
mutant "a new life keeps the old XP" $B 's/st.generation = when.generation, st.xp = 0,/st.generation = when.generation,/' \
  "Fold.XpIsThreeAWinTwoALossOrDrawOneAFizzleAndEachLifeStartsAtZero"
mutant "a pet-played battle moves prowess" $B 's/if (m.manualTurns > 0) st.prowess/if (true) st.prowess/' \
  "Fold.ProwessStepsAnEighthTowardEachBattlesPerformanceAndAPetPlayedBattleMovesItNot"
mutant "the skill is never saved" $B '/    st.skill = m.skillAfter;/d' \
  "Fold.SkillIsTheLifesNewestFrameAndANewLifeHatchesBlank"
mutant "a spared life is not owed" $B '/  if (m.wasSpared) sat(f.s.weOwe);/d' \
  "Fold.TheDarkLedgerCountsFallsSlayingsAndSparedLivesAcrossGenerations"
mutant "trophies pile up" $B '/  if (v.size() > BookState::kTrophies) v.erase(v.begin());/d' \
  "Fold.TrophiesAreTheNewestThreeSlainSpeciesNewestFirstAndSlayerIsThatLifesFeat"
mutant "a slaying earns no feat" $B '/    if (m.iSlew) st.feats |= feat::slayer;/d' \
  "Fold.TrophiesAreTheNewestThreeSlainSpeciesNewestFirstAndSlayerIsThatLifesFeat"
mutant "a vendetta never lapses" $B 's/ \&\& generation - it->sinceGeneration <= kVendettaGenerations//' \
  "Vendetta.AFallSwearsOneForTheNextThreeGenerationsAndNoLonger"
mutant "a spar avenges" $B 's/if (won \&\& forKeeps)/if (won)/' \
  "Vendetta.AScrapOrMortalWinAgainstThatLineAvengesItAndNothingElseDoes"
mutant "any line avenges" $B 's/return v.against == them;/return true;/' \
  "Vendetta.AScrapOrMortalWinAgainstThatLineAvengesItAndNothingElseDoes"
mutant "the newborn leans to the foe's own stance" $B 's/counterOf(foe.style)/foe.style/' \
  "Vendetta.ANewbornsSkillLeansToTheCounterOfTheVictorsKindAndOnlyAgainstItsElement"
mutant "a boot forgets the fled" $B '/  settleAll(b.state_);/d' \
  "Downed.ABootWithNoVerdictAfterADowningReadsItAsFled"
mutant "record repeats a battle" $B '/  if (logged(\*store_, Kind::Met, m.battle)) return;/d' \
  "Log.RecordIsIdempotentByBattleIdAcrossABoot"
mutant "open keeps a torn tail" $B '/  logframes::truncateTorn(store, kFile, valid);/d' \
  "Log.ATornTailIsTruncatedAndHidesNothingBeforeIt"
mutant "compaction keeps every Meeting too" $B 's/return at >= cut; },/return true; },/' \
  "Log.CompactionKeepsOneSummaryPerFriendAndTheSameBook"
mutant "compaction drops the friends" $B '/    add(Kind::Friend, o);/d' \
  "Log.CompactionKeepsOneSummaryPerFriendAndTheSameBook"
mutant "compaction drops the vendettas" $B '/    add(Kind::Vendetta, o);/d' \
  "Log.CompactionKeepsOneSummaryPerFriendAndTheSameBook"
mutant "compaction keeps no recent Meeting" $B 's/mets\[mets.size() - kKeepRecent\]/mets.back() + 1/' \
  "Log.CompactionKeepsOneSummaryPerFriendAndTheSameBook"
mutant "the line is named in lower case" $L '/  to\[0\] = char(std::toupper/d' \
  "Founding.TheFoundingPicksSpeciesNamesTheLine"
mutant "every line founds as Grungo" $L 's/    copySpeciesName(f.species, starter);/    copyName(f.species, "Grungo");/' \
  "Founding.TheFoundingPicksSpeciesNamesTheLine"
mutant "a grungo founds as grungo" $L 's/copyName(to, speciesInfo(p.species.body).name);/copyName(to, speciesInfo(p.species.body).name); if (p.species.body == kGrungo) to[1] = 0;/' \
  "Founding.AnOrdinaryPetsFoundingIsByteForByteWhatItWasBeforeSpecies"
exit $failed
