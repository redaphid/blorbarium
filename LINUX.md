# Flashing badges from Linux

This page takes a stock Linux laptop from nothing to a flashed badge over a
USB cable. The tools are the same as on the Windows desk:
`tools/flash_batch.py` flashes a batch, one badge at a time, and
`tools/flash_board.sh` flashes one board. By default both **keep each pet**.

The badge is a Waveshare ESP32-S3-LCD-1.28. Its USB bridge is a CH343, USB
id `1A86:55D3`. Linux's built-in `cdc_acm` driver runs it, so it shows up as
`/dev/ttyACM0` (or ACM1 and up) with no driver to install.

## 1. Install the tools

Debian or Ubuntu:

    sudo apt install git python3 python3-venv curl

Fedora: `sudo dnf install git python3 curl`. Arch: `sudo pacman -S git python curl`.

Install PlatformIO with its own installer. That puts it in
`~/.platformio/penv`, which is where the scripts look for it:

    curl -fsSL -o get-platformio.py https://raw.githubusercontent.com/platformio/platformio-core-installer/master/get-platformio.py
    python3 get-platformio.py
    echo 'export PATH="$HOME/.platformio/penv/bin:$PATH"' >> ~/.bashrc
    export PATH="$HOME/.platformio/penv/bin:$PATH"

`pipx install platformio` also builds. It has no `~/.platformio/penv`,
though, so for flashing add `pipx install esptool` and run the batch with
`uv run tools/flash_batch.py`. (`uv` reads pyserial from the script's
header. Install uv with `curl -LsSf https://astral.sh/uv/install.sh | sh`.)

## 2. Clone and build

    git clone https://github.com/redaphid/blorbarium
    cd blorbarium
    pio run -e badge128

The first build downloads the ESP32 toolchain, which takes a few minutes. It
ends with `merged.hex: 0x0 bootloader.bin  0x8000 partitions.bin ...` and
leaves `.pio/build/badge128/merged.hex`, the file every command below
writes.

## 3. Let your user open the badge

A normal user may not open `/dev/ttyACM*`. Do one of these two once.

Join the serial group (`uucp` on Arch), then log out and
back in:

    sudo usermod -aG dialout $USER

Or add a udev rule for the badge's USB id. It applies at the next plug-in,
with no new login, and it stops ModemManager from probing the badge:

    echo 'SUBSYSTEM=="tty", ATTRS{idVendor}=="1a86", ATTRS{idProduct}=="55d3", MODE="0666", ENV{ID_MM_DEVICE_IGNORE}="1"' | sudo tee /etc/udev/rules.d/99-blorbarium.rules
    sudo udevadm control --reload-rules && sudo udevadm trigger

Check: plug a badge in, then run `ls -l /dev/ttyACM*` and `pio device list`.
The badge's entry says `VID:PID=1A86:55D3 SER=<its USB serial>`. If you
skipped this step, the tools stop with "this user may not open
/dev/ttyACM0" and print both fixes.

## 4. Flash the batch (keeps each pet)

Rehearse first. The dry run walks the 9 boards of the MAC table on
pretend boards and writes nothing:

    ~/.platformio/penv/bin/python tools/flash_batch.py --dry-run

Then the real run:

    ~/.platformio/penv/bin/python tools/flash_batch.py

It says which mode it is in (`KEEPING each badge's pet: it must boot
Resumed`). Then, for each badge, it asks:
`Plug in badge 1 (keep pet), press Enter (q to quit)`. Plug in one badge and
press Enter. The script then works through these steps:

1. It finds the new `/dev/ttyACM*` by USB id and reads the badge's USB serial
   and MAC.
2. It copies the `pet` partition to `pet_backups/<mac>-<time>.bin`.
3. It writes `merged.hex` through `flash_board.sh`, which checks the serial
   and the MAC again first.
4. It boots the badge with the log open.
5. It prints one line and appends it to `flash_log.csv`, then asks you to
   unplug the badge.

A good line looks like this:

    OK badge 1: /dev/ttyACM0  mac d4:05:92:77:1b:3c  mossby  species mossby  genome 2ea98ea9  boot=Resumed  pet keep  (the pet adopted species mossby)

To carry on numbering from another badge, pass `--start 5`. To flash one
badge only, press Enter once and then `q`.

A badge fails, and you are asked to retry, skip or quit, when:

- its boot is not `Resumed`, unless its storage line says it never held a
  pet. A brand-new board boots `Fresh` and passes.
- its log shows `FromLineage`, `Sealed`, `ReadOnlyNewer` or any `!!!` line.
- `PET NOT FOUND` appears: the board booted Fresh over a pet. **Stop.** Do
  not flash that badge again. The old pet may still be on its flash, and
  its `pet` partition is in `pet_backups/`.

A note after `!!` on an OK line means the badge works but something is off.
`DUPLICATE SPECIES` means another MAC in `flash_log.csv` already has that
creature. `NOT THE TABLE'S SPECIES` means a known badge came up as a
different creature than the table below.

### Wiping instead (a new egg on every badge)

    ~/.platformio/penv/bin/python tools/flash_batch.py --wipe-pet

This erases each badge's `pet` and `petfs` partitions after backing up
`pet`, then demands `boot=Fresh`. The backup holds only the `pet` partition.
Erasing `petfs` deletes the lineage log for good. Use this only when the
owner wants every badge reset. The prompt says `(wipe pet)` and the run
starts with `WIPING each badge's pet`.

## 5. One badge by hand

    pio device list                                   # the port and SER=<usb serial>
    ~/.platformio/penv/bin/python ~/.platformio/packages/tool-esptoolpy/esptool.py --chip esp32s3 --port /dev/ttyACM0 read_mac
    tools/flash_board.sh --no-reset /dev/ttyACM0 <usb serial> <mac> .pio/build/badge128/merged.hex
    ~/.platformio/penv/bin/python tools/serial_log.py --port /dev/ttyACM0 --seconds 40 --reset

For example, for badge 3:
`tools/flash_board.sh --no-reset /dev/ttyACM0 5C94095006 d4:05:92:77:1b:34 .pio/build/badge128/merged.hex`.
The script refuses a port whose USB serial or MAC is not the one you name,
and it refuses anything but a `.hex`. Add `--wipe-pet` before `--no-reset`
to erase the pet first. `--no-reset` leaves the board waiting, so the first
boot of the new firmware happens when `serial_log.py --reset` starts it,
with the log already open. Without `--no-reset`, esptool restarts the board
and the boot lines are gone before a log can open.

If esptool cannot connect, hold the badge's BOOT button while you plug it
in. That puts it in download mode, and then it flashes the same way.

## 6. Read the boot log

    [hw] blorbarium 1a2b3c4 fw=0.7.0 fmt=1 badge128
    [hw] storage pet: ok
    [hw] storage petfs: ok
    [hw] boot=Resumed occupant=creature gen=0 genome=2ea98ea9 age=86400 species=grungo
    [hw] ble name=mossby heap=136244 big=90100
    [hw] adopted species=mossby gen=0 genome=5d0c11e2 name=Pip (the Bluetooth name follows at the next boot)

- `fw=0.7.0` is the firmware that is now running.
- `storage pet: ok` means the pet's storage opened as it was.
  `formatted: it was blank` means there was no pet to keep (a new board).
- `boot=Resumed` means **the pet came back**. `occupant=creature` with its
  `age` shows it is the same creature. `boot=Fresh` means a new egg.
  `species=` is what booted. A pet that 0.4.0 founded reads `grungo` here
  until it adopts its badge's species.
- `ble name=mossby` is the creature's species, which is also the name a
  phone sees over Bluetooth. Firmware 0.4.0 said `ble name=blorb-1b3c`
  instead. `flash_batch.py` reads both.
- `adopted species=mossby` shows up once, on the first 0.7.0 boot of a pet
  that 0.4.0 founded. The same pet, with its name and age, becomes its
  badge's species from the table below. The batch waits up to 5 s after the
  BLE name for this line. If it never comes, the OK line says
  `NOT THE TABLE'S SPECIES: ... is grungo, should be mossby`. Read the log
  again with `serial_log.py` to see whether the adoption came later.
- Any line with `!!!` is trouble. Leave that badge and say what it printed.

## 7. Which badge is which creature

From firmware 0.7.0 the 8 badges and the gift device each get their own
species from a fixed MAC table (`lib/blorb/include/blorb/defs/badges.def`,
from `docs/notes/decisions-2026-10-15.md` D3). The USB serial is
what `pio device list` shows without touching the board. The MAC is what
`read_mac` and the batch print.

| Badge | USB serial | MAC | Old BLE name | Species (new BLE name) |
|---|---|---|---|---|
| 1 | 5C94095000 | d4:05:92:77:1b:3c | blorb-1b3c | mossby |
| 2 | 5C94094999 | d4:05:92:77:1b:44 | blorb-1b44 | puffkin |
| 3 | 5C94095006 | d4:05:92:77:1b:34 | blorb-1b34 | gilly |
| 4 | 5C94094497 | d4:05:92:77:1c:b0 | blorb-1cb0 | mothwick |
| 5 | 5C94095003 | d4:05:92:77:1b:30 | blorb-1b30 | mudge |
| 6 | 5C94095002 | d4:05:92:77:1b:40 | blorb-1b40 | fizz |
| 7 | 5C94094576 | d4:05:92:77:22:58 | blorb-2258 | hushwing |
| 8 | 5C94094581 | d4:05:92:77:22:40 | blorb-2240 | tinker |
| gift | | d4:05:92:77:23:7c (assumed from its name) | blorb-237c | grungo |

The original gift device (`blorb-237c`) is the one grungo. It has no
updater, so any update needs a cable. Each species appears once in the
table, and any board that is not in it is a grungo.

## Why a cable flash keeps the pet

`merged.hex` is an Intel HEX file. It holds the bootloader, the partition
table and the app, and it has gaps over the `pet` (0x810000, 256 KB) and
`petfs` (0xA50000, 5.6 MB) partitions. esptool 4.7 and newer writes a .hex
segment by segment and never touches the gaps. In esptool 4.11 that is
`intel_hex_to_bin`, which splits the file into its segments. Both scripts
refuse an older esptool, which would write the hex text itself at 0x0.
Never `write_flash` a flat `.bin` at 0x0: it fills the gaps with 0xFF and
the pet is gone.

## When something goes wrong

- **"no badge found"**: use a data cable, not a charge-only one. Check that
  `ls /dev/ttyACM*` lists it, and that `sudo dmesg | tail` shows `cdc_acm ... ttyACM0`.
- **"may not open"**: do step 3, then log out and back in, or replug after
  the udev rule.
- **"no esptool"**: run `pio run -e badge128` once (it fetches
  PlatformIO's esptool), or run `pipx install esptool`.
- **"esptool ... need 4.7 or newer"**: an old distro package is first on
  PATH. Run `pipx install esptool`, or use PlatformIO's (step 1).
