#!/bin/bash
# Builds tools/duel_shot.cpp and holds the duel screen's frame goldens, in WSL survivor:
#   wsl -d survivor --exec bash /mnt/d/<this checkout>/tools/duel_film.sh [case ...]
#   wsl -d survivor --exec env UPDATE_GOLDEN=1 bash /mnt/d/<this checkout>/tools/duel_film.sh
#   wsl -d survivor --exec bash /mnt/d/<this checkout>/tools/duel_film.sh --sheet [mine:theirs ...]
#   wsl -d survivor --exec bash /mnt/d/<this checkout>/tools/duel_film.sh --film mine:theirs
# The tree is mirrored to WSL-local disk first, as sim_film.sh does. What the
# run writes comes back: outputs/ always, test/golden/ when blessing. --film
# leaves outputs/duel-film-<mine>-<theirs>.mp4 (and its frames) for a demo.
set -euo pipefail
src="$(cd "$(dirname "$0")/.." && pwd)"
dst="$HOME/.cache/blorbarium-build/$(printf '%s' "$src" | md5sum | cut -c1-12)"
mkdir -p "$dst"
rsync -a --delete --exclude .pio --exclude .git --exclude outputs "$src/" "$dst/"
cd "$dst"
~/.platformio/penv/bin/pio run -e duelshot -s
status=0
case "${1:-}" in
  --sheet) shift; python3 tools/duel_sheet.py "$@" || status=$? ;;
  --film)
    pair="${2:-gilly:fizz}"; mine="${pair%%:*}"; theirs="${pair##*:}"
    dir="outputs/duel-film-$mine-$theirs"
    rm -rf "$dir"; mkdir -p "$dir"
    .pio/build/duelshot/program --film "$dir" --mine "$mine" --theirs "$theirs" || status=$?
    if command -v ffmpeg >/dev/null; then
      ffmpeg -loglevel error -y -framerate 10 -i "$dir/frame_%04d.bmp" -vf scale=480:480:flags=neighbor \
        -pix_fmt yuv420p "outputs/duel-film-$mine-$theirs.mp4" || status=$?
    fi ;;
  *) python3 tests/duel_frames.py "$@" || status=$? ;;
esac
mkdir -p "$src/outputs"
rsync -a outputs/ "$src/outputs/"
if [ -n "${UPDATE_GOLDEN:-}" ]; then rsync -a test/golden/ "$src/test/golden/"; fi
exit $status
