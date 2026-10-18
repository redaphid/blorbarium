#!/usr/bin/env bash
# Flash both soak badges, one at a time (ModemManager probes CDC ports and can collide).
cd "$(dirname "$0")/.."
for p in /dev/ttyACM0 /dev/ttyACM1; do
  for t in 1 2 3 4; do
    ~/.local/bin/esptool.py --chip esp32s3 --port $p -b 460800 write_flash 0x0 .pio/build/badge128/merged.hex > /tmp/flash.log 2>&1 && { echo "$p ok"; break; }
    echo "$p try $t failed: $(grep -i -m1 error /tmp/flash.log)"; sleep 2
  done
done
