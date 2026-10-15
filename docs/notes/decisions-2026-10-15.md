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

## Personalities from the art

The owner: "Fill in the personalities yourself for now" and "Base the
personality on the image for each creature". Each species' sheet in
`personalities/<species>.txt` now starts from its frames (neutral, happy,
annoyed, foresee, sleep, egg), and each has its own line for every row except
`parrot`, which stays the owner's taught line word for word. Grungo keeps
"I FORESEE FLIES. IN MY MOUTH." beside a second peckish line.

- grungo: a squat frog in a brown hooded cloak, flat red-eyed stare, eyes glowing cyan when he foresees -> a nervous hooded pond-monk oracle who announces flies and damp as prophecy and is never hiding, only "hooded".
- mossby: a huge peach shell arched over a sweet round face like a bonnet, rosy cheeks, prim little frown -> a polite, unhurried homebody, proud of carrying his house, who never finishes anything today.
- puffkin: a near-perfect coral puffball, cream fluff, blush, a tiny fury face that isn't scary -> a loud, bouncy hugger who sneezes sparkles, goes POOF, and finds doom exciting.
- gilly: a pink axolotl with a frilly gill crown, a wide smile and arms held out for a hug, a lip-wobble pout that never lasts -> relentlessly sunny mender: it'll grow back, I'm fine, hugs.
- mothwick: a plump fuzzy lavender moth with a white gentleman's ruff, lamp-yellow eyes and the grumpiest squint -> a dreamy humming night creature drawn to every lamp, grumpy by day, and he did not eat the sock.
- mudge: a round blue fish on flippers with a cowlick fin and goalkeeper fins flung wide, beaming or flatly unimpressed -> a can't-sit-still racer who makes everything a race; one-sentence lines for his terse voice.
- fizz: a glossy gold beetle with spark-tipped antennae and a smug little smile -> a crackly, possessive collector of shiny things who calls dibs and zaps by accident.
- hushwing: a round snow-white owl with stern ear tufts, an orange stare and a secret blush -> the grave, quiet marsh librarian who hushes everyone, sees everything at night, and is soft underneath.
- tinker: a raccoon in an aviator cap and goggles, work overalls and a bandit mask -> a scrappy night-shift inventor with pockets full of bin treasure: I can fix that, nice parts.

- **Checks:** `tools/personality.py check` now refuses a species line that
  repeats the shared line or another species' line. A line that starts with
  `#` is a note, so an `oclock` line must not open with the number (eight
  drafts did and vanished silently until counted).
- **Tests touched:** `ASpeciesLineIsHisAlone` reads the shared peckish line
  through `kSharedOnly`, since mossby now has his own. `AShakeDuringAProphecy
  ReplacesItFromTheStart` waits for the first line to be a tenth across
  instead of a fixed 1.5 s, because a longer line crosses slower (`passTicks`).
- **Goldens re-blessed:** `thought`, `prophecy`, `no_phone`. Each frame
  differs only in grungo's marquee text: his own bored line and his own
  `p_pond_no` in place of the shared ones, and in `no_phone` the same frame
  scrolled to a slightly different place by a different-length line.
- **Flash:** badge128 firmware.bin 2,217,968 -> 2,256,992 bytes (+39,024,
  read-only data only; bss unchanged).

## D13. Death, as the owner described it

The owner, on the plane, after 0.7.0 shipped with Mortal duels and a Finish:

> "Death should not be a choice, and I've repeatedly talked about how I want them to die. But finish the release first"
> (2026-10-15 22:53 UTC, the release session, `D--Projects-blorbarium/e9822a01`)

### What they said before, in their own words

Every owner message about death, from the Claude Code transcripts of
blorbarium and the badge repo (`C:/Users/hypnodroid/.claude/projects/`),
user turns and question answers only. Times are UTC. The relay-queue store
(`D:/projects/relay-queue/data/events.jsonl`, through 2026-10-12) holds no
owner message about a pet's death, and the mindmeld hits were agent briefs
quoting `docs/notes/design/user-asks.md`.

1. **2026-10-03 03:28** (session `5a48df13`, blorbarium). Asked "Can blorb
   die?", they chose **"Can die, then lays an egg"**, whose text was "A dead
   blorb leaves an egg with a mutated copy of its genome. The lineage
   continues and the stakes are real." The safer "Never dies" was the
   recommended option, and they declined it.
2. **2026-10-03 03:49** (same session). Asked how a life should end, given
   Grungo's SOUL.md "never dies" rule: **"Ignore that part. It was
   overfitting."**
3. **2026-10-03 04:41** (same session), in answer to "Can neglect kill
   grungo?" (recommended: no) and "When unplugged, what happens?"
   (recommended: paused): **"Also I want it to 'catch up' over time, not
   freeze. So if you haven't seem him in a while, he might die. This is for
   adults who love video games; death is fine"**.
4. **2026-10-13 11:16** (session `d9cc3960`, the battles coordinator). Asked
   whether neglect death should hold on the kids' devices, with the default
   "kids' pets getting sick and recovering instead of dying": **"Pets should
   die. My family has a dark sense of humor. They want something more
   complex. It's fine"**.
5. **2026-10-14 06:31** (session `40b60ff7`). Asked what a phone's clock
   should do to a pet unplugged with no battery (options: frozen; capped and
   never fatal; the whole gap): **"It shouldn't _prevent_ death. Close to
   dead + time = dead"**.
6. **2026-10-14 06:36** (same session): **"We should have negative effects
   to the pet if it's been neglected for a while. Like, 3+ weeks. Once it
   has the latest time."**

Nothing they said asks for a duel death or for any death a person picks.
The "Pets should die" ruling answered a question about neglect on the
kids' devices. The battles design read it as a call for duel deaths, and
the Finish came from that reading. The lifespan question (2026-10-03 04:26,
"about 10 powered days") was interrupted and never answered, so the
default of about 10 powered days is the design's choice, not the owner's.

### How a pet dies now

These are the deaths the owner described, and the engine already had all
of them. None of their code changed.

- **Old age:** life halves every 3 days, and the old-age receptor ends him
  at about day 10 (`test_chemistry` "FedEveryTwoHoursHeLivesAboutTenDaysAndDiesOfOldAge").
- **Neglect:** a pet nobody feeds starves to death within 4 pet days
  (`kNeglectCanKill`, DEVIATIONS 2; "NeverFedHeDiesOfNeglectWithinFourDays").
- **Time catching up:** a phone's clock lives up to 12 h of an unplugged
  gap, and a pet already near death dies inside it ("close to dead + time =
  dead"; DEVIATIONS 3; `test_dish` "AfterALongAbsenceNeglectStillKillsHim").
  Three weeks or more away hurts him once ("ThreeWeeksAwayHeWakesStarvingAndHurtOnce").
- **Then an egg:** every death leaves the remains, the vigil and a clutch
  of mutated eggs ("Can die, then lays an egg").

### What changed

- **No player action can kill a pet.** The Mortal tier, the Mercy and
  Fallen phases, the verdict (`Verdict`, `Move::verdict`,
  `Outcome::downed/verdict`), `Creature::fall()`, and the BOOT rows for the
  Mortal confirm, Spare, Finish and Plead are gone. So is everything that
  only followed a duel death: the `Downed` book entry, the dark ledger
  (fell to, slew, owes us, we owe), trophies, vendettas, the `slayer` feat's
  award and the rim tokens. `lib/blorb`, `lib/paint`, `src/main.cpp`,
  `tools/duel_shot.cpp`.
- **What replaces Mortal: nothing. It is folded into Scrap.** A Scrap keeps
  its wounds, scars and trait drift, clamped by `woundRoom()` so no duel
  kills. I weighed keeping a tier with Mortal's bigger wounds and no death,
  and refused it. A tier picked because it is more dangerous is still a
  choice to risk a life, and "Mortal" would promise what it no longer does.
- **The debug link's DIE is gone** (verbs.def, console, jump, the page,
  the mock, docs/debug-ble.md). It sat behind the BOOT-press unlock, and it
  was clearly a test tool, but its one purpose was a death on demand, and it
  ran through the Finish path. The owner's ruling covers a death anyone
  chooses, and a tester can still reach every death the owner's way:
  `WARP` an elder through his last minutes, or `CHEM` him to the brink and
  let the next ticks run his genes. `test_debug_ble`
  "ThereIsNoDieVerbButHisGenesFinishWhatChemStarts".
- **The staking screen** shows two tiers. Spar is on the left and Scrap on
  the right, the tilts that pick them. 0.7.0 drew Scrap at 6 o'clock while a
  right tilt chose it.

### A mixed pair: 0.7.0 and this firmware

The badges carry 0.7.0 now, and they meet before each one is updated.

- **The frames are unchanged:** 77, 111 and 224 bytes, the same layout,
  and wire version 1. The tier bytes, the third tier's picture and skull,
  the hold byte and the spare log slot are all still there. This firmware
  writes them as 0 and skips them on read.
- **A Mortal byte (2) from a 0.7.0 badge reads as Scrap** (`stakesFromByte`).
  A 0.7.0 badge beacons Mortal as its ceiling by default. This firmware
  never sends more than Scrap, and both firmwares run the lower of the two
  choices. So a mixed pair agrees on a Scrap at most, nobody is downed,
  and neither side crashes or refuses the other. `test_duel_session`
  "A070BadgeStakingMortalDuelsAScrapAndNobodyIsDowned" rewrites one side's
  frames in flight into a 0.7.0 Mortal stake (40 seeds, 30% loss), and
  `test_duel_wire` "A070MortalStateIsHeardAsAScrap" decodes such a frame.
- **0.7.0's verdict bits are never read.** A verdict in a log, after the
  end or as stray bits on a turn, changes nothing in the Outcome
  (`test_duel_resolve` "A070VerdictInTheLogChangesNothing").
- **The Outcome hash is 0.7.0's for every Spar and Scrap.** It still
  hashes three zeros where 0.7.0 hashed its downing and verdict. The four
  Spar and Scrap resolve goldens kept their pinned hashes byte for byte, so
  the two books of a mixed pair still compare.
- **What remains:** two 0.7.0 badges can still Finish each other until
  they are updated. Update them all before they meet.

### Save formats

None change. The keepsake and `lineage.log` are untouched. `duels.log`
keeps 0.7.0's layout. The Met entry still carries its verdict and flag
bytes. The Self and Friend summaries still carry their feats and ledger
bytes. 0.7.0's Downed, Trophy and Vendetta entries are read past, and their
kind numbers are never reused, so a book either firmware wrote opens in the
other (`test_duel_book` "A070BookWithItsDarkLedgerOpensAndReadsItPast").
Registry ids stay put: stimuli 41 and 42 are retired but still name genes,
and the `slayer` feat row stays, so a lineage that earned it keeps its war
paint. The BootGlyph slots Skull, OpenHand, Plead and Candle stay too,
because they index the packed glyph art.

### What I measured and left alone

A duel's wounds heal on their own, with a 6-hour half-life, fed or not.
A pet beaten as close to death as a duel allows (injury 0.70) and then
left unfed dies at the same moment as an unhurt pet left the same way:
2.65 pet days, of starvation. If he is fed, he dies of old age at 9.6 pet
days instead of 10.0, because injury speeds ageing. So a duel never causes
a death, and a hard life is a slightly shorter one (`test_stakes`
"AfterTheDuel"). The owner never asked for wounds that kill when they are
left untended, so I did not add any. **For the owner:** if a wounded pet
nobody looks after should die of those wounds, the change is one gene:
let starvation slow his healing.

### Superseded

D8's "death needs two deliberate 3 s holds on Mortal stakes" and D10's
"die" among the unlocked verbs describe 0.7.0. This section replaces both
for the next release.
