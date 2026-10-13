# Testing on the hardware

Nobody can see the board's screen from a tool call, and this desk has no
camera. This page gives you the full loop on a board on the cable. You hold
off anything else that drives the port, back the board up, flash it, read
its log, press its button and take a picture of its screen. Every command
here was run on the test board on 2026-10-13.

## The boards

| Board | Port | USB serial | MAC | May be flashed |
|---|---|---|---|---|
| Test board, Waveshare ESP32-S3-LCD-1.28 | COM11 | `5C94095006` | `d4:05:92:77:1b:34` | Yes |
| The gift device, `blorb-237c` (grungo, genome `d30f00d1`) | usually COM10 | | | **No.** It holds a pet someone chose. |

Both use a CH343 bridge with the same USB id, `1A86:55D3`, and a COM number
can move between plugs. Identify a board by its USB serial number and MAC,
never by its COM number. `tools/flash_board.sh` checks both before it writes.

## 1. Keep the badge daemon off the port

This step applies only on this desk. The Claude Code status badge
(`D:\Projects\claude-notification-screen`) runs a daemon, `badged.py`, that
drives any CH343 board it finds. Its hooks restart it on every tool call.
blorbarium does not depend on it. It only shares the desk. Hold it off for
as long as you use the port:

    python D:/Projects/claude-notification-screen/host/daemonctl.py hold <pid>

A hold stops the daemon. It lasts while `<pid>` is alive, and for 15 minutes
after the hold file was last written, so refresh it every 5 minutes from a
process that lives as long as your work. This loop writes its own pid:

    python - <<'EOF'
    import os, subprocess, sys, time
    while True:
        subprocess.run([sys.executable, "D:/Projects/claude-notification-screen/host/daemonctl.py", "hold", str(os.getpid())])
        time.sleep(300)
    EOF

Confirm that the daemon has let go before you open the port. The last lines
of `~/.local/state/badged/badged.log` must say `asked to stop` and
`badge lost`, and `daemonctl.py status` must say `not running`. When you
finish, stop the loop. The hold then lapses by itself, and the hooks bring
the daemon back. Do not run `daemonctl.py start` for the owner.

## 2. Back up the whole flash, twice

Do not write anything until two reads match.

    E="$HOME/.platformio/penv/Scripts/python.exe $HOME/.platformio/packages/tool-esptoolpy/esptool.py --chip esp32s3 --port COM11 -b 921600"
    $E read_mac
    $E read_flash 0 0x1000000 read1.bin
    $E read_flash 0 0x1000000 read2.bin
    sha256sum read1.bin read2.bin && cmp read1.bin read2.bin

Each read takes about 221 seconds. Keep one copy under
`D:\projects\bak\blorbarium\` with its sha256 beside it. The test board as
it arrived (badge firmware 1.4.0, pet Puck) is
`testboard-5C94095006-badge-1.4.0-2026-10-13.bin`, sha256
`3668ece7cf2c44e5dccd86bfee75ed943b73d32e32659015a2edecc1b9a009fa`.

To restore that image, run the command below. It **erases blorbarium and
any pet on the board**, because a flat image overwrites every partition:

    $E write_flash 0x0 D:/projects/bak/blorbarium/testboard-5C94095006-badge-1.4.0-2026-10-13.bin

## 3. Build and flash

    ~/.platformio/penv/Scripts/pio.exe run -e badge128
    tools/flash_board.sh COM11 5C94095006 d4:05:92:77:1b:34 .pio/build/badge128/merged.hex

The build writes `merged.hex` (`tools/merge_image.py`). The script refuses
a port whose USB serial or MAC differs from the ones you name. It also
refuses anything but a `.hex`. A merged Intel HEX has gaps over the `pet`
and `petfs` partitions, so a cable flash keeps the pet. A flat `.bin` fills
the gaps with 0xFF and erases it.

esptool resets the board when it finishes. That first boot is the one that
founds a pet on a blank board (`boot=Fresh`). The log in step 4 starts
later, so it shows the next boot, `boot=Resumed`.

## 4. Read the log

    uv run tools/serial_log.py --port COM11 --seconds 300 --reset

The log runs at 115200 baud. The script holds DTR and RTS low when it opens
the port, because toggling them resets the board. `--reset` pulses RTS on
purpose, so you see the boot lines. Lines that start with `[hw]` are
diagnostics. The boot prints the firmware commit, PSRAM, storage, clock,
IMU, `boot=<how> occupant=<egg|creature|clutch> genome=<hash>` and the BLE
name. After that, a `[hw] up=` line arrives every 10 s with heap, stack
high-water mark, saves (`writes=`), occupant, IMU and button.

`--send "<line>"` sends one line after the port opens. You can send it more
than once. Serial-only `DEBUG` verbs never reach the phone:

| Line | Does |
|---|---|
| `DEBUG stage <hatchling\|child\|adult\|elder>` | Ages him there with real ticks and saves |
| `DEBUG press [ms]` | Holds BOOT down for `ms` (default 150, at most 10000). Under 1.2 s is a press, from 1.2 s on it is a hold |
| `DEBUG shot` | Sends the screen (see step 5) |
| `DEBUG bletest <bytes>` | Queues a test pattern to a connected BLE central |

## 5. Take a screenshot

    uv run tools/shot.py --port COM11 --out dish.png --scale 2
    uv run tools/shot.py --port COM11 --send "DEBUG press" --wait 800 --out pressed.png

`tools/shot.py` sends `DEBUG shot` and writes the PNG. The board answers
with `[hw] shot` lines: PackBits RLE over the RGB565 canvas, in base64,
then an end line that carries the byte count and a crc32 of the pixels.
`lib/paint/include/paint/shot.h` defines the format. The script checks the
crc against the decoded pixels and tries again, up to `--tries` times, when
a log line lands inside the shot. `--from <log>` decodes a shot from a
captured log instead.

What a shot shows, and what it does not change:

- It is the canvas, the frame as drawn, upright for whoever holds the board.
  The panel rotates to match.
- It saves nothing and changes no pet state. The Dish keeps sampling and
  ticking while the shot goes out. Only the glass holds its last frame until
  the last line is sent.
- It goes over the cable only, so a connected phone does not see it.

The encoder and the decoder are tested against each other. `test/test_shot` holds the
encoder to `test/fixtures/shot_v1.txt` byte for byte. `tests/test_shot.py`
decodes that same file with `tools/shot.py`. `tools/wsl_test.sh` runs both.
