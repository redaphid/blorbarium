#!/bin/bash
# Flashes the badges one at a time and proves each one's first boot:
#   tools/flash_fleet.sh --species gilly .pio/build/badge128/merged.hex [fleet-dir]
# Plug in ONE badge, run this, unplug it, repeat. It finds the single
# CH343 board on a cable that is not a test board, flashes it through
# flash_board.sh (so the serial and MAC are checked before writing), then
# reboots it and waits for the first boot to provision. A board passes only
# if its log shows both storage partitions ok or formatted and a boot= line:
# board 3 (2026-10-14) flashed cleanly and then hung with nothing on serial,
# and a flash that "succeeded" is not a badge that works. Each board's boot
# log and a row in fleet.csv (serial, MAC, BLE name, genome, verdict) land in
# the fleet dir, default D:/projects/bak/blorbarium/fleet. --species
# (a slug from species.def: grungo, mossby, puffkin, gilly, mothwick, mudge,
# fizz, hushwing, tinker) pins the egg a fresh device founds; a board that
# does not confirm the pin fails.
set -euo pipefail
species=
if [ "${1:-}" = --species ]; then species=$2; shift 2; fi
[ $# -ge 1 ] || { sed -n '2,3p' "$0"; exit 2; }
image=$1 dir=${2:-/d/projects/bak/blorbarium/fleet}
here=$(cd "$(dirname "$0")" && pwd)
# Test boards: COM11's, COM12's, board 3's and board 4's USB serials.
test_serials="5C94095006 5C94094999 5B91046680 5C94095000"
penv=~/.platformio/penv
py=$penv/Scripts/python.exe; [ -x "$py" ] || py=$penv/bin/python
pio=$penv/Scripts/pio.exe; [ -x "$pio" ] || pio=$penv/bin/pio
die() { echo "flash_fleet: $*" >&2; exit 1; }
mkdir -p "$dir"

mapfile -t boards < <("$pio" device list | awk '/^COM|^\/dev/ { port=$1 } /SER=/ && /1A86:55D3/ { match($0, /SER=[0-9A-Za-z]+/); print port, substr($0, RSTART+4, RLENGTH-4) }')
targets=()
for b in "${boards[@]}"; do
  set -- $b
  case " $test_serials " in *" $2 "*) echo "flash_fleet: skipping test board $1 ($2)";; *) targets+=("$b");; esac
done
[ ${#targets[@]} -eq 1 ] || die "want exactly one badge on a cable, found ${#targets[@]}: ${targets[*]:-none}"
set -- ${targets[0]}; port=$1 serial=$2
mac=$("$py" ~/.platformio/packages/tool-esptoolpy/esptool.py --chip esp32s3 --port "$port" read_mac | awk '/^MAC:/ { print $2; exit }')
[ -n "$mac" ] || die "$port did not answer with a MAC"
log="$dir/$serial-$(date +%Y%m%d-%H%M%S).log"

bash "$here/flash_board.sh" "$port" "$serial" "$mac" "$image"
pin=0
uv run -q "$here/fleet_boot.py" --port "$port" --seconds 20 ${species:+--species "$species"} > "$log" 2>&1 || pin=$?

verdict=PASS
[ $pin -eq 0 ] || verdict=FAIL
grep -Eq '\[hw\] storage pet: (ok|formatted)' "$log" || verdict=FAIL
grep -Eq '\[hw\] storage petfs: (ok|formatted)' "$log" || verdict=FAIL
grep -Eq '\[hw\] boot=(Fresh|Resumed)' "$log" || verdict=FAIL
grep -Eq 'Guru|panic|abort\(\)|SEALED|!!!' "$log" && verdict=FAIL
name=$(grep -oE 'ble name=[^ ]+' "$log" | head -1 | cut -d= -f2)
genome=$(grep -oE 'genome=[0-9a-f]+' "$log" | head -1 | cut -d= -f2)

[ -f "$dir/fleet.csv" ] || echo "when,serial,mac,ble,species,genome,verdict,log" > "$dir/fleet.csv"
echo "$(date -Is),$serial,$mac,${name:-?},${species:-rolled},${genome:-?},$verdict,$(basename "$log")" >> "$dir/fleet.csv"
echo "flash_fleet: $verdict  $serial $mac ${name:-?} ${species:-rolled} genome=${genome:-?}  log: $log"
[ "$verdict" = PASS ]
