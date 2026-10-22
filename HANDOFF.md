# HANDOFF

blorbarium is a little artificial-life creature on a round ESP32-S3 screen.
It began as a single gift for a friend and is now a family of devices: the
owner is giving **8** of them to the owner's niece and nephews (aged 6-13) and their
parents, and their pets duel each other over ESP-NOW. It shares ancestry
with [redaphid/claude-notification-screen](https://github.com/redaphid/claude-notification-screen)
but must not depend on it at build or run time (owner, 2026-10-13: "Make
sure this is independent of my claude code notification screen project").

The design is in `docs/design/` (DESIGN.md, DEVIATIONS.md, BATTLES.md,
SPECIES.md, `sketch/`). The task board is Vikunja project 41 "Blorbarium",
and a picture of this file is at https://draw.hypnodroid.com/b/blorbarium.

## Where things stand (2026-10-21, late: coordinator session; see issue #32)

Work is tracked as GitHub issues on redaphid/blorbarium; **#32 is the
sequence and merge order**. The system whiteboard to keep current is
https://draw.hypnodroid.com/b/blorbarium. Builds go through the
one-at-a-time lock `~/Worktrees/blorbarium/pio` (never /usr/bin/pio). The
test badge is the puffkin 5C94094999 (an adult, genome fe88419d, restored from
`pet_backups/d40592771b44-20261021-162606-*`; back it up before risky runs).

**On main (e3bf472):** perf (19 → ~48 fps; dual core, chunk push, frame memo),
smooth motion (between-tick tween, 1.2x screen speed, walk drawn ahead), feeding
(a cookie from the pantry, eaten at the mouth; refusals in words), the radial
duel HUD, controls latency (aim from gravity, tilt ~70/150 ms p50/p95, a brisk
tip is no knock), the pet-line fixes, ble_ota.py, the flip env.

**Open branches (unmerged; see each issue):** wifi-ota-2 (#26 heap fixed and
measured, #27 needs the owner's network and the family's badges; the flip
moves to NVS), egg-ux (#23: a badge lying tilted picked an egg by itself; the
movement-armed aim is in progress), controls-timeout (#24, parked on #23),
sleep-idle (#20/#21: unknown time read as night; refuse rotten food),
punch-up (#34: voice frames off species lines; RESTORE the octopus's
funeral and grave lines; lines are never policed for tone), grungo-cleanup (#36:
no gift device; grungo normal; the octopus the one-off), carry-react (#22),
feeding-allgone (#18), life-stats (#38/#50), ai-lines (#37 and the server
chain #39-#43, #47, #49), neighbours (#33, design), lineage-design (#45/#44/#46
LINEAGE.md; owner answers on #45), cf-ci (#48), and cyber-puck perf-backport
(claude-notification-screen #52).

**Running when this was written:** a detached flash watcher
(`~/Worktrees/blorbarium/flash_watch.sh`, pid in `flash_watch.pid`, log
`flash_watch.log`) flashes main's badge128_flip onto each NEW CH343 badge
plugged in (badges already attached are left alone). Stop it with
`kill $(cat ~/Worktrees/blorbarium/flash_watch.pid)`.

## Where things stand (2026-10-20, night: flip, duel icons, lines, laptop OTA)

A coordinator session: features done by agents in worktrees, merged to `main`
(9d5612d and after), each pushed.

- **Flip** (`flash.sh --flip`, env `badge128_flip`): `BLORB_ORIENT_HOME` in
  `board_lcd128.h` picks home (2 = USB-C up, default; 0 = USB-C down). Owner:
  "USB-C pointed down = up for these devices." The boot log says
  `[hw] home rot=N`. Flipped and checked (Resumed): 5C94094497 mothwick,
  5C94094999 puffkin, 5C94095003 mudge, all on 0.7.12 + the flip, not yet the
  icons or lines. **An OTA of a normal release turns a flipped badge back**:
  the flip is a build flag. Decide: make 0 the default, or keep it in NVS.
- **Duel icons**: the aimed action drawn at 200% from 48 px twins (was 170%).
  The stakes picker (Spar/Scrap/Mortal) still uses 170%.
- **Pet lines**: "the sky is down" only when hanging from home (it fired after
  any face-down moment, for minutes); teach lines goal first, per species; a
  lone knock asks for a second. `Thoughts.TheSkyIsDownOnly...` pins it.
- **`tools/ble_ota.py`** (docs/ota.md "From a laptop"): scan, status and push
  over BlueZ. Never yet connected to a real badge. It pushes only releases
  signed on soul; the owner hasn't chosen how laptop builds get signed (ssh to
  soul is the recommendation).
- **`tools/bench_watch.sh <checkout>`**: flashes every badge plugged in with
  that checkout's `badge128_flip` and prints the boot. Builds use `-j 3`: at
  8 jobs, parallel builds ran this 15 GB laptop out of memory (cc1plus ~2.4 GB
  each on the art headers).

**Open:**
- **Low power never seen** (owner: "I have yet to see any device enter low
  power mode"). Not yet looked at. Suspects: idle never reached (IMU noise or
  the knock/hold detectors counting as activity, BLE), or the flipped home
  making a resting badge read as held.
- **`framerate`** branch (not merged: never run on a board). Drawing was the
  limit (~190 ms a frame, ~5 fps), not the clock. 7b94954 makes the renderer
  ~4x faster on the host with byte-identical frames (`scripts/test/paint_bench.sh`);
  93cc5c5 caps frames at 20 ms, pushes only changed rows, SPI at 80 MHz
  (`BLORB_LCD_SPI_MHZ`, unproven: CLK goes through the GPIO matrix; 40 is one
  line) and holds 240 MHz awake on `badge128_pm`. Test plan:
  `uv run scripts/test/fps_probe.py /dev/ttyACM0 --clocks 40,80`, then watch the
  panel at 80 MHz for torn rows. Expect ~40-50 fps.
- **`wifi-ota`** branch (pushed): builds, 945 native tests pass, never run on
  a badge, one trivial conflict with main (`drawFrame`, keep both). Owner
  questions open: a gesture before accepting a network? 6 h interval?
- `pio run -e sim` fails on main (`KnockSense` not found); fixed on
  `framerate` (6044b45). With it, 6 of 22 sim goldens already drift on main
  (eating, shake_hop, hatch, no_phone, thought, prophecy): re-bless or look.
- The stale `bench` branch on origin equals an older main; delete it.

## Where things stand (2026-10-19, evening: 0.7.12 released; next, on-screen cues)

**Firmware 0.7.12 is live** at https://blorb.hypnodroid.com/fw/lcd128/manifest.json
(commit 775f0a4, env `badge128_pm`, built on this Linux laptop, signed on
`soul` from a throwaway clone `~/blorb-release` with
`--key /mnt/c/Users/hypnodroid/.config/blorbarium/ota-signing-key.pem`;
manifest commit after it; `main` = `low-power`). It is everything since 0.7.10:
light sleep in low power, the no-button controls, and from a live session
with the owner's hands on test board 5C94094999 (d4:05:92:77:1b:44, "puffkin"):

- **Knocks come from the samples, not the tap engine** (`blorb/knocks.h`): the
  tap engine missed every real knock knock and reported ~34 from handling.
  The IMU is read at 250 Hz awake; knocks work held and lying on a table.
  17/17 held, 9/9 on a table, 0 false, in `test/test_knocks` against every
  recording (`test/fixtures/hands_2026-10-19.txt`).
- **The IMU's z was inverted on every badge**: face up on a desk counted as
  lidded (bedtime, night entrainment). Fixed in `imu_qmi8658.h`.
- **Face down always sleeps** him and powers down after 2 min (owner).
- **A pose gesture fires once** until the badge is the right way up again.
- **USB-C up is home** (`ORIENT_HOME`); the hang is USB-C down.
- A knock knock no longer fires a shake.

Not proven: an OTA install of 0.7.12 itself over Bluetooth (the image was
cable-flashed and run for hours; the trial-and-rollback guard is unchanged).
Badges on 0.7.10 or older still ask for a BOOT press for this update; 0.7.11+
take "Turn me around!".

**Next, the owner's ask: on-screen cues so a child knows which gesture to
try, and feedback when they try.** Nothing is drawn for it yet. The design,
the code it touches and how to preview it are in
**`docs/notes/cues-design.md`**. Start there.

Tools from this session: `DEBUG trace <s>` (every 50 Hz sample + peak + what
fired), `DEBUG knock [1|2]`, `DEBUG side [ms] [l|r]`, `DEBUG hatch`;
`tools/imu_record.py` (marks and commands through `<log>.cmd`),
`tools/imu_analyze.py`, `tools/gesture_e2e.py`. The session's method and
numbers: `docs/notes/no-button-2026-10-19.md` and `docs/controls.md`.

## Where things stand (2026-10-19: controls with no button, branch `low-power`)

**With BOOT covered, the pets could not be fed.** A pellet was only ever a
BOOT press. Every word the button says now has a body twin
(`lib/blorb/include/blorb/controls.h`, `docs/controls.md`): **knock knock =
press** (feed, step the stakes, quick special, rematch, next egg), **turn
onto a side and hold = hold** (tuck in, keep an egg, heavy special; held 3 s:
Mortal, Finish, hug), **hang = double press** (fight mode). A whole Mortal
duel, a hug's friend egg, feeding and the clutch run with no button in
`test_two_dishes` (`NoButton.*`), and on badge 1 through the real IMU path
(`tools/gesture_e2e.py`, 7/7). Behaviour changes for the owner to confirm
are listed in `docs/notes/no-button-2026-10-19.md`.

**On-device e2e on `badge128_pm`** (same note): power 9/9 and debug link
139/139 on badge 1; `duel_soak.py` with two badges is still owed on
`badge128_pm`. A real knuckle and hand through the case are not yet tried.
New cable verbs: `DEBUG knock [1|2]`, `DEBUG side [ms] [l|r]`, `DEBUG hatch`;
debug link `KNOCK`, `SIDE`. `tools/flash_board.sh` now finds a pipx esptool
4.7 when PlatformIO's 4.5 is too old.

## Where things stand (2026-10-18, late: branch `low-power`)

**The badges have a battery and no off state, and the kids cannot use the
button.** The nephews' badges (0.7.x) drained their batteries and died. The
`low-power` branch (pushed, not merged, not released) adds:

- **Low power after 10 min idle** (`BLORB_IDLE_SECONDS`, default 600):
  dark panel, 80 MHz, the pet still lives, BLE still advertises; motion
  (drift, a shake, a double tap), a phone or the button wakes it. A 1 px
  idle bar on the rim shows the time left.
- **Light sleep in low power** on env `badge128_pm`: Arduino 2.0.17 rebuilt
  from its own pinned sources with only power management and BLE modem sleep
  on (`bash tools/pm_framework.sh` once, about an hour; then
  `pio run -e badge128_pm`). About 70% asleep when low, BLE stays connected,
  the pet's save reads back unchanged. `badge128` is untouched.
- **No constant fight mode**; a hang needs the badge on its edge.
- **Debug BLE verbs for everything the device does** (POWER, PRESS, DOUBLE,
  HANG, CRADLE, LEAN, NET, REBOOT, NAP, DUEL), all proven over real
  Bluetooth by `tools/debug_ble_e2e.py`.
- Firmware **0.7.11**, not released (the OTA key is on the Windows desk
  machine).

**Merged to main (2026-10-19). Release 0.7.11 from `badge128_pm`**: the
signing key is on `soul` (the Windows desk machine, `ssh soul`), which
cannot build `badge128_pm` (the PM framework is built on Linux). So build
here, sign there: on Linux `pio run -e badge128_pm` at the main commit,
copy `.pio/build/badge128_pm/firmware.bin` to the same path in soul's
checkout of that same commit (clean tree), then on soul
`uv run --with cryptography python tools/ota_release.py release --env badge128_pm --no-build`,
commit the manifest, deploy the site.

**Updates no longer need the button**: the question says "Turn me around!"
and a turn of the badge answers it as a press does (`src/hw/turn_yes.h`;
proven over Bluetooth on badge 1 with a throwaway key). Badges still on
0.7.10 or older ask for a press for this one last update.

Proof, gotchas and the next step are in
`docs/notes/low-power-2026-10-18.md`; how it works is `docs/power.md`.

**Next, agreed with the owner:** a real off state with no button. Measured
(`tools/wom_probe`): the QMI8658's INT1 is **GPIO47**, which cannot wake
deep sleep (not an RTC GPIO) but does wake **light sleep** (11 ms at a 2 mg
test threshold; a still badge at 150 mg stays asleep). So: after about an
hour in low power, save and enter a manual light sleep with the panel, BLE
and WiFi off and the IMU in wake-on-motion, woken only by GPIO47; with a
probation against backpack and car wakes, elapsed time carried into the
pet's catch-up, and a low-battery shutdown from the ADC on GPIO1.

**On the cable with `badge128_pm`:** a light-sleeping badge loses the first
bytes it hears (UART0 via the CH343 is deaf asleep). Send a few `\r`, wait
60 ms, then the line; `tools/power_e2e.py` and `tools/duel_soak.py` do.

**Correction:** comments in `dish.cpp`, `senses.h`, `thoughts.cpp`,
`main.cpp` and `dish.h` say the board has no battery. The 1.28 badges have
one; what they lack is a clock that survives the battery running flat.

Badges here: badge 1 (mossby, 5C94095000, d4:05:92:77:1b:3c) runs
`badge128_pm` 0.7.11 with the 600 s default (reflashed after the probe;
its pet is expendable, per the owner). Badge 5 (mudge, 5C94095003,
d4:05:92:77:1b:30) is unplugged on an older 0.7.11 build. Pre-existing
failures on this Linux host: `test_dish` Budget.ALifeAndARebootFitTheLoopTask...,
and `test_ota`, `test_adopt_species` and the sim build (gcc 11 `std::variant`
in `lib/ota/src/session.cpp:55`).

## Where things stood (2026-10-18)

**Firmware 0.7.10 is committed but NOT released**: the OTA signing key is on
the Windows desk machine, not the Linux laptop this was done on. To ship it,
on the desk machine: `git pull`, then
`uv run --with cryptography python tools/ota_release.py release`, commit the
manifest, deploy the site. 0.7.10 = 0.7.9 + fight mode pairing in a crowded
room (a badge who hears nobody no longer blocks every fight) + serial soak
verbs. Build with the pinned `espressif32@6.9.0` only: an unpinned Linux
build sealed both test badges. Two open readiness lockouts with no button
(too sleepy to wake; three losses want a cuddle) are owner decisions. All in
`docs/notes/fight-soak-2026-10-18.md`.

## Where things stood (2026-10-15, owner on a plane with all 9 badges)

**Live:** firmware **0.7.6** (manifest db3c011, firmware 987ae02): every BOOT
press and fight ask is logged with its gate on serial (`[diag]`) and the
debug panel ("Fight:" line, `! DEBUG boot double ms= gap=`, `fight ... gate=
result=`), and the fight banner shows `HEARD n`. Open question from the owner:
on 0.7.4, awake hatched pets did not enter fight mode; the cause is not
confirmed (hypotheses: a silent Sleepy/Hungry decline, the OTA trial gate, or
the double press not registering). These logs are meant to settle it.
Before it, **0.7.5** (manifest f86fac1, firmware ec40e53): fight
mode says why it cannot start (egg, asleep, sleepy, hungry, resting,
updating), a double press wakes a sleeper, fight mode waits out an OTA trial
and opens by itself, BOOT is debounced, and a red ring circles the rim while
fight mode searches. Before it, **0.7.4** (manifest 2c0c9a3, firmware 7577c64): sticky
FIGHT MODE. A BOOT double press (0.4 s) searches for 5 min or until a
second double press. Badges pair by claim, answer and lock (lowest MAC, no
badge claimed twice, no RSSI gate, 20 dBm). A fight shows only once both
badges have heard each other. BOOT picks the stakes (default Spar); tilt
only aims. Lid and drop no longer end a fight. Wire v2: 0.7.3 and 0.7.4 do
not see each other. Finish and Mercy are unchanged. The commit body of
c79cce7 has the root cause; BATTLES.md is not yet updated. Not proven on
hardware. Before it, 0.7.3 (manifest 9a51816, firmware b8766c5: 0.7.2 plus
sleep talk and the first-hours sleep retune for adopted pets, which fixes
"text only with a phone"; docs/notes/sleep-talk.md). Before it, **0.7.2** at https://blorb.hypnodroid.com/fw/lcd128/manifest.json
(manifest 05d7661, firmware commit a5e7a78, 2,257,904 B): 0.7.1 plus
car-proof senses (bed needs 30 s face down and still; road vibration fires
no shake, knock or drop) and every species' own lines written from its art.
Shipped by the owner's order before its gates; afterwards native 794/796
and sim 22/22 (no_phone re-blessed for its new line). 0.7.0 (daf4f19) and
0.7.1 (01b19c8) went out first. `main` = `release-0.7`. Every decision is in
`docs/notes/decisions-2026-10-15.md`.

What 0.7.x is: main + `battles` (nine species, ESP-NOW duels) + one species
per badge from `lib/blorb/include/blorb/defs/badges.def` (the 8 batch
badges take the 8 non-grungo species; the gift `blorb-237c` and any
unknown MAC are grungo) + adoption of the 0.4.0 grungo pets after the OTA
trial keeps the image + species BLE names (`blorb-xxxx` only while on
trial) + thoughts with no phone (TAP ME removed, idle talk; a phone adds
wall-clock, name and taught lines) + debug link 6E400005 with a
`?debug=true` panel + an offline site (service worker caches the latest
image) + Linux flashing (`LINUX.md`) + first-hours tuning (0.7.1: pet
morning at founding, 40 min naps, 15 min egg) + the Android GATT fixes
(0.7.1: debug subscribe and page writes retry when Android says
"GATT operation failed"; a debug session is never evicted as a squatter;
a squatter's refusal is 15 s).

Gates at 0.7.1: native 777/779 (2 skipped) exit 0, sim 22/22, page tests
all green (node 16/16, ota_page, offline_page, debug_page 4/4). A fresh
clone of origin/main in WSL builds badge128 and re-packs all nine species
byte for byte.

**Not proven on hardware by this session:** no badge was on USB. Nothing in
0.7.x was cable-flashed or updated here. The owner is updating badges
from the phone.

### Open work, in order

1. **Finish stays.** The owner, verbatim: "No no no you CANNOT remove
   finish. I have said this a lot". Branch `rel-death` (5c10132, WIP)
   removes Mortal, Mercy and Finish: it was ordered by mistake and must
   **never** be merged. A later "I am fine with whatever death work now"
   did not undo the Finish rule. Find the owner's own words on how pets
   die before touching death again.
2. **"Text only shows once a phone connects"** (owner, on 0.7.1). No phone
   or time gate exists in the code (thoughts.cpp:326-365, dish.cpp:177,
   main.cpp:162-190). Likely cause: the adopted field pets sleep for hours
   (they kept the old sleep genes, and nocturnal species sleep by pet day),
   and asleep only `dreaming` speaks; a phone's welcome wakes him. Branch
   Confirmed by `rel-sleep`'s FIELD_PETS_SWEEP and fixed in 0.7.3 (sleep
   talk, about 20 lines an hour asleep; adopted pets get the FirstHours sleep
   bytes). Not done: a one-time clock shift that would wake the field pets
   from their 0.4.0 pet night (irreversible; they wake by about 11 h anyway).
3. Known from the first-hours run: at 25-30 h, night BOOT presses leave
   pellets that rot and a starving pet eats them (injury up to 0.85).
   Levers are `rotMinutes` (90) and fed_bad's toxin (0.4).

### How the owner updates a badge (phone only)

Open https://blorb.hypnodroid.com in Android Chrome (works offline after
one visit), tap Connect, pick the badge (`blorb-xxxx` on 0.4.0, its
species name after), tap Update, press the badge's button when asked,
keep the phone close about 3 minutes until "Updated". Then "Connect
another badge" for the next one.

## Where things stand (2026-10-13, evening, paused)

**Deadline: the owner flies out Thursday 2026-10-15 with the 8 devices.**
Every agent was stopped with its work pushed. Nothing is running except a
ComfyUI art queue (below).

### Branches

| Branch | Tip | State |
|---|---|---|
| `main` | 045ad8a | Storage provisioning, the sealed screen, BLE OTA from the website. Proven on hardware. |
| `battles` | 5aa2372 | The integration branch for duels: units D1-D8, D10, D11 and ART1-4 landed. Native 693/694 (1 skipped). The duel engine, wire, sessions, scenes and 9 species are here, but the duel is **not yet inside the pet**. Not merged into `main`. |
| `battles-d11b` | 1da0659 | BOOT hint moved off the home marquee (test `BootHint.StaysOffTheMarqueeStrip`); duelists drop in on the greeting. Native 694/695. **Land first.** |
| `battles-d9` | 60f2f1b | D9, the duel inside the pet: a double press opens a duel, BOOT routing from `kBoot` with a rim hint on every screen, wounds/scars/fall/meeting/friend egg saved at once, the one-Checkpoint duel-gene graft, friend.egg consumed before the Birth (power-cut tested), slayer feat, war paint, vendetta, the losing-streak cuddle. Already merges `battles` 5aa2372 (its own `sceneOf` dropped for D11's). **Owed before landing:** a full native run at this tip (the last full run, 711/715, predates its pickEgg fix) and the guard rerun (stopped at 5/14). Left: the 50 Hz nest loop in `setup()`. |
| `battles-d13` | 4ebe148 | Film tooling: two simulators over `sim/serial_air.h` (a fake radio with loss, doubles and lateness), scripted kids in `sim/duel_bot.h`, `tools/duel_video.{py,sh}`, scripts in `tools/duels/`. |
| `roster-art` | 455a897 | `tools/comfy_queue.py` and `tools/art_sheets.py`, the ComfyUI candidate queue. |
| `flash3` | 2f8fd00 | What the first gift device (an adult Grungo, genome `d30f00d1`) runs. See "The first gift device" below. |

### Delivery plan to Thursday

Each release is proven on the two test boards, then cut as a signed OTA
image, before the next starts, so there is always something working to
ship. Anything not ready by Thursday reaches the devices over the air later.

1. **R0, Wednesday morning, the floor.** Land `battles-d11b`. Put the
   `battles` tip on both test boards and soak it for hours. Cut a signed
   OTA release. This is 9 creatures, OTA and safe storage.
2. **R1, Wednesday.** Run the full native suite and the guards on
   `battles-d9`, then land it. Refilming a Mortal through the Dish path is
   its acceptance test.
3. **R2, Wednesday.** D12: a real ESP-NOW duel between COM11 and COM12.
   This gates battles at the visit.
4. **R3, Thursday morning.** Flash all 8 gift devices, one on the cable at a
   time, with the newest release that soaked, then check OTA from the
   phone. Never flash a build that did not soak. Write
   `tools/flash_fleet.sh` first: it refuses COM11/COM12, flashes, waits for
   the first boot's provisioning, saves the boot log, and records each
   device's serial and MAC against the species it hatched.
5. If R2 slips, ship R0/R1 and deliver battles over OTA during the visit.
   After that, over OTA: the lobby fixes, D14 (the phone window), balance.

Undecided: the creature is rolled from the MAC, so with 8 devices and 9
species two kids may get the same one. The owner may want one pinned per
device, or "all different". Settle this before R3.

### Proven on hardware

- **OTA over BLE from the website** (Web Bluetooth, Android Chrome): the
  image is ECDSA P-256 signed over `blorbota2 board ver flags fmt size sha`,
  confirmed by a BOOT press on the device, and kept only after `OTA
  CONFIRM` plus 30 s, else it rolls back. 0.4.0 to 0.4.1 was kept at about
  15 KB/s, and the 0.4.2 fail-trial build rolled back.
- **Provisioning a factory board** (COM12): blank partitions are formatted,
  the first boot is Fresh, the next is Resumed. Issue #1 is closed.
- **All 9 species** draw on the real board.
- **A Spar over ESP-NOW between COM11 and COM14** (D12, firmware 0.6.0):
  `uv run tools/duel_hw.py COM11 COM14` plays it by DEBUG verbs (double,
  lean, press), 12 turns, both boards logged `outcome_hash=8430b2b9`. Heap
  with BLE and ESP-NOW up: 57 KB free, 49 KB largest block (from 95 KB);
  after the first close Wi-Fi keeps about 16 KB, stable across reopens.
  `src/hw/air_espnow.h` reads RSSI from promiscuous mode (IDF 4.4's ESP-NOW
  callback has none).
- **The body is sensed at 50 Hz on its own task** (`bodyq` in main.cpp): a
  frame takes about 190 ms to draw, so sampling between frames ran at 5 Hz
  and missed double presses and made aiming take 3 s. The draw itself is
  still slow (on `main` too, since the envelope in 0885b2d); not fixed.
- **Not yet proven:** Scrap and Mortal on hardware, OTA `ERR 409 BUSY`
  mid-duel, the RSSI at 30 cm and 2 m for `kNearRssi`.

### Boards and keys

- **COM11**, CH343 serial 5C94095006, MAC d4:05:92:77:1b:34: a test board
  holding Grungo `e045ab17` (blorb-1b34). Was a badge; its full backup is in
  `D:\projects\bak\blorbarium\`.
- **COM12**, CH343 serial 5C94094999: a test board holding an egg
  `e1fe2f88` (blorb-1b44). Its factory image is backed up beside it.
- Any badge on the cable may be flashed for testing. Do not back badges up
  first; the merged `.hex` flash keeps the pet partitions anyway.
- `tools/flash_board.sh` flashes one board (`--no-reset` to skip the reset).
  `tools/flash_batch.py` flashes a batch and keeps each pet unless
  `--wipe-pet`. **Flashing from a Linux laptop: [LINUX.md](LINUX.md).**
  `DEBUG shot` plus `tools/shot.py` is how an agent sees the screen.
- **The OTA signing key** is
  `C:\Users\hypnodroid\.config\blorbarium\ota-signing-key.pem`, and its
  public half is `src/hw/ota_pubkey.h`. Lose the key and every device needs
  a cable for its next update. Back it up before Thursday.
- This PC has no Bluetooth adapter. BLE work is proven through the owner's
  Android phone (`adb reverse` to a local site).

### Battles, in one paragraph

Three stakes: Spar (no harm), Scrap (wounds and scars), Mortal (the pet can
die; both kids hold BOOT to agree, and the winner holds 3 s to Finish,
while silence spares). Each turn is a stance from a wheel: Pounce beats
Trick, Trick beats Guard, Guard beats Pounce. A BOOT press queues a quick
special (one turn of wind-up, +1 heart) and a hold queues a heavy one (two
turns, +2). A Trick staggers it, a counter ripostes it, and Hush dodges it.
Afterwards there are friend eggs, a duel book, war paint for slayers and
vendettas for the fallen's line. `resolve()` is pure, integer and
mirror-symmetric, so both devices agree on the outcome hash. Packets:
Beacon 77 B, State 111 B, Genome 224 B. Pets dying is the owner's decision
(2026-10-13: "Pets should die. My family has a dark sense of humor.").

### Films and art

- Films are in `D:\projects\bak\blorbarium\duels\` and `reel\`. Only
  `scrap-*.mp4` (Mossby v Tinker) shows the real duel screen. `mortal-*`
  still shows the dish view and was one-sided (6-1). `spar-*` was cut off
  mid-encode; `tools/duels/spar.txt` is ready to render. `reel-*` is the
  sizzle reel.
- **ComfyUI** (`127.0.0.1:8188`) holds a candidate queue submitted by
  `tools/comfy_queue.py`, which renders into `D:/sync/Comfy/blorbarium/`.
  The first expression jobs come out smiling, because they start from each
  creature's smiling face. The fix (paint the expression in first, denoise
  at 0.6, "smile" as a negative prompt) is in the script but not yet proven
  on the GPU. `--cancel-mine` removes only this script's pending jobs, and
  `--fuzz N` randomises and records the settings. `tools/art_sheets.py`
  makes contact sheets. Candidates only: nothing goes into the firmware
  until the owner picks.

### Open bugs and questions

- **Lobby drawing** (`sceneOf`/`drawDuel`): an unknown friend is drawn as
  species 0 (Grungo), and the pet's own sprite is clipped by the rim. Fix it
  in the shared drawing, not the sim.
- **A turn-0 clash** in the Spar film: Guard lost to Pounce. `resolve()` is
  correct for those cards. Suspects, in order: the D8 session commits a
  special on an empty meter (playing its placeholder Pounce); the film swaps
  A and B (A is the lower MAC); a caption shows the aimed stance rather
  than the played one. Settle it from the raw `turns[0]`.
- **Balance:** Mothwick 0.394 and Mudge 0.413 Queuer v Queuer; Mudge beats
  Gilly 0.755 on autopilot. Move `kScarKinds` into `stakes.h`.
- GitHub: #3 (rc=6 notify retries), #4 (the 0x228 drop), #5 (docs step 2).
- For the owner: the layout defaults (42/54 sizes, stances at 9/3/6, strike
  bar only, assist hearts lost first, NOPE! for a riposte), a word for the
  "immortals" reversal, whether the new creatures are too cute next to
  Grungo and whether Mudge reads as a bird, and whether to graft duel genes
  into the first gift device's Grungo. Settled: BOOT is behind the screen,
  so the rim hint stays at about 1 o'clock.

### Rules for agents

1. Work only in this repo; read claude-notification-screen, never write it.
2. Any badge on the cable may be flashed and tested. No flash backups.
3. Never rewrite history: no rebase, force-push, amend, `reset --hard` or
   `branch -D` of unmerged work. Integrate by merging.
4. After every commit, `git push -u origin HEAD`, WIP included.
5. Opus or sonnet only, never fable, and never above high effort.
6. Say so in the report before changing the save format.
7. Re-bless a golden only after looking at the changed PNG and saying why
   it changed.
8. These go to children aged 6-13, and pets can die, in battle too.
9. Stop only processes you started, by PID or port, never by name.
10. No pet art or mark may read as a piercing, tattoo or ornament. Scars go
    on limbs, shoulders, sides or head, never the front of the chest or the
    belly.

**Landing on `battles`:** merge the unit branch into a fresh branch off
`origin/battles`, run the native suite (`bash tools/wsl_test.sh` inside WSL
from the `/mnt/...` path; git does not work in a worktree from WSL), and
push `HEAD:battles` only if the suite's own exit status is 0 and its output
has no `ERRORED` or `FAILED`. A merge was once pushed while its tests
failed, because the push was chained after a `cat`. Each gtest suite needs
its own `main()`. Scripts are mode 100644, so run them with `bash`.

## Bring-up history (2026-10-03, historical)

- **Board:** Waveshare ESP32-S3-LCD-1.28, usually COM10 on the owner's desk.
  It runs `flash3` 2f8fd00 with an adult Grungo, generation 0, genome
  `d30f00d1`, advertising as `blorb-237c`. He lives in `pet`
  (`snap.a`, `snap.b`) and on `petfs` (`lineage.log`). A cable flash of a
  merged `.hex` never touches them.
- **Website:** https://blorb.hypnodroid.com (branch `web`), public through a
  Cloudflare Access bypass application. Check the Zero Trust dashboard
  before changing Access.
- **Backups** in `D:\projects\bak\blorbarium\`: the whole original 16 MB
  flash (`flash-backup-lcd128-2026-10-03.bin`, sha256
  `b06cdfa43cafa6142a7980bd1bf3ce0469bb86cdde9e17431a6b4b1c8bd63dec`),
  `gift-ble-e9a1689.hex`, `gift-2431fea.hex`, `fallback-d1336ad.hex` and
  `flash3-2f8fd00.hex`.
- To reflash a known-good build, which keeps the pet because the hex has gaps:

      ~/.platformio/penv/Scripts/python.exe ~/.platformio/packages/tool-esptoolpy/esptool.py --chip esp32s3 --port COM10 -b 921600 write_flash 0x0 D:/projects/bak/blorbarium/gift-ble-e9a1689.hex

- To restore the original badge firmware, which **erases blorbarium and the
  pet**:

      ~/.platformio/penv/Scripts/python.exe ~/.platformio/packages/tool-esptoolpy/esptool.py --chip esp32s3 --port COM10 -b 921600 write_flash 0x0 D:/projects/bak/blorbarium/flash-backup-lcd128-2026-10-03.bin

- The bring-up's branch table and its open marble questions are in this
  file's history (`git log -p HANDOFF.md`).

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
- blorbarium ships on the 1.28 (env `badge128`).
- **A board's first boot formats what never held a pet** (issue #1,
  `lib/blorb/include/blorb/provision.h`). When `pet` or `petfs` will not
  open, boot reads it raw. One that holds a blorbarium mark is a damaged
  keepsake: it is left alone, the pet does not save, and the log says
  `LEFT ALONE`. Rescue that board by hand from a read-only dump. Without a
  mark, `pet` is formatted, judged by its own bytes alone. `petfs` is
  formatted when it is blank, or when it is foreign and `pet` holds no pet;
  foreign bytes beside a pet are left alone, because on the test board
  (round 2) junk over both heads left the pet resuming from NVS and
  formatting petfs threw his lineage away. Judging the pair wholly together
  was tried first and review found it wrong both ways: a factory NVS beside
  a petfs an older build had formatted stayed unsavable forever, and a real
  pet's blank petfs was never formatted. The pet marks are bytes every save already
  has: `BLRB` at a 32-byte NVS entry, and a LittleFS superblock of petfs's
  own geometry (4096 x 1440) at offset 8 of block 0 or 1. So a board saved
  before this rule (the gift device) is recognised with no new marker. The
  boot log names each partition: `[hw] storage pet: ok`, `formatted: it was
  blank`, `formatted: it held another firmware's bytes and never a pet`.
  The slots go through IDF's `nvs_*` calls, never Arduino's Preferences:
  Preferences prints an error's name from a table that stops at
  INVALID_LENGTH, so NEW_VERSION_FOUND, NO_FREE_PAGES or a flash fault
  printed through NULL and the test board panicked in a loop.
  Nothing is written before boot has decided: `pet` opens read-only (a
  read-write open creates a missing "blorb" namespace, which the test
  board did to a pet it then sealed), a missing namespace counts as will
  not open so the raw bytes are judged, and a board that leaves anything
  alone formats nothing and opens nothing for writing. IDF's
  nvs_flash_init_partition still runs first and can repair a damaged NVS on
  its own (it erased nothing in the test board's 64 KB case).
  A board with a partition LEFT ALONE boots `Boot::Sealed` (issue #2): the
  Dish reads and writes nothing and runs no life, the screen shows the pet
  asleep in amber with `SAFE BUT STUCK - ASK A GROWN-UP` (golden
  `test/golden/sealed.png`, sim `--sealed`), `STATE` ends ` sealed=1`, and
  serial says `!!! SEALED` with the ranges to dump. A newer firmware's save
  (`Boot::ReadOnlyNewer`, an update rolled back) shows the same screen, says
  `sealed=newer`, and serial says to update the firmware. Known gap: IDF itself decides most of
  what "will not open" means for `pet`. It opens NVS over random bytes (seen
  on the test board, 2026-10-13, so a new board's foreign case is a petfs case) or
  pages whose header CRC fails: it counts those pages as free and erases
  them when it needs room (`PageManager::load`). So a damaged NVS may be
  reclaimed by IDF before provision ever sees it. That predates provision,
  which only meets the NVS that IDF refuses: a newer NVS format, or no
  free page.

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
- The native tests and the simulator run under WSL (`survivor`).

## Working rules carried over

- Agents push every commit, on any branch.
- The pet is a keepsake. Saves are atomic, and nothing flashes over its
  state by accident.
- Anything a forker would want (drawing on a round screen, the BLE wire)
  goes in a small reusable header, separate from this particular creature.
