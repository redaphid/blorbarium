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

The final table is `lib/blorb/include/blorb/defs/badges.def`. The gift row
`d4:05:92:77:23:7c` is **assumed** from the BLE name `blorb-237c` and the
batch prefix: its full MAC is recorded nowhere. Any MAC not in the table
founds grungo, the one species the table leaves for the gift, so the nine
known devices hold nine different species. A `static_assert` in
`founding.cpp` checks nine rows, each species once and no MAC twice.

## D4. Adopt the 0.4.0 pets; do not re-found them

- **Decision:** a pet founded by 0.4.0 (a grungo genome with no
  `SpeciesGene`) on a badge in the table becomes that badge's species while
  keeping its age, generation, brain and lineage. Adoption appends the
  species, power, knack and duel genes, re-rolls its look in the new
  species' colour bands, sets the species' temperament bytes, records one
  lineage Checkpoint, and renames the line if it still has the default name
  "Grungo".
- **Why:** the owner preferred keeping the pet, and the data model already
  has this path (`Creature::extendGenome` plus `Lineage::recordEdit`, used
  by the duel-gene graft).
- **The trap it avoids:** an OTA image keeps itself only if the resumed pet
  matches the handover record (generation and genome hash). So adoption
  waits until the trial has kept the image (`adoptTableSpecies` in
  `src/main.cpp`), and it is idempotent across power cuts.
- **Never adopts toward grungo**, so the gift grungo's genome stays byte for
  byte (`TheGiftDevicesGrungoStaysByteForByteAndAdvertisesGrungo`).
- **Evidence:** `test/test_adopt_species` (16 cases),
  `tools/guards/adopt_species.sh` (13 mutants caught).

## D5. The BLE name is the species slug

- **Decision:** advertise `mossby`, `gilly` and so on, the species of the
  pet at boot, even when the owner named the pet on the site. One of each
  species makes it unique and stable. While an image is on OTA trial the
  badge still advertises `blorb-xxxx`, so a page served before this release
  can still confirm its update. After adoption the new name appears at the
  next boot.
- **Fits BLE:** the longest slugs (`hushwing`, `mothwick`) are 8 characters.
  The NUS UUID stays in the advertisement.

## D6. Founding: the MAC table, no chooser

- **Decision:** a fresh board founds its table species directly, with
  full-range colours rolled across that species' pack bands. The D10
  three-egg chooser no longer runs at boot. `DEBUG found <species>` over
  the cable still pins a species as the owner's override.

## D7. Thoughts: offline complete, a phone adds

- "TAP ME WITH YOUR PHONE" is removed everywhere (`askForTime` deleted).
  `TimeUnknownOutranksAnyThought` became
  `WithNoWallTimeHeStillThinksAndForetells`. The sim case `time_unknown`
  became `no_phone`.
- Sixty offline idle lines keep a content pet with no phone from going quiet
  for more than 180 s (6 h test: 222 lines, 53 distinct).
- A phone adds: wall-clock lines (the hour, morning, late night, Monday,
  the weekend, a Friday prophecy) only after a TIME this boot; lines with
  the owner-given name; and up to 4 taught lines of 30 characters (TWIST
  teach / unteach, kept in `petfs/taught.lines`, keepsake format
  unchanged), with a small control on the site.

## D8. Battles ship

- **Decision:** ship the ESP-NOW duel from `battles` (D1-D12).
- **Why:** it was proven on two boards (a Spar), Wi-Fi and ESP-NOW are off
  unless a duel is open, OTA is refused mid-duel, and death needs two
  deliberate 3 s holds on Mortal stakes between two adults. Removing it from
  a tree built around species-as-genes would have been riskier than keeping
  it.
- **Fixed today:** a BOOT hold carried into Mercy could Finish a pet with
  no fresh hold (250c855, test `AHoldCarriedIntoMercyIsNoFinish`).
- **Not proven on hardware:** Scrap, Mortal, the RSSI threshold, OTA right
  after a duel.

## D9. Art: keep the shipped faces

- All nine species already had a complete 10-face set plus an egg. The 828
  ComfyUI renders in `D:\sync\Comfy\blorbarium` are battle faces and poses
  (sad, angry, sick, hurt, fallen) that this firmware does not draw, so no
  face was swapped. `docs/expression-picks.md` covers all nine.
- The sources the packer reads were already committed under
  `pets/<species>/art/`. Proven: a fresh clone of origin/main in WSL built
  badge128 and re-packed all nine species byte for byte (the packer also
  needs scipy, now documented).
- The art is in the app image (52.9% of a 4 MB slot), not the `art`
  partition, so an OTA update carries all of it.

## D10. Debug link in every build, destructive commands need a BOOT press

- Characteristic `6E400005` (docs/debug-ble.md), with a panel at
  `?debug=true` (or `debug=1`). Hatch, stage, warp, chemicals, die, species
  and wipe need a once-per-connection unlock confirmed by the badge's
  button, so a stranger's phone in range cannot kill or wipe a child's pet.
  Thoughts, fortunes, stimuli, wall time and the time scale need no press.
  Everything that writes is refused while an OTA trial holds storage.

## D11. The site works offline

- A service worker caches the page, its scripts and the latest manifest
  together with the image it names, replaced whole when a newer release is
  fetched. Proven in Playwright by going offline after one visit and
  running the mock update from the cache.

## D12. Releases

- 0.7.0 (firmware commit daf4f19, manifest db465ef) was published
  first, without the car-proof senses, the first-hours tuning or the full
  personalities, because the owner was already on the plane. Those follow
  in 0.7.1.
