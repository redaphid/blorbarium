#!/usr/bin/env bash
# Builds the Arduino framework the badge128_pm env links against: Arduino
# 2.0.17's own libraries, rebuilt from the exact sources that made the stock
# package (tools/sdk/versions.txt: ESP-IDF v4.4.7 38eeba213a and the pinned
# components), with only power management switched on:
#
#   CONFIG_PM_ENABLE                 automatic light sleep and DFS (esp_pm_configure)
#   CONFIG_FREERTOS_USE_TICKLESS_IDLE   the chip sleeps whenever every task waits
#   CONFIG_BT_CTRL_MODEM_SLEEP       BLE's radio sleeps between events, so the link survives light sleep
#   CONFIG_BT_CTRL_LPCLK_SEL_MAIN_XTAL  its sleep clock: the board has no 32 kHz crystal
#   CONFIG_PM_PROFILING              time per power mode, for DEBUG power stats
#
# Everything else, LittleFS above all, is byte for byte the stock stack: the
# script refuses to install a framework whose sdkconfig differs from stock in
# anything but these lines (docs/power.md, "Light sleep"). Two more are let
# through: PHY_MAC_BB_PD, which only exists once PM is on, and
# ESP_SECURE_CERT_*, from esp_secure_cert_mgr, which RainMaker pulls in
# unpinned through the component manager and blorbarium never links.
# SKIP_BUILD=1 installs what the last build left in out/.
#
#   bash tools/pm_framework.sh            # an hour or so the first time
#   pio run -e badge128_pm
#
# Work tree: $LIB_BUILDER (default ~/src/esp32-arduino-lib-builder).
# Installs:  ~/.platformio/packages/framework-arduinoespressif32-pm
set -euo pipefail
LB=${LIB_BUILDER:-$HOME/src/esp32-arduino-lib-builder}
STOCK=$HOME/.platformio/packages/framework-arduinoespressif32
DEST=$HOME/.platformio/packages/framework-arduinoespressif32-pm
here=$(cd "$(dirname "$0")" && pwd)
die() { echo "pm_framework: $*" >&2; exit 1; }
[ -f "$STOCK/tools/sdk/esp32s3/sdkconfig" ] || die "no stock framework at $STOCK: run 'pio run -e badge128' once"
grep -q "esp-idf: v4.4.7 38eeba213a" "$STOCK/tools/sdk/versions.txt" || die "the stock framework is not Arduino 2.0.17 / IDF v4.4.7 38eeba213a"
command -v jq >/dev/null || die "needs jq"

# 1. The sources, pinned to tools/sdk/versions.txt.
[ -d "$LB" ] || git clone -q -b release/v4.4 https://github.com/espressif/esp32-arduino-lib-builder.git "$LB"
git -C "$LB" checkout -q 3eb9cb0   # 2024-05-22, the builder of 2.0.17
cd "$LB"
[ -d esp-idf ] || git clone -q https://github.com/espressif/esp-idf.git esp-idf
git -C esp-idf checkout -q 38eeba213a
git -C esp-idf submodule update --init --recursive -q
[ -f esp-idf/.pm_tools_installed ] || { esp-idf/install.sh esp32s3 && touch esp-idf/.pm_tools_installed; }
(cd esp-idf && patch -p1 -N -s -i ../patches/i2s.diff >/dev/null || true)   # what the builder always applied
pin() {
  local d=components/$1
  [ -d "$d" ] || git clone -q "$2" "$d"
  git -C "$d" fetch -q --all || true
  git -C "$d" checkout -q "$3"
  git -C "$d" submodule update --init --recursive -q
}
pin arduino https://github.com/espressif/arduino-esp32.git d75795f5
pin esp32-camera https://github.com/espressif/esp32-camera.git f0bb429
pin esp-dl https://github.com/espressif/esp-dl.git 0632d24
pin esp-rainmaker https://github.com/espressif/esp-rainmaker.git d8e9345
pin esp_littlefs https://github.com/joltwallet/esp_littlefs.git 41873c2
pin espressif__esp-dsp https://github.com/espressif/esp-dsp.git 9b4a8b4
[ -d components/arduino_tinyusb/tinyusb ] || git clone -q https://github.com/hathach/tinyusb.git components/arduino_tinyusb/tinyusb
git -C components/arduino_tinyusb/tinyusb checkout -q a0e5626bc

# 2. Power management on, in the S3's own defconfig, and nothing else.
cfg=configs/defconfig.esp32s3
git checkout -q -- "$cfg"
cat "$here/pm_framework.defconfig" >> "$cfg"

# 3. Build the S3 only (libs, bootloaders, memory variants), as the builder does.
# shellcheck disable=SC1091
source esp-idf/export.sh >/dev/null
# IDF 4.4's tools want a cmake; a host without one gets it in the IDF's own venv.
command -v cmake >/dev/null || python -m pip install -q "cmake==3.24.3"
[ "${SKIP_BUILD:-0}" = 1 ] || ./build.sh -s -t esp32s3

# 4. The sdkconfig may differ from stock only in power management.
built=out/tools/sdk/esp32s3/sdkconfig
[ -f "$built" ] || die "the build left no $built"
strip() { grep -v '^#\s*$' "$1" | grep -v '^# Automatically generated\|^# Espressif IoT\|^#$' | grep -E '^(CONFIG_|# CONFIG_)' | sort; }
extra=$(diff <(strip "$STOCK/tools/sdk/esp32s3/sdkconfig") <(strip "$built") | grep '^[<>]' |
        grep -Ev 'CONFIG_PM_|PM_ENABLE|TICKLESS|IDLE_TIME_BEFORE_SLEEP|BT_CTRL_(MODEM_SLEEP|SLEEP|LPCLK|MAIN_XTAL)|BTDM_CTRL_(MODEM_SLEEP|LPCLK|LOW_POWER)|BT_SLEEP|_SLEEP_CLOCK|LP_CLK|ESP_SYSTEM_PM|PM_SLP|PM_POWER_DOWN|PM_RESTORE|PM_TRACE|PM_PROFILING|PM_DFS|ESP32S3_LIGHTSLEEP|LIGHT_SLEEP|PHY_MAC_BB_PD|ESP_SECURE_CERT_' || true)
echo "pm_framework: sdkconfig against stock:"
diff <(strip "$STOCK/tools/sdk/esp32s3/sdkconfig") <(strip "$built") | grep '^[<>]' || true
[ -z "$extra" ] || die "the build differs from stock beyond power management:
$extra"

# 5. Install: the stock package with only the S3's SDK swapped.
rm -rf "$DEST.tmp"
cp -a "$STOCK" "$DEST.tmp"
rm -rf "$DEST.tmp/tools/sdk/esp32s3"
cp -a out/tools/sdk/esp32s3 "$DEST.tmp/tools/sdk/esp32s3"
python3 - "$DEST.tmp/package.json" <<'EOF'
import json, sys
p = sys.argv[1]
d = json.load(open(p))
d["name"] = "framework-arduinoespressif32-pm"
d["description"] = "Arduino 2.0.17 with ESP32-S3 power management (blorbarium tools/pm_framework.sh)"
json.dump(d, open(p, "w"), indent=2)
EOF
rm -rf "$DEST"
mv "$DEST.tmp" "$DEST"
echo "pm_framework: installed $DEST"
