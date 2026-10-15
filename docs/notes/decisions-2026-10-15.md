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
