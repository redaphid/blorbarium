# Species

Blorbarium has one creature today, grungo. This design adds the roster's nine species as data and makes species a heritable, crossable part of the genome. The battles that use them are in `BATTLES.md`. The sketch headers are in `docs/design/sketch/`, and `sketch/check.sh` compiles them (see "Proof" below).

The roster is bound from `roster/roster.json` on `origin/roster-art` (d98055e), and its ids are stable.

## The nine

| Id | Name | Species | Element | Loves | Power (cost) | What the power does in a duel |
|---|---|---|---|---|---|---|
| 0 | Grungo | frog | Splash | Trick | Foresee (5) | His eyes glow teal. He saw your move, and his stance becomes the one that beats it. |
| 1 | Mossby | snail | Sprout | Guard | Shell Up (5) | A glowing shell. The next hit that would land just bonks off. |
| 2 | Puffkin | puffball | Sprout | Pounce | Spore Puff (6) | Drowsy spores. The friend's pet nods off for the next turn. |
| 3 | Gilly | axolotl | Splash | Guard | Mend (6) | Her gills glow and one heart grows back. |
| 4 | Mothwick | glow-moth | Spark | Trick | Lantern Flash (4) | Dazzle. She wins a head bonk this turn, and for two turns the friend's hits miss half the time. |
| 5 | Mudge | mudskipper | Splash | Pounce | Splash Dash (3) | Always first. For two turns a head bonk goes his way. |
| 6 | Fizz | firefly beetle | Spark | Pounce | Static Spark (6) | A tickly zap. The friend loses one heart whatever the clash. |
| 7 | Hushwing | marsh owl | Sprout | Trick | Hush (5) | Silent wings. For two turns a clash she loses costs her nothing. |
| 8 | Tinker | raccoon | Spark | Guard | Gadget (5) | A surprise gadget: one of the other eight powers, drawn from his own luck. |

The elements form a cycle: Splash douses Spark, Spark singes Sprout, and Sprout drinks Splash. The stances form a second cycle: Pounce beats Trick, Trick beats Guard, and Guard beats Pounce. Each (element, loved stance) pair holds exactly one species, so the table is a 3x3 Latin square. `species.h` refuses any table that breaks it. Every species therefore has three good, three bad and three even element matchups. The roster's own habitats grouped them: the water-side three are Splash, the moss and spore three are Sprout, and the lamp and wiring three are Spark. Hushwing is the stretch, a marsh owl in the reeds.

## How a species lives in the genome

A species is data in two places, and no engine code branches on its id.

- **`defs/species.def`** holds the shared facts of a kind: element, loved stance, power, and the starter genome's species bytes (boldness, day or night sleeper, appetite, oracle voice). `defs/powers.def` holds each power's spark cost and glyph. One row plus one handler (`power_<name>` in `src/duel_powers.cpp`) adds a power, which is the codebase's usual extension shape.
- **The genome** says which kind this pet is, through three new gene kinds:

| Gene | Type byte | Class | Body | Rule |
|---|---|---|---|---|
| `SpeciesGene` | 0x24 | Look | species, reserved | The body. Fixed: point mutation never changes it. |
| `PowerGene` | 0x25 | Life | power, reserved | The power. Fixed, for the same reason. The one Life-class gene a cross may copy. |
| `KnackGene` | 0x1A | Mind | pounce, guard, trick | The line's taste in stances, ranked into an order. Mutable. |

The type bytes avoid v2's reserved 0x14 to 0x19, 0x23 and 0x33.

Expression is total. The first expressed SpeciesGene is the body, and none means grungo. The first expressed PowerGene is the power, and none means the body's own power. Every genome alive today, including the device's grungo `d30f00d1`, therefore reads as a pure-bred grungo with Foresee. It needs no migration, and its hash does not move.

Species and power are two genes, not one, and they sit far apart in the starter's gene order. That is what lets a friend egg's crossover give a pet one parent's body and the other's power (see "Crossing").

## What differs per species

- **Art.** `paint::packFor(species)` picks the sprite pack. Every device carries all nine, because any friend must be drawable and a friend egg can hatch any of them. The packs go in the 2 MiB raw `art` partition, which is memory-mapped. That is about 630 KB at grungo's under-70 KB a pack (an estimate from grungo.md). A missing pack falls back to grungo tinted by the pet's own palette genes, so a gap in the art is a recoloured frog, never a blank. Colours belong to the pack, so `species.def` has no hue columns, and palette genes tint relative to each pack's authored colours as they tint grungo today.
- **Starter genome.** `starterGenomeOf(species, seed)` is grungo's recipe with the row's bytes written in. It appends a SpeciesGene, a PowerGene, a KnackGene ranked toward the row's loved stance, and the duel feeling genes (`BATTLES.md`, "What the pets feel"). Nine starters come from one function and nine rows. All nine share grungo's gene uids, which is what makes crossing possible.
- **Temperament.** `bold` (startle gain), `nocturnal`, `appetite` and `voice` come from the row. The voices are the existing `voices.def` rows. Mudge is terse, Hushwing ominous, Mossby rambling, and so on.
- **Battle.** Element and loved stance place the species on the wheel. The power is its one special move. No species has more hearts, more damage or more speed than another. Species differ in style, not strength (see "Why no stats" below).

## How a device gets its species

A fresh device shows a **founding nest of three eggs**, one of each element, rolled from the MAC (`foundingNest(speciesSeed)`). The kid picks with the clutch gestures they will use for the rest of the pet's life. Tilt or knock moves the glint, and a double knock or a BOOT hold keeps the lit egg. Nobody choosing for 60 seconds keeps the first egg, so a board that boots alone still founds.

The owner can pin a species instead, over the cable before handing a device out, with `DEBUG found <id>` on the serial line. It writes `found.pin` on petfs, and at the next fresh founding the nest is that one egg. The website cannot do it, because the nest runs before the Dish and its Bluetooth exist. That matters for a family set, where the owner may want all nine species present (open question 1 in `BATTLES.md`).

After the founding, every egg inherits its parent's species. A species changes only in a friend egg. Existing pets never see the nest: a keepsake or a lineage on the device means there is no chooser.

Three eggs, not nine, is deliberate. Nine kids choosing freely from nine would converge on the coolest one, and mirror matches between same-element pets are the dullest on the wheel. A nest of one per element still gives the kid a real choice of style.

## Crossing: the friend egg

Species can cross. This is the genetics hook. Two cousins' pets have an egg together, and it might wear one parent's body with the other's power.

**How it starts.** After any duel, the friends screen asks "hug your pet to make a friend egg". Each kid cradles their own pet for 3 seconds within 10 seconds, on their own device. That is consent from both owners, each on their own device. When both hugs land, the two devices swap genomes over ESP-NOW. A grungo-sized genome of about 1.5 KB is 8 chunks, and the 8 KB cap is 41 (`BATTLES.md`, "Radio"). Each pet's next clutch then holds a speckled friend egg, within the clutch's limit of 3. A clutch of 1 or 2 grows by one, and a full clutch gives its last slot to the friend egg.

**What crossing is.** Crossing is allele exchange at shared loci, over an allowlist. Every recipe gene sits at a fixed uid on every device, whatever the build. A block a build leaves out, such as the starvation genes on a kids' build, skips its uids instead of shifting the rest, and the species, power and knack genes take fixed uids past the recipe's. `crossDiff` walks my genome by uid in linkage blocks, where the source parent switches about once every 8 genes. Wherever the friend's gene has the same uid, type and length, and its kind is crossable, it copies the friend's body bytes over mine. The crossable kinds are looks (palette, size, mark), faces, temperament, knack, species and power. Chemistry, stimulus, instinct and other Life genes never cross. The friend's bytes came over an unencrypted radio, and `viability()` only feeds a candidate for 48 pet-hours, so it would not catch a crossed `knock -> injury`. A `static_assert` holds the allowlist. Each copied byte is a `MutPoint`, the mutation op that exists today. The cross is therefore an ordinary `MutationDiff`:

    apply(mine, crossDiff + mutateDiff) == child

The lineage's Birth entry replays without the friend's genome, and `lineage.log`'s format and `kMutateVersion` stay as they are. Genes only the friend has (its own duplications, newer gene kinds) do not cross. That is the price of no format change, and it also means a cross cannot smuggle in an unknown gene kind. The mutation-policy gene is not crossable, so a friend cannot make my line mutate faster. So a hybrid has the friend's looks, face, temperament or power, with its own chemistry and needs.

**Hybrids.** SpeciesGene and PowerGene sit far apart, so about half of friend eggs are hybrids. A hybrid is grungo's body with Fizz's zap, or Gilly's body with Foresee. That rate is an estimate from the linkage-block rate. The clutch preview shows which egg is which, and the duel card carries body and power separately. A woken hybrid power is the one way a species gains a power it was not born with, so battles are how new powers spread through a family.

**Safety of the cross.** The friend's genome is parsed at the boundary with `Genome::parse`, checked against the hash announced in the State packet, and refused if it shares no uid with mine. The egg is checked twice, because `mutate()`'s last fallback returns its input unchecked. The crossed genome must pass `viability()`, and then the mutated child must pass it too. Either failing gives the parent's plain egg in that slot ("the friend magic fizzled"). No path hatches a lethal cross.

**The clutch.** At most one friend egg waits at a time, as `friend.egg` on petfs. A newer hug replaces it. The clutch shows it as the last, speckled egg. The cursor starts on the pet's own first egg, and the 30-minute timeout picks that one, never the friend egg. A friend egg is chosen by a kid or not at all. The file is consumed at the Birth, not when the clutch is laid, so a reboot during the 30-minute pick keeps the friend's genome.

## Save-format changes (the pet is a keepsake)

- **New gene kinds** 0x24, 0x25 and 0x1A. Older firmware carries unknown kinds without expressing them (genome.h's invariant). A rolled-back firmware shows a hybrid as its body species with the body's power, which is harmless.
- **New files** `friend.egg` and `found.pin` on petfs. Older firmware never opens them.
- **Keepsake clutch record** (inside `Chunk::Occupant`, `Kind::Clutch`): 5 trailing bytes, the friend egg's slot (0 = none, else 1 to 3) and its cross seed (u32). The decoder stops after the heirlooms and never checks for leftover bytes, so an older reader ignores them, and `kFormatVersion` stays 1. A committed fixture proves an old reader loads it. The clutch count stays 1 to 3, so lineage's three seeds keep their shape.
- **`lineage.log`: none.** A cross is MutPoint ops in an ordinary Birth.
- **New foundings only** carry the new genes. Living pets keep their genome, except for the one-time duel-gene graft in `BATTLES.md` (open question 6).

## Proof

`docs/design/sketch/check.sh` (WSL `survivor`, g++ 16.2.1) compiles `species.h` with `-Werror`, and it plants three bad rows:

- two species on one wheel cell, refused with "every (element, style) pair exactly once";
- two species sharing a power, refused with "a shared power";
- a power costing more than the meter holds, refused with "a cost the meter cannot hold".

The build unit's tests (`BATTLES.md`, units D1, D3, D7, D10):

- all nine starters pass `viability()`, and each expresses its own species and power;
- a genome with no SpeciesGene expresses grungo with Foresee, and the `d30f00d1` fixture's hash and phenotype are unchanged;
- 10,000 mutations never move a species or power byte;
- `apply(mine, crossDiff + mutateDiff) == child` over 10,000 seeds and all 81 species pairs;
- every friend egg passes viability or falls back;
- a hybrid appears in 35 to 65 percent of crosses, measured, not assumed;
- an owner-built and a kids'-built starter share every recipe uid, and a cross between them copies only same-kind genes;
- a planted friend genome carrying `knock -> injury` crosses nothing but its allowlisted genes;
- a clutch of 3 gives its last slot to the friend egg, and both viability checks fall back to the plain egg;
- the nest always holds one egg of each element, and a pinned species overrides it before the first hatch only.

## Alternatives rejected

- **Species by MAC roll only** (the badge's way). The kid never chooses, and a family could miss half the wheel by chance.
- **Nine eggs to choose from** (candidate A). Kids converge on one favourite, and mirror matches are the dullest pairing.
- **Species as tweak rows over one baseline genome** (candidate C, `species_tweaks.def`). It saves the three gene kinds, but a species would not be a gene, so it could not cross and a hybrid could not exist.
- **Gift genes** (candidate A). After each duel, a friend's Look gene or, one time in four, its SpeciesGene went in Dormant and woke in later eggs. It needs no format change, but it is a slow, invisible trickle. The friend egg is the moment a kid remembers ("our pets had an egg"), and expressing the cross as MutPoint ops gets it with almost no format cost.
- **A new `MutGraft` lineage op** (candidates B and C). It carries whole friend genes, so friend-only genes could cross, but it costs a lineage tag, a `kMutateVersion` bump, and a Checkpoint after every crossed birth for rolled-back firmware. Shared-loci crossing needs none of that.
- **Per-species battle stats** (candidate B's 512-point budget). A stat budget can be made fair on paper, but it invites "my species is stronger" and breeding for strength, which the fairness rule rules out. Style and one power per species is enough identity, and the probe shows it balances.
- **Species and power in one gene.** It is simpler, but crossover could never split them, so no hybrid would ever exist.

### Why no stats

Any reward for winning snowballs. Candidate A's probe measured one pair at 0.988 when winners earned sparks. A species or a bred line that is simply stronger breaks the rule that a 6-year-old's new pet must stand a chance against a parent's old one. So species, breeding and age change how a pet plays (its lean, its power, its knack, its mood), never how hard it hits.
