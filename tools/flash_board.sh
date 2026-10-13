#!/bin/bash
# Cable-flashes a merged .hex onto one named board and nothing else:
#   tools/flash_board.sh COM11 5C94095006 d4:05:92:77:1b:34 .pio/build/badge128/merged.hex
#   tools/flash_board.sh --no-reset COM12 5C94094999 d4:05:92:77:1b:44 .pio/build/badge128/merged.hex
# Before writing it checks that the port's USB serial number and the chip's
# MAC are the ones given, so a board that moved to another COM number (two
# CH343 boards share 1A86:55D3) is refused, not overwritten. Only a merged
# .hex: its gaps leave the pet's partitions alone, where a flat .bin pads
# them with 0xFF and the pet is gone. --no-reset leaves the board in download
# mode afterwards, so the first boot of the new image happens only when
# tools/serial_log.py --reset pulses it, with the log already open: on a new
# board that boot is the one that formats and hatches. docs/hardware-testing.md
# has the rest.
set -euo pipefail
after=()
if [ "${1:-}" = --no-reset ]; then after=(--after no_reset); shift; fi
[ $# -eq 4 ] || { sed -n '2,4p' "$0"; exit 2; }
port=$1 serial=$2 mac=$3 image=$4
die() { echo "flash_board: $*" >&2; exit 1; }
case "$image" in *.hex) ;; *) die "$image is not a merged .hex; refusing (a flat image erases the pet)";; esac
[ -f "$image" ] || die "no such image: $image"
penv=~/.platformio/penv
py=$penv/Scripts/python.exe; [ -x "$py" ] || py=$penv/bin/python
pio=$penv/Scripts/pio.exe; [ -x "$pio" ] || pio=$penv/bin/pio
esptool=(~/.platformio/packages/tool-esptoolpy/esptool.py --chip esp32s3 --port "$port" "${after[@]}")

"$pio" device list | grep -A2 -x "$port" | grep -q "SER=$serial" ||
  die "$port is not USB serial $serial"
got=$("$py" "${esptool[@]}" read_mac | awk '/^MAC:/ { print $2; exit }')
[ "$got" = "$mac" ] || die "$port answers with MAC $got, not $mac"
echo "flash_board: $port is $serial / $mac; writing $image"
exec "$py" "${esptool[@]}" -b 921600 write_flash 0x0 "$image"
