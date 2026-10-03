# blorbarium creature engine, final design

Grungo lives in a round dish on an ESP32-S3. His genome is the only description of him. A small integer engine interprets it into chemistry, a legible learning brain, a look and a temperament. Each life is a run. When he dies he leaves remains, then a clutch of one to three mutated eggs that the owner chooses between with the body alone. The lineage log is the meta-progression: feats earned by past lives unlock bigger clutches, wilder mutation and feat-gated genes, and what a parent learned can be born as an instinct in the child.

Base: candidate B (the user's pick). Grafts from A and C are listed in `SYNTHESIS.md`. Every header here compiles with `g++ -std=c++17 -fsyntax-only -Wall -Wextra -Werror=narrowing` in WSL `survivor` (g++ 16.2.1), and so does `test/usage_check.cpp`, which is the usage sketch below as code. Run `syntax_check.sh`.

## 1. Usage (caller's view)

### The firmware loop

`src/main.cpp` is the only file that includes Arduino, LovyanGFX or NimBLE.

```cpp
static LittleFsStorage store;                       // src/hw/storage_littlefs.h, the keepsake partition
static NusLink nus;                                 // src/hw/link_nus.h, ble.h minus the owner token
static blorb::Dish dish(store, speciesSeedFromMac(), lineageIdFromMac());
static paint::Canvas240 canvas;                     // 115,200 B, internal RAM on the 1.28

void loop() {
  uint32_t now = millis();
  if (now - lastSample >= blorb::kSampleMs) { dish.sample(readImu(), now); lastSample = now; }  // 50 Hz
  dish.tick(now, nus);                              // runs due 100 ms ticks, handles lines, saves
  if (now - lastFrame >= 40) {                      // 25 fps
    paint::draw(dish.appearance(), grungoPack(), canvas);
    pushToPanel(canvas);                            // board_*.h boardPresent
    lastFrame = now;
  }
}
```

Five calls: the constructor, `sample`, `tick`, `appearance`, `flush`. Time is an argument and the IMU is data, so the engine has no clock or IMU seam.

### A host test (`pio test -e native` in WSL, gtest)

```cpp
TEST(Reflex, ShakeMakesHimHop) {                    // user ask 8
  MemStorage store; NullLink link; uint32_t ms = 0;
  blorb::Dish dish(store, /*speciesSeed=*/7, /*lineageId=*/1);
  hatchNow(dish, link, ms);
  for (int i = 0; i < 6; ++i, ms += 80) { dish.sample(jolt(i % 2 == 0), ms); dish.tick(ms, link); }
  runFor(dish, link, ms, 300);
  auto a = dish.appearance();
  EXPECT_TRUE(a.reflexActive);
  EXPECT_EQ(a.reflex, blorb::reflex::hop);
  EXPECT_EQ(a.expression, blorb::expr::alarmed);
}

TEST(Heredity, EveryEggReplaysAndDiffers) {
  for (uint64_t seed = 0; seed < 10000; ++seed) {
    auto rng = blorb::Rng::seeded(seed);
    auto o = blorb::mutate(parent, blorb::policyOf(parent, {}), {}, rng);
    EXPECT_EQ(blorb::apply(parent, o.diff)->bytes(), o.genome.bytes());
    EXPECT_TRUE(blorb::viability(o.genome).ok);
    EXPECT_GE(lookChanges(o.diff), 1); EXPECT_GE(mindChanges(o.diff), 1);
  }
}
```

### The phone (the wire, not the website)

```
> #1a HELLO
< #1a OK fw=0.3.0 fmt=1 lineage=9f31c2d04a7b gen=5 name=Grungo_V phase=creature feats=0x0b
> #1b DIFF 5
< #1b + palette #12 (cloak) hue +31, brown -> moss
< #1b + stimulus #9 (shake) woke up: loves being shaken
< #1b + instinct #41 added: cradled + curl lowers fear (learned by gen 4)
< #1b OK 3
> #1c STIM petted
< #1c OK
> #1d RESTORE 15872 9a1c33f0
< #1d ERR 428 NEEDS_CONSENT window=20
< ! CONSENT granted                                 (the owner held BOOT)
> #1e RESTORE 15872 9a1c33f0
< #1e OK send
```

## 2. Module and file map

The engine and the renderer are PlatformIO libraries, so the firmware env and the `native` test env both link them with no source filters.

```
blorbarium/
  platformio.ini                 envs: native (tests), sim (SDL, WSL), badge128, badge146
  lib/blorb/include/blorb/       PURE C++17, integer only. No Arduino, LovyanGFX, NimBLE.
    fixed.h        Fx (Q8.24 saturating), Q15 storage, Decay, Rng (xoshiro128**), fnv1a, crc32
    ids.h          branded ids, Stage, cadence (10 Hz tick, 50 Hz sample), drive n = chem 1+n
    defs/*.def     13 registries: chemicals drives loci stimuli senses actions poses expressions
                   regions reflexes care gene_kinds feats commands      <- "one row" lives here
    registry.h     expands the defs: constants, tables, counts, FEATURES, compile-time id checks
    genome.h       GeneHeader (uid, featGate), Genome, GenomeBuilder, expressedAt, starterGenome
    genes.h        gene bodies, Tint, GENE_TYPES (from gene_kinds.def), expressStage
    mutate.h       ops by uid, MutationPolicy, mutate, apply, viability (with the dry run)
    chemistry.h    chem[256], locus[256], ChemRules, step, stepCoarse
    senses.h       BodySample, PetClock, SenseOut, detector structs, Detectors (from senses.def)
    habitat.h      pantry, pellets, marble
    brain.h        W[feature][action][drive], think, dream, beliefs, remap by stable ids
    actions.h      Body, ActiveReflex, behaviours, Behaviours (from actions.def)
    creature.h     Phenotype, Creature, Egg, Clutch, Occupant = variant<Egg, Creature, Clutch>
    lineage.h      the log, feats, unlocks, describeDiff
    appearance.h   Appearance, present, portrait                <- the presentation seam
    keepsake.h     snapshot TLV codec, two slots, SavePolicy
    protocol.h     framing, Reply, COMMANDS (from commands.def), Protocol
    seams.h        Storage, Link
    dish.h         Dish: the facade
  lib/blorb/src/   one .cpp per header, plus starter_genome.cpp
  lib/paint/include/paint/sprite_pack.h   SpritePack, FrameRef with eye anchors, draw, placeholderPack
  lib/paint/src/   draw.cpp (composite, halo, hop motion), placeholder_pack.cpp
  src/main.cpp     the loop above
  src/board/       display.h, board_lcd128.h, board_lcd146.h (+ Panel_SPD2010.*), copied as-is
  src/hw/          imu_qmi8658.h (orient.h trimmed to raw reads), storage_littlefs.h, link_nus.h
  pets/grungo/     art sources and the generated grungo_pack.h
  tools/           sprite_pack.py (converter), merge_image.py, pick_port.py, fw_stamp.py, partitions_16mb.csv
  sim/             Arduino.h, Wire.h, esp_mac.h, board_sim.h, find_sdl.py, sim.cpp (from claude-notification-screen)
  test/            test_<module>/ gtest suites, support/ (MemStorage, NullLink, ScriptedOwner), fixtures/, golden/, film.py, feeds/
```

Any question crosses at most three files. "Why did he curl up?" is `dish.h`, `creature.h`, `brain.h`. "What does a knock do?" is `defs/stimuli.def`, `creature.cpp::applyStimulus`, and the genome's stimulus genes.

## 3. Shape

### Core data types

- **Fx.** Q8.24, saturating, the only arithmetic type. A drive climbing 0 to 1 over 6 hours at 10 Hz moves 4.6e-6 per tick, which is below one LSB of B's Q16 and of C's Q16.16 (1.5e-5) and 77 LSB here (arithmetic). The brain's weights sit in RAM and on flash as Q15. No float anywhere, so host, device and website replay bit for bit.
- **Genome.** Genes of `[type][len][flags][stage][featGate][mutWeight][uid u16][body]`. Every body byte is legal. `uid` is the gene's identity across generations. `featGate` is the roguelite unlock: a gene expresses only once the lineage has earned that feat. Immutable for one life.
- **Registries.** Thirteen `.def` files. Each row expands into a typed constant, a table row the engine iterates and `SCHEMA` streams, and a derived count. Brain dimensions (20 features x 11 actions x 8 drives, measured from the rows), the feature vector and the recent-stimulus loci are derived, never listed twice.
- **Chemistry.** 256 Fx chemicals, 256 Fx loci, rules built by genes. The engine hard-codes one fact: drive n is chemical 1+n.
- **Loci** are the bus. Sense loci are written by detectors, the habitat and behaviours. Act loci are written by receptor genes and read by code: stage changes, death, startle and flinch, arousal, glow, size, hue shift, sleep gate.
- **Brain.** `W[f][a][d]`: when feature f is on and I do action a, drive d changes by this much. Plus habituation, an eligibility trace, 16 episodes for dreams, an instinct queue.
- **Occupant** = `variant<Egg, Creature, Clutch>`. "Dead with no egg yet" is a Clutch in its vigil; "no pet" cannot exist.

### Tick order

`Dish::sample` runs the detectors at 50 Hz into a pending `SenseOut`. `Dish::tick` runs whole 100 ms ticks (at most 10 per call). Each tick, in this order and no other:

1. `PetClock::advance`, then the tick-rate detectors (day, owner) add to `SenseOut`.
2. The habitat steps (refill, rot, roll by tilt) and writes its loci and stimuli. While the occupant is a Creature, a `button` stimulus drops a pellet; in a Clutch it moves the egg cursor instead.
3. The occupant ticks. For a Creature: sense loci in; each stimulus through this creature's stimulus genes onto chemicals, recent locus set to 1, `whenAsleep` gate; `Chemistry::step`; lifecycle (stage loci express the next stage once, the die locus ends the life, a reflex locus rising past 0.5 starts a reflex); brain on even ticks; behaviour step unless a reflex owns the body; face, stats, recent loci halve. For an Egg: incubate. For a Clutch: vigil, one preview per tick, body choice.
4. The protocol handles inbound lines and pumps events. `SavePolicy` decides.

### Senses become stimuli

Hardware thresholds are constants in `senses.cpp`, taken from the measured `orient.h`: a shake is 4 jolts of at least 0.55 g within 900 ms, at least 60 ms apart, which is why detectors sample at 50 Hz, not at the 10 Hz tick. What a knock means to this creature is its stimulus gene, so one firmware with two genomes gives two temperaments.

### Drives, reward, learning, instincts, sleep, dreaming

Drives are chemicals with half-lives, so they move whether anyone looks. Tonic production is an emitter on the `always` locus. Reward is the observed fall of a pressing drive over an action, so the same action is good when a drive is high and worthless when it is low. Decide: `score[a] = sum_d drive[d] * (-sum_f feat[f] * W[f][a][d]) - habit[a] + noise * (explore + arousal)`, argmax, held for `minTicks`. Learn: `W += learnRate * startFeat * trace * (observed - predicted)`. Instinct genes are `(cues, action, drive, level)`, queued when they switch on and replayed in dreams through the same update, which is also how the hatch burst gives a newborn reflexes before its first decision.

Sleep is an action. At pet night an emitter on low `light` makes melatonin, which reacts into sleepiness, and a `sleep_gate` receptor keeps Sleep running until it falls. Laying him face down (lid) makes `light` 0, so the owner can put him to bed with no phone. Asleep, the brain only dreams: one instinct or episode per dream tick, then `W` decays by `forgetRate`. The presentation shows the dream.

### Shake, startle and the hop (ask 8)

A shake fires `stim::shake`. His stimulus gene adds adrenaline (sensitivity is that amount) and discomfort. A receptor gene on adrenaline writes the `startle` locus (its gain is jump height). When `startle` rises past 0.5 the creature starts the `hop` reflex: 12 ticks of alarmed face and a procedural squash, leap and land, with height from the locus level, overriding his action. Adrenaline decays slowly, so shaking again while it is high gives no new edge: he does not hop on every shake, he gets rattled. The discomfort rise is punishment, so the brain learns what precedes shaking and which actions (curl, flee) lower fear afterwards. Repeated shaking stresses him through the same chemistry. All of it is genes, so a descendant can be bouncier, braver, or one who loves being shaken (a dormant stimulus gene in the starter genome).

### Life cycle as a roguelite

Life is chemical 16, seeded by a chem gene and decaying by its half-life. Receptors on the stage loci fire as it falls, and `expressStage` adds the next stage's genes (new faces, instincts, a bigger size). Death is the `die` locus, reached by receptor genes on low Life or sustained high injury. The `cause` locus records which.

At death the Dish records the Death entry (stats and feats), then `Creature::layClutch` captures up to `heirloomMax` strongest beliefs and draws the clutch seeds from the creature's rng, and the Dish saves. The Clutch shows the remains for a 30-minute vigil while it derives one egg preview per tick. Then the eggs appear. Clutch size is `unlocksFor(legacyFeats).clutchSize`, 1 to 3. Button or knock moves the cursor, a button hold or double knock picks, and after 30 minutes the cursor egg is picked, so an untended lineage continues. With one egg there is no choice.

What carries across a run, all through the genome:

1. Mutation: point, wild (a field rerolled across its range), duplication, deletion, dormant genes waking or sleeping, at rates from the genome's own mutation policy plus a feat bonus.
2. Heirlooms: the parent's strongest beliefs become instinct genes flagged `Heirloom`, shown as "learned by gen 4".
3. Unlocks: feats earned by any ancestor (`reached_elder`, `well_fed`, `fifth_generation`) raise clutch size and wild mutation, and wake feat-gated genes.
4. The note gene: an inherited motto the owner can edit.

Every egg has at least one Look change of at least `minVisibleDelta` and at least one Mind change, so each egg looks and acts different. `viability()` checks the static shape and runs a chemistry-only dry run of 48 pet-hours, so a lethal mutation never hatches. `mutate` retries from the same stream, so the result stays deterministic per seed.

## 4. Body-only and phone-only interactions (ask 4)

| Loop | Body only, no phone | Phone adds |
|---|---|---|
| Feeding | Press BOOT: a pellet drops from the pantry (refills on the pet clock). Tilt rolls pellets to him; he learns to eat. Old pellets rot and make him ill. | Read hunger and pantry. Cannot feed. |
| Comfort | Hold him still and upright (cradle). Righting him after a flip. | `STIM petted`, `STIM spoken_to` |
| Play | Knock and double knock; tilt to roll the marble, he chases it; shake (a hop, rough play, his genes decide if he likes it) | `STIM played` |
| Sleep | Pet night on his own clock; lay him face down to tuck him in; a long dark stretch entrains his night to yours | `TIME` snaps his day to the real one |
| Ageing | Life chemical and stage receptors | Stage, age, drives, chemicals, beliefs |
| Death | Automatic, remains shown for the vigil | Cause and the life summary |
| Choosing the next egg | Button or knock = next, hold or double knock = pick, timeout picks | `CLUTCH` previews, `PICK i` |
| Hatching | Hold him (warmth) to hatch faster, knock to wobble, or wait | Preview of who is inside |
| Learning the toy | The care hint shows the gesture for his most pressing need | none needed |
| Identity | Species seed from the MAC; name inherited ("Grungo V") | `NAME`, `EDIT` of owner-editable look genes, the note gene |
| Lineage | Generation shown on the rim at hatch | Family tree, per-generation diffs, ancestor portraits |
| Keepsake | Saves itself | `BACKUP`; `RESTORE` only with a button-hold consent |

The host test `body_only_full_life` runs hatch, every stage, death, a clutch pick and the next hatch with no Link ever connected.

## 5. Grungo's presentation seam

`present(occupant, habitat, clock, tick) -> Appearance` is pure and fixed-size. `paint::draw(Appearance, SpritePack, Canvas240)` is pure too, so the sim's frames are byte-identical. The engine says what he feels and does; the pack owns timing, transitions and motion.

- **Expressions with intensity.** Expression genes score faces over the drive mix; the creature keeps the winner with hysteresis. `Appearance` carries `expression`, `intensity`, `previous` and `exprTicks`. The pack shows neutral below its threshold, crossfades the registered face patch from `previous` (replacing the badge's glitch), and spends intensity on hold time and bob amplitude. Rows follow grungo's ten patches: neutral, happy, alarmed, annoyed, croak, blep, foresee, sleepy, asleep, yawn.
- **Startle and the shake hop.** `reflexActive`, `reflex`, `reflexPhase`, `reflexStrength`. Hop: phase 0 to 0.2 squash, 0.2 to 0.7 leap (height = strength x the pack's max), 0.7 to 1 land and settle. Face forced to alarmed. Flinch: hood up (curl pose) for 6 ticks.
- **The foresee glow (ask 7).** Every `FrameRef` (body and face patch) carries per-frame eye anchors (centre and radius), emitted by the converter from the eye mask, so the halo tracks the eyes through the hop, the bob, scaling and a face that moves them. `draw` composites a teal ring around each eye, tinted by the glow region's genetic tint, radius r x (1.5 + 0.5 x glow), pulsing at 1.25 Hz, with up to six orbiting sparks once glow passes 0.6. While the Foresee action runs, glow is at least `kForeseeGlowFloor` (0.6), whatever the genes say: genes can brighten and tint it, never hide it.
- **Genetic palette regions.** The converter tags every palette entry with a region (skin, belly, cloak, eye, mouth, glow, shell; outline is invariant). Palette genes give a `Tint` per region relative to the authored colours, so shading ramps survive; `RegionBand` per region in the pack clamps it so the cloak stays a cloak colour. A recolour is one 256-entry LUT rebuild per life. Mark genes pick overlay variants (mottling density, antenna length), taken modulo what the pack has.
- **Stage art.** `Kind::Egg` draws `egg(progress)` (whole, cracking, hatching). `Kind::Creature` with Baby or Child draws the hatchling (until that art exists, the adult scaled to about 55 percent with a bigger-head squash), Adult the adult, Elder the old frog. `Kind::Remains` draws the end of a run, cosy rather than grim. `Kind::Clutch` draws up to three eggs tinted from their previews with a cursor.
- **Renderer-owned idle life.** Blink (a flick to `asleep` for about 120 ms every few seconds, jittered by `lifeSeed`), breathing bob, the occasional yawn, the pantry pips on the rim, the care-hint glyph (the knock and button glyphs from the old `widgets.h`).

## 6. Persistence, lineage storage and the BLE protocol

### Keepsake

LittleFS on a dedicated `keepsake` partition in the unused upper 8 MB of the 16 MB flash (custom `partitions_16mb.csv`). `merge_image.py` leaves it unwritten, so a cable flash never touches him, and OTA's app slots never overlap it. It mounts with format-on-fail off.

- `snap.a` and `snap.b`: header (magic, format, seq, length, CRC-32) then TLV chunks with per-chunk versions. Written alternately through write-temp-then-rename; load takes the highest valid seq.
- Unknown chunk tags are carried and re-emitted. A newer format is never overwritten (Boot `ReadOnlyNewer`). Unreadable slots are renamed into `rescue/`, never deleted.
- If both slots are unreadable, the Dish rebuilds the latest genome from `lineage.log` and boots an egg of it (Boot `FromLineage`). The line survives the loss of a life.
- Not saved, because derived: the phenotype (re-expressed at load), detector state, clutch previews, the face crossfade.
- Saves every 5 minutes if anything changed, and at once on stage change, death, pick, hatch, rename, edit, `TIME` and disconnect. No shutdown save: phone power vanishes without warning, so a mid-interval unplug rewinds at most 5 minutes of ordinary life.
- The brain chunk stores its axes as stable ids. A firmware that adds a feature, action or drive remaps on load and keeps every learned weight.
- Unpowered time is stasis (open question 5).

### Lineage storage

`lineage.log` is append-only, one CRC per entry, and a torn tail is truncated at open. Entries: Founding (the full genome), Birth (the chosen egg's diff by uid, the clutch seeds and `mutateVersion`, so a sibling "road not taken" can be previewed later), Checkpoint (a full genome every 8 generations), Death (cause, stats, feats, name), Rename. Appends are idempotent by entry kind and generation, so a crash between the log and the snapshot converges. RAM holds only the current generation, name and feat set; the phone's reads stream from flash. Compaction at 192 KB keeps every Death summary and drops diffs older than 32 generations.

Cost (estimate): a Birth is about 30 B plus 6 to 10 B per op, a Death about 90 B, so about 200 B per generation plus a 3 to 8 KB checkpoint every 8. At a generation a week that is under 50 KB in ten years.

### BLE protocol

Nordic UART Service from `ble.h` (TX +21 dBm, 6 s supervision timeout, 30 to 50 ms interval, name `blorb-xxxx` in the scan response), without the owner-token scheme. Newline ASCII so nRF Connect can drive it.

- Requests `#<id> VERB args`; replies `#<id> + ...` continuation lines then `#<id> OK` or `#<id> ERR code text`; events `! NAME k=v` after `SUB`.
- Lines at most 200 bytes, one notification at MTU 247. Overlong input is an error, not a truncation.
- Binary (genome, backup) as base64 `+ <offset> <b64>` lines closed by `OK <len> <crc32>`.
- Verbs (`defs/commands.def`): HELLO, SCHEMA, STATE, CHEM, GENOME, GENE <uid>, EDIT <uid> <hex>, NAME, STIM, BRAIN, LINEAGE, ANCESTOR, DIFF, PORTRAIT, CLUTCH, PICK, TIME, SUB, BACKUP, RESTORE, HASH.
- `SCHEMA` streams every registry, including gene kinds with their byte rules, so the website renders new chemicals, senses, actions and gene kinds with no website release.
- Feeding is not a verb. `STIM` fires only Phone-source stimuli. `EDIT` touches only OwnerEditable genes and records the edit in the lineage.
- `RESTORE` needs physical consent: `ERR 428 NEEDS_CONSENT`, then a button hold on the device within 20 s, single use.
- Over-the-air firmware stays out of v1. If it comes back, it is a consent verb too.

## 7. Extension table

Each later idea and the exact files it touches. "Starter genome" is `lib/blorb/src/starter_genome.cpp`.

| Idea | Files touched | Engine code changed? |
|---|---|---|
| New sense (soft, from existing samples: "rocked", "spun") | `defs/senses.def` (row); `senses.h` (detector struct); `src/senses.cpp` (its `sample`/`tick`); `defs/loci.def` (row, if continuous); `defs/stimuli.def` (row, if it fires); starter genome (emitter or stimulus genes); `test/test_senses/` (a trace) | No |
| New sense (new hardware: gyro, touch stroke) | the above, plus a `BodySample` field in `senses.h` and the reader in `src/hw/imu_qmi8658.h` | One struct field |
| New chemical | `defs/chemicals.def` (row, a name for the phone; optional); starter genome (half-life, reactions, receptors) | No |
| New drive | `defs/drives.def` (row); starter genome (stimulus, receptor, expression, instinct genes); `defs/care.def` (row, if the body can relieve it) | No; brain dimensions and the save remap derive |
| New action | `defs/actions.def` (row); `actions.h` (behaviour struct); `src/actions.cpp` (its step); `defs/poses.def` (row, if a new pose); pack art (optional, falls back to idle); `defs/stimuli.def` (Self row, if it self-stimulates); starter genome (instincts) | No; dispatch is generated |
| New gene kind | `defs/gene_kinds.def` (row); `genes.h` (body struct); `src/genes.cpp` (`express_`, `describe_`, `rules_`); `creature.h` (a Phenotype field, only if it builds something new) | Only that field |
| New organ | `defs/gene_kinds.def` (an organ container kind); `genes.h`, `src/genes.cpp`; `chemistry.h` (an organ tag on rules, an organ list in `ChemRules`); `src/chemistry.cpp` (energy cost and stall) | Yes, the one idea that is real engine work |
| New phone verb | `defs/commands.def` (row); `src/protocol.cpp` (`cmd_<name>`) | No |
| New phone twist (prophecy, weather, camera colour; DEVIATIONS.md 4) | `defs/twists.def` (row); `src/twists.cpp` (`twist_<name>`, which parses its args and applies them to the live state) | No |
| New game or activity on the device | `defs/games.def` (row: id, name, struct); a struct beside the habitat with `step(input, habitat, SenseOut&)` that reads detector loci and stimuli (tilt, knock, shake, button), moves its own pieces and fires its outcomes as World stimuli; `defs/stimuli.def` (its outcome rows, such as `marble_hit`); `defs/actions.def` and `actions.h` (a play action, if he plays it himself); starter genome (stimulus genes saying what winning or losing does to his drives, and instincts); `lib/paint/src/draw.cpp` (its pieces). Play reaches drives and learning only through the stimulus path, so the brain learns a game like anything else. The tilt-rolled marble and the shake hop are the seeds: the marble moves out of `Habitat` into the first `games.def` row when a second game arrives | No; the Dish steps every row each tick |
| New face | `defs/expressions.def` (row); pack face patch (falls back to neutral); starter genome (an expression gene) | No |
| New look | recolour: genes only. New region: `defs/regions.def` (row) plus the converter's tagger and the pack's band. New overlay: pack art for a mark layer, mark genes in the starter genome | No |
| New reflex | `defs/reflexes.def` (row); `defs/loci.def` (its Act locus); starter genome (receptor genes); `lib/paint/src/draw.cpp` (its motion) | No |
| New feat | `defs/feats.def` (row); `src/lineage.cpp` (`feat_<name>`, and `unlocksFor` if it unlocks something) | No; old saves gain it |
| New board | `src/board/board_*.h`; an env in `platformio.ini`; `src/hw/` readers | No |

## 8. Memory and compute budgets (ESP32-S3, 1.28 board, PSRAM assumed absent)

Labels: **measured** (where), **arithmetic**, or **estimate**.

| Item | Size | Label |
|---|---|---|
| Internal DRAM the linker reports | 327,680 B | measured (PIO build of claude-notification-screen's badge env) |
| Static RAM of that sibling firmware with BLE | 49,112 B | measured (same build); blorbarium's will differ |
| Canvas 240x240 RGB565 | 115,200 B | arithmetic; `sizeof(Canvas240)` measured |
| `Dish` object (holds Creature, Brain, Chemistry, Habitat, detectors) | 10,960 B | measured, `sizeof` on x86-64; LX7 within a few percent (fixed-width members) |
| of which `Brain` / `Chemistry` / `ChemRules` arrays | 5,576 / 2,048 / 2,120 B | measured, x86-64 |
| Heap: genome (cap 8 KB, starter about 3 KB) | up to 8 KB | estimate |
| Heap: phenotype rule vectors (about 60 reactions, 120 emitters and receptors) | about 9 KB | estimate |
| Transient at death: one child genome and diff, plus the dry run's rules | about 20 KB, one egg per tick | estimate |
| Snapshot encode buffer | 10 to 16 KB, during a save | estimate |
| NimBLE host heap | 40 to 70 KB | estimate |
| LittleFS caches | about 10 KB | estimate |
| Lineage in RAM | under 100 B | by design |
| **Peak internal use** | **about 290 KB of 327 KB** | estimate; to be measured in build unit 20 |
| Sprite art (grungo body plus ten face patches, 8 bpp, RLE) | under 70 KB of flash, no RAM | estimate from grungo.md |

The margin is thin. Build unit 20 logs the minimum free heap across a boot, a save, a death and a clutch pick, and fails under 24 KB. The lever if it is short is to draw the canvas in 48-row bands (23 KB instead of 115 KB). The 1.46 board puts the canvas in its 8 MB PSRAM.

Compute, all **estimates** at 240 MHz: chemistry about 300 rules per tick at 10 Hz is about 0.12 M cycles/s; the brain about 4k multiply-adds per think at 5 Hz is about 0.2 M cycles/s; detectors at 50 Hz about 0.1 M cycles/s. Under 0.3 percent of one core. A viability dry run (576 coarse steps) is about 30 ms, one per tick during the vigil. Drawing is the cost: compositing about 15 cycles a pixel at 25 fps is about 9 percent of a core, and the SPI push at the board's 40 MHz is 23 ms a frame (arithmetic), so the bus is busy about 58 percent of the time at 25 fps. That is why the target is 25 fps, not 30.

## 9. Test plan

### Deterministic engine tests: `pio test -e native` in WSL `survivor`

gtest, seeded, no hardware, no wall clock. `test/support/` holds MemStorage (with fault injection at every byte of a write), NullLink, a scripted Link, and ScriptedOwner (a body-only caretaker that drops pellets, cradles, tucks in).

| Suite | Asserts | Defect it catches |
|---|---|---|
| fixed | saturation, Q15 round trip, a half-life byte halves in its tick count, closed-form decay equals stepping, the xoshiro golden sequence, crc32 test vector | wrong decay table, platform drift |
| registry | compile-time id checks (already static); `FEATURES` order; recent-locus mapping; `SCHEMA` lists every row | a table drifting from the wire |
| genome | parse round trip; every byte value of every body decodes; unknown kind carried byte for byte; duplicate uid rejected; `featGate` gating | a decoder that is not total |
| chemistry | receptors on one locus sum then clamp; an act locus clears when its receptor stops; reactions conserve ratios; `stepCoarse` within epsilon of stepping | wrong order, stuck effects |
| senses | 50 Hz traces give shake, knock, double knock, held, cradle, flip, free fall, lid, button and hold; rate limits; a 10 Hz sampling of the same shake is NOT detected (the reason for 50 Hz); `PetClock` entrainment moves night by at most an hour a day | gesture regressions |
| habitat | pantry refills on the clock; a rotten pellet fires `fed_bad`; tilt rolls pellets; bite reach | feeding loop breaks |
| brain | hungry plus pellets: eat probability rises; full: it does not; habituation; an instinct makes a newborn act with no experience; the remap keeps weights when an action row is added | missing urgency weighting |
| reflex | shake gives hop with alarmed face; a second shake while adrenaline is high gives no second hop; a genome with doubled receptor gain hops higher | ask 8 regressing |
| foresee | over a long run, every tick with action foresee has glow at least the floor, including for a genome whose glow genes are zeroed | ask 7 regressing |
| lifecycle | stage advances as Life falls; death fires once; the clutch timeout picks; warmth hatches faster; `body_only_full_life` with no Link | a core loop needing the phone |
| mutate | `apply(parent, diff) == child` for 10k seeds; at least one Look and one Mind change per egg; same seed gives the same child; viability rejects a planted lethal (Life half-life byte 1) | unreplayable eggs, dead lineages |
| lineage | founding plus 40 births rebuild every generation's hash; Birth and Death twice count once; torn tail truncated; compaction keeps summaries | lost or doubled history |
| keepsake | encode/decode byte-identical; torn slot B loads A; both torn boots `FromLineage`; newer format is read-only and untouched; unknown chunk survives a save; `keepsake_v1.bin` fixture loads forever | lost pets |
| protocol | fuzzed lines never crash; 201-byte line is ERR; `STIM fed` refused; `RESTORE` without consent refused, with consent accepted once; golden transcripts | protocol drift from the website |
| present | struct goldens for idle, eating, foresee, hop, sleep, egg, clutch, remains | the seam leaking or drifting |
| replay | same genome, seed and script: the same `Dish` hash after 24 simulated hours; the hash is committed and the device prints it from `HASH` for the cross-platform check | non-determinism |

### The simulator and frame goldens (ask 9)

Copy `sim/` from `D:\Projects\claude-notification-screen` (`Arduino.h`, `Wire.h`, `esp_mac.h`, `board_sim.h`, `find_sdl.py`, `sim.cpp`) and `tests/film.py`. `sim.cpp` includes `../src/main.cpp`, so the sim runs the firmware loop, with `paint::draw` and LovyanGFX 1.2.29 pinned as today.

- **Input.** Extend the fake QMI8658 in `Wire.h`: script lines `!shake`, `!knock`, `!dtap`, `!tilt x y`, `!flip`, `!lid`, `!hold <ms>`, `!button <ms>` produce the accelerometer and tap registers the real detectors read at 50 Hz.
- **Debug verbs** live in `sim.cpp`, not in the engine or the wire: `@<ms> DEBUG warp <ticks>` (fast-forward on the fixed clock), `DEBUG inject <chem> <level>` (calls `Creature::inject`), `DEBUG force <action>`, `DEBUG die`.
- **Shots.** `--headless --clock fixed --script tests/feeds/<state>.txt --shot out.bmp --after <ms>`; Pillow converts to PNG. The fixed clock, the seeded engine, integer arithmetic and the pure `draw` make two runs byte-identical, as the old sim already proved (sha256 match in `explore-device.md`).
- **Goldens.** `tests/film.py` keeps its tolerance (24 per channel, 0.2 percent of the disc), writes `.actual.png` and `.diff.png` on a miss, and `UPDATE_GOLDEN=1` blesses. Cases: idle, eating, foresee_glow, shake_hop (mid-leap), sleep, egg, hatch, plus clutch and remains. The mp4 step is optional, since WSL has no ffmpeg; it can call the Windows ffmpeg through interop.
- **Run** from Windows: `MSYS_NO_PATHCONV=1 wsl -d survivor --exec bash tools/sim_film.sh`.
- Before the grungo converter lands, the goldens use `placeholderPack()` and are re-blessed once, deliberately, when the grungo pack replaces it.

## 10. Build plan

Each unit is small, names its files and ends in a check that can be run. Do not start a unit until the previous check is green. WSL checks run as `MSYS_NO_PATHCONV=1 wsl -d survivor --exec bash tools/wsl_test.sh <filter>`, which runs `pio test -e native -f <filter>` in the repo.

| # | Unit | Files | Check |
|---|---|---|---|
| 0 | Scaffold: repo layout, `platformio.ini` (native, sim, badge128, badge146; LovyanGFX 1.2.29 and NimBLE 2.x pinned), `lib/blorb` with `fixed.h`, one trivial test, `tools/wsl_test.sh`, these headers and `syntax_check.sh` copied in | `platformio.ini`, `lib/blorb/include/blorb/*`, `test/test_fixed/test_main.cpp`, `tools/wsl_test.sh` | `wsl_test.sh test_fixed` green; `syntax_check.sh` green |
| 1 | Fixed point and rng | `src/fixed.cpp`, `test/test_fixed/` | half-life, closed-form decay, xoshiro golden, crc32 vector green |
| 2 | Registries | `registry.h`, `defs/*.def`, `test/test_registry/` | `FEATURES`, recent loci, tables green |
| 3 | Genome | `src/genome.cpp`, `test/test_genome/` | parse, builder, uid, `featGate`, fuzz 100k random byte strings |
| 4 | Gene kinds and a minimal starter genome | `src/genes.cpp`, `src/starter_genome.cpp`, `test/test_genes/` | every byte value decodes; `expressStage` twice is a no-op |
| 5 | Chemistry | `src/chemistry.cpp`, `test/test_chemistry/` | order, sum-then-clamp, act loci clear, `stepCoarse` epsilon |
| 6 | Senses and the pet clock | `src/senses.cpp`, `test/test_senses/` + traces | gestures from 50 Hz traces; 10 Hz shake not detected; entrainment bound |
| 7 | Habitat | `src/habitat.cpp`, `test/test_habitat/` | pantry, rot, roll, bite |
| 8 | Brain | `src/brain.cpp`, `test/test_brain/` | learns eat-when-hungry; habituation; instinct dream; remap |
| 9 | Actions, reflexes, the creature tick | `src/actions.cpp`, `src/creature.cpp`, `test/test_creature/` | shake hop; no re-hop while rattled; foresee glow floor; stage advance |
| 10 | Mutation and viability | `src/mutate.cpp`, `test/test_mutate/` | 10k replay; variety guarantee; planted lethal rejected |
| 11 | Egg, clutch, lineage, feats | `src/lineage.cpp`, `src/clutch.cpp`, `test/test_lifecycle/` | `body_only_full_life`; clutch size from feats; idempotent records; ancestor rebuild |
| 12 | Keepsake | `src/keepsake.cpp`, `test/test_keepsake/`, `test/fixtures/keepsake_v1.bin` | torn slot, newer format, unknown chunk, `FromLineage` |
| 13 | Protocol | `src/protocol.cpp`, `test/test_protocol/` + transcripts | fuzz; consent; `STIM fed` refused; golden transcripts |
| 14 | Dish | `src/dish.cpp`, `test/test_dish/` | pacing, save policy, 24-hour replay hash committed |
| 15 | Presentation | `src/appearance.cpp`, `test/test_present/` | struct goldens for the nine states |
| 16 | Renderer and placeholder pack | `lib/paint/src/draw.cpp`, `placeholder_pack.cpp`, `test/test_paint/` | halo pixels ring the given eye anchors; hop lifts the sprite by the expected rows |
| 17 | Simulator | `sim/*`, `src/main.cpp` (sim build) | two headless runs of one feed give identical sha256 |
| 18 | Frame goldens | `tests/film.py`, `tests/feeds/*.txt`, `test/golden/*.png`, `tools/sim_film.sh` | `sim_film.sh` green on the nine states |
| 19 | Grungo pack converter | `tools/sprite_pack.py`, `pets/grungo/grungo_pack.h` | converter output is byte-stable across runs; every eye anchor falls inside the eye mask; goldens re-blessed once, reviewed as PNGs |
| 20 | Firmware on the 1.28 | `src/hw/*.h` (including `storage_nvs_fs.h`: snapshot slots `save_a`/`save_b` as keys in the labelled `pet` NVS partition, `lineage.log` on the `petfs` LittleFS, both behind the `Storage` seam), `src/main.cpp`, `tools/partitions_16mb.csv` (the layout in `explore-ota.md` section 5: nvs, otadata, two 4 MB OTA slots, `pet`, `petfs`, coredump), `pick_port.py` | `pio run -e badge128` on Windows; on the board: minimum free heap at least 24 KB through a save, a death and a pick; `HASH` after a scripted serial feed equals the host hash |
| 21 (future) | Phone-side foresight: on a visit, the website runs this engine compiled to WebAssembly. It reads the creature's `SNAPSHOT` over BLE, simulates many futures, and sends back twist ops (a `prophecy` brain update, a chosen mutation) that the board applies to its live state (DEVIATIONS.md 4) | an Emscripten build of `lib/blorb`; `Keepsake::encode`/`decode` as the one self-contained blob pair; rows in `defs/twists.def` | the WASM build and the host replay the same snapshot and script to the same `Dish` hash; a malformed twist is refused without disturbing the board |
| 22 (future, hardware) | BLE OTA: the website sends a firmware image over BLE (no Wi-Fi) into the inactive app slot, verifies it, and switches slots with bootloader rollback. Built from the survey in the design scratchpad's `explore-ota.md` (section 5: what to copy from the old badge's `ble_ota.h`, the trial logic, the web sender) | `src/hw/ble_ota.h`, OTA verbs in `defs/commands.def`, the web sender | an update over BLE boots the new firmware with the `pet` partition untouched; a corrupted image is refused and the old slot keeps running; a keepsake written by the old firmware loads in the new one through `Keepsake::migrate` (a committed fixture per format version); while a new image is on trial it writes no save in a newer format, so a rollback never meets a save it cannot read |

Unit 21 is not built yet. Two constraints keep its door open now: `lib/blorb` stays buildable by Emscripten (no threads, no platform headers, no reliance on undefined behaviour, integer determinism), and the whole snapshot crosses the wire through the `Keepsake::encode`/`decode` pair.

## 11. Tradeoffs accepted

- We accept a linear belief table instead of neurons and dendrites in exchange for a 5.6 KB brain the phone can explain and heirlooms that fall straight out of it.
- We accept Q8.24 integer arithmetic, a little clumsier to author than floats, in exchange for bit-identical replay and slow rates that never round to zero.
- We accept thirteen `.def` files, which read worse than a plain table, in exchange for one row producing the constant, the table, the count and the wire name with no way to drift.
- We accept a clutch of 1 to 3 eggs and a 30-minute vigil, more surface than B's single egg, in exchange for the roguelite choice the user asked for and selection pressure the owner controls.
- We accept a chemistry-only dry run instead of a full embryo trial: it misses lethality that needs the brain (a creature that never chooses to eat), in exchange for about 20 KB and 30 ms instead of about 41 KB and a minute of CPU.
- We accept that unpowered time is stasis, so he cannot miss you across a week in a drawer, in exchange for a toy that never needs a wall clock.
- We accept that a mid-interval unplug rewinds up to 5 minutes of ordinary life, in exchange for no fragile shutdown path; life events save at once.
- We accept that the phone cannot feed him, so the core loop stays in the body.
- We accept the hold gestures (tuck-in, consent, egg pick) colliding with the 1.46's power button (open question 2).

## 12. Alternatives considered

- **A runtime embryo trial** (A): a candidate child lives 6 sim-hours headless before the egg is laid. Strongest guarantee, but it adds a phase, ships a scripted caretaker in firmware and needs a second Creature in RAM on a board with no confirmed PSRAM. Lost to the dry run.
- **Floats** (A): simpler authoring, but replay across x86, Xtensa and JS would need care about contraction, and the state hash test would be fragile.
- **A gesture-to-stimulus rebinding table** (C): flexible, but an indirection between detectors and stimuli that nothing in v1 rebinds. Detectors fire stimuli; a touch board adds a detector.
- **Storing every ancestor's genome**: simplest to read, 3 to 8 KB per generation, and the diff, which is what the user wants to see, would still need computing.
- **An NFC-sticker auth token** (A): needs a secret printed at first boot and written to the sticker. A button hold on the device proves possession with nothing to store.

## 13. Open questions for the user

1. **Can neglect kill him, or only old age?** Recommended default: neglect makes him ill (injury rises, his life chemical runs faster, so the run is shorter and the lineage records "neglected") but he dies only of old age or sustained severe injury. Starving to death is off.
2. **The BOOT hold against the 1.46's PWR button.** On the 1.46 the only button is PWR: a 4 s hold powers off, and the board ignores the boot press until it is released. Tuck-in, consent and the egg pick are 1.2 s holds. Recommended default: pick the 1.28 and the clash is moot; on the 1.46, cap every hold at 1.5 s with a rim countdown that stops there, and make double knock the alternative for each hold.
3. **Which board?** Recommended default: the Waveshare ESP32-S3-LCD-1.28 (non-touch). Its CH343 enumerates, which matters for phone USB power; its 240x240 panel matches the art with no 1.72x upscale; and BOOT is a free button. The 1.46 brings 8 MB PSRAM and touch (no driver written yet) at the cost of the PWR clash and uneven pixel scaling.
4. **How long is one life?** Recommended default: about 10 days of powered time, with a 30-minute egg. Long enough for a life to feel like a season with a personality, short enough that the clutch and the feats come round often.
5. **Does the phone stay plugged in as a dock, or travel with your friend?** If it travels, he is unpowered most of the time. Recommended default: stasis while unplugged (he sleeps in his box). If he should live through absences, a capped catch-up after a phone `TIME` is a Dish-only change later.

## 14. Next implementation step

Build unit 0: the repo scaffold, the PlatformIO envs and one native test green in WSL, with these headers and `syntax_check.sh` copied in, so every later unit starts from a passing check.
