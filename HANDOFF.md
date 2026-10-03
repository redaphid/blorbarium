# HANDOFF

blorbarium is a little artificial-life creature in a 3D-printed case, for a
friend who does not program. It is a sibling of
[redaphid/claude-notification-screen](https://github.com/redaphid/claude-notification-screen)
(local clone at `D:\Projects\claude-notification-screen`, the `phone` worktree
at `C:\Users\hypnodroid\Worktrees\claude-notification-screen-phone`). That
project is a Claude Code status badge with a pet on it. This one keeps the pet
and drops Claude.

This file is where things stand. The design history is in `docs/design/`
(DESIGN.md, DEVIATIONS.md) and in `docs/notes/` (being added by another
session).

## Where things stand (2026-10-03, end of the bring-up session)

### On the device

- **Board:** Waveshare ESP32-S3-LCD-1.28, CH343 `1A86:55D3`, usually COM10.
  ESP32-S3 QFN56 rev v0.2, 16 MB flash, 2 MB PSRAM.
- **Build:** `flash3` 2f8fd00, flashed at about 16:20. It boots
  `boot=Resumed occupant=creature gen=0 genome=d30f00d1` and has passed 10
  power cuts (11 of 11 boots good, 7 cuts during a save). Free heap with BLE
  advertising is 136,244 B, and the loop stack high-water mark is 12,256 B
  free of 16 KB.
- **Pet:** an adult grungo, generation 0, genome hash `d30f00d1`. The user
  chose him. Keep him. He lives in the `pet` NVS partition (`snap.a`,
  `snap.b`) and on `petfs` (`lineage.log`). A cable flash of a merged
  `.hex` never touches them.
- **Bluetooth:** advertises as `blorb-237c` (Nordic UART). The protocol is in
  `docs/ble.md`.
- **Website:** https://blorb.hypnodroid.com (Android Chrome, Web Bluetooth),
  from branch `web`. A Cloudflare Access bypass application keeps the site
  public. This file does not record its name and settings, so check the
  Cloudflare Zero Trust dashboard before changing Access.
- **Serial (115200):** a `[hw] up=...` status line every 10 s (heap, stack,
  IMU, rotation, button). `#n VERB` protocol lines work as they do over BLE.
  `DEBUG stage <hatchling|child|adult|elder>` is serial-only. A tool that
  toggles DTR/RTS on open resets the board.
- **The cyber-puck badge daemon** (`badged.py`, which drives this same USB id)
  was stopped for the bring-up. A detached keeper process holds it down
  until the next reboot, and it comes back by itself after a restart.

### Backup, fallbacks and restore

All are in `D:\projects\bak\blorbarium\`:

- `flash-backup-lcd128-2026-10-03.bin`: the whole 16 MB flash as it came
  (cyber-puck badge firmware 1.4.0, pet "dragon"). It was read twice, and
  both reads gave sha256
  `b06cdfa43cafa6142a7980bd1bf3ce0469bb86cdde9e17431a6b4b1c8bd63dec`.
- `gift-ble-e9a1689.hex`: the last build proven on the board with BLE.
- `gift-2431fea.hex` (no BLE) and `fallback-d1336ad.hex` (first bootable):
  older known-good images.
- `flash3-2f8fd00.hex`: what is on the device now.

To reflash a known-good build, which keeps the pet because the hex has gaps:

    ~/.platformio/penv/Scripts/python.exe ~/.platformio/packages/tool-esptoolpy/esptool.py --chip esp32s3 --port COM10 -b 921600 write_flash 0x0 D:/projects/bak/blorbarium/gift-ble-e9a1689.hex

To restore the original badge firmware, which **erases blorbarium and the
pet**:

    ~/.platformio/penv/Scripts/python.exe ~/.platformio/packages/tool-esptoolpy/esptool.py --chip esp32s3 --port COM10 -b 921600 write_flash 0x0 D:/projects/bak/blorbarium/flash-backup-lcd128-2026-10-03.bin

### Branches (all pushed to origin)

| Branch | Tip | State |
|---|---|---|
| `main` | a877d6f | The original handoff only. |
| `engine` | a1cb726 | The creature engine, units 0 to 19, verified. The base of everything below. |
| `hw` | e6dd6d4 | Unit 20 on the 1.28: partitions, storage, panel, IMU (the `word()` macro fix), rotation, the marquee widget, visual-center's framing and scaling, re-blessed goldens. Green. Flashed as 2431fea. |
| `gift-ble` | e9a1689 | `hw` plus BLE (NusLink, TwoLinks, docs/ble.md). Green. Flashed and power-cut tested. |
| `hw-next` | eda3a94 | `hw` plus the brisk marble physics (fb8af31), the glass marble art, thoughts and the shake 8-ball, foresee minTicks 15, and goldens. Green. |
| `third` | 4034e1d | `hw-next` plus "asleep always shows shut eyes", and the measured budgets in DESIGN 8 and DEVIATIONS 11. Green: native 290 of 291 (1 skipped), sim 13 of 13. |
| `flash3` | 2f8fd00 | `third` plus BLE with notify backpressure (hw-blefix), `TWIST say` (hw-say), and every shake foretells (hw-oracle). Green: native 302 of 303 (1 skipped), sim 13 of 13. **On the device.** This HANDOFF lives here. |
| `hw-blefix`, `hw-say`, `hw-oracle` | 40cc28b, c589280, 5483391 | The slices merged into `flash3`. |
| `hw-marble4` (also `hw-marble2`, `hw-marble3`) | 5b169ec | On `third`: rotation-correct tilt (TiltDetector re-expressed on a turn), kFlatG 0.6, faster tilt, a 900 ms crossing, render-time interpolation, and the replay re-bless. Native green. **Not merged:** sim_film 6 of 13. Rolling friction 1/100 alone shifts 7 frames slightly (breath phase or position; he still sleeps). Either re-bless them after looking, or keep 1/125 and re-measure the crossing. |
| `hw-unit20`, `hw-ble`, `hw-rotate`, `hw-marquee`, `hw-marble` | | Delegate branches, already merged into the rows above. |
| `visual-center` | ce97248 | Centring, the teal foresee face and scaling were taken into `hw`. Its later commits (the elder ageing look) are not taken. Its own goldens were not re-blessed. |
| `depth` | 1f64dd1 | Learning-depth measurements (learnsim, e2e). Only the Foresee minTicks idea was taken (as 15, via 74ead86's actions.def). |
| `depth-look`, `depth-u2`, `depth-u4` | | Newer depth work (mutate's forced-look floor, heirlooms, interrupting stimuli). Not reviewed here. |
| `thoughts` | 47d4308 | Thoughts and the 8-ball. 28224e3 and d88ee93 were taken. 0b4dd21 (heritable oracle gene) is red. |
| `dialogue` | 850fff5 | Regenerates `defs/thought_lines.def`. Not taken. |
| `web` | 8772309 | The website. Note that it commits `web/.wrangler/` caches, which belong in `.gitignore`. |
| `docs`, `e2e`, `video`, `art-flow` | | Docs, learning e2e harness, reel tooling, a ComfyUI art workflow. |
| `engine-*` | | The engine's per-unit history. Superseded by `engine`. |

The sprite expressions live in the separate `blorbarium-art` repo.

**Merge order for the next build:** `third`, then `hw-blefix`, then
`hw-say`, then `hw-oracle` (that is `flash3`), then a fixed marble branch,
then `dialogue`'s `thought_lines.def` alone. Re-bless goldens only on the
merged tip, and look at every re-blessed PNG.

### Bugs: fixed and open

- Fixed: IMU read zeros (`word()` macro, `hw`); asleep showing open eyes
  (`third`); the stale tilt after a quarter turn (`hw-marble2`, not merged);
  the marquee frozen on an egg (`hw`); thoughts scrolling twice (`hw-next`).
- Fixed in `flash3`, needs the phone to confirm: GENOME over BLE was garbled
  (1168/1528 bytes) because notifications were dropped. NotifyQueue now
  retries. `DEBUG bletest <bytes>` on serial, with a central connected,
  prints `[hw] ble tx chunks= retries=`.
- Open: **the marble.** The device's marble is the old slow one, and its
  tilt goes stale for about a second after a quarter turn. The fix is on
  `hw-marble4`, which is native-green. Its rolling friction of 1/100 shifts 7
  sim frames slightly. He is not woken (`marble_hit` cannot wake a sleeper).
  Decide between re-blessing those frames after looking at each, and
  keeping 1/125 and re-measuring the crossing. Then merge after `flash3`.
- Open: **Foresee test regression.** Resolved on `third`, whose test counts
  the pose only while he is still foreseeing. Recheck it on any marble
  merge.
- Open: **mutate's forced-look guarantee** (`depth-look`), not reviewed.
- Open: **the oracle-gene re-bless** (`thoughts` 0b4dd21, red on
  `Starter.CarriesEveryGeneKindAndExpressesEachAtSomeStage`). The every-shake
  rule in `flash3` doesn't need the gene.
- Open: the marble item near the rim is half-masked in `remains` (items are
  placed with the figure's offset). Thoughts move in 10 Hz steps.

### Next steps

1. Confirm `flash3` from the phone: GENOME reads whole, and `TWIST say`
   shows on the marquee.
2. Root-cause the marble's sleep-frame change, then merge the marble.
3. Ask the friend's phone: Android or iPhone (Web Bluetooth), and whether
   it keeps the board powered.
4. BLE OTA (DESIGN unit 22), now that the partition table is OTA-ready.

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
