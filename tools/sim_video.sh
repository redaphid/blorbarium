#!/bin/bash
# Films the clips in tools/reel as phone-friendly video, in WSL survivor:
#   wsl -d survivor --exec bash /mnt/d/<this checkout>/tools/sim_video.sh [clip ...]
# Builds the simulator on WSL-local disk, as sim_film.sh does, then
# tools/sim_video.py plays each clip on the fixed clock at 25 fps and writes
# outputs/videos/<clip>.mp4, reel.mp4 and a contact sheet per clip. OUT=<dir>
# writes somewhere else. The encoder is FFMPEG=<binary>, else ffmpeg or
# ffmpeg.exe on PATH, else imageio-ffmpeg's; with none the clips come out as
# animated WebP instead. survivor has no ffmpeg of its own; this serves:
#   python3 -m venv v && v/bin/pip install imageio-ffmpeg
#   FFMPEG=$(v/bin/python -c 'import imageio_ffmpeg as m; print(m.get_ffmpeg_exe())')
set -euo pipefail
src="$(cd "$(dirname "$0")/.." && pwd)"
dst="$HOME/.cache/blorbarium-build/$(printf '%s' "$src" | md5sum | cut -c1-12)"
mkdir -p "$dst"
rsync -a --delete --exclude .pio --exclude .git --exclude outputs "$src/" "$dst/"
cd "$dst"
~/.platformio/penv/bin/pio run -e sim -s
exec python3 tools/sim_video.py --out "${OUT:-$src/outputs/videos}" "$@"
