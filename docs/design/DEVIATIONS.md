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
pass, so a long absence can end in death and a clutch. Wall time comes
through a new seam (revised the same day: Wi-Fi NTP was dropped):

- `TimeSource` in `seams.h`, queried in priority order: an RTC that kept
  counting through deep sleep (the user may fit a small battery, and the
  board deep-sleeps when unplugged, so on wake the elapsed time is known),
  then the time a phone visit gives with `TIME` over BLE, then unknown.
  Deep sleep, unplug detection and battery measurement are hardware units
  after this branch's stop point; only the seam and host fakes for both
  sources exist now.
- With no source, he resumes as if no time passed, and catches up once a
  source appears. While time is unknown, `Appearance::timeUnknown` is set
  and the renderer scrolls a marquee reading "TAP ME WITH YOUR PHONE". The
  text is one constant, `kTimeUnknownMarquee` in `lib/paint/src/draw.cpp`.
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

## 4. User-directed: the phone adds twists; the board stays the one writer

The user (2026-10-02, final after two superseded same-day readings, neither
of which was built): the board simulates at all times, and he is played with
on the device itself most of the time. The phone is an add-on for twists
that only it can offer, because it is more powerful, online or has sensors.

1. On connect the board pushes nothing. The phone requests the state: `STATE`
   for the summary, `SNAPSHOT` for the whole `Keepsake::encode` blob as
   base64 `+` lines. `SUB` subscribes to live events.
2. The phone sends twist operations, `TWIST <kind> <args>`. The board applies
   each to its live state when it arrives, between ticks, and keeps
   simulating. The board is the only writer, so there is one world.
3. The board checks only that an op is well formed: it parses, the kind is
   known, and every id is in range. There are no caps.
4. Twist kinds are a registry, `defs/twists.def`: one row plus a handler
   `twist_<name>` in `src/twists.cpp` adds a kind. The first rows are
   `stimulus` (fire a stimulus), `gene_edit` (set one gene body byte by uid,
   recorded in the lineage), `rename`, `pick` (choose a clutch egg) and
   `prophecy` (a brain-weight update). Later rows (a weather stimulus, a
   camera colour) need no protocol change.

What this changes in DESIGN.md section 6: `EDIT`, `NAME`, `STIM`, `PICK`,
`BACKUP` and `RESTORE`, and the button-hold consent, are gone. Their jobs
are twist kinds (`gene_edit`, `rename`, `stimulus`, `pick`) or the
`SNAPSHOT` read. No verb replaces the whole state. `TIME` stays as the input
to the phone time source (entry 3).
