#!/bin/bash
# Breaks each of the species adoption's guards (founding.h adoptSpecies, Dish::adoptSpecies) once and expects the named test to catch
# it. Works on a private WSL-local copy of the tree, so the checkout and its
# build mirror are never touched; each mutated file is put back on exit,
# whatever the build did.
#   MSYS_NO_PATHCONV=1 wsl -d survivor --exec bash /mnt/d/<this checkout>/tools/guards/adopt_species.sh
set -u
src="$(cd "$(dirname "$0")/../.." && pwd)"
work="$HOME/.cache/blorb-guards/adopt-$(printf '%s' "$src" | md5sum | cut -c1-12)"
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
  elif out=$($pio test -e native -f test_adopt_species 2>&1); then
    echo "SURVIVED     $name (expected: $expect)"; failed=1
  elif grep -qF "$expect" <<<"$out"; then
    echo "CAUGHT       $name by $expect"
  else
    echo "RED, BUT NOT BY $expect: $name"; failed=1
    grep -m3 -E 'error:|: Failure' <<<"$out" | sed 's/^/    /'
  fi
  restore
}

F=lib/blorb/src/founding.cpp D=lib/blorb/src/dish.cpp
mutant "adopts while the storage is held" $D 's/if (!store_.writable() || duel_.active()) return Adoption::NotYet;//' \
  "Adopt.WaitsWhileTheStorageIsHeldAndTheTrialStillSeesThe040Pet"
mutant "adopts a pet that has a species" $F 's/  if (!predatesSpecies(c.genome())) return false;//' \
  "Adopt.APetWithASpeciesIsLeftAloneEvenIfHisLineLacksALaterGene"
mutant "no Checkpoint on the lineage" $D 's/lineage_.recordEdit(c->generation(), c->genome());//' \
  "Adopt.EachBadgeBecomesItsTableSpeciesAndKeepsAgeChemistryAndBrain"
mutant "the default name stays Grungo" $D 's/if (std::strcmp(was, now) != 0 \&\& std::strcmp(lineage_.currentName(), was) == 0) rename(now);//' \
  "Adopt.EachBadgeBecomesItsTableSpeciesAndKeepsAgeChemistryAndBrain"
mutant "an owner's name is replaced" $D 's/if (std::strcmp(was, now) != 0 \&\& std::strcmp(lineage_.currentName(), was) == 0) rename(now);/rename(now);/' \
  "Adopt.AnOwnersNameStays"
mutant "the save's name lags the log's" $D 's/  copyName(live_.settings.name, lineage_.currentName());//' \
  "Adopt.APowerCutAnywhereConvergesOnTheSamePet"
mutant "not saved at once" $D '/A rerun after a power cut/,/return Adoption::Adopted;/s/  save();//' \
  "Adopt.EachBadgeBecomesItsTableSpeciesAndKeepsAgeChemistryAndBrain"
mutant "the species bytes are not written" $F 's/ || (base->body\[i\] != f.body\[i\] \&\& mine->body\[i\] == base->body\[i\])//' \
  "Adopt.EachBadgeBecomesItsTableSpeciesAndKeepsAgeChemistryAndBrain"
mutant "the look is not rolled" $F 's/if ((rollOf(f, i, bands) || /if ((false || /' \
  "Adopt.EachBadgeBecomesItsTableSpeciesAndKeepsAgeChemistryAndBrain"
mutant "a row byte overwrites one 0.4.0's recipe wrote" $F 's/ \&\& mine->body\[i\] == base->body\[i\]//' \
  "Adopt.EachBadgeBecomesItsTableSpeciesAndKeepsAgeChemistryAndBrain"
mutant "the egg is adopted unhatched" $D 's/return predatesSpecies(g) ? Adoption::NotYet : Adoption::NotNeeded;/return Adoption::NotNeeded;/' \
  "Adopt.AnEggWaitsAndTheCreatureItHatchesIsAdopted"
mutant "a 0.4.0 pet advertises grungo until adopted" $F 's/  if (predatesSpecies(\*pet)) return badgeSpecies(mac);//'   "Advertised.ABadgeIsItsSpeciesBeforeAndAfterTheAdoption"
mutant "grungo is refused as a species to adopt" $F 's/  if (!predatesSpecies(c.genome())) return false;/  if (species == kGrungo || !predatesSpecies(c.genome())) return false;/' \
  "Adopt.Board237csPetAdoptsGrungoLikeAnyBadgeAdoptsItsSpecies"
mutant "a sealed board is adopted" $D 's/  if (readOnly_) return Adoption::NotNeeded;//' \
  "Adopt.ASealedBoardIsNeverTouched"
exit $failed
