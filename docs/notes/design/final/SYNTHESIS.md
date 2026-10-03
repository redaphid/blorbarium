# Synthesis note: blorbarium creature engine

## Base

Candidate B, chosen by the user (`user-asks.md` item 6), so no cross-judge pick was rerun here. B was kept for its shape: a genome interpreter whose brain is one legible table `W[feature][action][drive]`, a habitat that makes the toy complete without a phone (pantry, pellets, marble), heirlooms that turn learned beliefs into instinct genes, a lineage log of diffs plus checkpoints, a two-slot TLV keepsake, `#id` request framing, and a four-method Dish facade. Everything below was folded into that model, not pasted beside it.

## Grafts

| # | Graft | Source | What it deletes or fixes in B |
|---|---|---|---|
| 1 | X-macro `.def` registries for every extensible noun, including detectors, actions, gene kinds and phone verbs | A, C | B's hand-kept parallel lists: constants beside tables, `FEATURES` listed by hand, recent-stimulus loci numbered by hand, and `allDetectors()`/`allBehaviours()` that had to match registry order by index. One row is now the whole registration, and duplicate ids fail at compile time. |
| 2 | Brain weights saved with their axes as stable ids, remapped on load | A | B's "append-only order keeps old saves valid" convention, a rule in prose that a mid-list insert would silently break. |
| 3 | Gene uid in the header; mutation ops and `EDIT` address genes by uid | A | Positional ops whose meaning depends on every earlier op in the diff, and a replay that could hit the wrong gene silently. A missing uid now fails `apply()`. |
| 4 | The embryo trial's intent, as a chemistry-only dry run inside `viability()` | A (adapted) | Lethal mutations hatching. Kept at about 20 KB transient and 30 ms per attempt instead of a second Creature and a minute of CPU. |
| 5 | A clutch of 1 to 3 eggs chosen with the body, stored as seeds | C | B's single automatic egg. This is the roguelite "choose your next run" (user ask 2). Seeds plus captured heirlooms keep every egg replayable. |
| 6 | Feats unlocking clutch size and wild mutation, merged with B's `genMin` into one header byte, `featGate` | C, B | Two unlock mechanisms. Generation count is now just one feat (`fifth_generation`). |
| 7 | At least one Look change and one Mind change per egg, with a minimum visible delta | C | Eggs indistinguishable from the parent. B's `visibleTrait` bool became `GeneClass`. |
| 8 | Heirloom fallback, realised through B's own lineage log: an unreadable snapshot boots an egg of the latest genome rebuilt from `lineage.log`. Plus never-overwrite-newer and quarantine-not-delete | C, A | A lost line after a double torn write, with no new file. |
| 9 | Pet time from powered ticks with a phase offset; `TIME` aligns it; owner entrainment by long dark stretches | C, A | B's "no night until the first phone visit", the `TimeKnown` locus and the `known` flags. Night now always exists, phone or not. |
| 10 | Detectors sampled at 50 Hz, separate from the 10 Hz tick | A, C | A real defect in B: shake detection (jolts 60 ms apart, measured in `orient.h`) at a 10 Hz sample rate. The senses suite asserts the 10 Hz version misses it. |
| 11 | Time as an argument and the IMU as data | C, A | B's `Imu` and `Clock` seams, plus the `Voice` seam (neither board has a speaker). |
| 12 | Physical consent (a button hold) for destructive verbs | C | B's `RESTORE` was open to any phone in range. |
| 13 | Care hint: the presentation names the gesture for the most pressing need | C | A friend with no manual not knowing that BOOT feeds him (user ask 4). |
| 14 | Lineage appends idempotent by (kind, generation) | A | A doubled Death or Birth after a crash between the log and the snapshot. |
| 15 | No shutdown save | A | B's `powerGoingDown()` call, which these boards cannot detect. |
| 16 | PlatformIO `lib/` layout | A | Source filters to share engine code between firmware and `native`. |
| 17 | Dormant surprise genes in the starter genome | A | (content, not mechanism) awakenings visible from generation 1. |

**Arithmetic.** B used Q16 and Q15 (16-bit), C used Q16.16. Both have 16 fraction bits. A drive ramp over 6 hours at 10 Hz moves 4.6e-6 per tick, below that LSB (1.5e-5), so slow rates would round to zero. The final choice is one saturating `Fx` in Q8.24 (C's saturation, more fraction bits), with Q15 storage only for the brain table.

**User asks folded in that no candidate covered.** Reflexes (`reflexes.def`, the `startle` and `flinch` Act loci, `ActiveReflex` on the body) for the shake hop (ask 8). Per-frame eye anchors in `FrameRef` and a glow floor while foreseeing (ask 7). Palette regions and bands (grungo.md). The simulator plan (ask 9).

## Rejections

| Idea | Source | Why it lost |
|---|---|---|
| Runtime embryo trial | A | It needs about 41 KB more and a minute of CPU per attempt, adds a Dying phase and ships a scripted caretaker in firmware, on a board with no confirmed PSRAM. The dry run (graft 4) keeps the guarantee that matters. |
| Float levels | A | Replay determinism across x86, Xtensa and the website. |
| NFC-sticker AUTH token | A | A secret to print and store. A button hold proves possession for free (graft 12). |
| 64 generic morph slots | A | Grungo is fixed art. Palette regions plus mark variants map onto his art directly. |
| Seven life stages | A | The art the user asked for is egg, hatchling, adult, old, dead. Four stages map onto it. |
| Valence column and reward chemicals, STW/LTW split | A | B's reward from drive change covers the same ground with one table. |
| Gesture-to-stimulus rebinding layer | C | Nothing in v1 rebinds. A touch board adds a detector. |
| Traits registry ("gained Skittish") | C | Thresholds to tune and another registry. `describeDiff` already names each change. Deferred; it would follow the feats pattern. |
| No Lamarckism | C | B's heirlooms are the most roguelite thing in the design. Kept. |
| Concept-grid brain | C | B's table is smaller, legible and feeds heirlooms. |
| Heirloom A/B files | C | The lineage log already holds the genome history (graft 8). |
| Tonic field on the chem gene | C | An emitter on the `always` locus does it with no new field. |
| `INJECT`, `WIPE`, genome upload verbs | C, B | No product need in v1. `Creature::inject` stays for sim scripts and tests. |
| Separate Tomb state | C | Folded into the Clutch's vigil. One fewer variant. |
| Unpowered catch-up (four `catchUp` methods) | B | Stasis by default (open question 5). Chemistry keeps `stepCoarse`, which the dry run needs anyway. |
| Reserved `crossover` and `BREED` | A, B, C | Dead surface. A new lineage entry type can add breeding later with no migration. |
| B's 256-byte half-lives gene | B | A bug: its body overflows the `uint8` length byte. Merged into a per-chemical `ChemGene` with the initial level. |
| B's gene marker byte | B | Structural parse plus the keepsake CRC already guard the genome. |

## Laziness check

The final public surface is no bigger than B's for the same jobs. Removed: the `Imu`, `Clock` and `Voice` seams, `Detector` and `Behaviour` virtual bases with their registration functions, four `catchUp` methods, `crossover`, `INJECT`, `BREED`, `HalfLivesGene` plus `InitialChemGene` (now one kind), the `TimeKnown` locus. Added: `Dish::sample` (fixes the shake defect), `Clutch` (user ask 2), consent (closes an open destructive verb), reflexes and eye anchors (asks 7 and 8), the care hint. The Dish facade gained `sample` and lost nothing it needed.

## Verification

- `syntax_check.sh` in WSL `survivor`, g++ 16.2.1, `-std=c++17 -fsyntax-only -Wall -Wextra -Werror=narrowing`: all 18 engine headers, `paint/sprite_pack.h` and `test/usage_check.cpp` compile with no output. Each header is checked through a one-line translation unit, so each is standalone.
- The static asserts are real. A deliberately wrong `static_assert` on `locus::recent(stim::shake)` fails with "static assertion failed: deliberately wrong" (negative control, `scratchpad/sizes/neg.cpp`).
- `usage_check.cpp` static-asserts the derived registry facts: feature count, recent-locus mapping, the hop riding the startle locus, the gene table derived from `gene_kinds.def`, and feat-gated expression in both directions.
- Sizes measured with `sizeof` on x86-64 (WSL has no 32-bit multilib): 20 features, 11 actions, 8 drives; `Brain` 5,576 B, `Chemistry` 2,048 B, `Creature` 10,328 B, `Dish` 10,960 B, `Appearance` 292 B.
- One problem found during verification and fixed: `Lineage` held every entry in RAM, including checkpoint genomes (up to 32 KB). It now streams from flash and keeps under 100 B in RAM.
