# Battles

Two kids bring their devices together and their pets have a duel. Each turn, each kid secretly picks a stance with the body: tilt to aim, knock to lock in. Both devices compute the same battle from the same inputs, so they never disagree. Before it starts, both kids choose the stakes on their own device: a Spar is play, a Scrap leaves wounds and scars, and a Mortal duel can end a life. A death is never luck. A mortal loser is downed, and the winner's kid has to hold BOOT for three seconds while a skull fills to finish him. When a pet falls, its line remembers: remains, a clutch, a vendetta. The victor keeps a trophy, a scar and a feat. Pets also make friends, and if both kids hug their pets after a duel, a friend egg joins the next clutch.

The owner's ruling (2026-10-13) replaced the brief's "a battle never kills, injures or permanently weakens a pet": "Pets should die. My family has a dark sense of humor. They want something more complex." Neglect death stays as the engine has it, on every device. What is kept is consent on each device, with the stakes shown before anyone agrees, and a handicap that keeps a 6-year-old against a parent a real game.

This is a design, not a build. The species it uses are in `SPECIES.md`. The interface sketches are in `docs/design/sketch/`. `sketch/check.sh` compiles them with `-Werror` against this branch's engine headers, plants three bad registry rows that must fail, and prints the measured sizes quoted below. The unit table for builders is in "Units".

Labels used throughout: **measured** (a command run on 2026-10-13, or a number from HANDOFF.md or DESIGN.md section 8), **arithmetic**, or **estimate**.

## In fifteen lines

1. **Species** are `species.def` rows plus three genes: SpeciesGene (body), PowerGene (power) and KnackGene (taste). No gene means grungo, so every pet alive today is unchanged.
2. **A device gets its species** from a founding nest of three eggs, one per element, rolled from the MAC and picked by the kid. The owner can pin one over the cable first.
3. **To duel,** each kid presses BOOT twice on their own device. That press is the only way the radio turns on, and only a detector's press counts, never the phone's.
4. **Stakes,** chosen by both kids on their own devices, each tier confirmed by a bigger gesture. Spar is play. Scrap brings wounds, scars and trait drift. Mortal can end a life. The duel runs at the lower choice, and each pet's forecast is shown as a skull before anyone confirms.
5. **A turn:** tilt to Pounce, Guard or Trick, then knock to lock. BOOT fires the power when the meter holds its cost, a shake makes the pet foretell the friend's move, and a double knock sends a cheer. After 10 s the pet picks.
6. **Resolution** is one pure function, `resolve(cardA, cardB, seed, stakes, movesA, movesB)`. It is integer and mirror-symmetric, and it replays from turn 0 every time.
7. **Each device writes only its own move log.** Every packet carries the whole log, and the receiver keeps the longest consistent prefix. The mortal verdict rides as the winner's last log entry. Loss, duplicates and reordering only delay a turn. The stakes and the rematch number are part of the battle id.
8. **The wheel** is two rock-paper-scissors cycles. Stances: Pounce > Trick > Guard. Elements: Splash > Spark > Sprout. Nine powers are tuned by one cost column. Measured in a model where powers fire as soon as the meter is full: every species wins 0.450 to 0.546.
9. **Death is a choice, never luck.** No wound ever kills: every wound is clamped below the pet's own lethal level, less a margin as big as his own drop injury. A mortal loser is downed. Only the winner's kid decides, by holding BOOT 3 s to finish, or knocking (or letting 15 s pass on their own device) to spare.
10. **Death means something.** The fallen leaves remains and a clutch, and its line swears a vendetta: newborns seeded to fight the victor's kind, and the debt on the hatch marquee. The victor takes a trophy, a scar and the `slayer` feat. A spared pet owes a life, and the book keeps the dark ledger across generations.
11. **Fairness** is skill-based. The kid who has been playing worse gets up to two extra hearts and a brighter "whisper" of what the pet has read. Level, age and breeding add no strength. Mortal duels are refused unless the two kids are close in skill.
12. **Feelings and lasting change:** duel stimuli go through the pet's genes behind a per-tier code allowlist. Wounds are engine-applied from the agreed Outcome, never from genes. Scars last a life. `mettle` and `dread` drift for days.
13. **Learning:** a 12 x 3 duel Skill table learns which stance works against which kind of friend, and drives the 10 s autopilot and the lean. A vendetta seeds it in the next generation.
14. **Radio:** ESP-NOW v1 on fixed channel 1 at 10 dBm, with Wi-Fi up only while a duel is open. Packets are 77 to 224 B, under the 250 B frame (measured). BLE stays up through IDF software coexistence. Duels bind to OTA's own interlock: no duel while an update session is active or the image is on trial, and no update while a duel holds the radio.
15. **Save formats:** trailing bytes on the saved clutch (friend egg) and on the Body chunk (scars), three gene kinds, four petfs files. `lineage.log` and `kFormatVersion` are unchanged, because a battle death is recorded as `Injured`, which it is. One expression rule changes for every pet: only vital chemicals may write the die locus.

## Usage

### What two kids do (design fiction)

> Maya (7) and her dad both press BOOT twice. Each pet hops into a little ring and the rim starts to twinkle. Dad's Fizz appears on Maya's rim, and Maya's Gilly appears on his. They clink the devices together, and both screens flash **VS**.
>
> Three glyphs light the rim: an orange paw on the left (Pounce), a blue leaf shield on the right (Guard) and a purple swirl toward her (Trick). Gilly leans toward the shield and it glows softly, because Gilly has noticed that Fizz pounces a lot. Maya tilts right and knocks, and the shield stamps. A tick mark pops over Fizz: Dad has locked too. Both stances pop up. Shield beats paw: **BONK!** Fizz loses a heart.
>
> Maya shakes the device. Gilly's eyes swirl and a bubble rises over Fizz with a paw in it: "HE WILL POUNCE. HOW FUN!" Gilly was right, and she puffs up with pride.
>
> Dad fires Static Spark. Fizz's tail crackles and Gilly jumps, losing a heart whatever the clash. Gilly's meter fills, Maya presses BOOT, and Gilly's gills glow as a heart grows back. Eleven turns later Fizz swoons with spiral eyes. It was a Spar, so Fizz naps for three minutes and wakes up happy. Both pets sit side by side under a heart. "Hug your pet!" Maya cradles Gilly and Dad cradles Fizz, two hearts fill, and a speckled egg rolls onto both rims.

> A week later, Maya (now 8) and her cousin Theo (11) are close in skill. Both of their pets are adults. On the stakes screen Theo tilts to the skull and holds BOOT until it fills. Maya hesitates, then does the same. Both screens show the skulls: Gilly's is a sliver, and Theo's Mothwick's is half full, because Mothwick is still wounded from yesterday. Mothwick loses. She is down, wings crumpled. On Maya's screen a skull waits for her thumb, and on Theo's, Mothwick looks up at him. Maya knocks. **SPARED.** Mothwick limps home with a scar across one wing and a debt glyph in Theo's book. Three days later Mothwick wins the rematch, downs Gilly, and Theo holds BOOT. Gilly falls. Maya's clutch hatches two days after that, and the hatch marquee reads "GEN 2 REMEMBERS THE MOTH". A tiny wing-scale trophy sits on Theo's rim.

### The callers (compiled in `sketch/usage.cpp`)

```cpp
// firmware setup: a fresh device shows its nest before the Dish exists
if (std::optional<Genome> founder = chooseFounder(store, seed, read, millis)) options.founder = &*founder;
options.air = &espNowAir;                                  // hw::EspNowAir, gated on OTA's own session and trial
dish.emplace(store, seed, lineage, options);

// firmware frame: the duel scene replaces the dish while one is open
if (a.dueling) paint::drawDuel(a.duel, a, paint::packFor(a.species), paint::packFor(a.duel.them.species), canvas, now);

// OTA's interlock (branch `ota`): DishOtaHost::busy() also reports an open duel
bool DishOtaHost::busy() { return /* existing checks */ false || espNowAir.isOpen(); }

// dish.cpp runOneTick: the Dish's one call
saveNow |= duel_.step(occupant, lineage, options.air, raw, tiltX, tiltY, lineageId, petDay, tick);

// a native test: two sessions, 30% loss, 10% doubles, 20% reorder, up to 0.8 s late; the same battle
EXPECT_EQ(metA->battle, metB->battle); EXPECT_EQ(metA->outcomeHash, metB->outcomeHash);

// the pure core
Outcome o = resolve(cardA, cardB, duelSeed(nonceA, nonceB, rematch, stakes), stakes, movesA, movesB);
if (o.awaitingVerdict()) { /* Mercy: the skull waits for the winner's thumb */ }
```

## Battle flow

### Consent and discovery

1. **Ready.** A kid presses BOOT twice. That is v2's game-wheel gesture. Once v2's games registry lands, the duel becomes a wheel row: `BLORB_GAME(6, duel, Duel, captures(knock, double_knock, button), 300, 0, 12, 3, duel)`. `DishDuel` checks `readiness()`, makes a `LocalConsent` (the only place one is made), and opens the session. Opening turns Wi-Fi on and starts a beacon every 300 ms. **That press is the consent on that device.** A device whose owner did not press has its radio off: it hears nothing and sends nothing. The press must come from the detector. `Dish::fire`, which the phone's `TWIST stimulus` uses, marks what it fires as remote, and the duel ignores remote stimuli for opening and for every in-duel gesture. `TWIST stimulus` also refuses the double-press and every Duel-source stimulus outright.
2. **Declines.** A pet that is asleep, very sleepy, very hungry, resting after a duel, or wanting a cuddle after three losses in a row does not open, and shows why as a picture (zzz, an empty bowl, a hug). A pet that declines while a friend is near shows its picture on the friend's rim too. That is empathy, not an error.
3. **The lobby.** Ready friends appear as their species' faces on the rim, up to four. Only friends heard at -62 dBm or louder count (about arm's length, an estimate that unit D12 sets on a desk). A different roster greys the friend out ("they speak another frog").
4. **Matching needs both kids, and each device decides alone.** Either each kid tilts toward the other's face and knocks, so each beacon's `wants` names the other. Or the kids **clink**: they knock the two devices together, both knocks land within 300 ms, and each is the other's only coincident knocker. Both devices apply the same rule to the same beacons, so no "accept" message exists.
5. **Greeting.** The VS splash plays until the friend's first State packet arrives. A one-sided match falls back to the lobby within 2 s. After 60 s in the lobby with no match, the radio goes off.
6. **Stakes.** Both kids tilt to a tier on the rim and see both pets' skulls (each pet's forecast at that tier). Tiers a pet or the fairness rule forbids are greyed, with the reason as a picture: a cradle for "too young", a balance for "not a fair fight", a skull for "a scrap could kill him". Each pet's risk is drawn on its own device as pictures, not words: the pet itself on the tier glyph, a bandage over him for Scrap, and for Mortal a skull that fills as far as the worst case would bring him. The friend's pet gets the same pictures in the friend's corner. Each kid confirms with their tier's gesture. Spar is a knock, Scrap a BOOT press, and Mortal a BOOT hold of 3 s while a skull fills. The duel runs at `agreedStakes(mine, theirs)`, the lower of the two, which both screens show before turn 0. Either kid can always pull it down to a Spar. A choice is frozen once confirmed. Until turn 0 the pair is matched without the stakes in its id, and from turn 0 the id carries them. A State that already names the agreed tier's id counts as the peer's confirm, so a lost confirm packet cannot strand the step. Each side's hold progress travels in its State, so both screens show both skulls filling. Nothing is applied to either pet without this step.

### One turn

| Gesture | In a duel | Captured? |
|---|---|---|
| Tilt left / right / toward you | aims Pounce / Guard / Trick (the rotation already follows "up", so left is left) | |
| Knock | locks the aimed stance | yes |
| BOOT press | arms the power, if the meter holds its cost (otherwise the meter flashes "not yet") | yes |
| Double knock | sends a cheer (a heart, star, laugh or wave) | yes |
| Shake | the pet foretells the friend's next stance (ask 25: a shake always tells the future) | never |
| BOOT hold | outside Staking and Mercy, what it always means (today a tuck-in; under v2 a pellet). In Staking it is the Mortal confirm and in Mercy the Finish, and only there is it captured | Staking and Mercy only |
| Face down 2 s | ends the duel as a draw for both | never |
| Drop | ends the duel as a draw; the drop reaches the pet as a real drop | never |

"Captured" stimuli are the duel's, so the pet does not flinch at every lock-in knock. A `static_assert` in `duel_session.h` keeps shake, BOOT hold, drop and lid out of the turn's capture mask. The one exception is the BOOT hold in Staking and Mercy, where the hold is the confirm or the Finish. Taking it there means a mortal confirm does not also tuck the pet in. `DuelInput` carries the button's level (`bootDown`) so a 3 s hold can be timed.

The rim ring is a 10 s timer. When it runs out the pet chooses, from its Skill and its lean, and the screen celebrates it ("Gilly picked!"). A 6-year-old who only watches still plays a whole battle. Once both stances are known the turn reveals: both stances pop up, the "beats" arrow shows, and the bonk plays with hearts flying, which takes 3 s. A full battle is a median of 12 turns (measured in the probe), so about 2 to 3 minutes (arithmetic).

### Ending, friends, rematch and the friend egg

- In a Spar or a Scrap, a side at 0 hearts faints: spiral eyes, then a nap. In a Mortal duel it is **downed**, and the Mercy phase begins ("When a pet falls"). After 15 turns, more hearts wins, and equal hearts is a draw, with nobody downed.
- Each device applies its own pet's wounds from the shared Outcome, turn by turn as each turn resolves, and saves at once. In a Scrap or a Mortal duel, each heart lost adds injury, capped by the tier and clamped below his lethal level, and the two hardest turns leave scars.
- The friends screen shows both pets side by side under a heart. Each side records the Meeting in its duel book (idempotent by battle id).
- **Rematch:** both kids knock. That closes the session and reopens it pinned to the same friend, with fresh nonces, a fresh card, readiness, ceiling and forecast, and a full Staking step. So a wounded pet cannot be rushed back in at yesterday's forecast, and a mortal rematch needs the 3 s hold again. A rematch is refused if either pet has fallen. Fresh nonces give a fresh battle id, so a late packet from the last bout never feeds the next one.
- **Friend egg:** "hug your pet". Each kid cradles their own pet for 3 s within 10 s. When both State packets say `hugged`, the devices swap genomes (below) and each stores a friend egg for its next clutch. `SPECIES.md` has the genetics.
- The lid, face down, a drop or 20 s of silence ends the duel. In a Spar it is a draw. In a Scrap or a Mortal duel, the side that left while behind **fled**: it forfeits and keeps every wound already applied. Wounds land turn by turn and are saved at once, so pulling the cable undoes nothing. The tick a Mortal duel downs a pet, its device writes a "downed, pending" entry to `duels.log`. If the kid pulls the cable before the verdict, the pet lives, and the next boot folds the entry into "fled" forever. If instead the winner's side leaves during Mercy, the downed pet lives, the book says "the victor left", and nobody is marked fled. A log conflict is a draw for both. A drop during a duel reaches the pet as the real drop it is, and the wound clamp leaves room for it ("Stakes").


## Stakes

### The ladder

| | Spar | Scrap | Mortal |
|---|---|---|---|
| Wound per heart lost | none | injury +0.06 | injury +0.09 |
| Most injury one duel can add | none | +0.35 | +0.60 |
| Scars | no | up to 2 a duel | up to 2 a duel |
| `mettle` and `dread` drift | no | yes | yes |
| A side at 0 hearts | faints, naps | faints, naps | **downed**: the winner's verdict |
| Can this duel kill? | no | no: every wound is clamped | only by the winner's Finish |
| Youngest pet allowed | Baby | Child | Adult |
| Fairness gate (assist hearts at most) | 2 | 2 | **1** |
| Kids' own picked turns, each, at least | 0 | 0 | **30** |
| Head-to-head (3 or more duels between the lines) | any | the lower side won at least 1 in 4 | the lower side won at least 1 in 4 |
| For-keeps duels between the same two lines | any | 1 a pet-day (Scrap and Mortal together) | 1 a pet-day (Scrap and Mortal together) |
| Confirm gesture | knock | BOOT press | BOOT hold 3 s |

All numbers are starting values in `stakes.h` (`kStakes`), one row per tier, for the owner to tune. The starter genome has a receptor that kills the instant injury reaches 0.9 (`starter_genome.cpp`; it is digital, not sustained), plus the slower chain of injury, ageing and pain. A pet already at 0.4 would therefore not survive an unclamped Mortal loss. So **no wound ever kills**. Every wound is clamped by `woundRoom()`: the injury at which this pet's own expressed receptors write `die`, less the injury his own `dropped` genes give, less 0.05, less what he has now. That covers a spared loser, a battered winner and a fumble mid-duel alike. The forecast on the stakes screen (`forecast()`) shows how close the worst clamped case would bring him: every heart lost, plus a drop.

**Wounds are the engine's, not the genes'.** The amounts come from the tier table, applied from the agreed Outcome by `woundFor()` turn by turn. Stimulus genes still decide what a duel feels like, but `duelFilter()` refuses any stimulus write to injury, life, toxin or an unnamed chemical at every tier. Two expression-time guards (unit D3) cover the second step. A gene reading daze or pluck may write only `sleep_gate`. And a receptor writing the die or cause locus is inert unless it reads a vital chemical (life, injury, toxin, starvation). The second guard is a rule for every pet, duel or not. It closes a mutation that wires pain, adrenaline, a drive, mettle or dread to `die`, which would otherwise fire mid-Scrap. So a duel ends a life in exactly two ways: a Finish someone chose, and the pet's own ordinary chemistry over the following days if he is badly hurt and neglected. The second is how wounds work, and the engine already has it.

**Lasting change.** Scars last a life. They are drawn over the body by kind: a claw for a Pounce, a scorch for Spark, a notch for Sprout, a ripple for Splash, a ring for a zap. They fade toward, but never to, nothing. Two trait chemicals with a half-life of about six pet-days move with every Scrap or Mortal duel. `mettle` comes from wins, and `dread` from losses and scars. Both are read into the card's Mood (nerve), so a battle-hardened pet leans bolder and a beaten one leans to Guard. Both are ordinary chemicals that his genes can wire further. Once v2's Imprint (A6) lands, a child starts nudged toward its parent's final mettle and dread.

### How lethal by default (recommendation, and the alternatives)

**Recommended: every device allows all three tiers, and the stakes screen starts on Scrap each time.** A mortal duel needs both kids to hold for the skull, two adult pets, a fair fight (assist at most one heart, 30 turns each picked by the kids themselves, and a head-to-head record that is not lopsided), and then the winner's own 3-second Finish. That makes death available and serious, and it never happens to a kid who did not choose the tier on their own device. A parent can lower a device's ceiling from the website (`TWIST stakes spar|scrap|mortal`, kept in `duel.cfg`).

How often would a mortal duel even reach a verdict? In the probe, random players end 0.687 of battles with a pet down before turn 15 (measured, 24,300 battles). A reading parent against a habit-bound kid downs the kid's pet 0.882 of the time with no help and 0.686 with two assist hearts (measured). That second pair is why Mortal needs a fair fight. Without the gate, a mortal duel between a parent and a 6-year-old would mostly be the parent deciding the fate of the child's pet. Prowess alone cannot be the gate. A new device starts at 128, and so does a kid who lets the pet pick every turn, and prowess is a win share within each kid's usual pool. So Mortal also needs 30 picked turns on each side and, once the two lines have met three times, a lower side that won at least one in four.

| Alternative | What it does | Why not the default |
|---|---|---|
| Spar only (this design before the ruling) | nothing lasting | the owner asked for death and complexity |
| Scrap ceiling | wounds and scars, death only by later neglect | death never comes from a duel itself; less drama than asked for |
| Mortal without mercy | a downed pet dies | death becomes the turn game's luck, not a choice; "cheap" |
| Death by accumulation | no Finish; injury simply kills if it crosses the line | a kid's pet dies by arithmetic nobody chose; the forecast would have to refuse most duels |
| **Recommended ladder** | Spar, Scrap, Mortal with mercy | death is real, consented to on both devices, and always someone's decision |


## When a pet falls

### Mercy

When a mortal duel downs a pet, both screens show it lying in the dish. The winner's kid decides:

- **Spare:** a knock, or 15 s of nothing on the winner's device. The downed pet keeps the wounds he took (clamped, so he lives) and his scars. The `duel_spared` stimulus (relief, fondness) goes to him, and his line now owes the victor's line a life. A debt glyph sits in both books.
- **Finish:** hold BOOT for 3 s while a skull fills, and both screens show it filling. Letting go early stops the fill and decides nothing.

The verdict is one more entry in the winner's move log, in the slot right after the downing turn. Only the winner's device ever writes it, including the Spare when its own 15 s run out. The downed side never times out by itself. It waits for that slot, for up to 20 s of silence, after which the victor has left and the downed pet lives. So both devices read one verdict, and a lost packet only delays it. The winner's device takes the slaying (trophy, feat, `duel_slew`) only once the downed side's acknowledgement covers the verdict. If no acknowledgement arrives, it records "they left before the verdict" and no kill. The fallen side keeps sending from its last scene so the acknowledgement arrives. A device that holds a verdict never cancels it for a lid.

The downed pet's kid can only plead, with a cheer that floats up on the winner's screen. That is the moment the design is built around.

### The fallen

Each device applies its own pet's fate from the shared Outcome, so nothing reaches into another kid's device. A finished pet's device calls `Creature::fall()`, which writes the die and cause loci as `Injured`. Then the ordinary death path runs: the Death entry, the remains (the converter's settled sleeping frame, with his scars), the 30-minute vigil and the clutch. It is a full roguelite death, the same as old age.

What his line keeps:

- **A vendetta.** For up to three generations, or until avenged (a Scrap or Mortal win against the victor's lineage), every newborn's duel Skill is seeded against the victor's element. The victor's lineage is marked on the rim in the lobby, and the hatch marquee names the debt ("GEN 5 REMEMBERS THE FIZZ"). It lives in `duels.log`, which outlives every death, so it needs no genome change.
- **Heirlooms as usual.** His strongest beliefs become heirloom instincts, as at any death.
- **The book** records "fell to a Fizz of <the friend's line>, turn 9, spared once before" in the dark ledger.

### The victor

- **A trophy:** the fallen species' token (a shell, a spore cap, a gill frond, a wing scale, a fin, a lamp, a feather, a gear, a hood) on the rim, the newest three shown.
- **The `duel_slew` stimulus,** which his genes turn into pride or remorse. The starter gives pride, a little `dread` and a night of uneasy dreams, so some lines will be haunted.
- **The `slayer` feat** (`feats.def`). It is lineage meta-progression, so it opens feat-gated genes forever. The starters carry a dormant war-paint mark gated on it. It is awarded at the victor's death from `duels.log` (the Dish ORs `book.featsThisLife()` into the Death entry), so `LifeStats` and its save format do not change. It is one bit per line, so it cannot be farmed.
- **The wound** of whatever hearts he lost, and often a scar.

### The survivor's memory and the bystanders

- The brain keeps the duel as one of its 16 episodes (the largest drive change), so he dreams it.
- Once v2's oracle lands, the friend's arrival becomes an omen he braces for or looks forward to.
- At their next duel with any friend whose book knew the fallen pet, devices exchange the news, and pets that knew him get `duel_mourned`: loneliness, and a little dread.

## Resolution and convergence

`resolve()` is the contract both devices share. It reads two cards, one seed and two move logs, replays every turn both logs reach from turn 0, and returns an `Outcome`. The turn order is in `duel.h`:

1. A dozing side (Spore Puff) has no stance this turn. It loses the clash and deals nothing.
2. **Powers.** A move that asks for its power, with a meter holding the cost, spends it. Gadget draws the power it becomes from Tinker's own luck. BeforeClash handlers read both original stances. Foresee turns Grungo's stance into the counter. Dash and Lantern win head bonks. Lantern dazzles. Hush starts.
3. **Clash.** `beats()` decides. Equal stances are a head bonk, which a side with bonk turns left wins.
4. **Damage.** One heart goes to the clash loser. OnDamage handlers only add hits (Zap).
5. **Block.** Block handlers only take hits away: Shell absorbs one, Hush cancels a lost clash, and a dazzled winner's hit misses on its luck. Splitting adding from taking away keeps both sides' handlers in one phase from reading what the other just wrote, which the mirror property needs (Zap against Shell).
6. **After.** AfterDamage handlers run (Mend, Spores). Then sparks: +1 to each side that is not dozing, +1 more to the clash loser (the rally), capped at 6. The turn counters tick down. Nothing rewards the winner, because every winner reward snowballed in the probe.
7. **End.** A side at 0 hearts faints, and the battle ends. Both at 0 is a draw.

**Mirror symmetry.** Luck is `fnv(seed, turn, side.lineageId)` per side, and each use draws `fnv(luck, purpose)`, so Gadget's pick and a dazzle miss in one turn are independent. Also, the seed is symmetric in the nonces, and both handlers in one phase read the same inputs. So which device is side A (the lower MAC) changes only drawing order. A test asserts that `resolve(B, A, s, mB, mA)` mirrors `resolve(A, B, s, mA, mB)` over 10,000 random battles.

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

**Measured balance** (`sketch/tools/roster_probe.py 1500`, the synthesized rules with the roster's nine powers, Random vs Random, 81 pairs x 1,500 battles). One modelling difference from the spec: the probe fires a power the moment the meter holds its cost, while a kid must press BOOT, so these numbers describe kids who always fire. D4's C++ tournament measures the armed version too.

| Species | Grungo | Mossby | Puffkin | Gilly | Mothwick | Mudge | Fizz | Hushwing | Tinker |
|---|---|---|---|---|---|---|---|---|---|
| Win rate | 0.499 | 0.546 | 0.450 | 0.492 | 0.512 | 0.509 | 0.492 | 0.497 | 0.504 |

The worst single pair is Hushwing over Mudge at 0.672, and the median battle is 12 turns. The first pass, with flat costs and one-turn Hush and Lantern, had Hushwing at 0.328 and Mossby at 0.620. Three changes moved it: Hush and Lantern last two turns, Shell no longer refunds a spark, and each power got its own cost. Then a cost sweep settled Dash at 3 and Spores at 6. Each sweep step is in the probe's history and reruns in about 30 s. The C++ gate (`tools/duel_balance.cpp`, unit D4) re-measures through the real `resolve()`: species within 430 to 570 permille, pairs within 300 to 700.

**Measured through `resolve()`** (unit D4, `tools/duel_balance.cpp 1500`, 2026-10-13; the same numbers gate `test_duel_resolve`). Same players, 81 pairs x 1,500 battles, no cost changed:

| Variant | Grungo | Mossby | Puffkin | Gilly | Mothwick | Mudge | Fizz | Hushwing | Tinker | Worst pair | Median turns |
|---|---|---|---|---|---|---|---|---|---|---|---|
| Random, always fire (the probe's model) | 0.496 | 0.543 | 0.452 | 0.493 | 0.514 | 0.509 | 0.495 | 0.499 | 0.500 | Hushwing over Mudge 0.699 | 12 |
| Random, BOOT on a random half of turns (armed) | 0.523 | 0.527 | 0.479 | 0.476 | 0.508 | 0.509 | 0.492 | 0.482 | 0.504 | Fizz over Hushwing 0.590 | 11 |
| Both pets on autopilot (`petChoice`, 300 a pair) | 0.486 | 0.601 | 0.455 | 0.513 | 0.472 | 0.489 | 0.494 | 0.459 | 0.532 | Mossby over Puffkin 0.702 | 12 |

Every species lands within 0.004 of the probe. The worst pair is 0.699 at 1,500 a pair and 0.693 at 6,000 (measured), so it sits just inside the 700 bound. A one-pip sweep of every cost found no single change that moves it without pushing another pair or species out of bounds (Hush 6 drops Hushwing to 0.418, Dash 2 puts Mudge over Fizz at 0.718; measured), so the costs stand. Arming only half the time narrows the wheel, because a power that fires later matters less. The autopilot row is the demo case. With less noise in the pet's pick (one knack rank instead of four), two autopilots replayed the same battle every time, and Grungo lost to Puffkin 300 times in 300 (measured).

## Fairness: a 6-year-old against a parent

The probe's scripted players: a **parent** who counters the friend's most frequent stance, a **habit kid** who picks their pet's loved stance 60 percent of the time, and a **lean follower** who takes the pet's whispered pick half the time. The probe models the whisper as a perfect frequency reader. D4 measured the real one: `petChoice` over a Skill table that starts each battle at zero and learns after every turn.

| Parent's win rate | no help | 1 heart | 2 hearts | 3 hearts |
|---|---|---|---|---|
| against a habit kid | 0.923 | 0.872 | 0.801 | 0.705 |
| against a kid who follows the lean half the time | 0.491 | 0.311 | 0.172 | 0.092 |
| the same, through `resolve()` and the real `petChoice` (D4) | 0.478 | 0.291 | 0.160 | (max 2) |
| against a pet on autopilot every turn (D4) | 0.156 | 0.049 | 0.012 | (max 2) |
| against a habit kid, through `resolve()` (D4) | 0.924 | 0.876 | 0.803 | (max 2) |

The first two rows are measured in the probe, with the caveat above; the D4 rows are measured through `resolve()` at 300 battles a pair. The lesson is that hearts alone barely rescue a habit-bound kid, while a good enough whisper can turn a 0.92 rout into a coin flip. The real `petChoice` is as good as the probe assumed. A pet left on autopilot beats this scripted parent 0.844 of the time, because the parent counters a habit and the pet's Skill learns the counter to the counter. A real parent adapts, so read that row as "the autopilot is no pushover", not as a ranking. The design uses both, in this order:

1. **The pet whispers.** The pet's lean is its Skill reading of the friend, and it always shows as a soft glow on one glyph. Its brightness rises with the assist, so a young kid is shown the reading the pet has made. Following your pet is how a 6-year-old beats a parent, and it is what the toy wants kids to learn anyway.
2. **Assist hearts, by skill and not by level** (graft from candidate C). The card carries the kid's **prowess**, a running mean (1/8 step) of how well that kid has picked: the share of turns the kid chose (not the pet) that won or bonked. Inside a 24-point dead band nobody gets help. Past it, the weaker side gets one heart per 32 points, at most two. The stronger side loses nothing. A kid who starts following the lean plays better, their prowess rises, and the help backs off by itself, so the 0.311 and 0.172 cells do not persist.
3. **Nothing makes a pet stronger.** Level, age, generation and breeding change no number in `resolve()`. Level is a badge on the rim. A parent's ten-generation line is no tougher than a newborn.
4. **The shake.** A shake reads the friend's habit aloud ("HE WILL POUNCE"). It is the same reading as the whisper, said in the pet's voice and judged when the turn reveals.
5. **The stakes follow the fairness.** Each kid can always pull a duel down to a Spar. Mortal is refused while the assist would exceed one heart, until both kids have picked 30 turns themselves, and when the two lines' head-to-head record is lopsided (the lower side under one win in four, over three or more duels). A parent cannot take a mortal duel against a much weaker kid. A 6-year-old who has learned to follow their pet earns their way in.
6. **No farming.** A Scrap needs the head-to-head floor too, so after three lopsided Scraps between two lines only Spars are on offer between them until the weaker side wins one. At most one for-keeps duel (Scrap or Mortal) is allowed between the same two lines per pet-day, counted from `duels.log` on both devices. A pet with injury of 0.5 or more is offered only a Spar. Together these mean a stronger player cannot grind a weaker kid's pet down, whatever the stakes. The stakes do not change who wins a turn: wounds, scars and traits never enter `resolve()`, and mettle and dread move only the lean. So the probe's balance and fairness numbers stand for every tier, and no re-run was needed.

Candidate A's level-based underdog hearts were rejected. Level counts battles, not skill, so a parent who rarely duels would get help against a kid who duels daily.

## Traits and learning

- **Species:** element, power and art.
- **Knack** (a gene): the lean's prior, the autopilot's taste and a sparkle when the pet wins with its best stance. It is heritable and mutable, so a line becomes recognisable ("our Gillys always guard").
- **Mood** (graft from candidate B): nerve, pep, spirit and trust, frozen from his chemistry into the card when the duel opens. A frightened pet leans Guard, a bored one leans Pounce, and a trusting pet leans the way his kid usually goes. It colours only the lean and the autopilot, never a number in `resolve()`. It is frozen so both devices read the same bytes while his live chemistry goes on beside the duel.
- **Skill** (graft from candidate B, keyed by whom he fights): a 12 x 3 table of Q15 cells, 74 B (measured `sizeof`). The context is the friend's element (3) times the friend's last shown stance or none (4), and the moves are the three stances. NLMS learns after each resolved turn ("against Spark pets who just pounced, Guard works"). This is v2's `Skill` / `chooseMove` / `learnMove` contract for a games.def row. Until v2's Games chunk exists it is saved in `duels.log` per generation, so each life learns afresh. Once v2's lore (U12) lands, the strongest cells pass on as Knack lore: a hatchling can know "against the sparky kind, guard (since gen 3)".
- **The brain learns duels the ordinary way.** Duel outcomes are stimuli, so W credits actions with the drives they move, and once v2's oracle is built it learns that two BOOT presses bring friends.
- **The shake prophecy is a real prediction** from the friend's resolved history (habit, species style). Grungo reads two turns of pattern instead of one, so the seer is the better seer, with or without his power.

## What the pets feel

Duel outcomes reach the creature as stimuli through his own stimulus genes, so a lineage can drift toward proud, shy, goofy or haunted. They use a new `StimSource::Duel` (ids 40 to 47, a gap v2 leaves) and four chemicals (25 to 28). Every delta passes `duelFilter()` at the agreed stakes.

| Stimulus | Starter gene effect | What you see |
|---|---|---|
| `duel_bonked` | adrenaline +0.2, fear +0.05; pain +0.1 in a Scrap or Mortal duel | a flinch (hood up), an alarmed blink |
| `duel_landed` | pluck +0.3, boredom -0.2 | a happy bounce |
| `duel_won` | pluck +0.5, boredom -0.4, loneliness -0.2; mettle +0.1 in a Scrap or Mortal duel | hop_circles |
| `duel_fainted` | daze +1.0, fear -0.3; dread +0.1 in a Scrap or Mortal duel | spiral eyes, then a nap: daze (half-life 90 s) feeds `sleep_gate` |
| `duel_befriended` (both sides, always) | loneliness -0.5, need_touch -0.2, boredom -0.3, sleepiness +0.15 | content, a little sleepy |
| `duel_spared` | fear -0.3, fondness up, dread +0.2 | he limps, then hides his face in the hood |
| `duel_slew` | pluck +0.5, dread +0.1, and a dream episode | proud, then restless that night |
| `duel_mourned` | loneliness +0.3, dread +0.05 | sits still at the rim for a while |

`daze` is the faint, and it ends in a nap of about 3 to 4 minutes (arithmetic from its 90 s half-life and a receptor threshold). `pluck` is the battle high. `mettle` and `dread` are the lasting traits (see "Stakes"). `duel_befriended`'s small sleepiness rise means that after about five duels in an hour (an estimate from the gene amounts) the pets yawn, and at 0.8 sleepiness they decline. A pet with injury of 0.5 or more is offered only a Spar until he heals (a bandage on the tier screen). He still plays, he just does not fight.

**`duelFilter()` is an allowlist in code, per tier** (`sketch/blorb/stakes.h`). Genes mutate and every body byte is legal. So genes may set how much a duel stirs him, but the code sets what a duel stimulus may touch at all. Drives may fall freely and rise by at most 0.3. Pain may rise only in a Scrap or Mortal duel. Adrenaline may rise by at most 0.3. Daze and pluck move freely. `mettle` and `dread` move only in a Scrap or Mortal duel. Injury, life, toxin and every unnamed chemical are refused at every tier, because wounds come from `woundFor()` and the tier table, never from genes. Six `static_assert`s hold this. Daze and pluck are guarded one step further: at expression, any receptor or reaction that reads them is inert unless it targets `sleep_gate` (`kDuelChemTargets`). So a mutation cannot wire a swoon to the die locus. The only death a duel itself can cause is a Finish.

**Lineages from before duels.** A genome founded before this design has no duel feeling genes, so its pet would feel nothing. On the first duel open (never at boot, and never for a pet that never duels), `graftDuelGenes` appends the missing genes once at `nextUid()` (no uid is ever reused, DEVIATIONS 6), idempotently by gene kind and stimulus id, and records a lineage Checkpoint, the path `TWIST gene_edit` already uses. Grafted genes never cross. That edits a living keepsake genome, so it is open question 7. Wounds, scars and the Finish need no genes and apply either way.

## What each kid sees

The 240 px round screen uses the same layout on both devices, mirrored.

- **Me** at the bottom, large, facing up, with my live face and reflexes. **The friend** at the top, smaller, drawn from their `CardLook` with their species pack.
- **Rim:** my hearts on the lower-left arc and theirs on the upper-right, with spark pips beside each. While choosing, the three stance glyphs sit at 8, 4 and 6 o'clock, and the shrinking ring is the timer. My pet's lean glows on one glyph.
- **Over the friend:** "..." while they choose, a tick once they lock, the prophecy bubble and incoming cheers.
- **Centre:** the reveal (two glyphs and the "beats" arrow), BONK! or a bonk of heads, SUPER!, power glyphs and their effects (Grungo's Foresee reuses the existing halo code), the faint and the friends photo.
- **Assist hearts** look like the pet cheering, never like a label.
- **Stakes and fate:** the three tier glyphs (a feather for Spar, a bandage for Scrap, a skull for Mortal) with both pets' skulls; in Mercy, the downed pet, the skull filling under the winner's thumb, and the plea cheers floating up; after, the new scars drawn on, and the fallen pet's remains in the friend's dish.
- **At home:** scars on the pet for the rest of his life, trophies and debt and vendetta marks on the rim.
- **No reading needed.** Text appears only as the pet's voice line on the existing marquee. Everything a 6-year-old needs is a picture, and no state is shown by colour alone.

## Consent, stakes and fairness

The owner's ruling removed "never kills". What remains binding, and where each rule is enforced:

| Rule | Where it is enforced |
|---|---|
| No duel without consent on that device | the `LocalConsent` type; the Air is closed by default; only detector-fired stimuli open a duel or lock a move (`SenseOut::remote`); `TWIST stimulus` refuses the double-press and Duel stimuli; the `closedDeviceIsDeaf` test, and a test that fires every stimulus name over the phone |
| The stakes are shown, and agreed, before anything is applied | the Staking phase; each tier's own confirm gesture; the duel runs at the lower choice; both forecasts on both screens; the stakes are in the battle id, so the two devices cannot run different tiers |
| Death is never random or cheap | only a Finish kills (a 3 s hold, after the battle, by the winner's kid); every wound is clamped below the pet's own lethal level with room for a drop; wounds come from a code table, not mutable genes; only vital chemicals may write `die`; the winner's device alone decides the verdict, and silence spares |
| Death means something | the ordinary death path (remains, vigil, clutch, heirlooms); the vendetta; the victor's trophy, feat and memory; the dark ledger |
| A 6-year-old against a parent is a real game | the whisper, prowess assist hearts, no stats, the autopilot, the shake's reading; Mortal refused unless the fight is fair |
| No cross without consent on both devices | each kid's own hug, carried as a flag in their own State |
| Never during an OTA | `EspNowAir::open()` refused while `ota::Session::active()` or `hw::Trial::onTrial()`; `DishOtaHost::busy()` true while a duel holds the radio, so BEGIN answers `ERR 409 BUSY` |
| A parent never farms a kid's pet | for-keeps tiers need the fair-fight gates; at most one for-keeps duel per pair of lines per pet-day; a wounded pet is offered only a Spar; each kid can always pull a duel down to a Spar |
| No stranger contact | no names or text on the air (species, tints, stage only); a beacon carries no permanent id; picture cheers; a match needs mutual choice or a clink; 10 dBm and a -62 dBm gate make the range a room |

## Radio

- **ESP-NOW v1, 250 B a frame.** The core is Arduino-ESP32 2.0.17 on IDF 4.4 (measured: `framework-arduinoespressif32` 3.20017.241212). `ESP_NOW_MAX_DATA_LEN` is 250 (measured, `esp_now.h`). Packets never split. A beacon is 77 B, a State 104 B and a genome chunk 224 B (measured by `sketch/tools/sizes.cpp`, with a `static_assert` on 250).
- **Framing:** `'b' 'D'`, version, kind, body, CRC32. Three kinds, all state-based. Beacons are broadcast. State and Genome packets are unicast to the matched peer for the link-layer ack and retry. `decode()` is the boundary. It refuses a short frame, a wrong magic, a newer version, an unknown kind, a bad CRC, an unknown card version, a move count past 15 and a chunk past the genome cap. No packet type crosses above the session.
- **Channel 1, fixed.** No access point is ever joined. Any fixed channel works, and changing it is one constant. It matches the sibling badge's mirror channel, whose frames fail our magic bytes.
- **10 dBm on Wi-Fi** (`esp_wifi_set_max_tx_power(40)`) with a -62 dBm receive gate (graft from candidate C): a room, not a street. BLE keeps its own +21 dBm. IDF 4.4's ESP-NOW receive callback carries no RSSI (measured: `esp_now_recv_cb_t` takes mac, data and length only), so RSSI comes from a promiscuous-mode rx hook matched by source MAC. Unit D12 confirms that hook works beside ESP-NOW on the board.
- **BLE coexistence.** IDF software coexistence is compiled in for this board's variant (measured: `CONFIG_ESP32_WIFI_SW_COEXIST_ENABLE 1` in `esp32s3/qio_qspi/include/sdkconfig.h`). BLE keeps advertising during a duel, so a parent's phone can watch through `SUB` events. Turns are human-paced, so coexistence time-slicing costs nothing visible. Loss under coexistence is unmeasured, and the protocol tolerates any loss short of silence. The lever, if coexistence is bad on the board, is to pause BLE advertising while a duel holds the lease (candidate C's choice).
- **Wi-Fi exists only while a duel is open.** `open()` runs `WiFi.mode(STA)`, sets the channel and power and calls `esp_now_init`. `close()` deinits and runs `WiFi.mode(OFF)`, which returns the heap. A pet that never duels never pays for Wi-Fi.
- **The OTA interlock is OTA's own** (branch `ota`, `docs/ota.md`, "Other users of the radio"). The duel binds to it rather than adding a second one. `EspNowAir::open()` is refused while `ota::Session::active()` is true (from a verified BEGIN until the session ends) or while `hw::Trial::onTrial()` is true (the running image is still pending verification), and the pet shakes his head. In the other direction, `DishOtaHost::busy()` returns true while `EspNowAir::isOpen()`, so an update answers BEGIN with `ERR 409 BUSY` and holds its restart until the duel closes. No lock is needed. The session, the trial, `busy()` and every `EspNowAir` call run on the loop task, and the BLE task only pushes image packets into OTA's ring. A trial image never opens a duel, so it never writes a duel book entry, a wound or a friend egg that a rollback would meet. This replaces the earlier `RadioLease`, at the `ota` agent's request.
- **The friend egg's genome swap** uses Genome packets: a 64-bit bitmap of the friend's chunks I hold, plus one 200 B chunk of mine they lack, round-robin, four packets per send at 5 Hz. It is state-based, so loss and duplicates are harmless. Grungo's 1,528 B genome is 8 chunks, and the 8 KB cap is 41 chunks, about 2 s without loss (arithmetic). The friends screen stays open while a swap runs. If it does not finish in 15 s, the egg fizzles into sparkles and nothing is stored.

## Budgets

| Item | Size | Label |
|---|---|---|
| `DishDuel` (session and book), a member of `Dish` | 992 B | measured, `sizeof` x86-64 (`sizes.cpp`); no pointers except the session's genome buffer and storage, so the S3 layout matches up to padding (inferred) |
| `DuelView` added to `Appearance` | 408 B | measured, x86-64 |
| `Outcome` on the stack during `resolve` | 427 B, under the 2,560 B frame cap | measured `sizeof`; the cap is `tools/stack_check.sh`'s |
| `BattleCard` / `DuelSkill` / `MoveLog` | 64 / 74 / 17 B | measured, x86-64 |
| Scars in `Body` | 4 x 4 B, saved as trailing bytes on the Body chunk | arithmetic |
| Friend's genome during a swap | 1.5 KB typical, 8 KB at most, in PSRAM on the board | HANDOFF's GENOME reply (1,528 B); `kMaxGenomeBytes` |
| ESP-NOW receive ring (8 datagrams) | about 2 KB | arithmetic |
| Wi-Fi and ESP-NOW heap while a duel is open | 30 to 60 KB internal | **estimate**. `CONFIG_SPIRAM_TRY_ALLOCATE_WIFI_LWIP 1` lets buffers go to PSRAM (measured, sdkconfig), but the 8 static RX buffers stay internal (measured, `CONFIG_ESP32_WIFI_STATIC_RX_BUFFER_NUM 8`) |
| Free internal heap during a duel | about 75 to 105 KB against the 24 KB floor | arithmetic over 136,244 B free with BLE advertising (measured, HANDOFF) minus the estimate |
| Free heap outside a duel | unchanged | Wi-Fi is deinit at close |
| Compute | `resolve()` over 15 turns, the codec and 5 Hz sends: well under 0.1 percent of a core | estimate |
| Drawing a second pet | one more sprite composite a frame, inside the 40 ms frame (23 ms SPI push, measured) | estimate |
| Nine species packs | about 630 KB of the 2 MiB `art` partition | estimate (70 KB each, grungo.md) |
| Wi-Fi and ESP-NOW code | 300 to 500 KB of the 4 MB app slot (about 3.35 MB free) | estimate; the free figure is measured (sprite-space.md) |
| `duels.log` on petfs | under 64 KB, compacted to one summary per friend | arithmetic (about 160 B a duel with framing: the 64 B card and the 74 B Skill dominate) |
| `friend.egg` on petfs | 24 B record plus the friend's genome (8 KB at most) | measured `sizeof`; cap from `kMaxGenomeBytes` |

**The first hardware gate** (unit D12) is a soak: open, duel and close three times with a phone connected, then a friend-egg swap. It logs `minheap` and the largest internal block, and fails if `minheap` drops under 40 KB.

## Testing without hardware

All in `pio test -e native` in WSL `survivor`, except the sim film.

1. **`test_species`** (D3): nine viable starters, each expressing its own species and power; no SpeciesGene expresses grungo; the `d30f00d1` fixture unchanged; the nest holds one egg per element.
2. **`test_duel_resolve`** (D4): golden battles (fixed cards, seed and logs give a fixed `Outcome::hash`); the mirror property over 10,000 random battles; prefix monotonicity (resolving N+1 turns never changes turns 0 to N); a short meter plays plain; hearts stay within 0 to max; one case per power, including Gadget's draw and the two-turn counters.
3. **`test_duel_wire`** (D6): round trips; the size asserts; a fuzz of 1,000,000 random buffers (decode never crashes; a bad CRC is refused); a newer version refused.
4. **`test_duel_session`** (D6): two sessions on one `Ether` with seeded weather (30 percent drop, 10 percent doubles, 20 percent reorder, up to 0.8 s late). Over 1,000 seeds both Meetings exist, name the same `BattleId` and `outcomeHash`, and mirror `me`. Silence fizzles both within 20 s. A lid on one side ends both. A late packet from bout n never reaches bout n+1. A peer under -62 dBm is never matched, and a declining one is never matched. Only the coincident pair of three devices matches on a clink. `closedDeviceIsDeaf` holds. A refused `Air::open` (OTA) leaves the session closed. The genome swap completes at 30 percent loss and fizzles cleanly when cut off.
5. **`test_two_dishes`** (D9): two whole `Dish`es, each with `MemStorage` and a `FakeAir`, driven by scripted 50 Hz BodySamples (tilt, knock, shake, cradle). A full duel runs. The loser's daze rises, he naps and wakes. Both get `duel_befriended`. Both books hold the meeting. In a Spar, injury, toxin and life equal a control twin's; in a Scrap, injury rises by exactly `woundFor()`'s amount and nothing else moves. A shake mid-duel hops him and judges the prophecy. Both hugs give each side a `friend.egg`. `body_only_full_life` still passes.
6. **`test_friend_egg`** (D7): the `SPECIES.md` list (replay without the friend's genome, both viability checks, the allowlist, the hybrid rate, a clutch of 3 giving up its last slot, the timeout never picks it, a reboot during the pick keeps the friend's genome, an old reader loads the clutch record).
7. **`tools/duel_balance`** (D4): the 9 x 9 tournament through the real `resolve()`, gated on the bounds above.
8. **Stakes.** Unit D2's `test_stakes`: `duelFilter`'s asserts; no sequence of wounds at any tier, from any starting injury, plus a drop, kills a starter or any of 1,000 mutated descendants (`woundRoom` holds); planted `pain -> die` and `daze -> die` receptors are inert; `fall()` runs the ordinary death path once (Death entry `Injured`, remains, clutch). Unit D8's session cases: only the winner's device writes the verdict, and its timeout writes Spare; a verdict lost, doubled or late gives the same fate on both devices; the verdict sits at index `resolved` and decodes after a downing on turn 15; the winner applies the slaying only after the acknowledgement; Mortal is refused at assist 2, under 30 picked turns, or on a lopsided head-to-head; a lost confirm does not strand Staking; a rematch reruns Staking. Unit D9's Dish cases: a cable pull mid-duel keeps every wound applied so far; a downed pet whose device reboots before the verdict boots "fled"; a slayer's line expresses the war-paint gene from the next generation; the vendetta seeds the next generation's Skill and lapses after three or on avenging.
9. **The sim film** (D13; WSL, SDL headless): `sim --duel-bot` gives the simulator a FakeAir whose other end is an in-process `DuelSession` with a scripted kid, on the fixed clock, so two runs are byte-identical. The goldens are the lobby with a friend, VS, choosing with the lean, locked with the friend's tick, a reveal, SUPER!, each of the nine powers, a prophecy bubble, the faint, the friends photo, the hug and the friend egg rolling in, the stakes screen with both skulls, mercy (the skull filling), a fall, the trophy on the rim, scars on a pet, and the founding nest. Each is looked at before it is blessed.
10. **What needs hardware** (D12): the Wi-Fi heap; coexistence loss with a phone connected; whether a device-to-device clink registers on both QMI8658 tap engines; the RSSI hook and the -62 dBm distance; range in a house; OTA refused mid-duel.

## Save-format changes (the pet is a keepsake)

- **Keepsake `snap.a/b`:** two additive changes, both trailing bytes that older decoders ignore (the Body chunk's mouth byte already relies on that, DEVIATIONS 8), so `kFormatVersion` stays 1. A committed fixture proves each one.
  - The clutch record inside `Chunk::Occupant` (`Kind::Clutch`) gains 5 bytes: the friend egg's slot and its cross seed. A friend egg lives within `kMaxClutch` (3), so the saved count and lineage's three seeds keep their shape.
  - The Body chunk gains scars. The existing optional mouth byte is written only while he holds a bite, and a reader takes any next byte as the mouth. So once he has a scar, the mouth byte is always written (0 for nothing), then a scar count, then up to 4 scars of 4 bytes. An older reader reads a correct mouth and ignores the rest. A fixture with scars and an empty mouth proves it.
- **`lineage.log`: none.** A battle death is recorded as `DeathCause::Injured`, which is what he died of. The story of who felled him lives in `duels.log`. A friend egg's cross is MutPoint ops in an ordinary Birth. The one-time duel-gene graft is a Checkpoint, an entry kind that exists today.
- **New gene kinds** 0x24, 0x25 and 0x1A. Older firmware carries them unexpressed.
- **New petfs files:** `duels.log` (the book, the dark ledger, vendettas, trophies), `friend.egg`, `found.pin` and `duel.cfg` (the stakes ceiling and the duels switch). Older firmware never opens them.
- **Living pets' genomes are untouched**, except by the one-time graft (open question 7). A duel's wounds, scars and fall are state changes through ordinary saves, as any injury is.
- **One expression rule changes for every pet, duel or not:** a receptor writing the die or cause locus is inert unless it reads a vital chemical (life, injury, toxin, starvation). The starter's death receptors all read those, so no starter changes. A descendant carrying a mutated receptor from another chemical to `die` loses that way to die. It is open question 14.
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
- **From B:** the frozen `Mood` in the card, feeding the lean and the autopilot; Skill keyed by the friend's element; `Decline` reasons shown as pictures, with a cuddle after three losses; the friend egg, re-expressed as MutPoint ops so it needs no new lineage op; the atomic lease (later replaced by OTA's own interlock, at the `ota` agent's request); the one-time duel-gene graft for older lineages, gated behind an owner question.
- **New in synthesis:** the roster binding; per-power costs and two-turn powers, tuned with a re-measured probe; the rematch number in the battle id, which fixes the judge's stale-rematch defect; the whisper that brightens with the assist, which the probe showed is the kid's biggest lever.

**Rejected from the losers:**

- B's autobattler core. It takes the body out of the turn, kids' inputs land 1.2 to 2.4 s late, and every rule becomes a desync surface between two lockstep simulations.
- B's nine-seat ring wheel. It is provably fair but unreadable to a child.
- B's 512-point stat budgets and `MutGraft`.
- C's real-time contests. Their stop-and-wait pipe broke after about 0.5 s of silence, which one coexistence stall can cause, and they depend on v2's unbuilt ball world.
- C's denylist harm set.
- A's gift genes, superseded by the friend egg.

There were no dropouts.

**Adversarial review** (opus, read-only, after synthesis). It traced every desync path (rematch ids, late packets, a dozing side's auto-move, the two-turn counters, prefix adopt) and found none. It found 19 other defects, all fixed in this revision. The three that would have blocked a build were all in the friend egg: a fourth egg would have overflowed `kMaxClutch` and both save formats; kids' and owner builds numbered their genes differently, so crosses would have copied the wrong genes; and `mutate()`'s last fallback returns its input unchecked, so a lethal cross could hatch. The other majors were these. A friend's chemistry and stimulus genes could cross. Daze and pluck receptors were unguarded. The phone could fire the stimulus that opens a duel. A rematch could deadlock. And the balance probe's model differed from the spec, which the doc now says.

**Revision after the owner's ruling** (2026-10-13, "Pets should die"). The duel core did not change: the lockstep logs, `resolve()`, the wheel and the fairness model all carried over. What changed is a new layer, the stakes. It is a three-tier ladder agreed on both devices. Wounds are applied by code from the shared Outcome. Death comes only by a Finish the winner's kid chooses. The verdict rides in the winner's move log, so it converges like a stance. Mortal duels need a fair fight. A death leaves a vendetta, a trophy, a feat and a ledger. The death-model alternatives (Spar only, a Scrap ceiling, no mercy, death by accumulation) are compared in "Stakes". The old kid-safety allowlist survives as the per-tier `duelFilter()`, which still keeps genes from deciding wounds.

**Second adversarial review** (opus at high effort, on the stakes revision). It found 16 defects, all fixed. The four blockers: the two devices' mercy clocks could disagree on a death; the verdict slot was mis-indexed and the codec refused it after turn 15; a Spare or even a win could kill, because the die receptor is instant at 0.9; and a rematch skipped Staking. The fixes: only the winner's device decides the verdict, and it takes the slaying only after an acknowledgement; the verdict sits at index `resolved`; every wound is clamped by `woundRoom()`; and a rematch reopens through consent. The majors closed these gaps: escaping wounds by the cable, the drop outside the forecast, mutated `die` receptors, unreadable holds, a gate blind to new devices, a circular stakes id, an undeclared `LifeStats` change, scar bytes colliding with the mouth byte, and a unit cycle.

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
- We accept per-pair matchups up to 0.699 (measured through `resolve()`, Hushwing over Mudge), in exchange for nine distinct powers. No one-pip cost change tightens it without breaking another bound.
- We accept every device carrying all nine packs (about 630 KB, estimate), in exchange for drawing any friend and any hybrid.
- We accept Wi-Fi start-up latency (a guess of under a second) at each duel, in exchange for an untouched heap the rest of the time.
- We accept that only shared-locus genes cross, in exchange for no lineage format change.
- We accept that a kid can save a downed pet by pulling the cable before the verdict, in exchange for never holding a pending death across a reboot. The book records it as "fled" forever.
- We accept a battle death recorded in the lineage as `Injured`, with the duel's story in `duels.log`, in exchange for no lineage format change.
- We accept that wounds ignore genetic toughness, in exchange for no mutation ever making a Scrap lethal or a line a tank.
- We accept the Skill table living in `duels.log` until v2's Games chunk exists, in exchange for no keepsake chunk now.

## Units

Every file has one owner. "C" is created and "E" is edited. Engine paths are under `lib/blorb/` (`include/blorb/`, `src/`, `defs/`), and tests are under `test/`. The sketch headers in `docs/design/sketch/` are each unit's starting contract. Each builder pushes every commit to its own branch, as the repository requires, and any agent a builder spawns runs at high effort at most, on opus or sonnet.

| Unit | Owns | Acceptance | Depends |
|---|---|---|---|
| **D1 Species registry** | C `species.h`, `defs/species.def`, `defs/powers.def`, `tools/species_neg_check.sh` | the wheel and power `static_assert`s; the three planted rows from `sketch/check.sh` each fail with their own message; `rosterHash` stable across builds | none |
| **D2 Stakes, feelings and the filter** | C `stakes.h`, `src/stakes.cpp` (`woundFor`, `forecast`), `starter_duel.h`, `src/starter_duel.cpp`, `test/test_stakes/`; E `defs/stimuli.def` (ids 40 to 47 and `button_double`), `defs/chemicals.def` (25 to 28), `defs/feats.def` (`slayer`), `registry.h` (`StimSource::Duel`), `creature.h` (`SpeciesTraits` in Phenotype; `wound`, `addScar`, `fall`; `Body::scars`; the Clutch's friend-egg fields), `src/creature.cpp` (duelFilter in the stimulus step; `wound` clamped by `woundRoom`, `addScar`, `fall`) | the `static_assert`s; testing item 8; a creature fed every duel stimulus 1,000 times at each tier keeps injury, toxin and life equal to a twin's; planted `duel_bonked -> injury` and `daze -> die` genes change nothing; `fall()` runs the ordinary death path once | D1 |
| **D3 Species genes and starters** | C `src/species_starter.cpp`, `test/test_species/`; E `defs/gene_kinds.def`, `genes.h`, `src/genes.cpp` (express rules; daze and pluck readers inert outside `kDuelChemTargets`), `src/starter_genome.cpp` (a fixed uid per recipe line; the dormant war-paint gene gated on `slayer`) | `test_species` (testing item 1); 10,000 mutations never move a species or power byte; starters built with and without an optional recipe block share every recipe uid | D1, D2 |
| **D4 Duel rules and balance** | C `duel.h`, `src/duel_resolve.cpp` (including downed and the verdict slot), `src/duel_powers.cpp`, `src/duel_mind.cpp`, `tools/duel_balance.cpp`, `test/test_duel_resolve/` | testing item 2; the verdict is read only from the winner's slot past turn 15; the balance gate (species 430 to 570 permille, pairs 300 to 700, reader over habit at least 700); the C++ tournament reproduces `roster_probe.py` within 0.03 per species; the armed-power and `petChoice` variants measured and recorded | D1, D2 |
| **D5 Card and readiness** | C `duel_card.h`, `src/duel_card.cpp`, `test/test_duel_card/` | a card from each starter; mood bytes follow chemistry, mettle and dread; each `Decline` reason, `Wounded` included; `ceilingFor` per stage, forecast and device ceiling | D3, D4 |
| **D6 Book, ledger and feats** | C `duel_book.h`, `src/duel_book.cpp`, `test/test_duel_book/`; E `src/lineage.cpp` (`feat_slayer` returns false: the Dish ORs `featsThisLife` in; `Founding.species` from the founding pick) | the fold gives XP, level, prowess, Skill, the dark ledger, trophies and vendettas; `record` is idempotent by `BattleId`; a vendetta lapses after three generations or on avenging; a torn tail truncates; compaction keeps one summary per friend | D3, D4 |
| **D7 Friend egg and keepsake bytes** | C `friend_egg.h`, `src/friend_egg.cpp`, `test/test_friend_egg/`; E `src/clutch.cpp`, `src/keepsake.cpp` (the clutch record's and the Body chunk's trailing bytes), `test/fixtures/keepsake_v1_friend_egg.bin`, `test/fixtures/keepsake_v1_scars.bin` | testing item 6; scars round-trip, and an old reader loads both fixtures | D3 |
| **D8 Wire, air and session** | C `air.h`, `duel_wire.h`, `duel_session.h`, `src/duel_wire.cpp`, `src/duel_session.cpp`, `test/support/fake_air.h`, `test/test_duel_wire/`, `test/test_duel_session/` | testing items 3 and 4; the Staking phase agrees on the lower tier and shows both skulls; Mortal refused at assist 2; the mercy timeout spares; fleeing is a forfeit with the wound | D4, D5, D6, D7 |
| **D9 Dish integration** | C `dish_duel.h`, `src/dish_duel.cpp`, `test/test_two_dishes/`; E `dish.h` (`DishOptions::air`), `src/dish.cpp` (`fire` marks `remote`; wounds, scars and `fall` from the session; the vendetta's Skill at hatch), `senses.h` (`SenseOut::remote`), `defs/senses.def`, `src/senses.cpp` (a minimal double-press detector until v2 U3) | testing item 5; a Finish in a two-Dish test gives one Death entry, remains and a clutch on the fallen side and a trophy, feat and `duel_slew` on the other; captured knocks never reach the creature and a shake always does; a Meeting, a fall or a friend egg saves at once; `graftDuelGenes` is idempotent and recorded as one Checkpoint | D2, D8 |
| **D10 Founding** | C `founding.h`, `src/founding.cpp`, `test/test_founding/`; E `src/debug_verbs.h` (`DEBUG found <id>` writes `found.pin`) | the nest's gestures pick the lit egg; the timeout keeps the first; `needsFounding` is false with any keepsake or lineage; a `found.pin` makes the nest a single egg at a fresh founding and is ignored otherwise | D3, D6 |
| **D11 Presentation** | C `lib/paint/include/paint/duel.h`, `lib/paint/src/duel_scene.cpp`, `lib/paint/src/species_packs.cpp` (`packFor`), `lib/paint/src/scars.cpp`, `test/test_present_duel/`; E `appearance.h` (scars, trophies, marks), `src/appearance.cpp` | struct goldens of DuelView-driven Appearance per phase (staking, mercy, fallen included); scars track the body through the hop and the stages; `packFor` falls back to grungo, tinted; no state is colour-only (a lint) | D9; ART1 to ART3; the roster art |
| **D12 Firmware radio** (hardware; flashing only by the hw agent, on the test board) | C `src/hw/radio_espnow.h`; E `src/main.cpp` (the nest at first boot, the Air with a `RadioGate` over `updater.active()` and `trial.onTrial()`, `DishOtaHost::busy()` reporting the open duel, `drawDuel`) | `pio run -e badge128` builds. On two boards, only with the owner's go-ahead to flash: the heap soak (`minheap` at least 40 KB); a duel, a mercy, a fall and a friend egg; BLE connected throughout; OTA BEGIN answered `ERR 409 BUSY` mid-duel; a duel refused during an update session and on a trial image; RSSI at 30 cm and 2 m sets `kNearRssi` | D9, D10, D11; the `ota` branch merged |
| **D13 Sim film** | C `sim/duel_bot.h`, `tests/feeds/duel_*.txt`, `test/golden/duel_*.png`; E `sim/sim.cpp`, `tools/sim_film.sh` | testing item 9; each golden looked at before blessing | D11 |
| **D14 Phone window** | E `defs/commands.def` (`FRIENDS`, which lists the ledger), `src/protocol.cpp` (`! DUEL` events, FRIENDS streaming), `defs/twists.def` (`duels`, `stakes`), `src/twists.cpp` (`twist_duels` and `twist_stakes` write `duel.cfg`; `twist_stimulus` refuses `button_double` and Duel-source stimuli), `docs/ble.md` | golden transcripts; FRIENDS lists the book with no MAC or id; no verb or twist opens a duel, locks a move, sets the stakes of a running duel or gives a verdict (a test fires every stimulus name and walks COMMANDS and TWISTS); the ceiling survives a reboot | D6, D9 |

**Art units** (the roster's art contract ships 1 body, 10 faces, 1 egg, eye and mouth masks and `eyes.json` per species, and nothing for battles). Each is art plus its converter rows, owned by the art pipeline (`origin/roster-art`, `tools/sprite_pack.py`):

| Unit | Delivers | Acceptance | Needed by |
|---|---|---|---|
| **ART1 Scar overlays** | 5 kinds (claw, scorch, notch, ripple, ring) x 3 sizes, generic across species, recoloured from the skin region at draw time | byte-stable conversion; each reads on all nine bodies at 240 px (a contact sheet the owner looks at) | D11 |
| **ART2 Trophy tokens** | 9 tokens at 24 x 24, one per species (shell, spore cap, gill frond, wing scale, fin, hood, lamp, feather, gear) | byte-stable; legible on the rim at 24 px | D11 |
| **ART3 Duel glyphs** | stance glyphs (3), power glyphs (9), tier glyphs (feather, bandage, skull) with a skull fill, the debt and vendetta marks, the bandage for `Wounded` | byte-stable; each distinguishable without colour | D11 |

The downed pet, the fallen remains and the battle stances need no new art. Remains are the converter's settled sleeping frame. The downed pose and the lunge, curl and sidestep are the renderer's procedural motion on the one body. The power uses each species' face 6.

No file appears in two rows. **Order:** D1, then D2, then D3 and D4 in parallel, then D5, D6 and D7, then D10 and D8, then D9, then D11 and D14 (with ART1 to ART3 in parallel from the start), then D12 and D13. D4 goes first among equals after D2, because its numbers decide whether a duel is fun, and the C++ harness should confirm the probe before any radio code exists.

## Open questions for the owner

Only product calls. Each has the default this design ships. Your ruling that pets die, on every device, is applied throughout. Neglect death is unchanged everywhere, with no kid mode.

1. **How lethal by default?** Default: every device allows all three tiers, and the stakes screen starts on Scrap. Mortal needs both kids, two adult pets, a fair fight, and the winner's 3 s Finish. The alternatives are in "Stakes": a Scrap ceiling, Spar only, Mortal without mercy, or death by accumulation.
2. **Silence at the verdict:** spare or finish? Default: spare after 15 s. Death always needs a hand on the button.
3. **The fairness gate for Mortal:** refuse when the assist would exceed one heart? Default: yes. The alternative is to allow it with the skull shown, so a parent could take a mortal duel against a much weaker kid if both agree.
4. **Who decides each device's species?** Default: each kid picks from a nest of three (one per element), rolled from the MAC. The alternative is that you pin one species per device over the cable before handing them out (`DEBUG found <id>`), so the family covers all nine.
5. **Friend eggs: yes?** Default: yes, after both kids hug, one waiting at a time, never picked by the clutch timeout.
6. **Hybrids** (one species' body with another's power): fine? Default: yes.
7. **May grungo `d30f00d1` receive the duel feeling genes once, on his first duel, recorded in his lineage?** Default: yes. Without it, wounds, scars and death still apply, but his chemistry does not react to duels.
8. **Scars:** for one life, or heritable as a family mark? Default: one life. The lineage remembers them in words. A heritable scar would be a mark gene added at the Birth, and v2 parts (A11) could carry it.
9. **Vendetta:** three generations or until avenged? Default: yes. Longer feuds are one number.
10. **Pulling the cable to save a downed pet:** allowed, as "fled"? Default: yes. The alternative is saving a pending verdict across reboots, which is more code and a death that waits on a power cut.
11. **Should a device show the friend's pet name?** Default: no. Only species, colours and stage cross the air.
12. **Is 10 s per turn right for a 6-year-old?** Default: 10 s, after which the pet picks.
13. **Parent controls:** default all tiers allowed. `TWIST stakes spar|scrap|mortal` lowers a device's ceiling, and `TWIST duels off` turns duels off.
14. **Only vital chemicals may write the die locus, for every pet:** yes? Default: yes. It changes no starter. It stops a mutated line from dying of, say, pain or dread wired straight to `die`. Without it, that wiring could fire mid-duel, in a Scrap that promised no death.
15. **A drop during a duel:** does it injure? Recommended: yes, at every tier, as a drop always does, because the body is real. In a Scrap or a Mortal duel it also counts as fleeing (a forfeit). The wound clamp keeps room for one drop, so a fumble never turns a duel into a death. The alternative is that a drop during a Spar does not injure, which makes the friendly tier fully safe for the youngest.

## Risks

- **The Wi-Fi heap is an estimate.** D12 measures it first. The levers, in order: Wi-Fi buffers in PSRAM (already allowed), fewer static RX buffers, then pausing BLE advertising during a duel.
- **A clink may not trip both tap engines.** Pointing and knocking always works.
- **RSSI in IDF 4.4 needs a promiscuous hook.** If it misbehaves beside ESP-NOW, the gate becomes "matched by clink or mutual choice only", which is still consent on both sides.
- **The pair spread** (worst 0.699, Hushwing over Mudge) may feel unfair within one family, and with Mortal it matters more. Costs alone did not tighten it (D4's sweep); a rule change to Hush or the element head start would.
- **A child's pet killed by a cousin** is the point of the ruling, and it will still land hard some days. The mitigations are design, not refusal: consent with the skull shown, the fairness gate, mercy by default, and a death that leaves a clutch and a vendetta rather than nothing.
- **The forecast is coarse.** A Scrap it allows can still contribute to a later death if the pet is then neglected. That is intended ("wounds work"), and the stakes screen says so with the bandage.
- **v2's double-press detector does not exist yet.** D9 adds a minimal one, and v2 U3 replaces it.

## Next implementation step

Build D1's tables and D4's `resolve()` with `tools/duel_balance.cpp`, and reproduce `roster_probe.py`'s numbers in C++ before any art or radio binds to them.
