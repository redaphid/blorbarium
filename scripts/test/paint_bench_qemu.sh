#!/bin/bash
# The paint bench as the board's compiler builds it, run on Espressif's QEMU
# (an ESP32-S3 machine; no board, no serial port): S3 instructions a frame by phase
# for lib/paint as the badge builds it, and whether it draws the host's bytes.
#   bash scripts/test/paint_bench_qemu.sh [every=16]
# QEMU is github.com/espressif/qemu's release, fetched once into outputs/tools
# (esp-develop-9.2.2-20250817), or QEMU=/path/to/qemu-system-xtensa.
# Under -icount a virtual nanosecond is an instruction: it counts the work,
# not the stalls (cache misses, PSRAM, flash), so it ranks changes to the code;
# the board's own `DEBUG fps` says what the stalls add (about none, so far:
# the board ran near one instruction a cycle).
set -euo pipefail
root="$(cd "$(dirname "$0")/../.." && pwd)"
out="$root/outputs/paint_bench_s3"
qemu="${QEMU:-$root/outputs/tools/qemu/bin/qemu-system-xtensa}"
mkdir -p "$out"
if [ ! -x "$qemu" ]; then
  release=esp-develop-9.2.2-20250817
  mkdir -p "$root/outputs/tools"
  curl -sSL -o "$root/outputs/tools/qemu.tar.xz" \
    "https://github.com/espressif/qemu/releases/download/$release/qemu-xtensa-softmmu-${release//-/_}-x86_64-linux-gnu.tar.xz"
  tar -C "$root/outputs/tools" -xf "$root/outputs/tools/qemu.tar.xz"
fi
bash "$root/scripts/test/paint_bench.sh" 1 > /dev/null   # the host objects the dumper links against
g++ -std=c++17 -O2 -I"$root/lib/blorb/include" -I"$root/lib/blorb/src" -I"$root/lib/paint/include" -I"$root/lib/paint/src" \
    -I"$root/pets" -I"$root/pets/grungo" -I"$root/test/support" "$root/scripts/test/paint_shots_dump.cpp" \
    "$root"/outputs/paint_bench/common/blorb_*.o "$root"/outputs/paint_bench/paint/*.o -o "$out/dump"
"$out/dump" "${1:-16}" "$out/shots.inc.new"
cmp -s "$out/shots.inc.new" "$out/shots.inc" || mv "$out/shots.inc.new" "$out/shots.inc"
rm -f "$out/shots.inc.new"
(cd "$root" && pio run -e paintbench_s3 -j 3 > "$out/build.log" 2>&1) || { tail -30 "$out/build.log"; exit 1; }
build="$root/.pio/build/paintbench_s3"
fw="$HOME/.platformio/packages/framework-arduinoespressif32"
python3 - "$out/flash.bin" "$build/bootloader.bin" "$build/partitions.bin" "$fw/tools/partitions/boot_app0.bin" \
    "$build/firmware.bin" <<'EOF'
import sys
img = bytearray(b"\xff" * (8 << 20))   # the devkit's 8 MB, as the image header says
for at, path in zip((0x0, 0x8000, 0xE000, 0x10000), sys.argv[2:]):
    data = open(path, "rb").read()
    img[at:at + len(data)] = data
open(sys.argv[1], "wb").write(img)
EOF
rm -f "$out/run.log"
"$qemu" -nographic -machine esp32s3 -icount shift=0 -drive file="$out/flash.bin",if=mtd,format=raw \
    -serial file:"$out/run.log" -monitor none > /dev/null 2>&1 &
pid=$!
for _ in $(seq 2400); do   # 20 minutes at most
  kill -0 "$pid" 2> /dev/null || { echo "qemu exited"; break; }
  grep -q '\[bench\] done\|Rebooting\|Backtrace' "$out/run.log" 2> /dev/null && break
  sleep 0.5
done
kill "$pid" 2> /dev/null || true
grep '\[bench\]\|assert\|Backtrace' "$out/run.log"
