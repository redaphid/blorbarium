# The duel HUD: cyber-puck's infographics on the round dish

The owner (2026-10-21): "Low framerate during battles is terrible as well. We
can use radial graphs like ~/Projects/cyber-puck etc. ... port a little of the
cyber-puck infographic aesthetic in, instead of the rasterized hearts etc."
Branch `duel-hud`. The pets, the action icons, the bursts and the duel itself
are unchanged; this replaces the counters round them.

## What cyber-puck's screen does (read-only, `~/Projects/cyber-puck`)

cyber-puck is a 240x240 round agent badge. Its look, and where it is written down:

- **Arc gauges are its whole vocabulary.** "A filled arc with a track behind it
  and an optional marker across it ... anything else that is 'a fraction of
  something, on a round screen'" (`src/widgets.h`, `ArcGauge`, `drawArcTrack`,
  `drawArcFill`, `drawArcMark`). A track in `DUST` grey, a fill in a role
  colour, and the two flanks mirror each other (`fromEnd`).
- **Spend angle, not radius.** "Radius is the expensive dimension: there are
  only 120 px of it, and the pet takes most of them. Angle is cheap: a band at
  r83 sweeping 110 degrees is four times the length of a 40px bar for less
  ink" (`src/layout.h`). Gauges sit on the flanks between the pet and the rim.
- **The gauge that is read is the heaviest band**, and a second measure of the
  same thing is a thin concentric companion just inside it (`BURN_OUT/IN` 16 px,
  `arcInside`, `WEEK_THICK` 3 px, `WEEK_GAP` 1 px; `src/layout.h`).
- **Segmented rings with gaps** (`src/ring.h`: `segArc`, a 6 degree gap for six
  segments or fewer), flat bands of one thickness, state in colour only.
- **A spoke across a band** overhanging it either side, "so it reads as a
  graticule rather than a gap in the fill" (`drawArcMark`, the burn gauge's
  on-pace spoke, `BURN_SPOKE`).
- **The rim is for "that counted" and "time is passing"**: `RimCountdown`
  starts closed at twelve and drains, the gap opening at twelve; a hold is the
  same ring filling closed (`RIM_HOLD`, `RIM_GRACE`, `src/widgets.h`).
- **A bloom is one word for "look here"**: a flash toward `SPARK` white that
  decays, a bright head sweeping the arc once, a thin spark riding out off the
  disc (`bloomSince`, `drawSwarm` in `src/main.cpp`).
- **Roles, not colours**: one colour, one meaning, on a near-black ground
  (`src/theme.h`, cyberpunk: dust `{68,72,92}`, violet flow, amber `you` = "a
  human should act", crimson hurt, lime done, cyan read, `SPARK` near-white
  only for events). Nothing important in pure blue.
- **A 3 px floor**, every tick with a `SHADOW` outline because no colour beats
  every ground (`docs/design-notes.md` ground rules, S5).
- **Typography** is FreeSans 9 pt for labels and bold digits for counts. The
  duel keeps its comic lettering; the HUD uses no text, since its readers are
  6 to 13.

## What the duel draws now (lib/paint/src/hud.cpp)

| Element | Was | Now |
|---|---|---|
| Hearts (HP) | a row of 11x10 heart bitmaps up the rim, hollow when lost | a segmented arc gauge on each pet's side (mine 22-71 degrees, lower right; the friend's the same turned half round), r 98-112, one segment a heart: lime at 3 or more, amber at 2, crimson at 1 (breathing a glow); a lost heart is an empty dust track 4 px thin (shape, not only colour) |
| Assist hearts | gold hearts with a sparkle | the pet's cheer gold, a white spoke across the segment |
| Damage | lost hearts blink, pink hearts fly up | the hit's hearts stay full till the impact, then flash white, drain crimson from the far end, and a thin crimson spark rides out off the glass |
| Spark meter (power) | six diamond pips | a thinner cyan companion arc inside the hearts, six segments, a spoke where the power is paid for; when ready the paid segments brighten toward white and breathe, the spoke turns white |
| Turn clock, mercy clock | a cream ring tested per pixel by angle | the rim countdown: a band at r 112-119 drains from twelve clockwise, a white hand at its edge, dust track behind; text-grey, amber under 30%, crimson under 15% |
| The stance or tier under the tilt | (bigger icon) | four white reticle brackets round it |
| My lock, my confirmed tier | a green tick in a dark disc | the reticle closes into a heavy lime ring |
| A hold (Mortal confirm, the finish) | the skull's art filling | the skull still fills, and a ring round it fills clockwise from twelve (amber to confirm, crimson to finish) |
| The friend choosing / locked | a "..." speech bubble / a tick | a three-arc spinner beside his head / the ring closed lime with a tick |
| A special's countdown | diamond pips under the glyph, a bar for "now" | a ring of that many amber arcs round the power's glyph; "lands now" a closed ring beating crimson to white |
| Meeting | hearts appear | the gauges sweep in from their start, as cyber-puck's ring does at boot |

The pets, the action picker's icons (the 200% aimed stance from 48 px art),
the bursts, the arrow, the stakes' art (feather, plaster, skull), the trophy
and the BOOT hint are as they were. The BOOT hint's own pips are the home
screen's too, so they stay (an owner call; see below).

## Fast by construction

`hud::arc(cv, cx2, cy2, r0, r1, a0, a1, colour, alpha)` fills an annular sector
row by row:

- the band's circles come from `kHalf`, a 16 KB table of half-chords
  `floor(sqrt(4r^2 - k^2))` for every radius to 127 and every row offset,
  built by the compiler (constexpr), in flash;
- the wedge's two edges are a half-plane each: on a row that is one multiply
  and one divide per edge, from a quarter-wave sine table (Q14, 1/1024 turn,
  also built by the compiler from a Taylor series in constexpr `double`; no
  double at run time);
- an arc is cut at the quadrants, so each piece's rows lie between its four
  corners, and pieces are half-open, so arcs end to end tile with no pixel
  drawn twice (`Hud.ArcsEndToEndTileTheBand`);
- then a run of stores (or blends) along the row.

No per-pixel trig, atan2, square root or division. It is held to a per-pixel
double reference in `Hud.AnArcIsItsBandAndItsWedge` (zero wrong pixels; only
pixels within 0.6 px of an edge may round either way).

## Measured

The S3 is the stick (docs/notes/perf-climb.md): `scripts/test/paint_bench_qemu.sh 64`,
the board's compiler and flags on Espressif's QEMU, thousands of instructions
a frame over the bench's 54 duel frames (18 moments, 3 pairings); about
kinstr / 240 ms on the board. `paint_bench_s3` now prints the duel's phases.

| commit | duel kinstr/frame | ~ms on the board | arena (bg) | pets + props (fig) | HUD (rim) | hint |
|---|---|---|---|---|---|---|
| main d4c0a44 (perf-climb merged) | 9,426 | 39.3 | 4,146 | 5,154 | (in fig) | 89 |
| + the burst's spikes once a burst (fdcc951, same bytes) | 8,486 | 35.4 | 4,146 | 4,213 | - | 89 |
| + the HUD (this) | **6,902** | **28.8** | 4,146 | 2,564 | 103 | 55 |

The HUD (the clock and both pets' gauges) costs 103 kinstr a frame, about
0.4 ms; the hearts it replaced cost more than that on their own (the host's
`scripts/test/duel_kinds.sh` before: hearts 79 us a frame of 330 for every
prop, the per-pixel clock 75 us a frame it was on, against a frame of 971).
The icons' backing discs through the same spans took the BOOT hint from 89 to
55 (91 to 57 on home frames; home frames 3,640 to 3,605), the same bytes.

Same bytes where promised: the burst and the disc each checked alone against
perf-climb's bench hash (9e8b4d7adc51ddc4); the HUD's frames are new bytes, held
by the goldens.

**What is left is the arena**: 4,146 of 6,902, 60% of a duel frame, a
per-pixel gradient and two pools (`arena()` in duel_scene.cpp). perf-climb said
it is working on it; everything else in a duel frame is now 2,756 kinstr, about 11.5 ms.

Goldens re-blessed after looking at every one: `test/golden/duel_staging.txt`
and 48 of the 54 `test/golden/duelscene_*.png` (all but fallen and spared,
which draw no counters). Sheets: `outputs/duel-hud/{before,after,side_by_side}.png`
from `scripts/test/duel_hud_sheet.py` (each stance aimed, locked, a hit
landing and draining, low HP, a KO, a wind-up, the stakes, mercy, the end; for
grungo v fizz, gilly v mothwick, tinker v mudge).

## For the owner

- **Colours are roles** taken from cyber-puck: lime life, amber for "act"
  (two hearts, the clock short, a hold), crimson for damage and the last heart,
  cyan power, white only for events and reticles. Lime also closes a locked
  ring ("done choosing"), as cyber-puck's DONE; say if a lock should be another colour.
- **The friend's lock spinner** sits right of his head, not over it (the old
  "..." bubble crowded the BOOT hint at 12 o'clock).
- **The BOOT hint's pips** (its greyed "how far short" and a wind-up's turns)
  are still diamonds: the hint is the home screen's too and its goldens are
  someone else's. They could become a small arc the same way.
- **Before the impact** a hit's hearts now stay full (they used to show lost
  from the first frame of the clash), so the drain reads as the hit landing.
- Not tried on a board: the coordinator's `DEBUG fps` on a duel will say what
  the S3's 28.8 ms estimate really is.
