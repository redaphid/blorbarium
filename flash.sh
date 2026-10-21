#!/usr/bin/env bash
# Force-flash every badge the moment it is attached. No backup, no identity checks.
#   ./flash.sh            # build if needed, then flash badges as they appear (Ctrl-C to stop)
#   ./flash.sh --once     # flash one badge and exit
#   ./flash.sh --flip     # for cases held USB-C down: that is the right way up (env badge128_flip)
# Writes the merged .hex, whose gaps leave the pet partitions alone, so each pet stays.
# If a badge will not connect, unplug it, hold BOOT, and plug it back in.
set -u
cd "$(dirname "$0")"
env=badge128; once=0
for a in "$@"; do
  case $a in
    --once) once=1 ;;
    --flip) env=badge128_flip ;;
    *) echo "flash.sh: unknown option $a (--once, --flip)" >&2; exit 2 ;;
  esac
done
image=.pio/build/$env/merged.hex

# esptool 4.7+ is required: older ones write the hex text itself and destroy the pet.
et=${ESPTOOL:-}
if [ -z "$et" ]; then
  for c in "$HOME/.local/bin/esptool.py" "$HOME/.local/bin/esptool" "$(command -v esptool.py)" "$(command -v esptool)"; do
    [ -x "$c" ] && { et=$c; break; }
  done
fi
[ -n "$et" ] || { echo "flash.sh: no esptool; run: uv tool install esptool" >&2; exit 1; }
ver=$("$et" version 2>/dev/null | grep -oE '[0-9]+\.[0-9]+' | head -1)
{ [ "${ver%%.*}" -gt 4 ] || { [ "${ver%%.*}" -eq 4 ] && [ "${ver#*.}" -ge 7 ]; }; } 2>/dev/null ||
  { echo "flash.sh: $et is esptool ${ver:-?}; need 4.7+ (uv tool install esptool)" >&2; exit 1; }

# Always: a no-op when nothing changed, and a stale image is never written.
echo "flash.sh: building $env"; ~/.platformio/penv/bin/pio run -e "$env" || exit 1
[ $env = badge128_flip ] && echo "flash.sh: FLIPPED: USB-C down is up on these badges"

badge_port() {   # first /dev/ttyACM* that is a CH343 (1a86:55d3)
  local p d
  for p in /dev/ttyACM*; do
    [ -e "$p" ] || continue
    d=$(readlink -f "/sys/class/tty/${p##*/}/device") || continue
    while [ "${#d}" -gt 1 ] && [ ! -f "$d/idVendor" ]; do d=${d%/*}; done
    [ "$(cat "$d/idVendor" 2>/dev/null):$(cat "$d/idProduct" 2>/dev/null)" = "1a86:55d3" ] && { echo "$p"; return; }
  done
}

while true; do
  echo "waiting for a badge ..."
  until port=$(badge_port) && [ -n "$port" ]; do sleep 0.3; done
  sleep 1
  ok=0
  for try in 1 2 3 4 5 6; do
    [ -e "$port" ] || { port=$(badge_port); [ -n "$port" ] || break; }
    echo "flashing $port (try $try)"
    if "$et" --chip esp32s3 --port "$port" -b 460800 write_flash 0x0 "$image"; then ok=1; break; fi
    sleep 1
  done
  if [ $ok = 1 ]; then echo "FLASHED. Unplug the badge."; else echo "FAILED. Unplug, hold BOOT, replug."; fi
  [ $once = 1 ] && exit $((1 - ok))
  while [ -e "$port" ]; do sleep 0.3; done
done
