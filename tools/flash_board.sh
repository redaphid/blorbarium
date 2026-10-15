#!/usr/bin/env bash
# Cable-flashes a merged .hex onto one named board and nothing else:
#   tools/flash_board.sh /dev/ttyACM0 5C94095006 d4:05:92:77:1b:34 .pio/build/badge128/merged.hex
#   tools/flash_board.sh --no-reset COM12 5C94094999 d4:05:92:77:1b:44 .pio/build/badge128/merged.hex
# Linux (/dev/ttyACM*, /dev/ttyUSB*) or Windows Git Bash (COMn). Before
# writing it checks that the port's USB serial number and the chip's MAC are
# the ones given, so a board that moved to another port (two CH343 boards
# share 1A86:55D3) is refused, not overwritten. Only a merged .hex: its gaps
# leave the pet's partitions alone, where a flat .bin pads them with 0xFF and
# the pet is gone. That needs esptool 4.7 or newer, which writes a .hex by
# its segments. --no-reset leaves the board in download mode afterwards, so
# the first boot of the new image happens only when tools/serial_log.py
# --reset pulses it, with the log already open: on a new board that boot is
# the one that formats and hatches. docs/hardware-testing.md and LINUX.md
# have the rest. --wipe-pet erases the pet's partitions (pet, petfs) after
# the checks, so the next boot founds a new pet (boot=Fresh); the old pet is
# gone.
set -euo pipefail
after=() wipe=0
while [ $# -gt 0 ]; do
  case "$1" in
    --no-reset) after=(--after no_reset); shift;;
    --wipe-pet) wipe=1; shift;;
    *) break;;
  esac
done
[ $# -eq 4 ] || { sed -n '2,4p' "$0"; exit 2; }
port=$1 serial=$2 mac=$3 image=$4
die() { echo "flash_board: $*" >&2; exit 1; }
case "$image" in *.hex) ;; *) die "$image is not a merged .hex; refusing (a flat image erases the pet)";; esac
[ -f "$image" ] || die "no such image: $image"

# esptool: PlatformIO's own (penv python + the tool-esptoolpy package), else one on PATH.
py=
for p in ~/.platformio/penv/Scripts/python.exe ~/.platformio/penv/bin/python; do
  [ -x "$p" ] && { py=$p; break; }
done
script=~/.platformio/packages/tool-esptoolpy/esptool.py
if [ -n "$py" ] && [ -f "$script" ]; then et=("$py" "$script")
elif command -v esptool >/dev/null 2>&1; then et=(esptool)
elif command -v esptool.py >/dev/null 2>&1; then et=(esptool.py)
else die "no esptool: install PlatformIO and run 'pio run -e badge128' once (it brings one), or 'pipx install esptool'"
fi
ver=$("${et[@]}" version 2>/dev/null | grep -oE '[0-9]+\.[0-9]+' | head -1) || true
major=${ver%%.*} minor=${ver#*.}
[ -n "$ver" ] && { [ "$major" -gt 4 ] || { [ "$major" -eq 4 ] && [ "$minor" -ge 7 ]; }; } ||
  die "${et[*]} is esptool ${ver:-of no known version}; need 4.7 or newer, which writes a .hex by its segments ('pipx install esptool')"
esptool=("${et[@]}" --chip esp32s3 --port "$port" "${after[@]}")

case "$port" in
  /dev/*)
    [ -e "$port" ] || die "no $port (ls /dev/ttyACM* /dev/ttyUSB*)"
    [ -r "$port" ] && [ -w "$port" ] || die "this user may not open $port: 'sudo usermod -aG dialout \$USER' (uucp on Arch) and log in again, or add the udev rule in LINUX.md";;
esac

# The port's USB serial number: pyserial where a python has it, else Linux's sysfs.
usb_serial() {
  local p d
  for p in ${py:+"$py"} python3 python; do
    "$p" -c 'import sys
from serial.tools import list_ports
print(next((q.serial_number or "" for q in list_ports.comports() if q.device == sys.argv[1]), ""))' "$1" 2>/dev/null && return
  done
  d=$(readlink -f "/sys/class/tty/${1##*/}/device" 2>/dev/null) || return 0
  while [ "${#d}" -gt 1 ] && [ ! -f "$d/serial" ]; do d=${d%/*}; done
  [ ! -f "$d/serial" ] || cat "$d/serial"
}
got=$(usb_serial "$port" | tr -d '\r') || true
[ "$got" = "$serial" ] || die "$port is USB serial ${got:-unknown}, not $serial"
got=$("${esptool[@]}" read_mac | awk '/^MAC:/ { print $2; exit }') || true
[ "$got" = "$mac" ] || die "$port answers with MAC ${got:-none}, not $mac"
if [ $wipe = 1 ]; then
  echo "flash_board: $port is $serial / $mac; erasing its pet (pet 0x810000, petfs 0xA50000)"
  "${esptool[@]}" --after no_reset erase_region 0x810000 0x40000
  "${esptool[@]}" --after no_reset erase_region 0xA50000 0x5A0000
fi
echo "flash_board: $port is $serial / $mac; writing $image"
exec "${esptool[@]}" -b 921600 write_flash 0x0 "$image"
