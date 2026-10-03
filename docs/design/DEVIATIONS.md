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

## 5. Flash layout follows the OTA survey (affects unit 20, not the engine)

DESIGN.md section 6 put the keepsake on LittleFS in a `keepsake` partition.
The OTA survey (`explore-ota.md` section 5 in the design scratchpad) found
that the Arduino core auto-erases the first `nvs` partition on a full or
changed NVS, and recommends the layout unit 20 now ships on its first cable
flash: nvs, otadata, two 4 MB OTA app slots, a separately labelled `pet` NVS
partition, a `petfs` LittleFS (format-on-fail off), and coredump. The
snapshot's two slots become the keys `save_a` and `save_b` in `pet`, and
`lineage.log` lives on `petfs`. Both sit behind the `Storage` seam, so the
engine, the keepsake format (header, CRC, append-only TLV tags, unknown
tags carried) and its tests do not change. While a new firmware image is on
trial, it writes no save in a newer format.

## 6. Choices made while building, where the design was silent or wrong

Each was reported by the unit that made it and reviewed at integration.

- **Brain (unit 8).** Learning is normalised LMS: the documented update divided
  by the sum of squared features, so one step moves the prediction exactly
  `rate` of the way to what was observed and a mutated high learning rate
  cannot oscillate. Confidence is |effect| / 0.25, capped at 1, because a
  per-cell update count would cost 1.7 KB of state; `heirloomMinConfidence`
  therefore acts as a minimum |effect|.
- **Chemistry (unit 5).** `ChemRules::seeds` holds a chem gene's starting
  level (the design had nowhere for it). `stepCoarse` splits emission around
  the decay (the trapezoid rule) and counts decay applications exactly; the
  plain `stride >> shift` never decayed Life at all. A `featGate` past 32
  never opens, instead of shifting by 32 or more.
- **Senses (unit 6).** `PetClock::entrainedTicks` budgets entrainment, so a
  3-hour misalignment corrects in days rather than months while the bound
  stays one hour per pet day.
- **Creature (unit 9).** A rotten bite fires `fed` as well as `fed_bad`;
  without it he ate every rotten pellet and died of injury within a pet day.
  `age` is 1 - Life. Death and its cause are read from the loci, never stored.
- **Death cause.** Cause receptors sum, so the bands are powers of two in
  sixteenths (OldAge 1, Starved 2, Injured 4, Poisoned 8) decoded by the
  highest set bit; any combination decodes uniquely.
- **Uids.** Mutation never deletes the gene holding the largest uid, so the
  maximum only rises and no uid is reused in a lineage.
- **Catch-up (unit 14).** Gaps up to 60 s are clock drift; a source that runs
  backwards re-anchors and catches nothing up; a gap is capped at 30 days
  and the dropped remainder is recorded. A death inside a gap runs the
  vigil to its end, but the egg choice waits for the owner rather than
  auto-picking. An egg can hatch inside a gap.
- **Twists (unit 13).** The `stimulus` twist accepts any registry stimulus,
  `fed` and `button` included (entry 4: no caps). Each `gene_edit` appends a
  lineage Checkpoint, so `genomeOf` returns the edited genome.
- **Renderer (unit 16).** The halo draws whenever `glow > 0`, with a 5 px
  floor on its radius so the egg's small froglet eyes still glow; egg frames
  carry eye anchors. No blink or yawn while asleep or foreseeing, so the halo
  never jumps. `facing` is not drawn yet.
- **Grungo pack (unit 19).** The 840 to 120 px downscale is a 7x7 block vote
  weighted toward outline and glow, with the winning region's mean colour; box
  and Lanczos filters blurred the ink. The happy, alarmed, annoyed, croak and
  sleepy irises are shifted to neutral's red-brown before quantising. The egg
  is the user's pick (`egg-nest-s1007`, ground stripped).
- **Known gap for unit 20.** `Storage` has no offset read, so `Lineage` reads
  the whole log into RAM (up to 192 KB). The flash implementation must add a
  ranged read before the log grows.

## 7. The grungo pack landed before the simulator

The build plan puts the converter (unit 19) after the simulator and the
goldens (17, 18), with the goldens drawn first from `placeholderPack()` and
re-blessed once for grungo. The pack was built in parallel and was ready
first, so `engine` carries unit 19 (and the user's picked egg) before unit
17, and the goldens were grungo from their first commit. Nothing was
re-blessed for the pack swap; the one deliberate re-bless came later, with
the fixes to what the review frames showed.

The simulator's fixed clock steps 5 ms per loop, not 25 ms, so the IMU is
sampled at exactly 50 Hz as on the device; frames are still drawn every
40 ms. Until unit 20 the badge envs stop at an `#error`, because
`src/main.cpp` needs the board, flash storage and BLE headers that unit
brings.
