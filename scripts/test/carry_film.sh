#!/bin/bash
# Films every carried reaction (test/test_carry_film: a motion from
# src/hw/carry_ride.h through the whole body path, drawn as the badge draws)
# and makes strips of them in outputs/carry-react/.
#   PIO=<pio or a wrapper> bash scripts/test/carry_film.sh
set -euo pipefail
cd "$(dirname "$0")/../.."
out="$PWD/outputs/carry-react"
rm -rf "$out/ppm"
mkdir -p "$out/ppm"
export PLATFORMIO_RUN_JOBS=3
"${PIO:-pio}" test -e native -f test_carry_film --without-testing > /dev/null
CARRY_DUMP="$out/ppm" .pio/build/native/program --gtest_filter='Film.*'
python3 scripts/test/carry_sheet.py "$out"
