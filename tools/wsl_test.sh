#!/bin/bash
# Runs the native test suite in WSL survivor:
#   MSYS_NO_PATHCONV=1 wsl -d survivor --exec bash tools/wsl_test.sh [filter]
# The tree is mirrored to a WSL-local directory first, because compiling over
# the /mnt 9p mount is several times slower. Each checkout gets its own mirror,
# so parallel worktrees never share a build directory.
set -euo pipefail
src="$(cd "$(dirname "$0")/.." && pwd)"
dst="$HOME/.cache/blorbarium-build/$(printf '%s' "$src" | md5sum | cut -c1-12)"
mkdir -p "$dst"
rsync -a --delete --exclude .pio --exclude .git "$src/" "$dst/"
cd "$dst"
args=(test -e native)
if [ $# -gt 0 ]; then args+=(-f "$1"); fi
exec ~/.platformio/penv/bin/pio "${args[@]}"
