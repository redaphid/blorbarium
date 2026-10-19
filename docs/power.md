# Low power

The badges run on a battery. After **10 minutes with no user input** a badge
drops to low power, and anything a person does brings it back at once.

The timeout is `BLORB_IDLE_SECONDS` (default 600, `src/hw/power.h`). A test
build overrides it without touching the code:

    PLATFORMIO_BUILD_FLAGS="-DBLORB_IDLE_SECONDS=30" pio run -e badge128

Rebuild without the flag before a release. At run time `POWER idle <s>`
(debug link) or `DEBUG power idle <s>` (cable) changes it until reset, and
`idle default` goes back to the built-in value.

## What low power is

| | Awake | Low power |
|---|---|---|
| Screen | drawn about 5 times a second, backlight at the pet's brightness | panel in sleep (SLPIN), backlight off, nothing drawn |
| CPU | 240 MHz | 80 MHz, and the loop rests 20 ms a pass; on `badge128_pm`, light sleep between (below) |
| The pet | lives | lives: every 100 ms tick still runs, so he ages, sleeps and gets hungry as before |
| Body sensing | 50 Hz | 50 Hz (this is what wakes it) |
| Bluetooth | advertising, connectable | the same: a phone still finds it |
| ESP-NOW | only in fight mode | only in fight mode (fight mode keeps it awake) |

Waking turns the CPU back up, takes the panel out of sleep, draws a fresh
frame and only then lights the backlight, so the screen never flashes the
frame from ten minutes ago.

## Light sleep (`badge128_pm`)

The stock Arduino 2.0.17 libraries are built with power management off, so
on `badge128` low power can only slow the clock. `badge128_pm` links the same
Arduino, rebuilt from the same sources (ESP-IDF v4.4.7 38eeba213a and the
components pinned in `tools/sdk/versions.txt`) with these on, and nothing
else (`tools/pm_framework.defconfig`):

- `CONFIG_PM_ENABLE` and `CONFIG_FREERTOS_USE_TICKLESS_IDLE`: the chip
  light-sleeps whenever every task is waiting;
- `CONFIG_BT_CTRL_MODEM_SLEEP` (main crystal as its clock: the board has no
  32 kHz one): Bluetooth's radio sleeps between events and the link survives;
- `CONFIG_PM_PROFILING`: the time in each mode, for `slept_ms`.

Build the framework once (an hour or so; it needs jq, and fetches ESP-IDF
and the components to `~/src/esp32-arduino-lib-builder`), then the env:

    bash tools/pm_framework.sh      # installs ~/.platformio/packages/framework-arduinoespressif32-pm
    pio run -e badge128_pm

The script refuses to install a build whose sdkconfig differs from stock in
anything but power management, so LittleFS, NVS and the rest are the stock
stack: a badge flashed with it resumed its pet with the same genome and age.

Light sleep is only on in low power (`esp_pm_configure` from `cpuPower` in
`src/main.cpp`); awake, the clock is pinned at 240 MHz with no sleep, so the
panel's SPI and the backlight never meet a gated clock. In low power the
sense task's 50 Hz reads and the loop's 20 ms rests wake it; the pet ticks,
motion and the button wake it as before.

**The cable.** The cable is UART0 through the CH343, and a UART hears
nothing while the chip sleeps: a byte wakes it, but that byte and the next
few are lost. Any byte on the cable holds light sleep off for 3 s, so a
script sends a few `\r` (the link ignores them), waits ~60 ms, then its
line (`tools/power_e2e.py` and `tools/duel_soak.py` do). Output is never
lost. Lines sent without the preamble to a sleeping badge mostly arrive
mangled (`#0 ERR 404 UNKNOWN_VERB`).

**ESP-NOW** is not heard while the chip sleeps. Nothing needs it then:
fight mode holds the badge awake, and a badge enters it by a hang, which
wakes it first.

Measured on badge 1, idle on the desk, low power: the chip light-slept about
70% of the time with the cable quiet (11.3 s of 16.2 s), and 5.1 s of 8 with
a phone connected over Bluetooth. Current draw is still to be measured.

## The idle bar

A 1 px arc one pixel in from the rim (`paint::drawIdleBar`, colour 120,150,150)
shows the time left before the badge sleeps: it starts at 12 o'clock and runs
clockwise, full round just after any input, shrinking to nothing as the
timeout runs out. While something holds the badge awake it stays full. It is
drawn last, over every screen, on the board only (the simulator's goldens
never see it).

## What is user input

`src/hw/power.h` (`power::Policy`) decides, from the raw IMU sample (before
the panel's turn, so a rotation is no move):

| Input | Rule | Why |
|---|---|---|
| BOOT | any press | |
| A knock knock | the IMU's double tap, or two taps within 1.5 s | one tap alone is a bumped desk or a keyboard being typed on |
| Moved | gravity, smoothed over about 160 ms, drifts 120 mg (about 7 degrees) from where it last settled | picked up, tilted, turned over. A badge left alone drifts a few mg with temperature; the reference follows every move, so only a real one crosses it |
| Shaken | two samples within 300 ms each 350 mg off the smoothed gravity | one jolt alone is the table, not a hand |
| A phone | a new Bluetooth connection, or a protocol line on RX | the owner is looking at it |

The press that wakes a dark badge only lights it: the pet does not feel it,
so nobody drops a pellet they could not see.

Nothing on the debug characteristic counts, and nothing on the serial cable
does (the cable is a test harness). Nor does `LEAN`, which bends the sample
after the turn to the screen: only `HANG`, `CRADLE`, `PRESS`, `DOUBLE` and
real hands go through the raw sample.

## What keeps it awake

While any of these is up the badge never goes low, and one that starts wakes it:

- fight mode (searching, waiting for the radio, or a battle);
- an update session, or the update's question;
- the debug link's unlock question;
- a `DEBUG shot` going out.

Fight mode opens only when a kid asks (a double press or a hang). It used
to open by itself on every awake badge (0.7.8 to 0.7.10), which kept the
radio on and the badge awake for good; 0.7.11 took that out.

## Controls

Over the debug characteristic (docs/debug-ble.md), reach Show, no unlock:

```
#1 POWER
#1 OK power=full why=boot idle_ms=600000 left_ms=412000 cpu=240 frames=812 changes=0 held=0 sleep=off slept_ms=0
#2 POWER low
#2 OK power=low why=asked idle_ms=600000 left_ms=0 cpu=80 frames=815 changes=1 held=0 sleep=light slept_ms=0
#3 POWER wake
#4 POWER idle 30          (1..3600 s, until reset)
#5 POWER idle default
```

`POWER low` is refused `ERR 409 HELD_AWAKE` while something keeps it awake.
A badge asked to go low stays low until a hand wakes it (or `POWER wake`):
the page's own protocol lines do not undo it. Every change is a
`! DEBUG power=...` event on the debug link and a `[power] ...` line on the
cable. On the cable: `DEBUG power [low|wake|idle <s>|idle default]`, which
also prints `input=` (the last thing that counted), and `DEBUG power stats`
(the PM profiling's lock and mode table).

`sleep=` is `none` on a build without light sleep, `off` awake, `light` in
low power, and `cable` in low power while a cable byte holds it off.
`slept_ms` is the light-sleep time since boot.

## Tests

- `test/test_power`: the rules (idle, drift, shake, knock knock, stray taps
  and bumps ignored, holds, the phone, clock order).
- `tools/power_e2e.py <port>`: on a real badge, over the cable and Bluetooth.
  `light` (skipped on a build with `sleep=none`) proves the chip light-sleeps
  with the cable quiet and the pet lives on; `ble` proves it light-sleeps with
  a phone connected.
  Needs the badge lying still. `--only ten_minutes` runs the real default;
  `--only hand` asks you to pick it up.

Proven on badge 1 (mossby, d4:05:92:77:1b:3c) at 0.7.11, 2026-10-18: every
scenario passed, including 600 s to low untouched, the pet aging while dark,
and a phone connecting waking it. Current draw was not measured.

`badge128_pm` on badge 1, 2026-10-18: every scenario passed, `light` and the
light-sleep check in `ble` among them.
