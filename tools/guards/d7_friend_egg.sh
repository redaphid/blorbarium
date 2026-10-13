#!/bin/bash
# Breaks each of unit D7's guards once and expects the named test or
# static_assert to catch it. Works on a private WSL-local copy of the tree, so
# the checkout and its build mirror are never touched; each mutated file is put
# back on exit, whatever the build did.
#   MSYS_NO_PATHCONV=1 wsl -d survivor --exec bash /mnt/d/<this checkout>/tools/guards/d7_friend_egg.sh
#
# The two viability checks in childOf are one guard: mutate() hands back a
# viable child or its input unchanged, so either check alone covers the other,
# and only removing both can change an egg.
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
mutant() {   # mutant <name> <file> <sed expression> <test or static_assert message that must catch it>
  local name="$1" file="$2" expr="$3" expect="$4" out
  cp "$file" "$file.orig"
  pending="$file"
  sed -i "$expr" "$file"
  if cmp -s "$file" "$file.orig"; then
    echo "NOT APPLIED  $name"; failed=1
  elif out=$($pio test -e native -f test_friend_egg 2>&1); then
    echo "SURVIVED     $name (expected: $expect)"; failed=1
  elif grep -qF "$expect" <<<"$out"; then
    echo "CAUGHT       $name by $expect"
  else
    echo "RED, BUT NOT BY $expect: $name"; failed=1
    grep -m3 -E 'error:|: Failure' <<<"$out" | sed 's/^/    /'
  fi
  restore
}

K=lib/blorb/src/keepsake.cpp C=lib/blorb/src/clutch.cpp F=lib/blorb/src/friend_egg.cpp H=lib/blorb/include/blorb/friend_egg.h
mutant "an ordinary pet always writes a bite" $K 's/if (b.mouth != Mouthful::Nothing || scars.count) o.u8/o.u8/' \
  "Keepsake.AnOrdinaryPetWritesNoTrailingBytes"
mutant "scars without the bite before them" $K 's/if (b.mouth != Mouthful::Nothing || scars.count) o.u8/if (b.mouth != Mouthful::Nothing) o.u8/' \
  "Fixture.TheScarsFixtureIsTheV1PetPlusTrailingBytes"
mutant "scars are not read back" $K 's/c.scars_ = getScars(\*body);//' \
  "Keepsake.ScarsRoundTripWithAndWithoutABite"
mutant "a kind with no art is kept" $K 's/kind < kScarKinds ? Scar::Kind(kind) : Scar::Kind{}/Scar::Kind(kind)/' \
  "Keepsake.ANewerFirmwaresScarsLoadAsScarsThisOneCanDraw"
mutant "an ordinary clutch writes the friend tail" $K 's/if (k.friendSlot) o.u8(k.friendSlot), o.u32(k.friendSeed);/o.u8(k.friendSlot), o.u32(k.friendSeed);/' \
  "Keepsake.AnOrdinaryPetWritesNoTrailingBytes"
mutant "slot 1 read as a friend egg" $K 's/slot >= 2 \&\& slot <= k.count/slot >= 1 \&\& slot <= k.count/' \
  "Keepsake.AFriendEggRoundTripsAndABadSlotIsNoFriendEgg"
mutant "a slot past the clutch read as a friend egg" $K 's/slot >= 2 \&\& slot <= k.count/slot >= 2/' \
  "Keepsake.AFriendEggRoundTripsAndABadSlotIsNoFriendEgg"
mutant "the friend egg is not read back" $K 's/k.friendSlot = slot, k.friendSeed = seed;/(void)seed;/' \
  "Keepsake.AFriendEggRoundTripsAndABadSlotIsNoFriendEgg"
mutant "the snapshot hash ignores the friend egg" $K 's/if (k.friendSlot) mix(k.friendSlot), mix(k.friendSeed);//' \
  "the snapshot hash covers the friend egg"
mutant "the timeout picks the friend egg" $C 's/return friendSlot \&\& cursor == friendSlot - 1 ? uint8_t(0) : cursor;/return cursor;/' \
  "Clutch.TheTimeoutNeverPicksTheFriendEggButAKidMay"
mutant "a friend egg grows a full clutch" $F 's/k.friendSlot == 0 \&\& k.count < kMaxClutch/k.friendSlot == 0/' \
  "Clutch.AFriendEggJoinsWithinThreeAndAFullClutchGivesUpItsLastSlot"
mutant "a second join grows it again" $F 's/k.friendSlot == 0 \&\& k.count < kMaxClutch/k.count < kMaxClutch/' \
  "Clutch.AFriendEggJoinsWithinThreeAndAFullClutchGivesUpItsLastSlot"
mutant "the cross ignores the allowlist" $F 's/if (!fromFriend || !crossable(g.header.type)) return;/if (!fromFriend) return;/' \
  "Cross.APlantedKnockToInjuryCrossesNothingButAllowlistedGenes"
mutant "the cross ignores kind and length" $F 's/ || f->header.type != g.header.type || f->header.len != g.header.len//' \
  "Cross.OnlyLociBothShareWithTheSameKindAndLength"
mutant "the cross never switches parent" $F 's/if (rng.below(kBlockGenes) == 0) fromFriend = !fromFriend;/(void)rng.below(kBlockGenes);/' \
  "Cross.AHybridComesOfThirtyFiveToSixtyFivePercentOfCrosses"
mutant "a fizzled cross still hatches" $F 's/if (!crossed || !viability(\*crossed).ok) return k.child(i);/if (!crossed) return k.child(i);/;s/if (!viability(child.genome).ok) return k.child(i);//' \
  "Cross.AFizzledCrossIsThePlainEggInThatSlot"
mutant "another hug's friend.egg is crossed" $F 's/ || f->record.battleSeed != k.friendSeed//' \
  "Cross.AnotherSlotOrAnotherHugsEggIsThePlainEgg"
mutant "friend.egg's crc is not checked" $F 's/if (uint32_t(get(\&b\[kHeadLen + genomeLen\], 4)) != crc32(b.data(), kHeadLen + genomeLen)) return std::nullopt;//' \
  "Store.AnyDamageIsNoFriendEggNeverAMisreadOne"
mutant "the announced hash is not checked" $F 's/if (!g || g->hash() != announcedHash) return std::nullopt;/if (!g) return std::nullopt;/' \
  "Accept.RefusesDamageAWrongHashAndAStranger"
mutant "a stranger's genome is accepted" $F 's/if (!shares) return std::nullopt;//' \
  "Accept.RefusesDamageAWrongHashAndAStranger"
mutant "the allowlist takes a receptor" $H 's/GeneKindOf<PowerGene>::value};/GeneKindOf<PowerGene>::value, GeneKindOf<ReceptorGene>::value};/' \
  "a cross never copies chemistry, stimulus, instinct or Life genes"
exit $failed
