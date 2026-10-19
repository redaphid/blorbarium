# Low power

The badges run on a battery. After **10 minutes with no user input** a badge
drops to low power, and anything a person does brings it back at once.

## What low power is

| | Awake | Low power |
|---|---|---|
| Screen | drawn about 5 times a second, backlight at the pet's brightness | panel in sleep (SLPIN), backlight off, nothing drawn |
| CPU | 240 MHz | 80 MHz, and the loop rests 20 ms a pass |
| The pet | lives | lives: every 100 ms tick still runs, so he ages, sleeps and gets hungry as before |
| Body sensing | 50 Hz | 50 Hz (this is what wakes it) |
| Bluetooth | advertising, connectable | the same: a phone still finds it |
| ESP-NOW | only in fight mode | only in fight mode (fight mode keeps it awake) |

Waking turns the CPU back up, takes the panel out of sleep, draws a fresh
frame and only then lights the backlight, so the screen never flashes the
frame from ten minutes ago.

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
#1 OK power=full why=boot idle_ms=600000 left_ms=412000 cpu=240 frames=812 changes=0 held=0
#2 POWER low
#2 OK power=low why=asked idle_ms=600000 left_ms=0 cpu=80 frames=815 changes=1 held=0
#3 POWER wake
#4 POWER idle 30          (1..3600 s, until reset)
#5 POWER idle default
```

`POWER low` is refused `ERR 409 HELD_AWAKE` while something keeps it awake.
A badge asked to go low stays low until a hand wakes it (or `POWER wake`):
the page's own protocol lines do not undo it. Every change is a
`! DEBUG power=...` event on the debug link and a `[power] ...` line on the
cable. On the cable: `DEBUG power [low|wake|idle <s>|idle default]`, which
also prints `input=` (the last thing that counted).

## Tests

- `test/test_power`: the rules (idle, drift, shake, knock knock, stray taps
  and bumps ignored, holds, the phone, clock order).
- `tools/power_e2e.py <port>`: on a real badge, over the cable and Bluetooth.
  Needs the badge lying still. `--only ten_minutes` runs the real default;
  `--only hand` asks you to pick it up.

Proven on badge 1 (mossby, d4:05:92:77:1b:3c) at 0.7.11, 2026-10-18: every
scenario passed, including 600 s to low untouched, the pet aging while dark,
and a phone connecting waking it. Current draw was not measured.
