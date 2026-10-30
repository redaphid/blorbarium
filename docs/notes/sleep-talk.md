# Why the badges said nothing until a phone connected

Branch `rel-sleep` (from origin/main 9b4000d, 0.7.2). The owner, on 0.7.1 with the badges and no phone: "The text for the creatures does not show up until connected to phone".

## What was measured

`test/test_first_hours`, `FIELD_PETS_SWEEP=1` (`Sweep.FieldPetsHourByHour`) models each badge in the field:
- founded by 0.4.0 from `foundingGenome040` with the pet clock at 00:00;
- 1 h on 0.4.0, then adopted to its table species;
- 12 h of powered time, no phone, no wall time, with the seated and nobody owners from first-hours.md.

There are three variants:
- `@070`: adopted on 0.7.0, which kept 0.4.0's own sleep bytes (adoption never runs twice);
- `@071`: adopted on 0.7.1 or later, with the FirstHours bytes;
- `@070+retune`: adopted on 0.7.0, then given this branch's retune.

Update 2026-10-21 (issue #36): there is one starter recipe now, and the
retune below is gone, along with the variants above. Adoption leaves a 0.4.0
pet's own sleep bytes as they are (founding.h `adoptSpecies`); a pet that
still sleeps by them keeps them. `Sweep.FieldPetsHourByHour` runs one
variant, `@040`.

## The cause

The lead's hypothesis holds. The pets are asleep, and a sleeping pet was nearly silent:
- A 0.4.0 founding started its clock at pet midnight. Day species fell asleep about 1.2 h after founding and stayed asleep until about 11.5 h (`@070`) or 9.6 h (`@071`).
- Night-sleepers (mothwick, hushwing, tinker) were asleep from 3.2 h to 9.8 h (`@070`) or from 2.2 h to 8.1 h (`@071`).
- Asleep, only the dream line ("ZZZ... LUNCH...", 900 s cooldown) and, once hunger rose, the hunger lines could fire: about 4 lines an hour, with 15 minutes between lines. Someone glancing at the badge sees nothing.
- A phone's TIME runs `Dish::welcomePhone`. That says "THE GLOWING ONE RETURNS!" at once, even asleep, and snaps the clock to the real afternoon, so his night ends. That is why text appeared only once a phone connected.

The FirstHours retune alone would not have fixed it. `@071` pets, which already have it, still sleep through their 0.4.0 pet night. The retune shortens that night by 1.5 to 2 h.

## What changed

| Where | What | Why |
|---|---|---|
| thoughts.def rows 124-131, situation `dozing` (thoughts.cpp: asleep, dreaming or not) | snore, mumble, dream_chase, dream_feast, dream_flight, sleep_count, sleep_warm, dream_rain. Priority 0, cooldown 1800 s. | A sleeping pet murmurs every few minutes. At 2700 s the eight rows burned out and left 15-minute gaps; at 1800 s the longest gap is 6 to 13 min. The lead asked for these rows on the owner's behalf (the pet-personality skill says a new row needs the owner's OK). |
| personalities/_shared.txt and all nine species files | one shared line per row, and two lines per row in each species' voice (144 species lines, written by a subagent and checked with `personality.py say all`). `tools/personality.py build` regenerated species_lines.def and said_lines.inc. | "In each species' voice". With two lines per row, a long sleep repeats less. |
| founding.h/.cpp `retuneFirstHours`, `Dish::retuneFirstHours` (removed 2026-10-21, issue #36) | An adopted pet whose bytes, everywhere 0.4.0's numbers and FirstHours differ, still held 0.4.0's, took the FirstHours bytes: sleep pressure, energy, the happy face and the egg. Recorded as a lineage Checkpoint and saved. | 0.7.0 adopted with 0.4.0's bytes and adoption never runs twice. |
| main.cpp `adoptTableSpecies` (now `adoptBoardSpecies`, without the retune) | After adoption settled, the retune was asked once a second until it settled. | It ran under the same rules as adoption: never on an OTA trial (the trial's pet mark sees him as the old image saved him). |

## Numbers (seed 0x5EED0000, 12 h, from `Sweep.FieldPetsHourByHour`)

Lines per hour asleep, before -> after. The minimum in any full hour asleep went from 4 to 17-19:

| pet | nobody | seated |
|---|---|---|
| day species (mossby, puffkin, gilly, mudge, fizz) @070 | 8.7-9.2 -> 21.8-22.7 | 6.9-7.2 -> 20.9-21.3 |
| day species @071 | 7.3-7.8 -> 21.6-22.1 | 5.0-5.3 -> 20.0-20.4 |
| night-sleepers (mothwick, hushwing, tinker) @070 | 8.6-8.7 -> 22.0-22.4 | 4.5-4.7 -> 19.7-20.4 |
| night-sleepers @071 | 5.1-5.3 -> 20.2-20.5 | 4.7-4.8 -> 19.7-19.9 |

The "before" rates are averages that include the hunger lines late in the night. The early hours of the night held 4 lines an hour.

Hours asleep out of 12: day species 9.8 (`@070`) -> 8.3 (`@070+retune`, identical to `@071`); night-sleepers 6.0 -> 5.8 alone and 6.0 -> 6.6 seated. The night-sleepers' night moves 1 h earlier and ends 1.7 h earlier.

## Not changed

**The 0.4.0 pets' clock.** The field pets are still in the pet night their 0.4.0 founding gave them. A one-time clock shift for table badges would wake them, the way `kUnaskedFoundingHour` does for fresh pets. It is the next lever if the owner wants them awake rather than murmuring. It was not asked for, and it is irreversible once the clock is saved.

## Evidence

- Native: 803 cases, 800 passed, 3 skipped, exit 0. That included `Retune.*` (5 tests in test_adopt_species, removed with the retune) and `FieldPets.ASleepingPetStillTalks` (at least 12 lines an hour asleep, no asleep silence over 15 min, on a `@070` mossby, mudge and hushwing).
- Sim: 22 of 22 frames match, nothing re-blessed.
- badge128 builds.
