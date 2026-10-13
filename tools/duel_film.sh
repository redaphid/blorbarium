#!/bin/bash
# Films duels between two linked simulators, in WSL survivor:
#   wsl -d survivor --exec bash /mnt/d/<this checkout>/tools/duel_film.sh [--twice] [duel ...]
# Builds the simulator on WSL-local disk, as make_reel.sh does, then
# duel_film.py writes <duel>-wide.mp4, <duel>-square.mp4 and check/ to OUT.
set -euo pipefail
src="$(cd "$(dirname "$0")/.." && pwd)"
dst="$HOME/.cache/blorbarium-build/$(printf '%s' "$src" | md5sum | cut -c1-12)"
mkdir -p "$dst"
rsync -a --delete --exclude .pio --exclude .git --exclude outputs "$src/" "$dst/"
cd "$dst"
~/.platformio/penv/bin/pio run -e sim -s
venv="$HOME/.cache/reel-venv"
[ -x "$venv/bin/python" ] || { python3 -m venv --system-site-packages "$venv"; "$venv/bin/pip" install -q imageio-ffmpeg; }
export FFMPEG="$("$venv/bin/python" -c 'import imageio_ffmpeg as m; print(m.get_ffmpeg_exe())')"
exec "$venv/bin/python" tools/duel_film.py --out "${OUT:-/mnt/d/projects/bak/blorbarium/duels}" "$@"
