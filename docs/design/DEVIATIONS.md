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

## 4. User-directed: one writer at a time, through a lease

The user (2026-10-02): "the phone can mutate the state, once it gets the
current board from the device. This isn't a security thing. It's a
consistency thing. Same keepsake = same world." So the state has exactly
one writer at a time, handed over by a lease. (An earlier same-day reading,
with proposals capped by the board, was superseded and never built.)

1. On connect the board pushes nothing. The phone sends `LEASE`. The board
   replies with the whole snapshot (`Keepsake::encode`, as base64 `+` lines)
   and `OK lease=<id> <len> <crc32>`, and pauses its own simulation while the
   lease is held.
2. The phone may change anything (run the engine as WebAssembly, edit genes,
   apply stimuli). There are no caps and no per-field checks.
3. `RETURN <id> <len> <crc32>` with the blob as `+` lines writes the whole
   state back. The board accepts it only if the lease is current and the blob
   is well formed (it decodes, the CRC matches, the format version is known).
   It then resumes simulating from exactly that state and saves at once. If
   the genome changed, it appends a lineage checkpoint so every ancestor still
   replays. `RELEASE <id>` hands the lease back with no write.
4. If the link drops, or the phone sends nothing for `kLeaseTimeoutTicks`, the
   lease expires and the board resumes from its own paused state. A later
   `RETURN` with that id is refused. A malformed blob is refused and leaves
   the board untouched, paused under the same lease.

What this removes from DESIGN.md section 6: the state-writing verbs `EDIT`,
`NAME`, `STIM`, `PICK`, `BACKUP` and `RESTORE`, and with them the
button-hold consent. The phone does all of those by editing the leased
state. The read verbs stay, `SUB` events stay, and `TIME` stays, because it
is an input to the time source (entry 3), not a state write. The body keeps
every care loop, so the toy is still complete with no phone.
