# Device layer and toolchain for blorbarium

Source repo: `D:\Projects\claude-notification-screen` at `b1b5e3e`. All `file:line`
citations are into that repo unless marked otherwise. No repo file was edited. The
build proofs ran on throwaway copies, one in the scratchpad and one in WSL `/tmp/cns-sim`.

## TL;DR

- **Take as-is:** `display.h`, `board_lcd128.h`, `board_lcd146.h` (and its
  `Panel_SPD2010.*` and `spd2010_init_cmds.h`), the `merge_image.py`, `pick_port.py` and
  `fw_stamp.py`/`fw_id.py` scripts, and the `sim/` board and Arduino shims.
- **Take trimmed:** `orient.h`, which needs rework to expose senses instead of
  printing words. Also `ble.h`, without the owner-token scheme; `widgets.h`, without
  the ask and verdict cards; `pet.h`, as data only; `sim.cpp`; and `film.py`.
- **Don't take:** `theme.h` roles, beyond the `Rgb` struct; `layout.h`, beyond
  `CX/CY` and the 3px floor; `mirror.h`; `ring.h`.
- **orient.h has no gesture enum.** It exposes four functions: a quarter-turn
  rotation, a shake bool, a 0/1/2 tap code and a held bool. Its return value is
  discarded, and the tilt vector stays in static locals. The gesture words (KNOCK, DTAP,
  SHAKE, HELD, TAP, HOLD) are strings printed by `main.cpp`.
- **Persistence today is one NVS string** (`Preferences` namespace `"badge"`, key
  `"owner"`). The pet is derived from the MAC address and is never saved.
- **Toolchain:** firmware builds on Windows; I proved it today in 65s. Visual Studio
  Build Tools 2022 (MSVC 14.44, CMake 3.31 and Ninja 1.12 bundled) **is installed**,
  which contradicts HANDOFF's "no native compiler". It compiles and runs C++17. WSL
  `survivor` (Arch) has g++ 16, the PlatformIO `native` platform, SDL2, gtest and
  Pillow. The old SDL simulator builds there in 15s and renders headless frames that are
  byte-identical across runs.
- **Recommendation:** run engine unit tests and the frame-to-PNG render in WSL
  `survivor` with `pio test -e native` / the sim. Keep the engine free of
  Arduino and LovyanGFX headers so that MSVC can also compile it as a cross-check.

---

## A. Hardware seams

### `src/display.h` (42 lines). **Copy as-is.**

- Public surface: `CANVAS_W/CANVAS_H = 240` (`display.h:21-22`), with `SCREEN_W/H`
  aliases (`:26-27`). It selects the board by define: `BADGE_BOARD_SIM` →
  `board_sim.h`, `BADGE_BOARD_LCD146` → `board_lcd146.h`, else `board_lcd128.h`
  (`:29-35`). It owns `static RoundBadgeDisplay display;` and
  `static LGFX_Sprite canvas(&display);` (`:41-42`).
- Dependencies: LovyanGFX (`LGFX_USE_V1`, `:17-18`).
- Claude coupling: none. Only the `BADGE_` define prefix and the comments mention
  the badge.
- Contract every board header must meet: `RoundBadgeDisplay`, `boardPanelBegin()`,
  `boardPowerHold/Release/Off`, `boardButtonBegin/Down`, and `boardPresent(display,
  canvas, rot)`, plus the constants `BOARD_HAS_BUTTON`, `BOARD_POWER_OFF_MS`,
  `BOARD_SPRITE_PSRAM`, `ORIENT_R0`, `PIN_IMU_SDA/SCL` and `PIN_BATT_ADC`.

### `src/board_lcd128.h` (110 lines). **Copy as-is.**

- Pins: SCLK 10, MOSI 11, DC 8, CS 9, RST 12, BL 40, BATT ADC 1, BOOT 0
  (`board_lcd128.h:9-16`). IMU I2C SDA 6, SCL 7 (`:32-33`). `ORIENT_R0 = 1` (`:29`).
- GC9A01 on `SPI2_HOST`, 40MHz write (80MHz is untested, `:46-48`), `invert = true`
  (`:73`), PWM backlight at 12kHz on channel 7 (`:80-85`).
- `BOARD_SPRITE_PSRAM = false`: the 240x240x2 = 115KB sprite fits in internal RAM
  (`:25`). `BOARD_POWER_OFF_MS = 0` means there is no power switch (`:21`). The power
  functions are no-ops (`:97-99`).
- `boardPresent` does `setRotation(rot)` and `pushSprite(0,0)` (`:107-110`).
- Dependencies: LovyanGFX and Arduino `pinMode`/`digitalRead`. No Claude coupling.

### `src/board_lcd146.h` (223 lines). **Copy as-is if the 1.46 is chosen.** It also needs `Panel_SPD2010.cpp/.hpp` and `spd2010_init_cmds.h`.

- QSPI: SCK 40, D0-D3 46/45/42/41, CS 21, TE 18, BL 5 (`board_lcd146.h:25-32`).
  The panel reset goes through a TCA9554 expander at `0x20`, pin 2 (`:74-77`,
  `:152-168`), on the IMU bus (SDA 11, SCL 10, `:71-72`).
- The PWR button is GPIO6 and the power latch is GPIO7 (`:45-46`). Holding it for 4s
  powers off (`:51`); with a cable in, that means deep sleep (`:205-214`).
  `boardButtonDown` ignores the boot press until the button has been seen released
  (`:187-192`).
- `BOARD_SPRITE_PSRAM = true`, and the env needs `memory_type = qio_opi`
  (`platformio.ini:63-65`). `ORIENT_R0 = 0` (`:68`). `boardPresent` scales and rotates
  the 240 canvas by 412/240 = 1.72x with `pushRotateZoom` (`:220-223`), so the
  creature always draws at 240x240.
- **There is no touch driver.** No file in `src/` reads touch, although HANDOFF says
  the board has touch. If blorbarium wants touch on the 1.46, it has to write the
  SPD2010 touch read itself.
- Battery ADC is GPIO8 (`:56`). Nothing reads it yet.

### `src/orient.h` (265 lines). **Copy trimmed; it needs rework.** This is the creature's senses.

**Dependencies:** `Arduino.h`, `Wire.h`, `display.h` (for the pins and
`boardPowerRelease`, `orient.h:13`, `:209`). It also has a **hidden dependency on `Host`**
(the `HostLink` in `ble.h:284-301`), which is used at `:111`, `:117`, `:195`, `:219`,
`:252` and `:263`. That works only because `main.cpp:145-146` includes `ble.h` before
`orient.h`. Claude coupling: none in the logic. The comments talk about alerts.

**IMU setup** (`orientInit`, `:200-220`):
- QMI8658 at `0x6B` or `0x6A`, whichever answers with WHO_AM_I `0x05` (`:17`, `:29`,
  `:202-206`). With no IMU it halts forever (`:207-211`).
- CTRL1 `0x40` turns on auto-increment. CTRL2 `0x05` sets **±2g at 250Hz** (`:213`).
  CTRL7 `0x01` turns on the **accelerometer only; the gyro is off** (`:217`). CTRL8
  `0x81` enables the tap engine, with the CTRL9 handshake through STATUSINT bit 7.
- I2C runs at 400kHz (`:201`). Interrupt pins are not used; everything is polled (`:147-148`).

**Raw read:** `imuAccel(float*ax, float*ay, float*az)` returns g, as raw/16384
(`:91-102`). Every caller does its own 6-byte I2C read, so the IMU is read up to
three times on some frames.

**What it emits:**

| Function | Returns | Rate it samples at | Thresholds |
|---|---|---|---|
| `orientRotation(now)` (`:225-265`) | `int` 0..3, a quarter turn for the panel | every 100ms, `ORIENT_SAMPLE_MS` (`:68`) | Ignores samples where \|\|a\|−1g\| > 0.25g (`:66`, `:237`). EMA α = 0.25 on x and y (`:67`, `:239-241`). Holds the last value when the in-plane magnitude is below 0.35g, i.e. lying flat (`:63`, `:242`). `atan2` snaps to a quarter turn, plus `ORIENT_R0` (`:244-245`). Must hold 600ms before switching (`:69`, `:261`). |
| `orientHeld(ax, ay, now)` (`:187-198`) | `bool` held | called from `orientRotation`, so 10Hz | In-plane magnitude > 0.30g for 400ms means held; below that for 1500ms means put down (`:47-49`). **`orientRotation` discards the return value (`:236`).** The only effect is the `HELD 1/0` line it prints. |
| `orientShaken(now)` (`:161-183`) | `bool`, true once per shake | every 20ms, `SHAKE_SAMPLE_MS` (`:55`) | A jolt is \|\|a\|−1g\| ≥ 0.55g (`:54`). Jolts must be ≥ 60ms apart. Four jolts within 900ms fire. A second shake needs 600ms of stillness first (`:56-59`). |
| `tapPoll()` (`:149-152`) | `int` 0 none, 1 single, 2 double | the caller polls; `main.cpp:1726` does it every 50ms | QMI on-chip tap engine (`tapConfigure`, `:127-144`): peak window 40ms, quiet 100ms, double tap within 500ms, peak 0.5g², UDM 0.4g², Z axis first (`:37-43`). `tapReady` reports whether it configured. |

**Tilt vectors are not exposed.** The smoothed `sx, sy` are function-local statics
(`:239`). Raw `az` and the magnitude are computed and then thrown away. A creature
that wants "leaning left by 20°", "upside down", "free fall" or "being rocked" needs a
new API.

**The gesture vocabulary is in `main.cpp`, not here.** Lines are printed through
`Host` (documented at `main.cpp:123-128`):
- `DTAP` when `tapPoll()==2` (`main.cpp:1729-1731`).
- `KNOCK` when `tapPoll()==1`, rate-limited to one per 400ms (`KNOCK_MS`,
  `main.cpp:200`, `:1736-1744`). The face reacts on every knock.
- `SHAKE` (`main.cpp:1749-1750`).
- `HELD 1|0` (`orient.h:195`).
- `TAP` and `HOLD` come from the **BOOT button**, not the IMU. `TAP` is a
  release after more than 30ms; `HOLD` is 1200ms (`main.cpp:622`, `:637-661`).
- A rotation change within `PET_TURNS_WINDOW_MS` makes the pet "annoyed"
  (`main.cpp:1756-1765`).
- `TILT <deg> r<rot>` is printed on each rotation change (`orient.h:252`, `:263`).

**Suggested trim for blorbarium:** keep the register constants, `imuProbe/Write/
Accel`, `imuCommand`, `tapConfigure`/`tapPoll`, the shake counter and the held
hysteresis. Replace them with one `sensesPoll(now) -> Senses{ax,ay,az, tiltX,
tiltY, mag, rot, held, shaken, taps}` that reads the IMU once per tick. Replace `Host.printf`
with a log hook. Consider turning the gyro on (CTRL7 bit 1) if "spun" or "rocked"
is a stimulus. The sim's `Wire.h` fake (`sim/Wire.h`) answers only the accelerometer
and tap registers, so it would need extending.

### `src/ble.h` (347 lines) plus `src/ble_ota.h` (222 lines). **Copy trimmed.**

- Public surface: `hostFeed(char, bool overAir)` must be defined by the app
  (`ble.h:38`). The header provides `bleInit(mac)`, `blePoll()`, `bleWrite(s,n)`,
  `bleAdvName()`, `bleMayDrive()`, `bleOwnerLine()` and `bleAnnounce()`, plus `HostLink Host` with
  `print/println/printf`, which writes to Serial and BLE (`:284-301`). With
  `BADGE_BLE` undefined, everything compiles to stubs (`:266-278`).
- Dependencies: NimBLE-Arduino `^2.3.0` (`platformio.ini:182`), `Preferences`.
  `ble_ota.h` uses `esp_ota_ops`, `mbedtls/sha256` and a 32KB ring. `enable_ble.py`
  puts NimBLE on `lib_ignore` unless `BADGE_BLE=1` (`enable_ble.py:9-14`).
- **NUS framing:**
  - Service `6E400001-…`. RX `6E400002-…` takes write and write-without-response from the host.
    TX `6E400003-…` is notify from the device (`ble.h:45-47`, `:168-171`).
  - The wire carries **newline-terminated ASCII lines**. Either `\n` or `\r` ends a line;
    an empty line is ignored. Each wire (USB and air) has its own 256-byte line
    buffer. Characters past 255 are **silently dropped**, and the line is still
    dispatched truncated (`main.cpp:588-606`). There is no checksum, sequence number or
    acknowledgement.
  - Inbound bytes cross from the NimBLE task to the loop through a 2048-byte
    single-producer/single-consumer ring. On overflow, the rest of that write is dropped (`ble.h:53-55`,
    `:76-86`, `:245-248`).
  - Outbound lines are split into notifications of at most MTU−3 bytes; the host
    reassembles them by reading to the newline (`:251-262`). The device asks for MTU 247 (`:158`).
    `Host.printf` truncates at 256 bytes (`:293`).
  - Command grammar: one leading letter, a space, then arguments (`main.cpp:7-128`).
    Device-to-host words: `FW`, `BLE`, `PET`, `IMU`, `HELLO`, `TAP`, `HOLD`, `DTAP`,
    `KNOCK`, `SHAKE`, `HELD`, `OK/LOST`, `OWNER …`, `BLE conn <ms> <ms>`, `BLE drop
    0x..`. `?` asks for a re-announce.
- Radio parameters worth keeping: TX power +21dBm (`:164`); supervision timeout 6s
  (`CONN_TIMEOUT_10MS = 600`, `:95`); connection interval 30-50ms, requested 2s after
  auth and only if the current parameters don't already fit (`:99-100`, `:231-236`);
  advertising every 100-150ms (`:185-186`); the name in the scan response, because it
  doesn't fit beside a 128-bit UUID (`:175-180`); and re-advertising every 1s when
  nothing is connected (`:195-204`).
  The name is `badge-%02x%02x` from MAC bytes 4 and 5 (`:151`). Change it to `blorb-xxxx`.
- **Claude and desk coupling: the owner-token scheme.** It covers `:16-31`, `:62-74`,
  `:106`, `:211`, `:213`, `:221-223`, the auth gate at `:231`, `:313-346`, and
  `hostFeed`'s gate at `main.cpp:596-597`. Remove it, as HANDOFF suggests. **Caveat:** `ble_ota.h`
  allows OTA only on a claimed badge with an authenticated connection
  (`ble_ota.h:25-27`). If blorbarium keeps OTA over Web Bluetooth (useful for a friend
  who doesn't program), it needs some other gate. Without one, any phone in range can
  reflash the device.
- OTA protocol (`ble_ota.h:5-17`): its own service `6E400010`, CTRL `…11` (text
  `B <size> <sha256> <board>`, `E`, `A`), DATA `…12` (write without response, windowed by
  `OTA at <n>` every 4KB). The board-name check is on `OTA_BOARD` (`:39-43`). Rollback is
  armed by overriding `verifyRollbackLater()` to return true (`:52-54`), and the new image is marked
  valid after 15s (`:48`, `:190`).

### `src/widgets.h` (396 lines). **Copy trimmed.**

- Dependencies: `display.h` (the global `canvas`), `layout.h` (only `CX`, `CY` and
  `RING_IN`, via `ringHalfWidth`, `widgets.h:37-40`), `theme.h` (`Rgb`). There is an
  **implicit Arduino `millis()`** in `drawMarquee` (`:165`) and Arduino `max` (`:203`, `:208`).
- Generic, keep: the colour helpers `rgb`, `shade`, `scale`, `lerp` and `mix` (`:20-33`); the `ArcGauge` with
  `drawArcTrack`, `drawArcFill`, `arcInside` and `drawArcMark` (`:51-87`); the `Marquee`/`MarqueePhase` with
  `drawMarquee`, `drawMarqueeTimed`, `marqueeRest` and `marqueeTimedDuration` (`:98-217`); the `Banner`
  (`:229-242`); the `RimCountdown`/`rimArc`/`drawRimFlash` (`:258-282`); and `thickLine`
  (`:286-295`). Make `ringHalfWidth` take a radius instead of `RING_IN`.
- Specific to the Claude ask flow, drop or keep as an idea: `GestureGlyph` (`:300-314`, a
  knock/knock-knock/shake glyph; mildly reusable for teaching the owner), `AskCard`
  (`:325-361`) and `Verdict` with `drawVerdictCard` (`:366-396`).

### `src/theme.h` (134 lines). **Take only `struct Rgb` (`theme.h:26-28`) and the pattern.**

The `Theme` roles (`dust, flow, you, hurt, toolRead…`, `:30-48`) and the three palettes
(`:60-89`) are session and tool states. The compile-time `BADGE_THEME` define
(`:97-101`) conflicts with recolouring from the website. blorbarium needs runtime
colours that live in its genome.

### `src/layout.h` (111 lines). **Don't take**, apart from `CX/CY` (`layout.h:18-19`) and the design rules.

Everything else positions the Claude HUD: the session ring, CI bands, burn and context arcs, the week, and tool
ticks (`:23-111`). Two rules are worth keeping: "nothing meaningful is thinner than 3px",
because of the 1.72x upscale on the 1.46 (`:11-12`), and "spend angle, not radius" (`:14-16`).
`PET_SCALE = 6`, which makes the 16x16 art 96px (`:49`).

### `src/pet.h` (456 lines). **Copy the art and data only.**

- Dependencies: `Arduino.h` and `string.h`.
- Data: 18 species of `PetSkin {name, body rgb, accent rgb, 16 rows}` (`pet.h:16-21`,
  `:23-330`). Each art cell is one of `#` body, `+` accent, `w` eye-white, `o` dark, `.` clear (`:6`).
- `PetSprite`/`PetAnim`: indexed-palette bitmaps with named, multi-frame expressions
  (`:351-363`), supplied by a pet pack (`pets/puck/`, `pet_pack.py`).
- Roll: FNV-1a over the MAC plus the salt `"friend-2026-401"`, then Mulberry32 (`:334-346`).
  This picks species, rarity 0-4 at 60/25/10/4/1% and shiny at 1% (`:438-456`). It is
  stateless and lasts the board's lifetime. This is a reasonable seed for a genome, but
  blorbarium's creature must persist and change, so the roll can only be the birth
  seed.
- Claude coupling: the "Claude Buddy" framing and salt (`:1-4`, `:337`), and the pack macros
  `PET_WORDMARK`, `PET_SPLASH_*` and `PET_DRIVE_LABELS` (`:387-424`).
- **The rendering is not in `pet.h`.** It lives in `main.cpp:705-948`
  (`petBody`, `drawSpriteFrame`, `petBase`, `petFrame`, `drawThought` and `drawPetAt`). That code is
  coupled to the global `pet`, to `petExpr` and `petReflex` set by host lines, and to the
  Claude `Mood` enum `SLEEP/WORK/ALERT/SAD/CHEER/CONTENT` (`main.cpp:668`). Worth
  lifting: the run-length sprite blit with scale, step, flip, solid and shear jitter
  (`main.cpp:734-758`); the skin blit (`:902-914`); the bob and blink (`:848-851`); and the
  expression-change "glitch" (`:871-900`). Rewrite them as functions that take the
  creature's state as arguments.

### `platformio.ini` and `extra_scripts`

`platformio.ini:1-95`. Envs: `badge` (1.28, `qio_qspi`, USB `1A86:55D3`, `CDC_ON_BOOT=0`,
`:49-56`), `leader` (1.46, `qio_opi`, `303A:1001`, native USB CDC, `LGFX_USE_QSPI`,
`:60-74`), and `sim` (`platform = native`, `-std=c++17 -Werror=narrowing
-DBADGE_BOARD_SIM`, compiles only `sim/sim.cpp`, `:80-95`). LovyanGFX is pinned at
**exactly 1.2.29** because the goldens depend on its pixels (`:11-15`). Flash is `qio` with
`flash_size = 16MB` (`:45-46`).

| Script | What it does | Take? |
|---|---|---|
| `enable_mirror.py` (10 lines) | `BADGE_MIRROR=1` adds the `BADGE_MIRROR` define (ESP-NOW mirror) | No |
| `enable_ble.py` (14 lines) | `BADGE_BLE=1` adds the define; otherwise it adds NimBLE to `lib_ignore` | Only if BLE stays optional. Blorbarium always wants BLE, so put NimBLE in `lib_deps` unconditionally |
| `pick_port.py` (35 lines) | Picks the upload port by `custom_usb_id` VID:PID using pyserial `list_ports`, and refuses an ambiguous match. It runs only for upload, monitor, uploadfs or erase | **Yes, as-is.** It works on Windows COM ports |
| `pet_pack.py` (24 lines) | `PET_PACK_DIR` adds a CPPPATH and the `PET_PACK` and `PET_PACK_ALWAYS` defines | Probably not |
| `fw_stamp.py` (16 lines) plus `fw_id.py` (72 lines) | Defines `FW_ID` (the semver from `VERSION` plus build metadata) and `FW_VERSION` | Yes, trimmed of the mirror, BLE and pack metadata |
| `merge_image.py` (117 lines) | Post-build `merged.hex` (Intel HEX) at the platform's offsets, **leaving the NVS gap unwritten so a cable flash keeps the saved state** (`merge_image.py:13-15`). It falls back to its own HEX writer when esptool can't write HEX (`:65-97`) | **Yes, as-is.** This protects the keepsake |

**Partition table actually used** (decoded from `.pio/build/badge/partitions.bin`
after today's build). The board JSON selects `default_8MB.csv` even though
`flash_size = 16MB`, so **the top 8MB of flash is unused**:

```
nvs        type=1 sub=0x02 off=0x009000 size=0x005000 (20 KiB)
otadata    type=1 sub=0x00 off=0x00e000 size=0x002000 (8 KiB)
app0       type=0 sub=0x10 off=0x010000 size=0x330000 (3264 KiB)
app1       type=0 sub=0x11 off=0x340000 size=0x330000 (3264 KiB)
spiffs     type=1 sub=0x82 off=0x670000 size=0x180000 (1536 KiB)
coredump   type=1 sub=0x03 off=0x7f0000 size=0x010000 (64 KiB)
"partitions": "default_8MB.csv"
```

### NVS and flash persistence patterns in the firmware

- **The only persistent write in the whole firmware** is
  `Preferences prefs; prefs.begin("badge", readOnly)` with `getString/putString("owner")`
  (`ble.h:152-156` for the read at boot, `ble.h:336-340` for the write on `o <token>`). Command run:
  `grep -n "Preferences\|prefs\.\|nvs_\|LittleFS\|SPIFFS\|esp_partition\|EEPROM\|Update\.\|esp_ota" src/*.h src/*.cpp`.
  The other hits are all OTA (`ble_ota.h`).
- No LittleFS or SPIFFS use, although a 1.5MB `spiffs` partition exists.
- The pet is not stored; it is derived from the MAC (`pet.h:1-4`).
- Flash safety today: `merge_image.py` writes HEX with gaps so that flashing doesn't erase
  NVS (`merge_image.py:43-45`), and OTA is A/B with bootloader rollback (`ble_ota.h:19-24`).
- Blorbarium has no prior art for an **atomic creature save**. Options: NVS
  `putBytes` of a versioned, CRC'd struct into two alternating keys with a generation
  counter (NVS is wear-levelled and power-loss safe per entry, but 20KB is small), or
  LittleFS on the `spiffs` partition with write-temp-then-rename. Either way, use a custom
  partition CSV that takes the 16MB.

### `sim/` (876 lines). **Copy the shims; trim `sim.cpp`.**

- How it works: `sim.cpp` `#include "../src/main.cpp"`, so the simulator runs the same
  code as the device (`sim/sim.cpp:18`, `sim/README.md:21-24`). `board_sim.h` is a
  `Panel_sdl` with the 1.28's pins and blit (`sim/board_sim.h`). `Arduino.h` fakes
  Serial, millis, delay, pinMode and digitalRead, with a **fixed clock** mode where
  millis() advances 25ms per frame and starts at 1000 (`sim/Arduino.h:54-61`). `Wire.h` fakes a
  QMI8658 driven by keys or `!tilt/!knock/!dtap/!shake` lines: gravity from the arrows,
  2.2g while shaking, latched taps (`sim/Wire.h`). `esp_mac.h` fakes the MAC.
- CLI: `--headless` (SDL dummy driver), `--clock fixed`, `--script feed.txt` (`@<ms>
  <line>`), `--shot out.bmp --after <ms>`, `--record dir/ --fps --for`, `--pty`
  (`sim/sim.cpp:11-14`, `README.md:5-11`). Shots read back from the **panel**, so the
  rotation is applied (`sim.cpp:81-83`). The output is a 24-bit BMP (`sim.cpp:79-106`).
- **POSIX only:** `sim/Arduino.h:19` includes `<unistd.h>` and uses `::read`/`::write`.
  `sim.cpp:20-24` uses `fcntl`, `termios` and `unistd`. `tests/simbadge.py:46` looks for
  `~/.platformio/penv/bin/pio`, the Linux layout. LovyanGFX itself errors out on
  Windows without SDL: `platforms/common.hpp` falls through to `#error unknown
  platform...`, and SDL2 is not installed on Windows.
- Claude coupling: the `!button` keys and the `V/S/X` feed examples. The mechanism itself
  is generic.

### `tests/film.py` (125 lines). **Copy trimmed.**

It plays a feed on the fixed clock, records BMP frames at 20fps (`film.py:44-45`), checks
chosen frames against `tests/golden/<name>.png` within a per-channel tolerance of 24 and a
0.2% share of the disc (`:37-39`, `:89-96`), writes `.actual.png` and `.diff.png` on a miss
(`:99-125`), and encodes an mp4 with ffmpeg (`:80-85`). `UPDATE_GOLDEN=1` rewrites
the goldens. Dependencies: Pillow, ffmpeg, and `simbadge.build()`, which runs `pio run -e sim`
into `.pio/test-build`. **ffmpeg is not in WSL** (see below), so the mp4 step would fail
there. Make it optional or call the Windows `ffmpeg.exe` through interop.

---

## B. Toolchain on this Windows machine

### PlatformIO on Windows: present, and it builds the firmware

```
$ ~/.platformio/penv/Scripts/pio.exe --version
PlatformIO Core, version 6.2.0
$ ~/.platformio/penv/Scripts/python.exe --version
Python 3.14.0
$ pio pkg list -g
Platforms
└── espressif32 @ 7.1.0 (required: platformio/espressif32)
Tools
├── framework-arduinoespressif32 @ 3.20017.241212+sha.dcc1105b   (= Arduino-ESP32 2.0.17)
├── tool-esptoolpy @ 2.41100.260830
├── tool-scons @ 4.40801.0
├── toolchain-riscv32-esp @ 8.4.0+2021r2-patch5
└── toolchain-xtensa-esp32s3 @ 8.4.0+2021r2-patch5
```

There is **no `native` platform** under `~/.platformio/platforms` on Windows; only
`espressif32` is installed. The old repo's `.pio/build/leader/` has a `firmware.bin` and `merged.hex` dated Sep 26,
so a build already worked here. I proved it again today on a copy, for the 1.28 with BLE:

```
$ BADGE_BLE=1 pio run -e badge        # in scratchpad/cns-copy
Tool Manager: Error: ... Failed to resolve 'usc1.contabostorage.com' ...   (mirror DNS blip, harmless)
RAM:   [=         ]  15.0% (used 49112 bytes from 327680 bytes)
Flash: [==        ]  21.3% (used 710313 bytes from 3342336 bytes)
merged.hex: 0x0 bootloader.bin  0x8000 partitions.bin  0xe000 boot_app0.bin  0x10000 firmware.bin
========================= [SUCCESS] Took 64.59 seconds =========================
```

### Host compilers on Windows

Nothing is on PATH:

```
gcc g++ cc c++ clang clang++ clang-cl cl zig cmake ninja make mingw32-make tcc ffmpeg  -> (not on PATH)
wsl /c/WINDOWS/system32/wsl   uv ~/.local/bin/uv   python /c/Python314/python   node /d/tools/node/node
```

None of these locations exist: `C:\msys64`, `C:\msys2`, `C:\MinGW`, `C:\mingw64`, `C:\TDM-GCC-64`, `C:\Strawberry`,
`C:\Program Files\LLVM`, scoop, chocolatey, the Android NDK, zig, `~/.cargo`. **But Visual Studio Build
Tools is installed:**

```
$ vswhere -all -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools
C:\Program Files (x86)\Microsoft Visual Studio\2019\BuildTools
MSVC toolsets 2022: 14.44.35207      2019: 14.29.30133
cl.exe: ...\2022\BuildTools\VC\Tools\MSVC\14.44.35207\bin\Hostx64\x64\cl.exe
cmake.exe / ninja.exe: ...\2022\BuildTools\Common7\IDE\CommonExtensions\Microsoft\CMake\{CMake\bin,Ninja}\
Windows SDK: 10.0.19041.0, 10.0.26100.0
components: VC.Tools.x86.x64, VC.CMake.Project, Windows11SDK.26100, Windows10SDK
clang-cl: (none; the VC.Llvm component is not installed)
```

Proof that it compiles and runs C++17 (`scratchpad/cltest/t.cpp` renders a 240x240 round-mask
RGB565 framebuffer):

```
call vcvars64.bat && cl /nologo /std:c++17 /Zc:__cplusplus /EHsc /W4 t.cpp
t.cpp
$ ./t.exe
C++201703 lit=45244
exit=0
cmake version 3.31.6-msvc6
ninja 1.12.1
```

Two gotchas. First, `NoDefaultCurrentDirectoryInExePath=1` is set in this shell, so `cmd`
won't run `t.exe` without a path. Second, vcvars prints a harmless `'vswhere.exe' is not recognized`.

**PlatformIO `native` cannot use MSVC.** Its builder hard-codes GCC, from the WSL
copy of `platforms/native/builder/main.py`:

```
33:env.Tool("gcc")
34:env.Tool("g++")
```

So `pio test -e native` on Windows would need MinGW gcc, and none is installed.

ffmpeg 9.0.1 is installed through winget but is not on PATH:
`%LOCALAPPDATA%\Microsoft\WinGet\Packages\Gyan.FFmpeg_…\ffmpeg-9.0.1-full_build\bin\ffmpeg.exe`.

### WSL

```
$ wsl -l -v
  NAME              STATE           VERSION
* survivor          Running         2
  homeassistant     Stopped         2
  docker-desktop    Running         2

$ wsl -d survivor -- which g++ gcc clang++ cmake make ninja pkg-config sdl2-config ffmpeg python3 uv
/usr/sbin/g++
/usr/sbin/gcc
clang++ not found
cmake not found
/usr/sbin/make
ninja not found
/usr/sbin/pkg-config
/usr/sbin/sdl2-config
ffmpeg not found
/usr/sbin/python3
uv not found
```

`survivor` is Arch Linux (rolling). Other details: g++ (GCC) 16.2.1; PlatformIO Core 6.2.0 at
`~/.platformio/penv/bin/pio`; platforms `espressif32` and **`native` 1.2.1**;
`sdl2-compat 2.32.72-1`, `sdl3 3.4.16-1` and `/usr/include/SDL2/SDL.h`; `gtest 1.18.0-1`
(no doctest or catch2 packages); Pillow 12.1.1; WSLg (`WAYLAND_DISPLAY=wayland-0`,
`DISPLAY=:0`), so even the SDL window would open. **No cmake, ninja or ffmpeg.**

**The old simulator builds and renders headless in WSL.** I tested a copy in `/tmp/cns-sim`:

```
$ pio run -e sim
Linking .pio/build/sim/program
========================= [SUCCESS] Took 14.53 seconds =========================
$ ./program --headless --script feed.txt --shot shotN.bmp --after 3000     # feed: "@0 V pet", twice
run1 exit=0   sim: wrote /tmp/cns-sim/shot1.bmp
run2 exit=0   sim: wrote /tmp/cns-sim/shot2.bmp
13628d2e59ce8b729061c102c5931808649752e448ab40de9a0d9ffe28069ea3  shot1.bmp
13628d2e59ce8b729061c102c5931808649752e448ab40de9a0d9ffe28069ea3  shot2.bmp
bmp 240 x 240 bytes 172854
```

I converted it to PNG with Pillow (`scratchpad/shot1.png`, 6 colours). It shows the
sleeping "cactus" pet with a `z` inside the round mask, so the frame is real and deterministic.

Invocation gotchas from Windows, all hit during this run:
- `wsl -d survivor -- bash -c '...$x...'` **re-parses the arguments through the login shell**, so
  `$vars` disappear. Use `wsl -d survivor --exec bash /mnt/c/.../script.sh` instead.
- Git Bash rewrites `/home/...` arguments into `C:/Program Files/Git/home/...`. Set
  `MSYS_NO_PATHCONV=1`.
- Builds ran in WSL `/tmp`. I did not measure building straight from `/mnt/d` (the 9p
  mount), which is usually several times slower.

### Recommendation

1. **Firmware:** build on Windows with `~/.platformio/penv/Scripts/pio.exe run`
   (proven: 65s). `pick_port.py` handles COM ports by VID:PID.
2. **Engine unit tests:** make the creature engine pure C++17 with no Arduino and no
   LovyanGFX (time is passed in, the RNG is seeded, and it writes to a plain struct or framebuffer).
   Run its tests in **WSL `survivor` with `pio test -e native`**. The platform, g++ 16 and
   SDL are already there; Unity is fetched on first use, or use the installed gtest or a
   vendored `doctest.h`. This is the same environment as the sim, and the same compiler
   family as an `ubuntu-latest` CI job. Drive it from Windows with
   `wsl -d survivor --exec bash <script>`.
   - Zero-WSL fallback and portability check: MSVC 14.44 through `vcvars64.bat` with the bundled
     CMake and Ninja. This is proven, but needs a small CMakeLists and does not go through
     `pio test`.
3. **Headless frame-render-to-PNG:** copy the `sim/` pattern (a board-header seam,
   `--headless --clock fixed --script --shot/--record`), build it in WSL, and convert BMP to PNG
   and compare goldens with WSL Pillow, using a trimmed `film.py`. Make the mp4 step optional,
   or call the Windows ffmpeg path, because WSL has no ffmpeg. A pure-Windows render
   through LovyanGFX would need SDL2 installed, so it is not free today. The alternative is
   to have the engine draw into its own RGB565 buffer and write PNGs from a host test,
   which MSVC can also do. That loses LovyanGFX pixel parity with the device.

## Open items I did not check

- Whether the prebuilt Arduino 2.0.17 bootloader really has
  `CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE`, which `ble_ota.h:21-23` relies on.
- How much PSRAM the 1.28 (S3R2) has. The env sets `BOARD_HAS_PSRAM` and `qio_qspi`,
  but the sprite deliberately stays in internal RAM.
- Whether Chrome's Web Bluetooth `namePrefix` filter sees a name that is carried only in the
  scan response. I believe it does, but it is untested.
- Scratch left behind: `scratchpad/cns-copy/`, `scratchpad/cltest/`, and WSL `/tmp/cns-sim/`.
