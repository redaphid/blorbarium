#!/bin/bash
# Rebuilds test/test_provision's flash fixtures with the real tools, so they are
# the formats on the board and not this repo's idea of them:
#   nvs_pet_v1.bin     ESP-IDF's NVS generator: namespace "blorb" holding
#                      keepsake_v1.bin as snap.a and snap.b, as Preferences does
#   nvs_foreign.bin    the same generator: an NVS another firmware wrote
#   petfs_head.bin     blocks 0 and 1 of mklittlefs's image at petfs's geometry
#                      (4096 x 1440): the superblock pair; the rest is 0xFF
# Needs uv and PlatformIO's tool-mklittlefs. Runs where both are (Git Bash here).
set -euo pipefail
root="$(cd "$(dirname "$0")/.." && pwd)"
out="$root/test/fixtures"
work="$(mktemp -d)"
trap 'rm -rf "$work"' EXIT
nvsgen() { uv run --quiet --with esp-idf-nvs-partition-gen python -m esp_idf_nvs_partition_gen generate "$@" >/dev/null; }

cp "$out/keepsake_v1.bin" "$work/"
cd "$work"
printf 'key,type,encoding,value\nblorb,namespace,,\nsnap.a,file,binary,keepsake_v1.bin\nsnap.b,file,binary,keepsake_v1.bin\n' > pet.csv
nvsgen pet.csv "$out/nvs_pet_v1.bin" 0x6000
printf 'key,type,encoding,value\nmisc,namespace,,\nboot_count,data,u32,7\nssid,data,string,factory-demo\n' > foreign.csv
nvsgen foreign.csv "$out/nvs_foreign.bin" 0x3000

mkdir fs
head -c 3000 /dev/zero | tr '\0' 'L' > fs/lineage.log
mklittlefs="$(ls ~/.platformio/packages/tool-mklittlefs/mklittlefs* | head -1)"
"$mklittlefs" -c fs -b 4096 -p 256 -s 0x5A0000 petfs.bin >/dev/null
head -c 8192 petfs.bin > "$out/petfs_head.bin"
ls -l "$out"/nvs_pet_v1.bin "$out"/nvs_foreign.bin "$out"/petfs_head.bin
