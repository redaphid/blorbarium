#!/bin/bash
# Rebuilds the sizzle reel in WSL survivor (the sim needs g++ and SDL2):
#   wsl -d survivor --exec bash /mnt/d/<this checkout>/tools/reel/make_reel.sh [--only <beat> ...]
# Builds the simulator on WSL-local disk, as sim_video.sh does, then
# make_reel.py writes reel-square.mp4, reel-wide.mp4 and check/ to OUT.
# ASSETS holds what git does not: the board's framebuffer shots (shots/), the
# phone screenshot (phone.png) and the roster art (roster/, from roster-art).
set -euo pipefail
src="$(cd "$(dirname "$0")/../.." && pwd)"
dst="$HOME/.cache/blorbarium-build/$(printf '%s' "$src" | md5sum | cut -c1-12)"
mkdir -p "$dst"
rsync -a --delete --exclude .pio --exclude .git --exclude outputs "$src/" "$dst/"
cd "$dst"
~/.platformio/penv/bin/pio run -e sim -s
venv="$HOME/.cache/reel-venv"
[ -x "$venv/bin/python" ] || { python3 -m venv --system-site-packages "$venv"; "$venv/bin/pip" install -q imageio-ffmpeg; }
export FFMPEG="$("$venv/bin/python" -c 'import imageio_ffmpeg as m; print(m.get_ffmpeg_exe())')"
exec "$venv/bin/python" tools/reel/make_reel.py --out "${OUT:-/mnt/d/projects/bak/blorbarium/reel}" \
  --assets "${ASSETS:-/mnt/d/projects/bak/blorbarium/reel/assets}" "$@"
