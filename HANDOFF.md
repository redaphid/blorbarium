# HANDOFF

blorbarium is a little artificial-life creature in a 3D-printed case, for a
friend who does not program. It is a sibling of
[redaphid/claude-notification-screen](https://github.com/redaphid/claude-notification-screen)
(local clone at `D:\Projects\claude-notification-screen`, the `phone` worktree
at `C:\Users\hypnodroid\Worktrees\claude-notification-screen-phone`). That
project is a Claude Code status badge with a pet on it. This one keeps the pet
and drops Claude.

Nothing is built yet. This file is where things stand.

## What it is meant to be

1. **Powered by a phone over USB.** No battery and no laptop. The phone is
   only power. Nothing on the phone talks to the board over the cable.
2. **Purely alife.** The creature lives, eats, sleeps, learns and changes on
   the board itself, with no host running it. Its world is what the board can
   sense: being picked up, shaken, tilted and tapped, the time of day, and
   its owner visiting from the phone.
3. **A printed case with an NFC sticker inside.** Tap the case with a phone
   and the phone opens a website. The URL can carry the device's name (for
   example `?d=blorb-2ac8`) so the page only offers that one device.
4. **The website talks to the device over Web Bluetooth** and is where you
   customize the pet: look at its genes and drives, name it, recolour it,
   feed it, and whatever else turns out to be fun. It is served online over
   HTTPS, which Web Bluetooth requires. The owner's Cloudflare account is the
   obvious host.
5. "A really cool modern alife device." The round screen looks a lot like a
   petri dish, and that is worth leaning into. Options that came up but were
   not decided:
   - Port the Creatures-style single pet (see below) into C++ on the board.
   - Particle Life (Ventrella's "Clusters"): a few hundred particles whose
     species attract and repel by a small matrix. The matrix is the genome,
     and it maps neatly onto a phone UI where you drag the rules and watch
     life reorganise.
   - Lenia or another continuous cellular automaton on a coarse grid.
   - An evolving population with genomes, eating and splitting, in the dish.
   This is a product call. The honest next step is two or three cheap
   prototypes at 240x240 in a round mask, compared side by side.

## What to take from claude-notification-screen

All paths below are in that repo.

- **The board layer.** `src/display.h` is the seam that picks a board, and
  `src/board_lcd128.h` / `src/board_lcd146.h` are the boards. Everything
  draws into one 240x240 canvas (LovyanGFX 1.2.29, pinned on purpose). The
  1.46 board scales it up 1.72x on the way out.
- **The IMU.** `src/orient.h` reads the QMI8658 and turns it into up, tilt
  and gestures (tap, knock, shake, hold). This is most of the creature's
  senses.
- **Bluetooth.** `src/ble.h` is a Nordic UART Service on NimBLE-Arduino 2.x,
  with TX at +21dBm and a supervision timeout of 6s.
  `docs/bluetooth-link.md` has the measurements behind those numbers. A
  browser can speak NUS directly with
  `navigator.bluetooth.requestDevice({filters:[{services:[NUS]}]})`. The
  owner-token scheme in `ble.h` exists for a desk with several computers and
  probably does not belong here.
- **The UI widgets.** `src/widgets.h`, `src/theme.h` and `src/layout.h`
  (arc gauges, marquee, banner) follow the "built to be forked" rule in
  that repo's CLAUDE.md. Generic drawing goes in widgets, and things specific
  to this creature go in main.
- **The pet art.** `src/pet.h` has eighteen 16x16 species rolled from the
  MAC address. `pets/puck/` is a pet pack.
- **The alife design.** `host/alife/` is a stdlib-Python, Creatures-style
  engine: twelve chemicals with half-lives (`biochem.py`), drives, a brain
  that picks actions by softmax over instinct plus learned reward minus
  habituation (`brain.py`), temperament genes that drift during sleep
  (`genome.py`), and a learned lexicon (`lexicon.py`). Its README is a good
  spec. Its inputs are Claude Code events, so the stimulus table needs
  rewriting for a body with only an IMU, a clock and a phone. It runs on a
  host in Python, so on this device it has to be rewritten in C++ (an
  ESP32-S3 at 240MHz has plenty of room for it).
- **The simulator and golden-frame tests.** `sim/` runs the firmware in an
  SDL window on Mac or Linux, and `tests/film.py` checks frames against
  goldens. It is worth copying. It does not run natively on the Windows box
  (see below).
- **The versioning and flashing rules** in that repo's CLAUDE.md
  (`VERSION` bumped by hand, merged Intel HEX rather than a flat .bin so NVS
  survives a flash). The pet is a keepsake, and NVS or flash is where it
  lives.

## Hardware facts already established

- **Waveshare ESP32-S3-LCD-1.28** (SKU 26541, the **non-touch** one): 240x240
  GC9A01 over SPI, QMI8658 IMU on I2C (SDA 6, SCL 7), BOOT button on GPIO0,
  backlight on GPIO40, battery ADC on GPIO1, and a CH343 USB-serial bridge
  (USB id `1A86:55D3`). There is no touchscreen, so "tap" means an IMU knock.
- **Waveshare ESP32-S3-Touch-LCD-1.46**: 412x412 SPD2010 over QSPI, 8MB
  octal PSRAM, native USB (`303A:1001`), and it has touch. Its PWR button is
  also its power switch (see `board_lcd146.h`).
- Which board blorbarium uses is not decided.

## Things to check before they bite

- **iPhones have no Web Bluetooth.** Safari does not support it, and every
  iOS browser is Safari underneath. Chrome on Android does. If the friend
  has an iPhone, the website needs a fallback: the Bluefy browser, or a
  design where the NFC tap still does something useful without Bluetooth.
  Find out what phone the friend has first.
- **Phone USB power is not guaranteed.** Android phones supply 5V over OTG
  when the cable says "I am a device" through its CC resistors, and some
  only do it while a USB device enumerates. The CH343 on the 1.28 board does
  enumerate. Whether a given phone keeps powering the board, and whether it
  pops up a "USB device connected" prompt every time, has not been tried.
  This is a guess until it is measured on the friend's phone.
- **The case and the antenna.** An NFC sticker against a metal or PCB ground
  plane detunes. Put it on the inside of the lid, away from the board, or
  use an on-metal tag. The BLE antenna also wants plastic, not the sticker,
  in front of it.
- **NFC on the two phone platforms.** An NDEF URL record opens the browser
  on both iPhone (XS and newer, in the background) and Android, with no app.
  Write the sticker once, with the device name in the URL.

## Toolchain on this Windows machine (2026-10-02)

- PlatformIO lives in `~/.platformio/penv`, and Node v22 and Python 3.14 are
  on PATH. There is no native g++ or clang. WSL is installed, so the SDL
  simulator would have to run under WSL, or a sim harness would have to
  render frames to PNG in some other way.
- No board was plugged in when this was written.

## Working rules carried over

- Agents push every commit, on any branch.
- The pet is a keepsake. Saves are atomic, and nothing flashes over its
  state by accident.
- Anything a forker would want (drawing on a round screen, the BLE wire)
  goes in a small reusable header, separate from this particular creature.
