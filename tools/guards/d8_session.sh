#!/bin/bash
# Breaks each of unit D8's guards once and expects the named test to catch it.
# Works on a private WSL-local copy of the tree, so the checkout and its build
# mirror are never touched; each mutated file is put back on exit, whatever
# the build did.
#   MSYS_NO_PATHCONV=1 wsl -d survivor --exec bash /mnt/d/<this checkout>/tools/guards/d8_session.sh
set -u
src="$(cd "$(dirname "$0")/../.." && pwd)"
work="$HOME/.cache/blorb-guards/d8-$(printf '%s' "$src" | md5sum | cut -c1-12)"
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

W=lib/blorb/src/duel_wire.cpp S=lib/blorb/src/duel_session.cpp
mutant "decode trusts a frame without its crc" $W 's/if (crc.u32() != crc32(bytes, len - 4)) return std::nullopt;/(void)crc;/' \
  test_duel_wire "DuelWire.AMillionRandomBuffersNeverCrashDecode"
mutant "decode takes a log longer than a battle" $W 's/ok \&= p.moveCount <= DuelRules::kLogSlots \&\& p.heard <= DuelRules::kLogSlots;/ok \&= true;/' \
  test_duel_wire "DuelWire.TheBoundaryRefusesEachMalformedFrame"
mutant "a beacon broadcasts the lineage" $W 's/putCard(o, p.card, false);/putCard(o, p.card, true);/' \
  test_duel_wire "DuelWire.ABeaconNeverBroadcastsTheLineage"
mutant "the lobby hears a friend across the room" $S 's/if (d->rssi < T::kNearRssi) continue;/(void)d->rssi;/' \
  test_duel_session "DuelSession.AFriendFarAwayIsNeverMatched"
mutant "a declining friend is matched" $S 's/s.beacon.decline == Decline::None \&\& (!pin/(!pin/' \
  test_duel_session "DuelSession.ADecliningFriendShowsWhyAndIsNeverMatched"
mutant "a clink of three picks one" $S 's/return coincident == 1 ? found : std::nullopt;/return found;/' \
  test_duel_session "DuelSession.ThreeDevicesKnockingAtOnceMatchNobody"
mutant "Mortal ignores the assist" $S 's/if (assist > r.maxAssistHearts || /if (/' \
  test_duel_session "DuelSession.MortalIsRefusedAtTwoAssistHearts"
mutant "a confirm counts only under the pair's first id" $S 's/  if (b.phase == DuelPhase::Staking) {/  if (b.phase == DuelPhase::Staking \&\& st.battle.stakes == Stakes::Spar) {/' \
  test_duel_session "DuelSession.ALostConfirmDoesNotStrandStaking"
mutant "a packet from the last bout feeds this one" $S 's/return id.rematch == b.battle.rematch \&\& id.seed == .*;/return true;/; s/ || st.battle != b.battle) return;/) return;/' \
  test_duel_session "DuelSession.ARematchRerunsStakingAndALatePacketFromTheLastBoutIsIgnored"
mutant "the downed side writes a verdict too" $S 's/^    if (o.downed\[me\]) {/    if (false) {/' \
  test_duel_session "DuelSession.TheWinnersSilenceSparesAndAFinishNeedsTheAck"
mutant "the victor slays before the fallen side hears" $S 's/!b.metSent \&\& b.theyHold > o.resolved/!b.metSent/' \
  test_duel_session "DuelSession.AFinishTheFallenSideNeverHeardSlaysNobody"
mutant "fleeing is a fizzle" $S 's/m.fled = !theyLeft;/m.fled = false;/' \
  test_duel_session "DuelSession.FleeingAScrapWhileBehindForfeitsAndKeepsTheWounds"
mutant "silence never spares" $S 's/ || tick - b.phaseTick >= kMercyTicks))/))/' \
  test_duel_session "DuelSession.TheWinnersSilenceSparesAndAFinishNeedsTheAck"
mutant "a phone's knock locks a move" $S 's/if (s.v < 64 \&\& (remote \& duelStimBit(s))) continue;/(void)remote;/' \
  test_duel_session "DuelInput.APhonesStimuliAreNeverGestures"
exit $failed
