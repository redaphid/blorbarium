#!/bin/bash
# Builds test_feeding, writes its film before (the screen as it was, FeedView
# cleared) and after, and the PNGs and sheets: outputs/feeding/.
set -euo pipefail
cd "$(dirname "$0")/../.."
PLATFORMIO_RUN_JOBS=3 ~/.platformio/penv/bin/pio test -e native -f test_feeding --without-testing > /dev/null
out="$PWD/outputs/feeding"
mkdir -p "$out/ppm_before" "$out/ppm_after"
FEEDING_DUMP="$out/ppm_after" .pio/build/native/program --gtest_filter='Film.*' > /dev/null
FEEDING_PLAIN=1 FEEDING_DUMP="$out/ppm_before" .pio/build/native/program --gtest_filter='Film.*' > /dev/null
python3 scripts/test/feeding_sheet.py "$out"
