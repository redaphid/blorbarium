# Decisions, 2026-10-15 (release while the owner is flying)

The owner was unreachable. Each decision below says what was chosen, why,
and the evidence.

## D1. Release from main + battles, not from main alone

- **Decision:** merge `battles` (f92f345, 184 commits) into main's line on
  branch `release-0.7`, and release from that.
- **Why:** everything the goals ask for already lives on `battles`: nine
  species packs, species as a gene, the board thinking with no phone
  (ff81ce5), the welcome when a phone returns, the ESP-NOW duel (D9, D12) and
  personalities. Main had only the batch flasher and `foundingGenome` (6
  commits). The badges got main 0.4.0 aee2f23 on 2026-10-15. No record says
  `battles` was held back on purpose: the batch session never mentioned it.
- **Evidence:** `git rev-list --count main..battles` = 184,
  `battles..main` = 6. The live site already served 0.5.0 from `battles`
  (87bf63c). Merge 95bb89a had conflicts only in `src/main.cpp` (founder line)
  and `test/test_founding` (main's tests moved to `test/test_founding_look`).

## D2. Version 0.7.0

- **Decision:** release as 0.7.0.
- **Why:** the site serves 0.5.0, and `battles` says 0.6.0, which went on
  test boards by cable (D12). A dish refuses an image that is not newer, so
  0.7.0 is newer than anything any board has run.

## D3. One badge per species, from a fixed MAC table

- **Decision:** the 8 badges in `flash_log.csv` take the 8 species that are
  not grungo, one each, in badge order. The original gift device
  (`blorb-237c`, an adult grungo the owner's friend chose, on flash3 with no
  updater) stays the one grungo. A MAC not in the table falls back to a hash
  over the nine species.

  | Badge | MAC | Old BLE name | Species (new BLE name) |
  |---|---|---|---|
  | 1 | d4:05:92:77:1b:3c | blorb-1b3c | mossby |
  | 2 | d4:05:92:77:1b:44 | blorb-1b44 | puffkin |
  | 3 | d4:05:92:77:1b:34 | blorb-1b34 | gilly |
  | 4 | d4:05:92:77:1c:b0 | blorb-1cb0 | mothwick |
  | 5 | d4:05:92:77:1b:30 | blorb-1b30 | mudge |
  | 6 | d4:05:92:77:1b:40 | blorb-1b40 | fizz |
  | 7 | d4:05:92:77:22:58 | blorb-2258 | hushwing |
  | 8 | d4:05:92:77:22:40 | blorb-2240 | tinker |
  | gift | (ends 23:7c) | blorb-237c | grungo (unchanged, needs a cable for any update) |

- **Why:** the owner: "There will only be one of each." A hash cannot promise
  that for 8 MACs, so a table does. Nine creatures for nine devices.
- **Evidence:** `flash_log.csv`, `flash_log.old.csv` (badge 1 only),
  `docs/hardware-testing.md` (the gift device).
