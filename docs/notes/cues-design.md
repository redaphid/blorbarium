# On-screen cues for the gestures (design, not started)

The owner, 2026-10-19: "We need UI cues that a child can follow to understand
the gestures they need to perform. They can be discoverable too, when they try
something. But they need to know what to try."

The gestures exist and work on hardware (docs/controls.md, firmware 0.7.12):
knock knock = press, turn onto a side and hold = hold, hang (USB-C down) =
double press, plus tilt, one knock, shake, cuddle, face down. What is missing
is the screen telling a 6-year-old which to do. Today:

- `paint::drawBootHint` (lib/paint/src/duel_scene.cpp) draws the act's glyph
  at 1 o'clock (`kBootAt` {146,18}), **beside the physical BOOT button**,
  which the case now covers. It shows *what* happens, never *how*.
- The care hint (`hint()` in lib/paint/src/draw.cpp, 9x9 glyphs at the
  bottom, `defs/care.def`) shows a BOOT glyph for "feed", a knock glyph for
  "play", the lid for "tuck in": the button glyph is now wrong for feeding.
- Words on screen still say presses: `"TOO HUNGRY - PRESS ONCE TO FEED"`,
  `"PRESS TWICE OR HANG TO STOP"` (lib/paint/src/draw.cpp, `notReady`,
  `lobbyLine`). The OTA screen says "Turn me around!" (fine).
- The pet teaches by thoughts: `FLIP ME OVER. LET'S FIGHT`, `SHAKE ME. I SEE
  THINGS.`, `KNOCK KNOCK ON ME FOR SNACKS` (hungry), `TIP ME SIDEWAYS. NIGHT
  NIGHT` (defs/thoughts.def, personalities/_shared.txt; `python3
  tools/personality.py build` after editing).
- Feedback that exists: the hang fills a red ring (`hangRing`, from
  `Appearance::hangCharge`); a hold fills the BootHint's hold ring
  (`BootHint::holdFill`, from `Dish::holdDown_`, which a side hold drives too).

## Proposal

### 1. A cue: the gesture, then what it gets

A small animated pictogram of **the badge itself** doing the gesture, then an
arrow, then the act's existing glyph (pellet, moon, swords, skull, heart...):
"[badge being knocked] -> [pellet]". No reading needed.

- The mini badge: a circle (r ~14 px) with a tiny face on it and the USB-C
  tab drawn on its rim (up at home), so its turn is visible.
- Knock knock: a knuckle taps it twice, a ripple per tap. Turn and hold: it
  rotates 90 degrees and a ring fills round it. Hang: it flips 180 degrees
  (tab at the bottom) with the red ring. Shake: it jiggles. Cuddle: a cupped
  hand under it. Face down: it flips over (moon).
- **Frames take ~190 ms on the board** (about 5 fps): animate in 2-3 held key
  poses (~400 ms each), never smooth motion.
- Where: a strip at the bottom of the dish (y ~185-212 is clear of the rim
  tokens at (170..207, 170..207) and the pantry pips at the top). The thought
  marquee sometimes runs low (`kMarqueeStrip` mirrored when he stands high):
  skip the cue while a low strip shows. The care hint lives there today:
  fold it in (the most pressing need's gesture goes first).
- On duel screens, beside the BootHint glyph at the top: replace the
  double-press bars (meaningless with no button) with the gesture pictogram.

### 2. What to show when: a pure `Coach` in lib/blorb

From the `kBoot` row of the current `BootContext` (boot.h): press -> knock
knock cue with the press act's glyph; hold (when it differs) -> turn-and-hold
cue with its glyph and `holdMs`; double -> hang cue. Cycle through them every
~3 s when nothing is being tried; the care need reorders (hungry: the knock
knock first; sleepy: face down or the side hold). A greyed BootHint (empty
pantry, meter short) greys its cue. Carried in `Appearance` (a new
`Coach coach`), derived each frame, never saved. Natively testable like
`bootHint()` in dish_duel.cpp.

### 3. Discoverable: feedback while trying

- **One knock landed** (KnockSense's burst holds one knock): the knock-knock
  cue with one of two dots lit, "one more!". Needs plumbing: KnockSense is in
  the shell (main.cpp `feel()`), so expose its burst count on `BodySample`
  (like `knocking`) and have the Dish remember it for the Appearance.
- **Turned onto a side**: the turn cue in its turned pose with the ring
  filling from `Dish::holdSinceMs_` (already the level the duel reads); if
  the context's hold means nothing, show it greyed so the kid learns "not
  here".
- **Upside down**: the hang ring exists; add the hang cue.
- A refused gesture already says why in words (`noFightStrip`): keep.

### 4. Fix the words

`notReady` Hungry -> "TOO HUNGRY - KNOCK KNOCK TO FEED"; the lobby's "PRESS
TWICE OR HANG TO STOP" -> "HANG ME UPSIDE DOWN TO STOP". Check every string
in lib/paint/src for PRESS/BUTTON/HOLD.

## How to see it

- `PAINT_DUMP=/tmp/shots pio test -e native -f test_paint` writes PPMs from
  `Previews.DumpWhenAsked`; convert (`python3 -c "from PIL import Image; ..."`,
  PIL is installed) and look at them with the read tool. Add the cue states
  there first.
- On a badge: `DEBUG shot` + `tools/shot.py --port /dev/ttyACM0` (9 s a frame).
- Goldens: the simulator does not build on this Linux host (gcc 11,
  `lib/ota/src/session.cpp:55`), so sim goldens cannot be re-blessed here.
  Either draw the cue on the board only (as `drawIdleBar` is: main.cpp after
  `drawFrame`), or fix the sim build first. Rule 7: re-bless only after
  looking at the changed PNG. The duel goldens (`tools/duel_film.sh`) see
  `drawBootHint`: changing it there means looking at them all.

## Then prove it with a person

Ask the owner (or a kid) to pick up a badge with no instructions and do what
the screen says: feed him, tuck him in, open fight mode. `DEBUG trace` with
`tools/imu_record.py` / `tools/imu_analyze.py` records what they did against
what fired (docs/notes/no-button-2026-10-19.md has the method).
