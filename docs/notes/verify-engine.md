# Independent verification: blorbarium `engine` at a693a0b

Verifier: read-only with respect to the branch. Nothing committed or pushed.
Checked out `origin/engine` detached at `a693a0b docs: record the fixes that came from reviewing the frames`.

## Verdict

**Not ship-ready for unit 20 (firmware on the 1.28).** It is ready for any further host-only work.

The host engine is solid. Both test environments pass exactly as claimed, the frame goldens reproduce byte for byte, and 24 of 26 deliberate breakages turned a test red. Two device-memory defects would crash or brick the badge, though. Neither is in the DESIGN budget, and DEVIATIONS mentions only half of one. They need fixing, or an explicit unit-20 gate, before anyone flashes a board.

Evidence labels used below: **measured** (a command I ran, output quoted), **arithmetic**, **estimate**, **guess**.

## Findings, by severity

### 1. CRITICAL: engine stack frames far exceed the ESP32 loop-task stack (not in DESIGN or DEVIATIONS)

**Evidence (measured).** I compiled every `lib/blorb/src/*.cpp` with the installed ESP32-S3 compiler (`xtensa-esp32s3-elf-g++ 8.4.0`, `-O2 -fstack-usage`). These are the largest frames:

```
keepsake.cpp:571  Keepsake::load()                    52432  static
keepsake.cpp:500  Keepsake::Codec::decode(...)        21056  static
keepsake.cpp:393  Keepsake::Codec::getOccupant(...)   20848  static
dish.cpp:66       Dish::Dish(...)                     10800  static
protocol.cpp:487  cmd_snapshot(...)                   10480  static
dish.cpp:218      Dish::hash() const                  10464  static
dish.cpp:209      Dish::save()                        10464  static
dish.cpp:172      Dish::onHatch(Egg&)                 10304  static
variant move-assign of Occupant                       10240  static
```

The cause is that `Snapshot` is 10,424 B and `Creature` is 10,196 B on Xtensa (measured `sizeof`, below), and they are passed and held by value on the stack. `Dish::save()` does `keep_.save(snapshot())`. `Dish::hash()` is `snapshot().hash()`. `Keepsake::load()` holds `Slot slots[2]`, each an `optional<Snapshot>`, plus the decode temporaries. `onHatch` builds a `Creature born` on the stack.

`src/main.cpp:85-93` constructs the Dish in `setup()` (`dish.emplace(store, ...)`), and `step()` calls `dish->tick()` from `loop()`. Both run on the Arduino loop task. That stack defaults to 8 KB on arduino-esp32, and `main.cpp` sets no `SET_LOOP_TASK_STACK_SIZE` (grep: no match). So boot needs at least 63,232 B of stack (Dish ctor 10,800 + `load` 52,432, measured), and every save, hash, hatch and SNAPSHOT needs about 10.4 KB. On the device that is a stack overflow at first boot (inferred from the measured frames and the arduino-esp32 default).

If unit 20 simply raises the loop stack to fit, it spends about 64 KB of internal DRAM, which DESIGN section 8's 290 KB estimate does not include (see finding 3).

**Fix.**
1. Stop moving `Snapshot` and `Creature` by value on hot paths. `Keepsake::load` should validate both slots' headers and CRCs first, then decode only the winner, directly into a heap-owned `std::unique_ptr<Snapshot>`. `Dish::save` and `Dish::hash` should encode and hash from the Dish's members (`Codec::encode(const Dish&)`, a `hashInto` visitor), not from a `snapshot()` copy. `onHatch` should construct the creature in place (`occ_.emplace<Creature>(...)`).
2. Encode the rule as a lever: a `tools/stack_check.sh` that runs the Xtensa `-fstack-usage` build over `lib/blorb` and fails on any frame over 4 KB. Run it with `syntax_check.sh`. My script is `scratchpad/mem/stack.sh`.
3. Make unit 20's check also log `uxTaskGetStackHighWaterMark` beside the minimum free heap.

### 2. HIGH: the lineage log is read whole into RAM on every access, compaction never runs, and gene edits grow it 1.5 KB at a time

**Evidence.**
- `readLog` (`lineage.cpp:222-227`) allocates `std::vector<uint8_t>(s.size(kLog))` and reads the entire file. It runs from `Lineage::open` (every boot), `entries()` (so `recordDeath`, `genomeOf`, `diffOf`, `forEach` and every phone `LINEAGE`, `ANCESTOR`, `DIFF` and `PORTRAIT`), `appendWhole`'s rollback path and `compactIfNeeded`. `genomeOf` also copies every Birth diff up to the target generation into a second vector (`lineage.cpp:346-354`).
- **`compactIfNeeded` has no caller.** `grep -rn compactIfNeeded lib src test sim tools` finds only its declaration and definition. The log is unbounded, and the 192 KB cap is never applied. M12, which guts the function, stayed green.
- Growth (measured, `scratchpad/mem/growth.cpp`, 12 body-only generations). The starter genome is 1,528 B and the founding log is 1,565 B. Each generation adds 138 B (Birth plus Death). Every 8th generation adds a 1,539 B checkpoint (gen 8: `+1677`). Every `gene_edit` twist appends a full Checkpoint, about 1,539 B (arithmetic, same encoding; `Lineage::recordEdit`, `lineage.cpp:335-337`, called from `Dish::editGene`, `dish.cpp:194`).
- DESIGN section 6 says "RAM holds only the current generation, name and feat set; the phone's reads stream from flash. Compaction at 192 KB". DEVIATIONS 6 admits the whole-log read ("Known gap for unit 20") but not that compaction is dead. DESIGN section 8 still lists "Lineage in RAM: under 100 B, by design".

**Consequence (inferred).** With no generation pressure the log stays small for years: about 330 B per generation on average (arithmetic), so about 12 KB a year at one life per 10 days. A phone recolour UI that sends one `gene_edit` per slider step adds 1.5 KB per step, though, so a few dozen edits put tens of KB on the heap at every boot and every death. Once the log exceeds the largest free heap block, `Lineage::open` throws `bad_alloc` in the Dish constructor, at every boot. That is a boot loop: the pet is bricked until the flash is wiped, which defeats the "line survives" promise.

**Fix.**
1. Add `Storage::readAt(name, offset, buf, len)` now (the seam is host-testable) and walk frames through a 512 B window, decoding one entry at a time. `genomeOf` keeps only the running genome and applies each Birth as it streams.
2. Call `compactIfNeeded()` after every append (`recordBirth`, `recordEdit`, `recordDeath`), and lower the cap to something the heap can afford (32 KB). Make compaction stream too: append kept frames to `lineage.tmp`, then rename.
3. Record an owner edit as a small `Edit {generation, uid, offset, from, to}` entry (about 14 B) instead of a 1.5 KB Checkpoint. `genomeOf` applies it like a one-op diff.
4. Tests: compaction keeps every Death and still rebuilds the last 32 generations (DESIGN section 9 lists this and it is missing). Also a `MemStorage` that records the largest single read and fails the test past 1 KB.

### 3. HIGH: peak internal RAM on the 1.28 (327,680 B, no PSRAM) leaves no margin once the stack is paid for

Measured `sizeof` with the Xtensa compiler (`scratchpad/mem/sizes.sh`). Host x86-64 values are in brackets.

| Object | ESP32-S3 | host |
|---|---|---|
| `paint::Canvas240` | 115,200 | 115,200 |
| `Dish` | 10,864 | 11,048 |
| `Snapshot` | 10,424 | 10,592 |
| `Occupant` / `Creature` | 10,208 / 10,196 | 10,360 / 10,352 |
| `Brain` | 5,560 | 5,576 |
| `Phenotype` (inline part) | 2,216 | 2,328 |
| `ChemRules` / `Chemistry` | 2,096 / 2,048 | 2,144 / 2,048 |
| `Appearance` | 296 | 296 |
| `Lineage` | 40 | 40 |

Peak estimate:

| Item | Bytes | Label |
|---|---|---|
| Canvas (static) | 115,200 | measured |
| Dish (static `optional<Dish>`) | 10,864 | measured |
| Core, FreeRTOS, NimBLE static | about 49,100 | DESIGN's sibling measurement; blorbarium's will differ |
| NimBLE host heap | 40,000 to 70,000 | estimate (DESIGN) |
| LittleFS and NVS caches | about 10,000 | estimate (DESIGN) |
| Genome heap (creature; a clutch holds a parent copy too) | 1,528 to 4,600 | measured size, arithmetic count |
| Phenotype rule vectors | about 9,000 | estimate (DESIGN) |
| Snapshot encode during a save | about 1,800 B output, about 2x while the vector grows | measured encoded size 1,758 to 1,854 B |
| Loop-task stack that fits the measured boot path | about 64,000 | measured frames (finding 1) |
| Other task stacks (BLE host, idle, timer, IPC) | about 15,000 | estimate |
| Lineage log vector (today) | 1,565 to 4,843 | measured at gen 0 to 12 |
| **Total** | **about 317,000 to 350,000 of 327,680** | estimate |

**Conclusion.** With an 8 KB loop stack the RAM fits (about 260 to 290 KB), but finding 1 crashes it. With a stack that fits, the board is at or over budget before the lineage grows at all. The lineage load does not break it at today's log sizes (under 5 KB, measured). It breaks it as soon as the log outgrows the remaining few KB, which with gene edits can happen within weeks (inferred). DESIGN's own lever, drawing the canvas in 48-row bands (115 KB to 23 KB), recovers about 92 KB and restores a real margin. Combined with findings 1 and 2, the budget becomes comfortable (arithmetic).

**PSRAM contradiction to settle in unit 20 (guess).** `platformio.ini` `[env:badge128]` sets `-DBOARD_HAS_PSRAM` and `board_build.arduino.memory_type = qio_qspi`, which describes quad PSRAM. DESIGN section 8 assumes none. My recollection is that the Waveshare ESP32-S3-LCD-1.28 ships an ESP32-S3R2 with 2 MB of quad PSRAM, but I have not verified it. Unit 20 should print `ESP.getPsramSize()` on its first boot and correct either DESIGN or the env. If PSRAM is present, put the canvas and the lineage window there.

### 4. MEDIUM: one body-only behaviour is unguarded, so hold-to-pick can break silently

Mutation M10a removed the body pick (`clutch.cpp:77`, `if (s == stim::button_hold || s == stim::double_knock) return cursor;`). The whole suite stayed green (measured: `test_dish PASSED`, `test_lifecycle PASSED`). `body_only_full_life` passes anyway, because the 30-minute timeout (`clutch.cpp:80`) picks the same cursor egg the owner had moved to. Removing both the hold and the timeout (M10b) went red at `test_main.cpp:263` (`picked`), but `test_lifecycle` hung for 5 minutes instead of failing, because a test loop waits for a pick with no bound.

**Fix.** In `body_only_full_life`, assert that the Birth's `at` tick is earlier than the death tick + `kVigilTicks` + `kPickTimeoutTicks`, and that `owner.counts().picks >= 1`. Better, have the owner pick egg 2 by the hold while the cursor sits elsewhere, so only the gesture can produce that egg. Give the unbounded `test_lifecycle` loop a tick cap that fails with a message.

### 5. MEDIUM: work deferred without a named unit

- RTC, deep sleep, unplug detection and battery (user ask 10). DEVIATIONS.md:28-31 calls them "hardware units after this branch's stop point". There is no unit number, and `src/main.cpp:91` passes no `DishOptions{rtc}`.
- On-device games registry (ask 13). There is no `defs/games.def`. DESIGN.md:248 defers it to "when a second game arrives", with no unit.
- "A tap triggers foresee" (ask 11). It is not wired: `owner_arrived` only lowers loneliness (`starter_genome.cpp:196`). It is not in unit 21's check either.

**Fix.** Add build-plan rows: unit 23 "RTC time source, deep sleep and unplug detection" (check: a scripted unplug and replug on the board catches up by the RTC gap), and unit 24 "games.def with the marble moved in" (check: the marble behaves identically and the replay hash is unchanged). Add "a phone connect starts foresee" to unit 21's check.

### 6. LOW: a failed save drops the urgency of an event save

`Dish::save()` (`dish.cpp:209-212`) clears `eventDirty_` even when `keep_.save` returns false. A failed save at death, pick, hatch, rename, edit or TIME is not retried until the 5-minute interval. Lineage idempotence limits the damage, but a power cut in that window rewinds a life event. **Fix:** clear `eventDirty_` only on success, and add a MemStorage `tearAt` test that a failed event save retries on the next tick.

### 7. LOW: the rotten bite draws the fresh fly (builder's admission, confirmed)

`pelletInMouth` (`lib/paint/src/draw.cpp:549-562`) always blits `pack.item(Appearance::Item::What::Pellet)`. `Appearance` carries only `eating`, not which kind he bit. The owner cannot see that he ate rot, which is the cue for learning to clear old pellets. **Fix:** record the bitten kind in `Body` when Eat bites (rotten or fresh), present it as `Appearance::bitten` (`Item::What`), draw that item, and add a `test_present` struct golden for a rotten bite.

### 8. LOW: the mutation fallback can hatch an unchecked child

After `kAttempts` failures, `mutate` (`mutate.cpp:346-349`) returns the parent plus the forced Look and Mind changes without calling `viability`. That contradicts DESIGN section 3, "a lethal mutation never hatches". The forced changes are unlikely to be lethal, but nothing checks. **Fix:** check viability on the fallback. If it fails, retry the forced Mind op on other genes, then fall back to the forced Look change alone. Add a test with a policy that makes every random pass lethal.

### 9. LOW (product call, documented): the phone can feed him

DEVIATIONS 6 records that the `stimulus` twist accepts `fed` and `button` (`twists.cpp:53-60`, any `STIMULI` row). That follows user ask 12 ("validation is for well-formedness only"). It contradicts DESIGN section 4 ("Phone adds: ... Cannot feed"), section 11 ("We accept that the phone cannot feed him") and the spirit of ask 4. The DESIGN text was not updated. **Fix:** ask the user which ask wins. Either way, update DESIGN sections 4, 9 and 11 so the document matches the code.

### 10. LOW: documentation drift not covered by DEVIATIONS

- DESIGN section 8 still says "Lineage in RAM: under 100 B, by design", and its peak budget omits stacks (findings 1 to 3).
- DESIGN section 6 "Compaction at 192 KB" is not true while nothing calls compaction (finding 2).
- DESIGN section 9 promises a lineage test "compaction keeps summaries". It does not exist (M12 stayed green).
- `Dish::pick` (`dish.cpp:182-187`) accepts a phone pick during the 30-minute vigil, before the previews are derived. That is allowed by "no caps" (DEVIATIONS 4) but not stated.

**Fix:** one DEVIATIONS entry, "8. Known memory gaps for unit 20", that states findings 1 to 3. Also correct DESIGN section 8's table.

## 1. Test runs (measured)

`wsl -d survivor --exec bash tools/wsl_test.sh` (env `native`):

```
test/test_paint/test_main.cpp:560: Previews.DumpWhenAsked: Skipped	[SKIPPED]
=========== 235 test cases: 1 skipped, 234 succeeded in 00:01:41.347 ===========
```

`wsl -d survivor --exec env PIO_ENV=native_san bash tools/wsl_test.sh` (ASan and UBSan at -O1):

```
test/test_paint/test_main.cpp:560: Previews.DumpWhenAsked: Skipped	[SKIPPED]
=========== 235 test cases: 1 skipped, 234 succeeded in 00:05:43.244 ===========
```

There were no `runtime error` or `AddressSanitizer` lines in the sanitizer log. The builder's claim, 234 of 235 with one opt-in skip, holds in both environments.

## 2. Frame goldens (measured)

`wsl -d survivor --exec bash tools/sim_film.sh`, run twice:

```
idle          sha256 ff422874d94e59a98c4d029198e316dad07a25d7061e7528b5ffec38eb507f3c x2  ok
eating        sha256 305310e9e564a855ee3430c3f5b280e7bf7e9ab2ca114eed2d8df8e08f0c52d1 x2  ok
foresee_glow  sha256 27f5f94c405e94225226acbdab378247bdb51ce4c732ac1ebeeede64c5b27b4f x2  ok
shake_hop     sha256 a5e8ba7857ebb2b382eaa7ab4c10810a385b08fc7c92ecae7fe7da0ca6b57ef6 x2  ok
sleep         sha256 92b26f718fae8164dca933aac0160197c71af101d1291e03e70476770ddd1ff3 x2  ok
egg           sha256 34e89e7b52db5a6d63279df41e1e0c6738fa4107cf8f419392e39a842697f72a x2  ok
hatch         sha256 a26c529dde0fd6db68cca180967e66487ac5bf1cdd76c565eb50d809d23f1734 x2  ok
clutch        sha256 e12d98cfda108e6c762974b40d777342c7794644c01a203edae8bbcaad1c0572 x2  ok
remains       sha256 87d189b34ccda9a23cf632cf812a6bcba8c5f020850281e659d3cb6577810526 x2  ok
time_unknown  sha256 91611d7da06bc5a5a8272aa2241c37d31fa5ab01459cf3a6d4f78fcd5a4fcd48 x2  ok
10 of 10 frames match their goldens
exit 0
```

Each run shoots every case twice and requires identical BMP hashes ("x2"). Across the two full runs, the sha256 of every `outputs/frames/*.png` was identical ("run1 == run2: identical sha256 for all frames"). `film.py` matches with a tolerance (24 per channel, 0.2 percent of the disc), so I also compared decoded pixels. After undoing the 2x nearest-neighbour enlargement, every frame differs from its golden in **0 pixels**.

## 3. Test quality: deliberate breakage (measured)

Lever: `scratchpad/mut/mutants.py`. It copies the checkout to `~/.cache/blorb-mut` in WSL, applies one exact-string mutation (asserted to match exactly once), runs the named suites and restores the file. My worktree was never edited. Per-mutant logs are in `scratchpad/mut/logs/`. PlatformIO strips gtest's expected/actual text, so the evidence is the failing assertion's `file:line`. (The harness's own `verdict` field mis-parses PlatformIO's line format. The red/green calls below come from the logs.)

| # | Behaviour | Mutation | Result | Failing assertion |
|---|---|---|---|---|
| M1 | death from neglect | `kNeglectCanKill = false` | RED | test_chemistry:228 `Life.NeverFedHeDiesOfNeglectWithinFourDays`; test_dish:327 `CatchUp.DaysOfNeglectKill...` |
| M2a | catch-up idempotence | catch-up stops advancing the pet clock | RED | test_dish:356 `CatchUp.TheSameWallTimeIsNeverAppliedTwice` |
| M2b | catch-up idempotence | anchor never moves after a catch-up | green (equivalent mutant: the advanced tick counter already accounts for the gap, so state is unchanged) | none |
| M2c | catch-up idempotence | no re-anchor, no save, tick not advanced | RED | test_dish:299, 352, 390 |
| M3a | shake-hop | the reflex never starts | RED | test_creature:124 `Reflex.AShakeHopsWithTheAlarmedFace` (+3 more) |
| M3b | shake-hop face | hop keeps the current face | RED | test_creature:128 |
| M3c | no re-hop while rattled | rising-edge check removed | RED | test_creature:140 `Reflex.ASecondShakeWhileRattledGivesNoSecondHop` |
| M4a | twist on live state | `stimulus` twist does not fire | RED | test_protocol:163 `Twist.AStimulusTwistChangesTheLiveStateWhileTicksRun` |
| M4b | twist on live state | `prophecy` not applied | RED | test_protocol:194 `Twist.RenameProphecyAndPickApply` |
| M4c | twist on live state | `gene_edit` not re-expressed into the phenotype | RED | test_protocol:184 `Twist.AGeneEditChangesTheGenome...` |
| M5a | two-slot save | always write the same slot | RED | test_keepsake:165, 186; test_dish:160 |
| M5b | two-slot recovery | load takes the oldest seq | RED | test_keepsake:169, 193 |
| M5c | append atomicity | a torn lineage append is not rolled back | RED | test_lifecycle:149 `Lineage.ATornTailIsTruncatedAtEveryByte` |
| M5d | quarantine | a corrupt slot is not renamed to rescue/ | RED | test_keepsake:188, 208 |
| M6a | mutation variety | no forced Look change | RED | test_mutate:90, 108; test_look:72, 86 |
| M6b | mutation variety | no forced Mind change | RED | test_mutate:91, 109 |
| M6c | sibling variety | siblings share one look slot | RED | test_look:72 `Siblings.EveryPairOfEggsInAClutchLooksDifferent` (test_mutate stays green) |
| M7a | determinism | Dish rng seeded from its address | RED | test_dish:227 `Replay.TwentyFourHours...` (the two dishes differ) |
| M7b | replay hash coverage | creature hash skips the brain | RED, only via the committed constant at test_dish:228 | test_brain, test_creature and test_keepsake stay green |
| M8 | foresee glow floor | floor removed | RED | test_present:250, 257 `Foresee.EvenWhenEveryGlowGeneIsZeroed` |
| M9 | body-only feeding | BOOT drops a pellet only with a phone connected | RED | test_dish:272 `body_only_full_life` ("the button fed him"); test_dish:228 |
| M10a | phone not required | hold or double knock no longer picks | **GREEN** (finding 4) | none |
| M10b | phone not required | hold and the timeout both removed | RED, but test_lifecycle hung for 5 min | test_dish:263 `picked` |
| M11 | large-log coverage | log reads capped at 4 KB | RED | test_lifecycle:107 |
| M12 | compaction | `compactIfNeeded` does nothing | **GREEN** (untested and uncalled) | none |
| M13 | describeDiff | `describeDiff` writes nothing | RED | test_protocol:350 `Golden.Diff` |

Only M10a among the ten requested behaviours stays green with its behaviour broken. M2b is an equivalent mutant, not a gap.

## 4. User asks 1 to 14

| Ask | Status | Where |
|---|---|---|
| 1 Creatures-style, modular | Implemented | 15 X-macro registries `lib/blorb/include/blorb/defs/*.def`; `registry.h` |
| 2 Genetics, roguelite, legible heredity | Implemented | `lineage.h`; `describeDiff` `lineage.cpp:425`; DIFF, LINEAGE, ANCESTOR, PORTRAIT `defs/commands.def:13-16`; forced variety `mutate.cpp:196-234`; feats and unlocks `lineage.cpp:400-421` |
| 3 Dies and lays mutated eggs | Implemented | `Dish::onDeath` `dish.cpp:157`; `Creature::layClutch` `clutch.cpp:46` |
| 4 Complete with no phone | Implemented (hold-to-pick unguarded, finding 4; the phone can also feed, finding 9) | BOOT pellet `dish.cpp:141`; body pick `clutch.cpp:71-80`; `body_only_full_life` test_dish:235 |
| 5 Grungo art and personality | Implemented | `pets/grungo/`, `test/test_grungo_pack` |
| 6 Candidate B base | Implemented | DESIGN, SYNTHESIS |
| 7 Foresee glow halo, sparks, gene tint | Implemented | floor `appearance.cpp:118`, `kForeseeGlowFloor` `appearance.h:20`; pulse and sparks `draw.cpp:38-43, 370`; eye anchors in `FrameRef` |
| 8 Shake makes him hop, genetic | Implemented | rising-edge reflex `creature.cpp:179-184`; tests `Reflex.*` |
| 9 Simulator and goldens | Implemented | `sim/`, `tests/film.py`, 10 goldens `test/golden/*.png` |
| 10 Neglect kills; time catches up; RTC, else phone, else marquee | Engine implemented; RTC hardware deferred to an **unnamed** unit (finding 5) | `kNeglectCanKill` `starter_genome.cpp:13,155-160`; `TimeSource` `seams.h`; `checkWall` and `catchUp` `dish.cpp:233,259`; marquee `draw.cpp:50` |
| 11 Phone computes, WASM, tap triggers foresee | Deferred to unit 21 (DESIGN.md:343). Constraints hold now: no platform headers in lib/blorb (only `<cstdlib>` beyond the std containers), `SNAPSHOT` `commands.def:18`. "Tap triggers foresee" is not wired and not in unit 21's check (finding 5) | |
| 12 Same keepsake same world; twists; board single writer | Implemented | `TWIST`, `SUB`, `SNAPSHOT` `commands.def:18-21`; `defs/twists.def:9-13`; `twists.cpp` |
| 13 On-device games registry | Seeds implemented (marble in `habitat.cpp`, hop). The registry is deferred with **no unit** (DESIGN.md:248; finding 5) | |
| 14 OTA over BLE, own state partition, old saves readable | Deferred to unit 22 (DESIGN.md:344); layout in DEVIATIONS 5 for unit 20. Versioned format present: `Keepsake::migrate` `keepsake.cpp:568-569`, fixture `test/fixtures/keepsake_v1.bin`, `Fixture.TheCommittedV1KeepsakeLoads` | |

## 5. Memory

See findings 1 to 3. Scripts: `scratchpad/mem/sizes.sh` (sizeof on Xtensa and host), `scratchpad/mem/stack.sh` (Xtensa `-fstack-usage`), `scratchpad/mem/growth.cpp` (log and snapshot sizes over 12 generations).

## 6. Untested code

- `Lineage::compactIfNeeded`: confirmed untested (M12 green) and also uncalled (finding 2).
- `describeDiff`: **the builder's admission is wrong.** It is covered by the DIFF golden transcript (M13 red at test_protocol:350 `Golden.Diff`).
- Rotten bite draws the fresh fly: confirmed (finding 7).
- Other public lib/blorb and lib/paint functions that no test executes (gcov, every native suite instrumented with `--coverage -O0`; `scratchpad/mem/cov.sh`):

The run passed: `235 test cases: 1 skipped, 234 succeeded`. gcov flags header-inline functions (`Fx` operators, `Creature::body()`, `Dish::occupant()`, `PhoneTime::unixSeconds()` and the like) at 0% in some translation units while they run in others. I set those aside as false positives. The out-of-line functions that executed **zero lines** across all suites are:

- **Seven of the 16 protocol verbs:** `cmd_genome`, `cmd_chem`, `cmd_lineage`, `cmd_ancestor`, `cmd_portrait`, `cmd_clutch` and `cmd_hash` (`protocol.cpp`). `HASH` is unit 20's cross-platform replay check, and `LINEAGE`, `ANCESTOR` and `PORTRAIT` are the paths that load the whole log (finding 2). The fuzz test never reaches them with valid arguments. **Fix:** a golden transcript per verb (DESIGN section 9 calls for golden transcripts), including `HASH` after the 24-hour routine returning the committed `0x79bce0d7`, and `LINEAGE` and `ANCESTOR` over a multi-generation log.
- **`describeDiff` for every op except a point mutation.** The `MutDup`, `MutDel`, `MutWake`, `MutSleep` and `MutHeirloom` branches never run. `Golden.Diff` covers point mutations only, so the heirloom text the phone shows ("learned by gen 4") is untested. **Fix:** a DIFF golden over a diff that holds one op of each kind.
- **Two `Protocol::pump` event formats** (the `const char*` and `unsigned` overloads of its emit lambda). Some event kinds are never pushed under `SUB` in a test. **Fix:** extend `Events.SubPushesStateEveryTwoSeconds...` to trigger a stage change, a death and a hatch, and assert each event line.
- **`Lineage::compactIfNeeded`** (also uncalled, finding 2).

## 7. Design drift not in DEVIATIONS

Findings 2 (compaction dead, "streams from flash"), 3 (section 8's budget and the PSRAM flag), 8 (unchecked fallback child) and 10 (the remaining stale text). Everything else I compared matched DESIGN or a DEVIATIONS entry: the tick order (`dish.cpp:130-155`), the save triggers, two-slot TLV with quarantine and read-only-newer, idempotent Birth and Death appends, the clutch vigil and timeout, the 50 Hz detectors, and the glow floor.
