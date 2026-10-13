# Battles

Two kids bring their devices together and their pets have a duel. Each turn, each kid secretly picks a stance with the body: tilt to aim, knock to lock in. Both devices compute the same battle from the same inputs, so they never disagree. Nobody is hurt. The loser swoons into a nap and wakes up happy. Both pets make a friend, both grow, and if both kids hug their pets, a friend egg joins the next clutch.

This is a design, not a build. The species it uses are in `SPECIES.md`. The interface sketches are in `docs/design/sketch/`. `sketch/check.sh` compiles them with `-Werror` against this branch's engine headers, plants three bad registry rows that must fail, and prints the measured sizes quoted below. The unit table for builders is in "Units".

Labels used throughout: **measured** (a command run on 2026-10-13, or a number from HANDOFF.md or DESIGN.md section 8), **arithmetic**, or **estimate**.

## In fifteen lines

1. **Species** are `species.def` rows plus three genes: SpeciesGene (body), PowerGene (power) and KnackGene (taste). No gene means grungo, so every pet alive today is unchanged.
2. **A device gets its species** from a founding nest of three eggs, one per element, rolled from the MAC and picked by the kid. The owner can pin one from the website first.
3. **To duel,** each kid presses BOOT twice on their own device. That press is the only consent, and only it turns the radio on.
4. **To match,** the kids either point at each other's pet on the rim and knock, or clink the two devices together.
5. **A turn:** tilt to Pounce, Guard or Trick, then knock to lock. BOOT fires the power when the meter is full, a shake makes the pet foretell the friend's move, and a double knock sends a cheer. After 10 s the pet picks.
6. **Resolution** is one pure function, `resolve(cardA, cardB, seed, movesA, movesB)`. It is integer and mirror-symmetric, and it replays from turn 0 every time.
7. **Each device writes only its own move log.** Every packet carries the whole log, and the receiver keeps the longest consistent prefix. Loss, duplicates and reordering only delay a turn; they never change it.
8. **The wheel** is two rock-paper-scissors cycles. Stances: Pounce > Trick > Guard. Elements: Splash > Spark > Sprout. Nine powers are tuned by one cost column. Measured: every species wins 0.450 to 0.546 of its battles.
9. **Fairness** is skill-based. The kid who has been playing worse gets up to two extra hearts and a brighter "whisper" of what the pet has read about the friend. Level, age and breeding add no strength.
10. **Feelings:** five duel stimuli go through the pet's own genes, behind a code allowlist (`kidSafe`) that refuses injury, life, toxin and pain. A faint is a nap.
11. **Both sides gain** XP, a friend-book entry, the befriended stimulus and the chance of a friend egg. A pet that is not up for it declines with a picture.
12. **Learning:** the pet's duel Skill table learns which stance works against which kind of friend. It drives the 10 s autopilot and the lean, and v2's lore can make it heritable.
13. **Radio:** ESP-NOW v1 on fixed channel 1 at 10 dBm, with Wi-Fi up only while a duel is open. Packets are 76 to 223 B, under the 250 B frame (measured). BLE stays up through IDF software coexistence.
14. **OTA:** one atomic `RadioLease` keeps duels and OTA apart, and no duel runs on an image still on trial.
15. **Save formats:** two Clutch-chunk trailing bytes, three new gene kinds, and two new petfs files. `lineage.log` and `kFormatVersion` are unchanged.

## Usage

### What two kids do (design fiction)

> Maya (7) and her dad both press BOOT twice. Each pet hops into a little ring and the rim starts to twinkle. Dad's Fizz appears on Maya's rim, and Maya's Gilly appears on his. They clink the devices together, and both screens flash **VS**.
>
> Three glyphs light the rim: an orange paw on the left (Pounce), a blue leaf shield on the right (Guard) and a purple swirl toward her (Trick). Gilly leans toward the shield and it glows softly, because Gilly has noticed that Fizz pounces a lot. Maya tilts right and knocks, and the shield stamps. A tick mark pops over Fizz: Dad has locked too. Both stances pop up. Shield beats paw: **BONK!** Fizz loses a heart.
>
> Maya shakes the device. Gilly's eyes swirl and a bubble rises over Fizz with a paw in it: "HE WILL POUNCE. HOW FUN!" Gilly was right, and she puffs up with pride.
>
> Dad fires Static Spark. Fizz's tail crackles and Gilly jumps, losing a heart whatever the clash. Gilly's meter fills, Maya presses BOOT, and Gilly's gills glow as a heart grows back. Eleven turns later Fizz swoons with spiral eyes. Both pets sit side by side under a heart. "Hug your pet!" Maya cradles Gilly and Dad cradles Fizz, two hearts fill, and a speckled egg rolls onto both rims. Fizz naps for three minutes and wakes up happy.

### The callers (compiled in `sketch/usage.cpp`)

```cpp
// firmware setup: a fresh device shows its nest before the Dish exists
if (std::optional<Genome> founder = chooseFounder(store, seed, read, millis)) options.founder = &*founder;
options.air = &espNowAir;                                  // hw::EspNowAir over the shared hw::RadioLease
dish.emplace(store, seed, lineage, options);

// firmware frame: the duel scene replaces the dish while one is open
if (a.dueling) paint::drawDuel(a.duel, a, paint::packFor(a.species), paint::packFor(a.duel.them.species), canvas, now);

// the OTA's BEGIN (DESIGN unit 22): one lease decides
if (!lease.acquire(hw::RadioUse::Ota)) return reply("OTA err busy:duel");

// dish.cpp runOneTick: the Dish's one call
saveNow |= duel_.step(occupant, lineage, options.air, raw, tiltX, tiltY, lineageId, petDay, tick);

// a native test: two sessions, 30% loss, 10% doubles, 20% reorder, up to 0.8 s late; the same battle
EXPECT_EQ(metA->battle, metB->battle); EXPECT_EQ(metA->outcomeHash, metB->outcomeHash);

// the pure core
Outcome o = resolve(cardA, cardB, duelSeed(nonceA, nonceB, rematch), movesA, movesB);
```

## Battle flow

### Consent and discovery

1. **Ready.** A kid presses BOOT twice. That is v2's game-wheel gesture. Once v2's games registry lands, the duel becomes a wheel row: `BLORB_GAME(6, duel, Duel, captures(knock, double_knock, button), 300, 0, 12, 3, duel)`. `DishDuel` checks `readiness()`, makes a `LocalConsent` (the only place one is made), and opens the session. Opening turns Wi-Fi on and starts a beacon every 300 ms. **That press is the consent on that device.** A device whose owner did not press has its radio off: it hears nothing and sends nothing.
2. **Declines.** A pet that is asleep, very sleepy, very hungry, resting after a duel, or wanting a cuddle after three losses in a row does not open, and shows why as a picture (zzz, an empty bowl, a hug). A pet that declines while a friend is near shows its picture on the friend's rim too. That is empathy, not an error.
3. **The lobby.** Ready friends appear as their species' faces on the rim, up to four. Only friends heard at -62 dBm or louder count (about arm's length, an estimate that unit D12 sets on a desk). A different roster greys the friend out ("they speak another frog").
4. **Matching needs both kids, and each device decides alone.** Either each kid tilts toward the other's face and knocks, so each beacon's `wants` names the other. Or the kids **clink**: they knock the two devices together, both knocks land within 300 ms, and each is the other's only coincident knocker. Both devices apply the same rule to the same beacons, so no "accept" message exists.
5. **Greeting.** The VS splash plays until the friend's first State packet for the same battle id arrives. A one-sided match falls back to the lobby within 2 s. After 60 s in the lobby with no match, the radio goes off.

### One turn

| Gesture | In a duel | Captured? |
|---|---|---|
| Tilt left / right / toward you | aims Pounce / Guard / Trick (the rotation already follows "up", so left is left) | |
| Knock | locks the aimed stance | yes |
| BOOT press | arms the power, if the meter holds its cost (otherwise the meter flashes "not yet") | yes |
| Double knock | sends a cheer (a heart, star, laugh or wave) | yes |
| Shake | the pet foretells the friend's next stance (ask 25: a shake always tells the future) | never |
| BOOT hold 1 s | a pellet drops for later, as always | never |
| Face down 2 s | ends the duel as a draw for both | never |
| Drop | ends the duel as a draw; the drop reaches the pet as a real drop | never |

"Captured" stimuli are the duel's, so the pet does not flinch at every lock-in knock. A `static_assert` in `duel_session.h` keeps shake, BOOT hold, drop and lid out of the capture mask.

The rim ring is a 10 s timer. When it runs out the pet chooses, from its Skill and its lean, and the screen celebrates it ("Gilly picked!"). A 6-year-old who only watches still plays a whole battle. Once both stances are known the turn reveals: both stances pop up, the "beats" arrow shows, and the bonk plays with hearts flying, which takes 3 s. A full battle is a median of 12 turns (measured in the probe), so about 2 to 3 minutes (arithmetic).

### Ending, friends, rematch and the friend egg

- A side at 0 hearts faints: spiral eyes, then a nap. After 15 turns, more hearts wins, and equal hearts is a draw.
- The friends screen shows both pets side by side under a heart. Each side records the Meeting in its duel book (idempotent by battle id).
- **Rematch:** both kids knock. The rematch number goes into both the seed and the battle id, so a late packet from the last bout never feeds the next one. Candidate A kept one id across rematches; the cross-judge found that a late State could then seed the new bout's empty log.
- **Friend egg:** "hug your pet". Each kid cradles their own pet for 3 s within 10 s. When both State packets say `hugged`, the devices swap genomes (below) and each stores a friend egg for its next clutch. `SPECIES.md` has the genetics.
- The lid, a drop, 20 s of silence ("wandered off") or a log conflict ends the duel as a draw for both.

## Resolution and convergence

`resolve()` is the contract both devices share. It reads two cards, one seed and two move logs, replays every turn both logs reach from turn 0, and returns an `Outcome`. The turn order is in `duel.h`:

1. A dozing side (Spore Puff) has no stance this turn. It loses the clash and deals nothing.
2. **Powers.** A move that asks for its power, with a meter holding the cost, spends it. Gadget draws the power it becomes from Tinker's own luck. BeforeClash handlers read both original stances. Foresee turns Grungo's stance into the counter. Dash and Lantern win head bonks. Lantern dazzles. Hush starts.
3. **Clash.** `beats()` decides. Equal stances are a head bonk, which a side with bonk turns left wins.
4. **Damage.** One heart goes to the clash loser, unless it is hushing, or the winner is dazzled and its luck misses. Then OnDamage handlers run (Zap, Shell).
5. **After.** AfterDamage handlers run (Mend, Spores). Then sparks: +1 to each side that is not dozing, +1 more to the clash loser (the rally), capped at 6. The turn counters tick down. Nothing rewards the winner, because every winner reward snowballed in the probe.
6. **End.** A side at 0 hearts faints, and the battle ends. Both at 0 is a draw.

**Mirror symmetry.** Luck is `fnv(seed, turn, side.lineageId)` per side, the seed is symmetric in the nonces, and both handlers in one phase read the same inputs. So which device is side A (the lower MAC) changes only drawing order. A test asserts that `resolve(B, A, s, mB, mA)` mirrors `resolve(A, B, s, mA, mB)` over 10,000 random battles.

**Convergence.** Each device writes only its own `MoveLog`. The friend's log grows only by `adopt()` from a packet. Every State packet carries the sender's whole log (up to 15 moves of one byte each), so any one packet that arrives is enough.

| Weather | What happens |
|---|---|
| a packet is lost | the next one, 200 ms later, carries the same log and more |
| a packet is doubled | `adopt()` of a held prefix is a no-op (`Same`) |
| packets arrive out of order | an older packet is a shorter prefix (`Stale`), so a no-op |
| a packet arrives seconds late | the same; a different battle id is ignored outright |
| a prefix disagrees with what is held | `Conflict`: only a bug or another battle can do it; the duel ends as a draw for both |
| the friend goes silent for 20 s | "wandered off": a draw here; the friend's book records what it saw |

Senders send at 5 Hz while the friend lacks something and at 1 Hz otherwise. A turn becomes visible only when both logs reach it, so two screens may reveal a turn a few hundred milliseconds apart, but never differently. **No desync is possible by construction.** The only inputs are two cards and two nonces (in every packet) and two logs (prefix-merged), and `resolve()` is a pure function of them.

**Hidden moves.** A friend's move for the turn being chosen sits in RAM before mine is locked, but no API returns it. `Outcome` holds only turns both logs reach. `DuelView` shows `theyLocked`, not their stance. `petChoice` and `foretell` read only resolved turns. That is enough secrecy among a family's gifts. Commit-reveal would double the round trips to defend against a threat that does not exist here.

## The wheel and balance

`SPECIES.md` has the nine and their powers. An element advantage gives one head-start pip on the spark meter and a "SUPER!" flash. The probe measured a damage bonus for elements swinging pairs to 0.965 (candidate A's run), while a head-start pip barely moves them. A loved stance gives no number at all. It is the pet's lean, which the friend can learn to read: a mind game for a parent, and a habit a kid learns about their own pet.

**Measured balance** (`sketch/tools/roster_probe.py 1500`, the synthesized rules with the roster's nine powers, Random vs Random, 81 pairs x 1,500 battles):

| Species | Grungo | Mossby | Puffkin | Gilly | Mothwick | Mudge | Fizz | Hushwing | Tinker |
|---|---|---|---|---|---|---|---|---|---|
| Win rate | 0.499 | 0.546 | 0.450 | 0.492 | 0.512 | 0.509 | 0.492 | 0.497 | 0.504 |

The worst single pair is Hushwing over Mudge at 0.672, and the median battle is 12 turns. The first pass, with flat costs and one-turn Hush and Lantern, had Hushwing at 0.328 and Mossby at 0.620. Three changes moved it: Hush and Lantern last two turns, Shell no longer refunds a spark, and each power got its own cost. Then a cost sweep settled Dash at 3 and Spores at 6. Each sweep step is in the probe's history and reruns in about 30 s. The C++ gate (`tools/duel_balance.cpp`, unit D4) re-measures through the real `resolve()`: species within 430 to 570 permille, pairs within 300 to 700.

## Fairness: a 6-year-old against a parent

The probe's scripted players: a **parent** who counters the friend's most frequent stance, a **habit kid** who picks their pet's loved stance 60 percent of the time, and a **lean follower** who takes the pet's whispered pick half the time.

| Parent's win rate | no help | 1 heart | 2 hearts | 3 hearts |
|---|---|---|---|---|
| against a habit kid | 0.923 | 0.872 | 0.801 | 0.705 |
| against a kid who follows the lean half the time | 0.491 | 0.311 | 0.172 | 0.092 |

All measured. The lesson is that hearts alone barely rescue a habit-bound kid, while the pet's whisper turns a 0.92 rout into a coin flip. The design uses both, in this order:

1. **The pet whispers.** The pet's lean is its Skill reading of the friend, and it always shows as a soft glow on one glyph. Its brightness rises with the assist, so a young kid is shown the reading the pet has made. Following your pet is how a 6-year-old beats a parent, and it is what the toy wants kids to learn anyway.
2. **Assist hearts, by skill and not by level** (graft from candidate C). The card carries the kid's **prowess**, a running mean (1/8 step) of how well that kid has picked: the share of turns the kid chose (not the pet) that won or bonked. Inside a 24-point dead band nobody gets help. Past it, the weaker side gets one heart per 32 points, at most two. The stronger side loses nothing. A kid who starts following the lean plays better, their prowess rises, and the help backs off by itself, so the 0.311 and 0.172 cells do not persist.
3. **Nothing makes a pet stronger.** Level, age, generation and breeding change no number in `resolve()`. Level is a badge on the rim. A parent's ten-generation line is no tougher than a newborn.
4. **The shake.** A shake reads the friend's habit aloud ("HE WILL POUNCE"). It is the same reading as the whisper, said in the pet's voice and judged when the turn reveals.

Candidate A's level-based underdog hearts were rejected. Level counts battles, not skill, so a parent who rarely duels would get help against a kid who duels daily.

## Traits and learning

- **Species:** element, power and art.
- **Knack** (a gene): the lean's prior, the autopilot's taste and a sparkle when the pet wins with its best stance. It is heritable and mutable, so a line becomes recognisable ("our Gillys always guard").
- **Mood** (graft from candidate B): nerve, pep, spirit and trust, frozen from his chemistry into the card when the duel opens. A frightened pet leans Guard, a bored one leans Pounce, and a trusting pet leans the way his kid usually goes. It colours only the lean and the autopilot, never a number in `resolve()`. It is frozen so both devices read the same bytes while his live chemistry goes on beside the duel.
- **Skill** (graft from candidate B, keyed by whom he fights): a 12 x 3 table of Q15 cells, 74 B (measured `sizeof`). The context is the friend's element (3) times the friend's last shown stance or none (4), and the moves are the three stances. NLMS learns after each resolved turn ("against Spark pets who just pounced, Guard works"). This is v2's `Skill` / `chooseMove` / `learnMove` contract for a games.def row. Until v2's Games chunk exists it is saved in `duels.log` per generation, so each life learns afresh. Once v2's lore (U12) lands, the strongest cells pass on as Knack lore: a hatchling can know "against the sparky kind, guard (since gen 3)".
- **The brain learns duels the ordinary way.** Duel outcomes are stimuli, so W credits actions with the drives they move, and once v2's oracle is built it learns that two BOOT presses bring friends.
- **The shake prophecy is a real prediction** from the friend's resolved history (habit, species style). Grungo reads two turns of pattern instead of one, so the seer is the better seer, with or without his power.

## What the pets feel

Duel outcomes reach the creature only as stimuli, through his own stimulus genes, so a lineage can drift toward proud, shy or goofy. There are five stimuli with a new `StimSource::Duel` (ids 43 to 47, a gap v2 leaves) and two chemicals (25 and 26). Every delta passes `kidSafe()`.

| Stimulus | Starter gene effect | What you see |
|---|---|---|
| `duel_bonked` | adrenaline +0.2, fear +0.05 | a flinch (hood up), an alarmed blink |
| `duel_landed` | pluck +0.3, boredom -0.2 | a happy bounce |
| `duel_won` | pluck +0.5, boredom -0.4, loneliness -0.2 | hop_circles |
| `duel_fainted` | daze +1.0, fear -0.3 | spiral eyes, then a nap: daze (half-life 90 s) feeds `sleep_gate` |
| `duel_befriended` (both sides, always) | loneliness -0.5, need_touch -0.2, boredom -0.3, sleepiness +0.15 | content, a little sleepy |

`daze` is the faint. It ends in a nap of about 3 to 4 minutes (arithmetic from its 90 s half-life and a receptor threshold). `pluck` is the battle high, and it reacts boredom away. The loser's swoon is calming (fear falls), and winner and loser both get `duel_befriended`. Its small sleepiness rise means that after about five duels in an hour (an estimate from the gene amounts) the pets yawn, and at 0.8 sleepiness they decline ("too sleepy"). That is a session limit parents will like, made of chemistry rather than a timer.

**`kidSafe()` is an allowlist in code, not a gene** (`sketch/blorb/kid_safe.h`). Genes mutate, and every body byte is legal, so a descendant could carry a gene that makes `duel_bonked` raise injury. The creature's stimulus step passes every Duel-sourced delta through `kidSafe()`. Drives may fall freely and rise by at most 0.3. Pain may not rise. Adrenaline, daze and pluck may move. Everything else is zero, including any chemical added next year, until someone decides it is safe. Seven `static_assert`s hold the rule. Candidates B and C used denylists, which miss a private chemical a mutation wires to the die locus.

**Lineages from before duels.** A genome founded before this design has no duel feeling genes, so its pet would feel nothing. On the first duel open (never at boot, and never for a pet that never duels), `graftDuelGenes` appends the missing genes once, idempotently by uid, and records a lineage Checkpoint, the path `TWIST gene_edit` already uses. That edits a living keepsake genome, so it is open question 6. Without it, the pet still faints and recovers in the scene, but its chemistry does not react.

## What each kid sees

The 240 px round screen uses the same layout on both devices, mirrored.

- **Me** at the bottom, large, facing up, with my live face and reflexes. **The friend** at the top, smaller, drawn from their `CardLook` with their species pack.
- **Rim:** my hearts on the lower-left arc and theirs on the upper-right, with spark pips beside each. While choosing, the three stance glyphs sit at 8, 4 and 6 o'clock, and the shrinking ring is the timer. My pet's lean glows on one glyph.
- **Over the friend:** "..." while they choose, a tick once they lock, the prophecy bubble and incoming cheers.
- **Centre:** the reveal (two glyphs and the "beats" arrow), BONK! or a bonk of heads, SUPER!, power glyphs and their effects (Grungo's Foresee reuses the existing halo code), the faint and the friends photo.
- **Assist hearts** look like the pet cheering, never like a label.
- **No reading needed.** Text appears only as the pet's voice line on the existing marquee. Everything a 6-year-old needs is a picture, and no state is shown by colour alone.

## Kid safety and fairness

| Rule (owner, binding) | Where it is enforced |
|---|---|
| Never kills, injures, poisons or hurts | `kidSafe()` allowlist on every Duel-sourced delta (`static_assert`s); `test_two_dishes` compares injury, toxin and life against a control twin with no duel, over starters and mutated descendants |
| Never permanently weakens | the card has no stat a loss lowers; XP only grows (a loss earns 2); Skill changes choices, not numbers |
| The loser faints and recovers | `duel_fainted` raises daze, and daze feeds `sleep_gate` (a nap); nothing reads daze into the die or cause loci |
| Both sides gain | XP, `duel_befriended`, a friend-book entry and a friend-egg chance for both |
| A 6-year-old against a parent is fun | the whisper, prowess assist hearts, no stats, the autopilot, the shake's reading |
| No battle without consent on that device | the `LocalConsent` type; the Air is closed by default; no verb, twist or packet path to `open`; the `closedDeviceIsDeaf` test |
| No cross without consent on both devices | each kid's own hug, carried as a flag in their own State |
| Never during an OTA | `RadioLease`, plus refusal while the image is on trial |
| No stranger contact | no names or text on the air (species, tints, stage only); picture cheers; a match needs mutual choice or a clink; 10 dBm and a -62 dBm gate make the range a room |

## Radio

- **ESP-NOW v1, 250 B a frame.** The core is Arduino-ESP32 2.0.17 on IDF 4.4 (measured: `framework-arduinoespressif32` 3.20017.241212). `ESP_NOW_MAX_DATA_LEN` is 250 (measured, `esp_now.h`). Packets never split. A beacon is 76 B, a State 99 B and a genome chunk 223 B (measured by `sketch/tools/sizes.cpp`, with a `static_assert` on 250).
- **Framing:** `'b' 'D'`, version, kind, body, CRC32. Three kinds, all state-based. Beacons are broadcast. State and Genome packets are unicast to the matched peer for the link-layer ack and retry. `decode()` is the boundary. It refuses a short frame, a wrong magic, a newer version, an unknown kind, a bad CRC, an unknown card version, a move count past 15 and a chunk past the genome cap. No packet type crosses above the session.
- **Channel 1, fixed.** No access point is ever joined. Any fixed channel works, and changing it is one constant. It matches the sibling badge's mirror channel, whose frames fail our magic bytes.
- **10 dBm on Wi-Fi** (`esp_wifi_set_max_tx_power(40)`) with a -62 dBm receive gate (graft from candidate C): a room, not a street. BLE keeps its own +21 dBm. IDF 4.4's ESP-NOW receive callback carries no RSSI (measured: `esp_now_recv_cb_t` takes mac, data and length only), so RSSI comes from a promiscuous-mode rx hook matched by source MAC. Unit D12 confirms that hook works beside ESP-NOW on the board.
- **BLE coexistence.** IDF software coexistence is compiled in for this board's variant (measured: `CONFIG_ESP32_WIFI_SW_COEXIST_ENABLE 1` in `esp32s3/qio_qspi/include/sdkconfig.h`). BLE keeps advertising during a duel, so a parent's phone can watch through `SUB` events. Turns are human-paced, so coexistence time-slicing costs nothing visible. Loss under coexistence is unmeasured, and the protocol tolerates any loss short of silence. The lever, if coexistence is bad on the board, is to pause BLE advertising while a duel holds the lease (candidate C's choice).
- **Wi-Fi exists only while a duel is open.** `open()` runs `WiFi.mode(STA)`, sets the channel and power and calls `esp_now_init`. `close()` deinits and runs `WiFi.mode(OFF)`, which returns the heap. A pet that never duels never pays for Wi-Fi.
- **The OTA interlock is one lease.** `hw::RadioLease` (`sketch/hw/radio.h`) is an atomic compare-and-swap from Idle to Duel or Ota (graft from candidate B, because the OTA receiver may ask from the BLE host task). The OTA's BEGIN replies `OTA err busy:duel` while a duel holds it, and `Air::open()` fails while the OTA holds it, so the pet shakes his head. A duel is also refused while the running image is still on trial (`ESP_OTA_IMG_PENDING_VERIFY`), so a trial image never writes a duel book entry or a friend egg that a rollback would meet. The `ota` sibling has been sent this proposal.
- **The friend egg's genome swap** uses Genome packets: a 64-bit bitmap of the friend's chunks I hold, plus one 200 B chunk of mine they lack, round-robin. It is state-based, so loss and duplicates are harmless. Grungo's 1,528 B genome is 8 chunks (arithmetic), and the 8 KB cap is 41. If the swap does not finish in 10 s, the egg fizzles into sparkles and nothing is stored.

## Budgets

| Item | Size | Label |
|---|---|---|
| `DishDuel` (session and book), a member of `Dish` | 952 B | measured, `sizeof` x86-64 (`sizes.cpp`); no pointers except the session's genome buffer and storage, so the S3 layout matches up to padding (inferred) |
| `DuelView` added to `Appearance` | 392 B | measured, x86-64 |
| `Outcome` on the stack during `resolve` | 393 B, under the 2,560 B frame cap | measured `sizeof`; the cap is `tools/stack_check.sh`'s |
| `BattleCard` / `DuelSkill` / `MoveLog` | 64 / 74 / 16 B | measured, x86-64 |
| Friend's genome during a swap | 1.5 KB typical, 8 KB at most, in PSRAM on the board | HANDOFF's GENOME reply (1,528 B); `kMaxGenomeBytes` |
| ESP-NOW receive ring (8 datagrams) | about 2 KB | arithmetic |
| Wi-Fi and ESP-NOW heap while a duel is open | 30 to 60 KB internal | **estimate**. `CONFIG_SPIRAM_TRY_ALLOCATE_WIFI_LWIP 1` lets buffers go to PSRAM (measured, sdkconfig), but the 8 static RX buffers stay internal (measured, `CONFIG_ESP32_WIFI_STATIC_RX_BUFFER_NUM 8`) |
| Free internal heap during a duel | about 75 to 105 KB against the 24 KB floor | arithmetic over 136,244 B free with BLE advertising (measured, HANDOFF) minus the estimate |
| Free heap outside a duel | unchanged | Wi-Fi is deinit at close |
| Compute | `resolve()` over 15 turns, the codec and 5 Hz sends: well under 0.1 percent of a core | estimate |
| Drawing a second pet | one more sprite composite a frame, inside the 40 ms frame (23 ms SPI push, measured) | estimate |
| Nine species packs | about 630 KB of the 2 MiB `art` partition | estimate (70 KB each, grungo.md) |
| Wi-Fi and ESP-NOW code | 300 to 500 KB of the 4 MB app slot (about 3.35 MB free) | estimate; the free figure is measured (sprite-space.md) |
| `duels.log` on petfs | under 64 KB, compacted to one summary per friend | arithmetic (about 120 B a duel) |
| `friend.egg` on petfs | 24 B record plus the friend's genome (8 KB at most) | measured `sizeof`; cap from `kMaxGenomeBytes` |

**The first hardware gate** (unit D12) is a soak: open, duel and close three times with a phone connected, then a friend-egg swap. It logs `minheap` and the largest internal block, and fails if `minheap` drops under 40 KB.

## Testing without hardware

All in `pio test -e native` in WSL `survivor`, except the sim film.

1. **`test_species`** (D3): nine viable starters, each expressing its own species and power; no SpeciesGene expresses grungo; the `d30f00d1` fixture unchanged; the nest holds one egg per element.
2. **`test_duel_resolve`** (D4): golden battles (fixed cards, seed and logs give a fixed `Outcome::hash`); the mirror property over 10,000 random battles; prefix monotonicity (resolving N+1 turns never changes turns 0 to N); a short meter plays plain; hearts stay within 0 to max; one case per power, including Gadget's draw and the two-turn counters.
3. **`test_duel_wire`** (D6): round trips; the size asserts; a fuzz of 1,000,000 random buffers (decode never crashes; a bad CRC is refused); a newer version refused.
4. **`test_duel_session`** (D6): two sessions on one `Ether` with seeded weather (30 percent drop, 10 percent doubles, 20 percent reorder, up to 0.8 s late). Over 1,000 seeds both Meetings exist, name the same `BattleId` and `outcomeHash`, and mirror `me`. Silence fizzles both within 20 s. A lid on one side ends both. A late packet from bout n never reaches bout n+1. A peer under -62 dBm is never matched, and a declining one is never matched. Only the coincident pair of three devices matches on a clink. `closedDeviceIsDeaf` holds. A refused `Air::open` (OTA) leaves the session closed. The genome swap completes at 30 percent loss and fizzles cleanly when cut off.
5. **`test_two_dishes`** (D9): two whole `Dish`es, each with `MemStorage` and a `FakeAir`, driven by scripted 50 Hz BodySamples (tilt, knock, shake, cradle). A full duel runs. The loser's daze rises, he naps and wakes. Both get `duel_befriended`. Both books hold the meeting. Injury, toxin and life equal a control twin's. A shake mid-duel hops him and judges the prophecy. Both hugs give each side a `friend.egg`. `body_only_full_life` still passes.
6. **`test_friend_egg`** (D8): the `SPECIES.md` list (replay without the friend's genome, viability fallback, hybrid rate, the timeout never picks it, an old reader loads the Clutch chunk).
7. **`tools/duel_balance`** (D4): the 9 x 9 tournament through the real `resolve()`, gated on the bounds above.
8. **The sim film** (D13; WSL, SDL headless): `sim --duel-bot` gives the simulator a FakeAir whose other end is an in-process `DuelSession` with a scripted kid, on the fixed clock, so two runs are byte-identical. The goldens are the lobby with a friend, VS, choosing with the lean, locked with the friend's tick, a reveal, SUPER!, each of the nine powers, a prophecy bubble, the faint, the friends photo, the hug and the friend egg rolling in, and the founding nest. Each is looked at before it is blessed.
9. **What needs hardware** (D12): the Wi-Fi heap; coexistence loss with a phone connected; whether a device-to-device clink registers on both QMI8658 tap engines; the RSSI hook and the -62 dBm distance; range in a house; OTA refused mid-duel.

## Save-format changes (the pet is a keepsake)

- **Keepsake `snap.a/b`:** the Clutch chunk gains trailing bytes for the friend egg (index and cross seed). Older readers ignore trailing bytes, as the Body chunk's mouth byte already relies on. `kFormatVersion` stays 1. A committed fixture proves it. No other chunk changes.
- **`lineage.log`: none.** A friend egg's cross is MutPoint ops in an ordinary Birth. The one-time duel-gene graft is a Checkpoint, an entry kind that exists today.
- **New gene kinds** 0x24, 0x25 and 0x1A. Older firmware carries them unexpressed.
- **New petfs files** `duels.log` and `friend.egg`. Older firmware never opens them.
- **Living pets' genomes are untouched**, except by the one-time graft (open question 6).
- **While an image is on trial, no duel runs**, so nothing new is written that a rollback could meet.

## Fit with v2

v2 is designed and unbuilt (`origin/notes`, `design/v2/final`). Duels fit it without waiting for it:

- **Games registry (v2 U10):** the duel becomes a `games.def` row (12 contexts x 3 moves, capturing knock, double knock and BOOT press, never a shake). Until then `DishDuel` steps it directly with its own capture mask, and `DuelSkill` is the same `Skill` shape.
- **BOOT routing (v2 U3):** two presses open the wheel, and the duel is a wheel row. Until U3, unit D9 adds a minimal double-press detector that U3 replaces.
- **Oracle (v2 U4):** the shake prophecy is a duel-local reading today. Once the oracle exists, "a friend arrives" can be an omen, and duels feed `E` like any stimulus.
- **Lore (v2 U12):** strong duel Skill cells become Knack lore at death.
- **Bond (v2 U20):** a duel friend is not the owner, so duels do not touch the owner print.

## Synthesis decision

The design came from the pstack architect skill: Phase A grounding, then three runners in an arena, each on a different whole shape. Panel: opus, opus, sonnet.

| Candidate | Shape | Cross-judge total (sonnet) |
|---|---|---|
| A (opus) | **The lockstep duel.** Kids pick each turn's stance with the body; a pure `resolve()` over per-side append-only move logs | 25 / 30 |
| B (opus) | **Pets fight, kids coach.** An autobattler; the pet's genes, mood and Skill pick moves; kids cheer, call guard, call the power; lockstep inputs land two turns later | 24 / 30 |
| C (sonnet) | **Contests, not combat.** A best-of-three of two-screen body games (Marble Mail, Croak-Back, Foretell); powers bend one rule per game; no hit points | 21 / 30 |

**Base: A.** It has the simplest protocol that converges provably (state-based logs, no ack protocol, no host), the strongest structural safety (an allowlist and a consent type), kid agency through the body, no keepsake or lineage format change of its own, and the only measured balance. The cross-judge and I picked it independently.

**Grafts:**

- **From C:** handicap by the kid's skill (prowess), not by level; no names or free text on the air; 10 dBm transmit and an RSSI gate; the founding nest instead of a nine-egg carousel (also B's).
- **From B:** the frozen `Mood` in the card, feeding the lean and the autopilot; Skill keyed by the friend's element; `Decline` reasons shown as pictures, with a cuddle after three losses; the friend egg, re-expressed as MutPoint ops so it needs no new lineage op; the atomic lease; the one-time duel-gene graft for older lineages, gated behind an owner question.
- **New in synthesis:** the roster binding; per-power costs and two-turn powers, tuned with a re-measured probe; the rematch number in the battle id, which fixes the judge's stale-rematch defect; the whisper that brightens with the assist, which the probe showed is the kid's biggest lever.

**Rejected from the losers:**

- B's autobattler core. It takes the body out of the turn, kids' inputs land 1.2 to 2.4 s late, and every rule becomes a desync surface between two lockstep simulations.
- B's nine-seat ring wheel. It is provably fair but unreadable to a child.
- B's 512-point stat budgets and `MutGraft`.
- C's real-time contests. Their stop-and-wait pipe broke after about 0.5 s of silence, which one coexistence stall can cause, and they depend on v2's unbuilt ball world.
- C's denylist harm set.
- A's gift genes, superseded by the friend egg.

There were no dropouts.

## Alternatives considered

- **An autobattler** (candidate B). The pet is fully alive in the fight, which suits an alife toy, but kids become spectators with a late remote control, and a 12-year-old finds it passive. This design keeps the pet's mind in the lean, the autopilot, the mood and the prophecy, so the alife core still shows.
- **Contests instead of combat** (candidate C). It is safe by construction and the most varied, but it is not the "battle" kids were promised. It needs real-time two-device play over a coexisting radio, and its feel rests on v2's marble. A contest round (Marble Mail) could later be a second duel mode.
- **Host authority** (one device resolves and broadcasts). The host's loss ends the battle, an asymmetric role has to be negotiated, and the guest must trust the host's screen. A symmetric `resolve()` costs the same code and none of that.
- **Real-time action** (tilt to dodge in a shared arena). It needs rollback netcode, rewards a parent's reflexes, and is hard to read on 240 px.
- **Commit-reveal per turn.** It doubles the round trips to defend against a threat that does not exist among a family's gifts.
- **BLE between devices.** NimBLE's central role costs more RAM and competes with the phone link and the OTA for the role, and the owner chose ESP-NOW.
- **Level-based handicap** (candidate A). Level counts duels, not skill.
- **A Skill table in a new keepsake chunk.** It is cleaner once v2's Games chunk exists, but it is a format change today for 74 B, so it waits in `duels.log`.

## Tradeoffs accepted

- We accept that a friend's unrevealed move sits in RAM, readable by a modified firmware, in exchange for one packet kind per turn and no cryptography.
- We accept that a battle one device never finished is a fizzle there and a result on the other side, in exchange for no agreement protocol beyond the logs. Rewards are nearly equal either way.
- We accept that genes shape style and not strength, in exchange for no line ever becoming a bully.
- We accept per-pair matchups up to 0.672 (measured), in exchange for nine distinct powers. The C++ harness can tighten them.
- We accept every device carrying all nine packs (about 630 KB, estimate), in exchange for drawing any friend and any hybrid.
- We accept Wi-Fi start-up latency (a guess of under a second) at each duel, in exchange for an untouched heap the rest of the time.
- We accept that only shared-locus genes cross, in exchange for no lineage format change.
- We accept the Skill table living in `duels.log` until v2's Games chunk exists, in exchange for no keepsake chunk now.

## Units

Every file has one owner. "C" is created and "E" is edited. Engine paths are under `lib/blorb/` (`include/blorb/`, `src/`, `defs/`), and tests are under `test/`. The sketch headers in `docs/design/sketch/` are each unit's starting contract. Each builder pushes every commit to its own branch, as the repository requires.

| Unit | Owns | Acceptance | Depends |
|---|---|---|---|
| **D1 Species registry** | C `species.h`, `defs/species.def`, `defs/powers.def`, `tools/species_neg_check.sh` | the wheel and power `static_assert`s; the three planted rows from `sketch/check.sh` each fail with their own message; `rosterHash` stable across builds | none |
| **D2 Feelings and the safety rule** | C `kid_safe.h`, `starter_duel.h`, `src/starter_duel.cpp`, `test/test_kid_safe/`; E `defs/stimuli.def`, `defs/chemicals.def`, `registry.h` (`StimSource::Duel`), `src/creature.cpp` (kidSafe in the stimulus step) | the `static_assert`s; a creature fed every duel stimulus 1,000 times keeps injury, toxin and life equal to a twin's; a planted gene routing `duel_bonked` to injury changes nothing; daze gives a nap and a wake | D1 |
| **D3 Species genes and starters** | C `src/species_starter.cpp`, `test/test_species/`; E `defs/gene_kinds.def`, `genes.h`, `src/genes.cpp`, `src/starter_genome.cpp`, `creature.h` (`SpeciesTraits` in Phenotype; the Clutch's friend-egg fields) | `test_species` (testing section, item 1); 10,000 mutations never move a species or power byte | D1, D2 |
| **D4 Duel rules and balance** | C `duel.h`, `src/duel_resolve.cpp`, `src/duel_powers.cpp`, `src/duel_mind.cpp` (petChoice, learnTurn, foretell), `tools/duel_balance.cpp`, `test/test_duel_resolve/` | testing item 2; the balance gate (species 430 to 570 permille, pairs 300 to 700, reader over habit at least 700); the C++ tournament reproduces `roster_probe.py` within 0.03 per species | D1 |
| **D5 Card and readiness** | C `duel_card.h`, `src/duel_card.cpp`, `test/test_duel_card/` | a card from each starter; mood bytes follow chemistry; each `Decline` reason from a crafted occupant; three losses then a cradle clears `WantsACuddle` | D3, D4 |
| **D6 Book** | C `duel_book.h`, `src/duel_book.cpp`, `test/test_duel_book/` | the fold gives XP, level, prowess and Skill; `record` is idempotent by `BattleId`; a torn tail truncates; compaction keeps one summary per friend; prowess moves by 1/8 and not at all for a pet-only battle | D4 |
| **D7 Friend egg** | C `friend_egg.h`, `src/friend_egg.cpp`, `test/test_friend_egg/`; E `src/clutch.cpp`, `src/keepsake.cpp` (Clutch trailing bytes), `test/fixtures/keepsake_v1_friend_egg.bin` | testing item 6 | D3 |
| **D8 Wire, air and session** | C `air.h`, `duel_wire.h`, `duel_session.h`, `src/duel_wire.cpp`, `src/duel_session.cpp`, `test/support/fake_air.h`, `test/test_duel_wire/`, `test/test_duel_session/` | testing items 3 and 4 | D4, D5, D6, D7 |
| **D9 Dish integration** | C `dish_duel.h`, `src/dish_duel.cpp`, `test/test_two_dishes/`; E `dish.h`, `src/dish.cpp`, `src/senses.cpp` (a minimal double-press detector until v2 U3) | testing item 5; captured knocks never reach the creature and a shake always does; a Meeting or a friend egg saves at once; `graftDuelGenes` is idempotent and recorded as one Checkpoint | D2, D8 |
| **D10 Founding** | C `founding.h`, `src/founding.cpp`, `test/test_founding/`; E `src/lineage.cpp` (`Founding.species` from the pick) | the nest's gestures pick the lit egg; the timeout keeps the first; `needsFounding` is false with any keepsake or lineage; a pinned species wins before the first hatch only | D3 |
| **D11 Presentation** | C `lib/paint/include/paint/duel.h`, `lib/paint/src/duel_scene.cpp`, `lib/paint/src/species_packs.cpp` (`packFor`), `test/test_present_duel/`; E `appearance.h`, `src/appearance.cpp` | struct goldens of DuelView-driven Appearance per phase; `packFor` falls back to grungo, tinted; no state is colour-only (a lint) | D9; the roster art |
| **D12 Firmware radio** (hardware, owner's go-ahead) | C `src/hw/radio_lease.h`, `src/hw/radio_espnow.h`; E `src/main.cpp` (the nest at first boot, the Air, the lease, `drawDuel`) | `pio run -e badge128` builds. On two boards, only with the owner's go-ahead to flash: the heap soak (`minheap` at least 40 KB); a duel and a friend egg; BLE connected throughout; OTA BEGIN refused mid-duel; a duel refused on a trial image; RSSI at 30 cm and 2 m sets `kNearRssi` | D9, D10, D11; DESIGN unit 22 adopts the lease |
| **D13 Sim film** | C `sim/duel_bot.h`, `tests/feeds/duel_*.txt`, `test/golden/duel_*.png`; E `sim/sim.cpp`, `tools/sim_film.sh` | testing item 8; each golden looked at before blessing | D11 |
| **D14 Phone window** | E `defs/commands.def` (`FRIENDS`), `src/protocol.cpp` (`! DUEL` events, FRIENDS streaming), `defs/twists.def` (`found`, `duels`), `src/twists.cpp`, `docs/ble.md` | golden transcripts; FRIENDS lists the book with no MAC or id; no verb or twist opens a duel (a test walks COMMANDS and TWISTS); `found` is refused after the first hatch; `TWIST duels off` keeps BOOT x2 from opening | D6, D9 |

No file appears in two rows. **Order:** D1. Then D2, D3 and D4 in parallel. Then D5, D6, D7 and D10 in parallel. Then D8. Then D9. Then D11 and D14. Then D12 and D13. D4 goes first among equals, because its numbers decide whether a duel is fun, and the C++ harness should confirm the probe before any radio code exists.

**Outside these units:** the nine sprite packs belong to the roster art agent (`origin/roster-art`, `pets/<slug>/` via `tools/sprite_pack.py`). D11 falls back to tinted grungo until they land.

## Open questions for the owner

Only product calls. Each has the default this design ships.

1. **Who decides each device's species?** Default: each kid picks from a nest of three (one per element), rolled from the MAC. The alternative is that you pin one species per device before you hand them out (`TWIST found` or `DEBUG found`), so the family covers all nine and no two cousins share one.
2. **Should duels be allowed between any two family devices, parent against child included?** Default: yes, with the prowess assist.
3. **Friend eggs: yes?** Default: yes, after both kids hug, one waiting at a time, and never picked by the clutch timeout. "No" drops unit D7 and the hug. Duels still work.
4. **Hybrids (a body of one species with another's power):** fine? Default: yes. Battles are then how powers spread through a family.
5. **Should a device show the friend's pet name?** Default: no. Only species, colours and stage cross the air, and the friend book shows species. The phone may let an owner nickname a friend locally later.
6. **May the pet already on the device (grungo `d30f00d1`) receive the duel feeling genes once, on his first duel, recorded in his lineage?** Default: yes ("learned to play-fight"). "No" means he faints and recovers in the scene, but his chemistry does not react to duels.
7. **Should neglect be able to kill on the children's devices?** DEVIATIONS 2 made it lethal for the adult owner. Battles never kill either way. Default for the kids' devices: off (`kNeglectCanKill = false`).
8. **Is 10 s per turn right for a 6-year-old?** Default: 10 s, after which the pet picks.
9. **A parent switch?** Default: `TWIST duels off` from the website, on by default.

## Risks

- **The Wi-Fi heap is an estimate.** D12 measures it first. The levers, in order: Wi-Fi buffers in PSRAM (already allowed), fewer static RX buffers, then pausing BLE advertising during a duel.
- **A clink may not trip both tap engines.** Pointing and knocking always works.
- **RSSI in IDF 4.4 needs a promiscuous hook.** If it misbehaves beside ESP-NOW, the gate becomes "matched by clink or mutual choice only", which is still consent on both sides.
- **The pair spread** (worst 0.672) may feel unfair within one family. D4 can tighten it with costs.
- **v2's double-press detector does not exist yet.** D9 adds a minimal one, and v2 U3 replaces it.

## Next implementation step

Build D1's tables and D4's `resolve()` with `tools/duel_balance.cpp`, and reproduce `roster_probe.py`'s numbers in C++ before any art or radio binds to them.
