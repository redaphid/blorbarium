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

## 4. User-directed: the board is authoritative; the phone only proposes

Binding rule: the microcontroller holds the authoritative state. The phone
reads a read-only snapshot and proposes; the board validates, caps or
rejects every proposal through its own rules. No wire verb sets state
directly. Audit of the designed verbs (section 6 of DESIGN.md), before any
protocol code existed:

| Verb | As designed | Now |
|---|---|---|
| reads (`HELLO` .. `HASH`, `BACKUP`) | read-only | unchanged; `STATE` and `HELLO` also report `v=<stateVersion>` |
| `STIM` | fires a Phone-source stimulus | unchanged in kind (an input his own genes interpret), but carries `v=` |
| `EDIT <uid> <hex>` | rewrote an owner-editable gene body | **changed.** `EDIT v=<n> <uid> <offset> <value>` proposes one byte. The board applies it through the mutation path (`apply()` of a one-op `MutationDiff`, recorded in the lineage), only on `OwnerEditable` genes, and caps the move to `kMaxOwnerEditDelta` per byte per edit. Then `viability()` must pass, or the edit is refused |
| `NAME` | set the name | carries `v=`; the board trims it and filters it to printable ASCII |
| `PICK i` | picked an egg | carries `v=`; the index must name an egg in the current clutch |
| `TIME` | set the clock | carries `v=`; it feeds the phone time source (entry 3). The catch-up cap bounds its effect, and a wall time earlier than the anchor is refused |
| `RESTORE` | replaced the snapshot | **changed.** It is a proposal too. With consent, the board decodes the blob through `Keepsake::decode` and accepts it only if the lineage id is this board's, the genome parses and passes `viability()`, and its own boot checks pass. Otherwise it is refused and nothing changes |

Staleness: the board keeps a `stateVersion`, saved in the snapshot and bumped
on every change a phone could have read (hatch, stage, death, pick, edit,
rename, restore). Every mutating verb (`kMutating` in `commands.def`) must
carry `v=<n>`. The dispatcher checks it in one place, and a mismatch answers
`ERR 409 STALE v=<current>`. Unit 21's brain-weight proposals will use the
same version check and board-side caps.

On connect the board pushes nothing. The phone requests the snapshot (`STATE`
for the summary, `BACKUP` for the whole `Keepsake::encode` blob), and both
replies carry `v=<stateVersion>`, which every later proposal quotes. Events
flow only after the phone sends `SUB`. This was already the design's
request-and-reply shape; the rule makes the version and the silence on
connect explicit.
