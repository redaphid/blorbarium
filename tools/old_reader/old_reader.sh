#!/bin/bash
# What an older firmware does with this checkout's keepsakes. Builds
# old_reader.cpp against the engine at <ref> (default 418c787, release 0.4.0)
# and has it load:
#   * the scars and friend-egg fixtures: each loads as the same pet as the
#     fixture with its trailing bytes cut off, and re-encodes to those bytes;
#   * the committed v1 fixture and the starter genome's egg, creature and clutch
#     as this checkout writes them: each re-encodes to its own bytes, so an
#     ordinary pet's save carries nothing an older firmware cannot write.
#   MSYS_NO_PATHCONV=1 wsl -d survivor --exec bash /mnt/d/<this checkout>/tools/old_reader/old_reader.sh [ref]
set -euo pipefail
ref="${1:-418c787}"
src="$(cd "$(dirname "$0")/../.." && pwd)"
# git does not run in a worktree from WSL, so the archive comes from the common dir.
if [ -d "$src/.git" ]; then
  common="$src/.git"
else
  gitdir="$(sed -n 's/^gitdir: //p' "$src/.git" | tr -d '\r')"
  common="$(cd "$(wslpath -u "$gitdir")/../.." && pwd)"
fi
work="$HOME/.cache/blorb-old-reader/$(printf '%s' "$src" | md5sum | cut -c1-12)"
rm -rf "$work"
mkdir -p "$work/tree" "$work/plain"
git -c safe.directory='*' --git-dir="$common" archive "$ref" lib/blorb | tar -x -C "$work/tree"
g++ -std=c++17 -O1 -w -I"$work/tree/lib/blorb/include" "$src/tools/old_reader/old_reader.cpp" \
  "$work/tree/lib/blorb/src/"*.cpp -o "$work/old_reader"

BLORB_D7_PLAIN_DIR="$work/plain" bash "$src/tools/wsl_test.sh" test_friend_egg >"$work/test.log" 2>&1 ||
  { tail -20 "$work/test.log"; exit 1; }

echo "== the engine at $ref reads:"
cd "$src"
"$work/old_reader" cut body test/fixtures/keepsake_v1_scars.bin
"$work/old_reader" cut clutch test/fixtures/keepsake_v1_friend_egg.bin
"$work/old_reader" same test/fixtures/keepsake_v1.bin "$work/plain/starter_creature.bin" \
  "$work/plain/starter_clutch.bin" "$work/plain/starter_egg.bin"
