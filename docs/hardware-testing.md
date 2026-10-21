# Testing on the hardware

Nobody can see the board's screen from a tool call, and this desk has no
camera. This page gives you the full loop on a board on the cable. You hold
off anything else that drives the port, flash it, read
its log, press its button and take a picture of its screen. Every command
here was run on the test board on 2026-10-13.

## The boards

Any Waveshare ESP32-S3-LCD-1.28 badge on the cable may be flashed and tested.
Do not back it up first: the merged `.hex` leaves the pet partitions alone.

Every badge uses a CH343 bridge with the same USB id, `1A86:55D3`, and a COM number
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

## 2. Build and flash

    ~/.platformio/penv/Scripts/pio.exe run -e badge128
    tools/flash_board.sh COM11 5C94095006 d4:05:92:77:1b:34 .pio/build/badge128/merged.hex

The build writes `merged.hex` (`tools/merge_image.py`). The script refuses
a port whose USB serial or MAC differs from the ones you name. It also
refuses anything but a `.hex`. A merged Intel HEX has gaps over the `pet`
and `petfs` partitions, so a cable flash keeps the pet. A flat `.bin` fills
the gaps with 0xFF and erases it.

esptool resets the board when it finishes. That first boot is the one that
founds a pet on a blank board (`boot=Fresh`), and it is over before any log
can open, so a log started afterwards shows only the next boot,
`boot=Resumed`.

To capture a first boot, flash with `--no-reset` and let the logger boot the
board:

    tools/flash_board.sh --no-reset COM12 5C94094999 d4:05:92:77:1b:44 .pio/build/badge128/merged.hex
    uv run tools/serial_log.py --port COM12 --seconds 40 --reset > first-boot.log

`--no-reset` passes `--after no_reset` to esptool, so the board stays in
download mode ("Staying in bootloader.") and prints nothing until the
logger's RTS pulse. On a new board, the log then starts with
`storage pet: formatted: it was blank`, `storage petfs: formatted: it was
blank` and `boot=Fresh`. The serial and MAC checks are the same as without
it.

## 3. Read the log

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
| `DEBUG fps` | Frames a second since it last asked, with the draw and push times (docs/power.md, "Awake: full speed") |
| `DEBUG lcd [MHz]` | The panel's SPI write clock until reset (built at `BLORB_LCD_SPI_MHZ`, 40; it prints the clock the divider gives) |

## 4. Take a screenshot

    uv run tools/shot.py --port COM11 --out dish.png --scale 2
    uv run tools/shot.py --port COM11 --send "DEBUG press" --wait 800 --out pressed.png

`tools/shot.py` sends `DEBUG shot` and writes the PNG. The board answers
with `[hw] shot` lines: PackBits RLE over the RGB565 canvas, in base64,
then an end line that carries the byte count and a crc32 of the pixels.
`lib/paint/include/paint/shot.h` defines the format. The script checks the
crc against the decoded pixels and tries again, up to `--tries` times, when
a log line lands inside the shot. `--from <log>` decodes a shot from a
captured log instead.

An adult in the dish encodes to about 74 KB in 770 lines and takes about
9 s at 115200 baud (measured 2026-10-13: 73,605 bytes, 9.1 s). A flat
screen takes a fraction of a second.

What a shot shows, and what it does not change:

- It is the canvas, the frame as drawn, upright for whoever holds the board.
  The panel rotates to match.
- It saves nothing and changes no pet state. The Dish keeps sampling and
  ticking while the shot goes out. Only the glass holds its last frame until
  the last line is sent.
- It goes over the cable only, so a connected phone does not see it. This
  desk has no Bluetooth adapter, so a shot with a phone connected has not
  been tried on the board yet.
- On 2026-10-13 a `[hw] up=` line landed inside a shot. The shot still
  decoded, and `writes=` did not change across it.

The encoder and the decoder are tested against each other. `test/test_shot` holds the
encoder to `test/fixtures/shot_v1.txt` byte for byte. `tests/test_shot.py`
decodes that same file with `tools/shot.py`. `tools/wsl_test.sh` runs both.
