# Fix-up pass on blorbarium `engine`

Branch `origin/engine`, from `a693a0b` to `36ceffd`. Evidence labels: **measured** (command run), **arithmetic**, **estimate**.

## Commits (in order, each pushed)

| Commit | Unit |
|---|---|
| `808fd3d` | 1. `tools/stack_check.sh`: ESP32-S3 `-fstack-usage` over lib/blorb and lib/paint, fails any frame over 2,560 B; `wsl_test.sh` runs it first. Red on a693a0b with 10 frames over. |
| `0b73952` | 2. No Snapshot or Creature on the stack: the Dish owns one `Snapshot live_`; `Keepsake::load(Snapshot&)` / `decode(..., Snapshot&)` fill it in place; occupants built by `emplace`; hatch is a Creature constructor; `viability`'s Chemistry on the heap. |
| `a1a3377` | 3. Lineage streams from flash: `Storage::read` takes an offset; frames read one at a time; `genomeOf` replays as it streams; truncation, rollback and compaction share one streamed rewrite through `lineage.tmp`; `compactIfNeeded` runs after every append (keeps the newest Checkpoint and Rename per generation). |
| `f6f86d2` | 4. Budget with no PSRAM: lean keepsake codec (counting pass, one exact buffer, in-place CRC, chunk views, one slot at a time); engine heap and stack measured in `test_dish` `Budget.*`; DESIGN section 8 rebuilt from measured rows; PSRAM discrepancy noted in DESIGN 8, the unit 20 check, `platformio.ini` and DEVIATIONS 9. |
| `0fba337` | 5. Tests (delegated, then cherry-picked and extended): hold-to-pick observable, capped loops, golden transcripts for GENOME, CHEM, LINEAGE, ANCESTOR, PORTRAIT, CLUTCH, HASH; DIFF golden for dup, del, wake, sleep, heirloom; rotten bite fixed (`Body::mouth`, `Mouthful`) with test_present and test_paint checks; plus the re-anchor assertion that makes M2b red and capped hatch loops. |
| `9c24ce4` | 6. DESIGN build plan: unit 23 (RTC, deep sleep, unplug, battery), unit 24 (`games.def`, marble moved in); unit 21's check gains "a phone connect starts foresee". |
| `faf4aae` | Follow-up to 4: the heap counter also replaces the nothrow `new`/`delete` (test_dish aborted under ASan without it). |
| `36ceffd` | Follow-up to 3 and 2: tests for the newest-Checkpoint compaction rule and load's re-decode (mutants F4 and F6 were green). |

## Stack per function (ESP32-S3 `xtensa-esp32s3-elf-g++ 8.4.0 -O2 -fstack-usage`, measured)

| Function | Before (a693a0b) | After (36ceffd) |
|---|---|---|
| `Keepsake::load` | 52,432 | 128 |
| `Keepsake::Codec::decode` | 21,056 | 512 |
| `Keepsake::Codec::getOccupant` | 20,848 | 432 |
| `Dish::Dish` | 10,800 | 416 |
| `cmd_snapshot` | 10,480 | 48 |
| `Dish::hash` | 10,464 | inlined (`live_.hash()`), no frame |
| `Dish::save` | 10,464 | inlined, no frame |
| `Dish::onHatch` | 10,304 | 160 |
| variant move-assign of `Occupant` | 10,240 | gone (no variant-to-variant moves) |
| `viability` | 4,368 | 2,320 |
| Largest frame anywhere (lib/blorb + lib/paint) | 52,432 | 2,400 (`drawCreature`) |
| Frames over the 2,560 B budget | 10 | 0 |

Whole loop path (setup + loop calls: founding boot, a whole life with saves and clutch dry runs, reboot decoding a creature, 12 phone reads), measured on a painted x86-64 thread stack: **4,816 B** of the 8,192 B loop task, so the loop stack size is unchanged. A by-value Snapshot copy in `save` (mutant F1) raises it to 11,880 B and fails the test.

## Peak RAM against 327,680 B internal, no PSRAM

| Item | Before (verifier) | After | Label |
|---|---|---|---|
| Canvas | 115,200 | 115,200 | measured |
| Dish (static) | 10,864 | 10,872 | measured, ESP32-S3 `sizeof` |
| Core, FreeRTOS, NimBLE static | about 49,100 | about 49,100 | sibling measurement |
| NimBLE host heap | 40,000 to 70,000 | 40,000 to 70,000 | estimate |
| LittleFS and NVS caches | about 10,000 | about 10,000 | estimate |
| Engine heap peak (genome, phenotype, save blob, lineage reads, dry runs, phone replies) | not measured; the save alone was 53,737 at peak | **16,879** | measured, x86-64, `Budget.*` (bar 20 KB) |
| Loop task stack that fits | about 64,000 | 8,192 (default; 4,816 used) | measured frames / painted stack |
| Other task stacks | about 15,000 | about 15,000 | estimate |
| Lineage log in RAM | 1,565 to 4,843, unbounded | one frame, under 2,048 | measured (`largestRead`) |
| **Total** | **about 317,000 to 350,000** | **about 265,000 to 295,000 (32 to 62 KB free)** | arithmetic |

Engine heap before the codec fix: 53,737 B (measured with the same counter on a1a3377, before the codec change): a creature's keepsake is about 9.5 KB (Brain chunk 7.3 KB), not the 1.8 KB an egg's is, and encode held eleven chunk buffers, a doubling payload and two whole copies. Load held 28 KB. The verifier's 1.8 KB snap figure is an egg or clutch slot (growth probe below).

Lineage growth, rerun on 36ceffd (measured, unchanged format): starter genome 1,528 B, founding log 1,565 B, +138 B per generation, +1,677 B at generation 8, 4,843 B at generation 12.

## Mutants caught

Harness: `scratchpad/fix/mut/mutants.py` (the verifier's, with anchors ported where the fix-up moved code, own cache `~/.cache/blorb-mut-fix`, RED judged by suite exit code because the original parser missed PlatformIO's failure lines).

| Set | Before (verifier, a693a0b) | After (36ceffd) |
|---|---|---|
| Verifier's M1 to M13 (26 mutants) | 23 red, M2b green (called equivalent), M10a green, M12 green; M10b hung 5 min | **26 of 26 red** |
| Fix-up guards F1 to F6 (save copies Snapshot, lineage whole read, CRC over a copy, compaction keeps every Checkpoint, compaction never called, load skips the re-decode) | n/a | **6 of 6 red** (F4 and F6 needed the tests in 36ceffd) |

M2b was not equivalent: when a catch-up is clamped at 30 days, the re-anchor is what drops the excess. `CatchUp.AnAbsenceLongerThanTheCapIsClampedAndRecorded` now asserts `wallNow()` equals the RTC after the boot.

## Reruns on 36ceffd (measured)

- native: 250 cases, 249 passed, 1 opt-in skip (`Previews.DumpWhenAsked`).
- native_san: 250 cases, 248 passed, 2 skips (the dump, and `Budget.*`, which skips under ASan), no sanitizer reports.
- `tools/stack_check.sh`: green, largest frame 2,400 B.
- `tools/syntax_check.sh`: green.
- `tools/sim_film.sh` twice: 10 of 10 frames match, sha256 identical to the verifier's run at a693a0b. No golden was updated: the rotten-bite fix changes no golden frame (the eating feed bites a fresh pellet); only test_present's `Golden.Eating` struct golden moved one tick earlier (the bite now shows on the frame the pellet leaves the dish).

## Not fixed

- PSRAM on the 1.28 stays unverified by design: unit 20 must print `ESP.getPsramSize()` and correct DESIGN 8 or the `badge128` env.
- `describeDiff` prints no "learned by gen N" for an heirloom, though lineage.h promises it (found by the unit 5 delegate; its golden pins today's text).
- Verifier findings outside this brief: 6 (a failed event save clears `eventDirty_`), 8 (the mutation fallback skips `viability`), 9 (the phone can feed: a product call for the user), and the `Dish::pick` vigil note in 10.
- The per-path stack need is a host proxy (x86-64 frames); unit 20 must log `uxTaskGetStackHighWaterMark` on the board.
