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
  Deep sleep, unplug detection, battery measurement and the RTC itself are
  build unit 23 (DESIGN section 10); only the seam and host fakes for both
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
   known, and every id is in range. There are no caps, except that basic
   food is body-only (entry 10).
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
- **Twists (unit 13).** The `stimulus` twist accepts any registry stimulus
  except `button` and `fed`, which entry 10 makes body-only. Each `gene_edit` appends a
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
- **Closed (DEVIATIONS 9).** `Storage` had no offset read, so `Lineage` read
  the whole log into RAM. `Storage::read` now takes an offset, and the log is
  walked one frame at a time.

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

## 8. Fixes from reviewing the frames

The first full set of simulator frames read wrong in five places. The fixes
changed the design in these ways:

- **Visible siblings.** `minVisibleDelta` alone let eggs differ by a shade no
  one could see. The forced Look change now steps the skin's hue or value by
  24 to 40 inside the band, and each egg in a clutch takes its own slot (hue
  up, value down, hue down, value up, rotated by the parent's hash), carried
  by `MutationPolicy::lookSlot`. The skin gene can no longer be deleted or
  put to sleep, and the egg's jelly follows the skin tint, so the clutch
  choice shows each child's colour. `test_look` holds it: siblings differ by
  at least 1957 rendered pixels over 16 parents, and every child differs
  from its parent. The founder's skin hue moved about 11 degrees cooler to
  centre it in its band.
- **Eating reads.** Eat chews for 15 ticks after the bite instead of ending on
  it, and the renderer draws the bitten fly at his mouth.
- **Items and his body.** The founder places the marble away from him, he
  noses it on when he chases it, items sort by depth, and an item inside
  his footprint is hidden under him.
- **The hop reads as a leap** with a contact shadow on the floor. film.py
  fails the hop golden if it matches idle.
- **The time-unknown marquee** runs rim to rim above his head, each row to
  its chord, instead of a box over his legs.

A rotten bite used to draw the fresh fly, because `present()` did not say
which kind he bit. `Body::mouth` (a `Mouthful`: nothing, pellet or rotten
pellet) is set at the bite and empties when the chew ends or the eat pose is
left. `Appearance::mouth` replaces `eating`, so the bite shows on the frame it
leaves the dish. The keepsake's Body chunk carries it as one trailing byte,
written only while he holds a bite. Every body format 1 could hold encodes
as before, and a reader that predates it ignores the byte, so `kFormatVersion`
stays 1.

## 9. Memory gaps the engine verification found

An independent verification of `engine` (stack frames measured with the
ESP32-S3 compiler, heap counted on the host) found that DESIGN section 8's
budget left out every stack and guessed the save transient. Fixed in the
fix-up series:

- **Stack.** `Snapshot` (10,424 B) and `Creature` (10,196 B) were held by
  value, so `Keepsake::load` took 52,432 B of stack and the Dish constructor,
  save, hash, hatch and `SNAPSHOT` about 10.4 KB each, against the loop
  task's 8,192 B. The Dish now owns one `Snapshot` that the keepsake encodes,
  hashes and decodes in place, and occupants are built in the variant.
  `tools/stack_check.sh` fails any frame over 2,560 B and runs before the
  native suite.
- **Lineage.** The log was read whole on every boot, death and phone read,
  and compaction had no caller. It now streams one frame at a time through a
  ranged `Storage::read`. The append that passes 192 KB compacts it, keeping
  the newest Checkpoint and Rename of each generation.
- **Save transient.** A creature's keepsake is about 9.5 KB, not the 1.8 KB
  an egg's is, and encoding it held about 53 KB of heap (eleven chunk
  buffers, a doubling payload and two whole copies for the header and the
  CRC). Encode now sizes the blob in a counting pass and writes it once, the
  CRC runs over it in place, decode reads chunks as views, and load holds one
  slot at a time. The engine's heap peak through a life, a reboot and the
  phone's reads went from 53,737 B to 16,879 B (measured on x86-64).
- **PSRAM, still open.** The `badge128` env sets `-DBOARD_HAS_PSRAM` and quad
  PSRAM, while DESIGN section 8 assumes none. The budget now holds without
  it. Unit 20 reads `ESP.getPsramSize()` off a real board and corrects
  whichever side is wrong.

## 10. User-directed: the phone brings only special food (ask 15)

The verification of `engine` (finding 9) found that entry 4's "no caps"
let the phone feed him: `TWIST stimulus button` dropped a pellet and
`stimulus fed` gave a meal's satiety. That contradicted DESIGN sections 4
and 11 and ask 4. The user chose "Only special food" (ask 15): basic food is
body-only.

- `TWIST stimulus button` and `TWIST stimulus fed`, by name or number, are
  refused `403 BODY_ONLY` and change nothing. Every other stimulus still
  fires, so the rest of entry 4 stands.
- The phone delivers only special treats that body feeding cannot. Each is a
  row in `defs/twists.def` whose effect differs from a pellet. The seed is
  `prophecy_treat`, a treat baked from the phone's futures: it raises the
  vision chemical by half, so his receptor genes light the foresee glow,
  and it adds no food and fires no `fed`. Camera-colour and weather treats
  are future rows.
- Tests: `Twist.ThePhoneCannotDeliverPlainFood` and
  `Twist.AProphecyTreatMakesHimGlowAndDoesNotFeedHim` in `test_protocol`.

## 11. User-directed: thoughts, prophecies and his voice (asks 18 and 19)

He says a line now and then, scrolled once across the dish. A shake may
bring a prophecy instead of a hop. What he foresees and how he says it are
genetic. `lib/blorb/include/blorb/thoughts.h` is the entry point.

- **Registries.** `defs/thoughts.def` holds every line. A row with topic
  `none` is a thought; a row with a topic (`defs/topics.def`) is a
  prophecy. Each row names a predicate over what anyone could see of him,
  plus a priority, a cooldown and its text. `defs/voices.def` holds the
  voices and the frames each one wraps a line in. `defs/thought_lines.def`
  holds a voice's own words for a row, which replace the frame. That last
  table is data only, so a generated table (the `dialogue` branch,
  `tools/dialogue/`) is a file swap.
- **The oracle gene** (`OracleGene`, type 0x14, class Mind): the chance a
  shake foretells, the voice (its byte taken modulo the voices) and a weight
  per topic. It mutates like any gene, so siblings can differ. A genome
  without one keeps grungo's species default (`Phenotype::Oracle`), so a
  lineage older than the gene still prophesies.
- **Presentation, not life.** The `Thinker` lives in the Dish beside the
  occupant. It is never saved or hashed, and it draws from its own seed
  (genome hash and tick), never the creature's rng. A replay is
  bit-identical with or without it, and a reboot starts it afresh. It runs
  only while wall time is known, so "TAP ME WITH YOUR PHONE" always wins.
- **Pacing.** A hatchling says nothing for its first three minutes. After
  each thought comes a quiet gap of one to three minutes. Lines of priority
  `kUrgentThought` and up, and prophecies, interject during the gap without
  restarting it.
- **Prophecy versus hop is presentation only.** On a prophecy, `show()` sets
  the foresee face and glow and hides the shake's hop. The body still ran
  the hop reflex, so `LifeStats::hops` counts it. Making the engine skip the
  reflex would mean the Thinker writing creature state, which the
  presentation seam forbids.
- **Face.** The renderer measures the rows every face patch and eye halo can
  cover where he stands. When those rows reach the band above his head, the
  line runs in a low band above the care hint instead
  (`Thought.NeverCoversHisFaceAnywhereInTheDish` in `test_paint`).
