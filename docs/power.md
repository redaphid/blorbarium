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
| Screen | drawn up to 50 times a second, only the rows that changed sent (below), backlight at the pet's brightness | panel in sleep (SLPIN), backlight off, nothing drawn |
| CPU | 240 MHz, flat out | 80 MHz, and the loop rests 20 ms a pass; on `badge128_pm`, light sleep between (below) |
| Panel SPI | 40 MHz (`BLORB_LCD_SPI_MHZ`) | idle |
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
panel's SPI and the backlight never meet a gated clock. `esp_pm_configure`
only records the limits (the clock moves at the next PM lock change), so
awake also holds a `CPU_FREQ_MAX` lock ("awake"): taking it on waking puts
the clock at 240 MHz at once, and giving it back on sleeping drops it at once. In low power the
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

## Awake: full speed

The badges spend most of their time in low power, so awake is allowed to
cost more (owner, 2026-10-20: "We can go full speed on the chips when on").
Awake, the CPU runs at 240 MHz and the loop draws a frame every 20 ms
(`kFrameMs`; the simulator keeps 40 ms, the pace its goldens were filmed at).

- **The renderer** (`lib/paint`) draws the same bytes it always did, without
  the work that made a frame take ~190 ms on the board: a 64-bit square root
  for every pixel of the dish's background (now only the rim's, in 32 bits),
  a 64-bit division for every pixel a sprite covers (now stepped along the
  row), a soft-float `atan2` for every pixel of the idle bar (now worked out
  once) and of the hang's ring (now only at its edge), and the outline's
  walk once per transform. `scripts/test/paint_bench.sh` draws thousands of
  frames of every species, the duel's moments, a hang, fight mode and the
  amber, and hashes every one: `REF=main bash scripts/test/paint_bench.sh`
  builds the same bench with another commit's renderer to compare.
- **Frames between ticks** (`lib/paint/include/paint/tween.h`): the pet lives
  on 100 ms ticks, and each 20 ms frame draws him between the tick before and
  the tick now, so he moves every frame rather than in 10 Hz steps. Clocks
  (his pose and face clocks, a hop's phase, a thought's crawl, a duel act)
  run on past the tick exactly; where he stands, the pellets, the glow and
  the hang's charge are eased one tick (100 ms) behind. An idle layer
  (`lib/paint/src/lively.cpp`) adds breath, a sway, a bob, a glance and blinks
  at each species' own tempo, from the screen's clock and his genome.
  `paint::kScreenSpeed` (1.2) runs his animations and a duel's staging that
  much faster than the ticks; his life runs as it always did. All of it is
  the screen's: present() leaves the fields zero, and zero draws the tick's
  own frame byte for byte (`hash=` in the paint bench; the sim's `--steps`).
  `DEBUG smooth off` draws each tick's own frame until reset, to compare.
- **The panel** keeps what it was sent, so `boardPresent` sends only the rows
  whose hash changed: a pet at rest sends the few rows his breath and blinks
  move (about 11 a frame for grungo in the sim), a walk about 70, a hop about
  130, and a crossing thought its strip. Every 64th frame, and every frame
  after the panel wakes, sends all of it.
- **The SPI clock** is 40 MHz. The panel's clock pin (GPIO10) is not SPI2's
  IO-MUX clock pin, so the bus goes through the GPIO matrix, which IDF 4.4
  holds to 40 MHz; LovyanGFX does not clamp it, and a command byte garbled at
  80 is not healed by a repaint. 80 (the wiki's figure, a whole frame in
  11.5 ms against 23) is opt-in on a bench badge: `-DBLORB_LCD_SPI_MHZ=80`, or
  `DEBUG lcd 80` until reset. `spi_mhz` reports the clock the divider really
  gives (`lcd 30` runs at 26.6).

The sense task (250 Hz awake) has a higher priority than the loop, so drawing
never delays a sample; the loop takes the samples between frames, as before.

`DEBUG fps` on the cable prints what frames cost since it last asked (the
shape of the line; these numbers are not a measurement):

    [fps] fps=48.6 frames=486 over_ms=10001 draw_ms=11.9 push_ms=1.6 worst_ms=33 rows=21 cpu=240 cpu_now=240 spi_mhz=40.0 frame_ms=20 power=full

`draw_ms` is the canvas, `push_ms` the panel, `rows` the rows sent a frame
(240 is all of it), `cpu_now` the clock as it runs. In low power it reports
no frames and `cpu_now=80` (or 40, between, on `badge128_pm`).

A frame whose inputs (the Appearance byte for byte, the idle bar's step, no
overlay, no duel, nothing on the clock: fight mode, the no-fight strip, the
amber) are the last drawn frame's is not drawn or sent: the panel already
shows it (`framememo`, src/main.cpp). So `fps` and the ms count the frames
that changed, and `same=` the ones skipped; at rest between his 100 ms ticks
most frames are the same. A second line splits a changed frame by phase
(`look bg fig rim text rings mask hint over push wait send`), and
`DEBUG fps bench` times the canvas's memory and the frame shown now by phase.

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
so nobody drops a pellet they could not see. The same goes for the knock
knock that wakes it, the press's no-button twin (docs/controls.md).

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
