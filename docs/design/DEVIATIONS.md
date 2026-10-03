# Deviations from DESIGN.md

Each entry names what the build does differently from the design, and why.

## 1. `usage_check.cpp` lives in `tools/`, not `test/`

PlatformIO compiles every source file in the `test/` root into every test
suite. The usage sketch declares test doubles it never defines, so in `test/`
it breaks the link of every suite. It moved to `tools/usage_check.cpp`, and
`tools/syntax_check.sh` still compiles it with `-fsyntax-only`.

## 2. User-directed: neglect can kill (overrides open question 1)

The user (2026-10-02): the toy is for adults who love video games, and death
is fine. Sustained neglect makes him ill, and the illness can progress to
death, followed as usual by the vigil (the tomb) and the clutch. The switch
is one named constant, `kNeglectCanKill` in `starter_genome.cpp`, now `true`.

## 3. User-directed: time catches up while unplugged (overrides open question 5)

The design chose stasis while unpowered. The user wants unpowered time to
pass, so a long absence can end in death and a clutch. The board has no RTC
that survives power-off, so wall time comes through a new seam:

- `TimeSource` in `seams.h`, queried in priority order: Wi-Fi NTP (the
  credentials arrive once over BLE; only the seam and a host fake exist
  now, because Wi-Fi is a hardware unit), then the time a phone visit gives
  with `TIME`, then none.
- With no source, he resumes as if no time passed, and catches up once a
  source appears.
- The keepsake persists a wall anchor: the last known wall time and the pet
  tick it was read at. When wall time arrives, the unpowered gap is
  `wallNow - anchorWall - poweredSecondsSinceAnchor`. The Dish fast-forwards
  a coarse simulation over it (the `stepCoarse` machinery the viability dry
  run uses, at a five-minute stride, capped in cost), so hunger, illness,
  ageing and death advance. A death inside the gap records the Death and
  lays the clutch, as a live death would.
- The anchor moves to `(wallNow, tick)` and is saved at once, so the same gap
  is never applied twice.

This folds into units 12 (the anchor in the snapshot), 13 (`TIME` feeds the
phone source) and 14 (the seam, the catch-up and its tests).
