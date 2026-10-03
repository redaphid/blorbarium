#!/bin/bash
# Builds the learnsim driver against this checkout, in WSL survivor:
#   MSYS_NO_PATHCONV=1 wsl -d survivor --exec bash tools/learnsim/build.sh <label>
# The tree is mirrored to ~/.cache/learnsim/<label>/tree first (compiling over
# the /mnt mount is slow), and the binary lands at ~/.cache/learnsim/<label>/life.
set -euo pipefail
label=${1:?usage: build.sh <label>}
src="$(cd "$(dirname "$0")/../.." && pwd)"
out="$HOME/.cache/learnsim/$label"
mkdir -p "$out/tree"
rsync -a --delete --exclude .git --exclude .pio --exclude outputs "$src/" "$out/tree/"
cd "$out"
g++ -std=c++17 -O2 -Itree/lib/blorb/include -Itree/test/support tree/lib/blorb/src/*.cpp tree/tools/learnsim/life.cpp -o life
echo "built $out/life"
