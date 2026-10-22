# Lineage: heritable traits that change the game

Issues #45 (lineage that changes gameplay), #44 (personality changes
behaviour) and #46 (fights have consequences). This is a design. No engine
code changes with it. The one piece of code is a test-only prototype of the
genetics, `scripts/test/lineage_sim.py`, which the engine does not use.

The owner's words:

> "I do want the attacks they can do etc to vary based off of the creature,
> and have significant, obvious, fun effects of creature lineage that
> changes gameplay and causes delight. Think about a light version of the
> game 'Rogue Legacy 2' for inspiration - but be based off of the actual
> genetics for the creature. We can evolve the genetics system to support
> this. Wiping out the pets and starting fresh is an easy option at this
> point." (#45)
>
> "maybe the behavior of the pet should change with the personality as
> well." (#44)
>
> "I want winning or losing fights to effect the creature, and the user
> should get something if they win, or lose it if they lose." (#46)

Labels: **measured** (run on 2026-10-21, command given), **arithmetic**,
**estimate**.

## In twelve lines

1. **Traits** are named, visible quirks such as Hiccups, Giant, Twin Strike,
   Upside-Down and Lucky. Each is read from real genes. There are 38 of them,
   and each is marked positive, negative or silly.
2. **Three sources**, all in the genome. *Polygenic* traits are a gene byte
   that sits far from its species' norm, such as SizeGene.base for Giant or
   the startle receptor's gain for Jumpy. *Mendelian* traits are alleles on
   a new diploid `AlleleGene`, with dominant and recessive alleles. *Sports*
   are rare products of mutation (Shiny, and woken dormant genes).
3. **Egg laying** gives each egg one allele from the parent and one from a
   **pond mate**: the parent's other allele 35% of the time, otherwise a
   draw from the species' wild allele pool. A friend egg replaces the pond
   mate with the friend, a real second parent. So recessives skip a
   generation and come back, and Grandpa's hiccups return.
4. **Egg previews** show every egg's traits as glyphs, with their rarity, a
   "?" speckle for carried recessives, and a dimmed glyph for traits that
   only appear in the grown-up. Choosing an egg is choosing an heir, as in
   Rogue Legacy 2. In the prototype, the kid's choice roughly doubles a
   line's traits within ten generations.
5. **Attacks** are the species' base (element, loved stance and power) plus
   up to **two active duel traits** that attach riders to stances ("a
   winning Pounce hits twice, but a head bonk trips him") plus a **power
   flavour** gene (Swift, Classic or Mighty). So two Grungos fight
   differently.
6. **Fairness changes from "no stats" to "no edge on balance".** Every duel
   trait is a sidegrade. The balance harness measures it, and it must stay
   within 0.46 to 0.54 against the field. Hearts stay within 4 to 6. No
   trait touches the whisper or the assist, which are what a 6-year-old
   relies on.
7. **Determinism:** a trait's duel effect is a pure rule in `resolve()`,
   keyed by trait ids carried in the BattleCard (5 more bytes). The rules
   table goes into `rosterHash`. Any randomness is the side's own luck, as
   for powers.
8. **Screen and control quirks** (Upside-Down, Dizzy, Sepia, Squinty) are
   local drawing only. They never reach `resolve()`, so they cannot desync.
   They are mild, and a picture explains each one.
9. **Courage**, the Rogue Legacy 2 gold: negative and silly traits pay the
   line in better eggs (a golden egg, more eggs). They never pay in duel
   strength.
10. **Personality to behaviour (#44):** the server turns the personality
    text into 6 numbers from -2 to +2, plus an idle and a lean. The badge
    applies them over the genes to activity, curiosity, boldness,
    playfulness, affection and chattiness. They never touch a number that
    `resolve()` reads.
11. **Spoils (#46):** at each tier both badges put up an ante (a snack, a
    charm, a crest). The winner takes the loser's ante, and both badges
    apply the result from the shared Outcome. A win or a loss shows on the
    creature (swagger or sulk, scars, trophies), and the next clutch can
    lean toward the beaten line's traits.
12. **Fresh start:** genome v2. Old pets get a farewell, their lineage is
    archived on the badge, and each badge founds a nest of three v2 eggs of
    its own species. It is the kids' first heir choice.

## 1. Rogue Legacy 2, the light version

### What makes RL2's heirs delightful

- **The traits are visible and funny first.** Gigantism and Dwarfism change
  the body. Vertigo flips the screen. Colour Blind and Nostalgic change the
  palette. IBS makes noises. Hypochondriac inflates damage numbers. You see
  the trait in the first second of the run, and you laugh.
- **They change how you play, not just a number.** Vertigo makes the
  controls feel wrong. A giant fills corridors. Pacifist cannot attack at
  all. A trait is a little rule change, and the run becomes a different
  game.
- **Positive, negative and silly traits are mixed**, and many cut both ways.
  Some only change the presentation (Nostalgic). Some are real handicaps.
- **Risk is paid for.** In RL2 a harder trait raises the gold you earn on
  that run. A bad heir can still be the right pick, and the choice has a
  real trade-off.
- **The choice of heir is the ritual.** Three heirs, each with a class, a
  spell and 0 to 2 traits, lined up in a row. Picking one is a moment of
  anticipation, and it is the game's main "one more run" hook.
- **Rarity and discovery.** An unseen trait shows as "???" until you play
  it, and the discovery is a small celebration. Some traits are rare enough
  to talk about.
- **The family tree.** A named lineage with numbered heirs, where earlier
  lives are remembered.

### What fits a 240 px round screen and a kid's attention

| RL2 idea | Here | Why |
|---|---|---|
| Up to 2 traits an heir | up to 3 traits shown on a pet, at most 2 active in a duel | a picture each, readable on an egg at 200%; a duel stays a rock-paper-scissors game |
| Gold bonus for bad traits | **Courage**: better eggs next time, never duel strength | duel strength must stay fair across ages 6 to 13 |
| Vertigo, Colour Blind, Nostalgic | Upside-Down, Dizzy, Sepia, Squinty | local drawing only, so the duel protocol never sees them |
| "???" undiscovered traits | an undiscovered trait's glyph is a "?" with a sparkle until the line first hatches it; then NEW TRAIT on the marquee | discovery without reading |
| Class and spell | species (body and power) and the power flavour | already exists; only the flavour is new |
| Three heirs in a row | the clutch's three eggs where the duel's stances sit (#23) | same tilt-and-knock picker the kids already know |
| Text descriptions | the trait's glyph, plus one line in the pet's own voice ("I HAVE THE HICCUPS. HOW FUN!") | ages 6 and up; no reading needed to play |
| Death every run | a life is about 10 days, and Mortal duels exist | lives are long, so traits must be fun for days, not minutes: slow jokes (a hiccup every few minutes), never constant noise |

Not taken: traits that hide what you need to play (RL2's "can't see your
health"), traits that punish the youngest players hardest (anything that
corrupts the pet's whisper), and the gross ones a parent would mute.

## 2. The trait catalogue

Polarity: **+** positive, **-** negative, **~** silly or both ways.
Source: **P** polygenic (a byte of an existing gene, judged against the
species' starter value: the trait shows when the byte sits at least 18 from
the norm), **M** Mendelian (an allele on an `AlleleGene` locus, D dominant
or r recessive), **S** a sport (made by mutation only).
Rarity is the steady share of pets showing it in the prototype (section 4):
common 12% or more, uncommon 4 to 12%, rare 1 to 4%, legendary under 1%.

Duel effects marked ⚖ are starting guesses for the balance harness to tune
(section 6). Effects marked "local" never reach `resolve()`.

### Body and size

| # | Trait | ± | Genes that express it | Inheritance, rarity | Duel | Idle, feeding, sleep | Presentation |
|---|---|---|---|---|---|---|---|
| 1 | **Tiny** | ~ | P: adult `SizeGene.base` (and Baby's), low; a deleted SizeGene duplicate counts | regresses toward the norm each egg; uncommon | ⚖ max hearts -1; **Slippery**: a clash lost to a Pounce misses on his luck half the time | zips about (wander speed x1.5); bites half a pellet, so eats twice as often | drawn at 70%, a squeaky hop, sits in the pellet bowl |
| 2 | **Giant** | ~ | P: adult `SizeGene.base` high; a **duplicated** SizeGene (MutDup) adds | as Tiny; uncommon | ⚖ max hearts +1; every special he commits is Heavy (BOOT press = hold) | slow stomp; appetite +30%; the dish shudders 1 px when he lands a hop | drawn at 130%, his head clipped by the rim; peeks in from the edge |
| 3 | **Wobbly** | ~ | M: footing locus, D | common (13%) | ⚖ head bonks go his way half the time (luck); no element head-start pip | jiggles when he stops; falls over on a shake and rights himself | jelly wobble from `SizeGene.squash` |
| 4 | **Clumsy** | - | M: footing locus, r | common (15%) | ⚖ powers cost 1 less; a heavy special lands as a quick one a third of the time ("TRIP!") | trips on his hop now and then; sometimes knocks his pellet away | the trip is a squash and a skid |

### Mind and temper

| # | Trait | ± | Genes | Inheritance, rarity | Duel | Idle, feeding, sleep | Presentation |
|---|---|---|---|---|---|---|---|
| 5 | **Jumpy** | ~ | P: the adrenaline-to-startle receptor's gain (the species row's `bold`), high | uncommon | ⚖ starts with +1 spark; a lost clash also costs him a spark | hops twice as high on a shake; flinches at knocks | big hops with a contact shadow |
| 6 | **Fearless** | + | P: the same gain, low, and the shake stimulus gene's fear amount | uncommon | ⚖ cannot be staggered (a Trick still hits him, but his special stays at full strength); no element head-start pip | never curls at knocks; walks to the rim and looks out | chest out |
| 7 | **Curious** | + | P: `TemperamentGene.explore`, high | uncommon | local: the duel Skill learns at 1/2 a step instead of 1/4, so his autopilot adapts faster | noses everything, follows a tilt sooner | "?" bubble over him at new things |
| 8 | **Scatterbrain** | - | P: `TemperamentGene.explore` low with `forgetRate` high | uncommon | none (never the whisper: section 6) | walks to the wrong side for a pellet; forgets a taught line now and then | a moth circles his head |
| 9 | **Grumpy** | ~ | M: temper locus, D | uncommon (11%) | ⚖ **Grudge**: after a turn he lost a heart, his next winning clash deals +1; -1 starting spark | turns his back on a knock; grumbles lines | the annoyed face weighted up |
| 10 | **Stubborn** | ~ | M: temper locus, r | common (17%) | ⚖ the same stance as last turn: a win gives +1 spark, a loss costs 1 | keeps doing whatever he started | arms folded |
| 11 | **Cuddlebug** | + | M: heart locus, D | common (28%) | ⚖ **Second Wind**: the first time he drops to 1 heart, +2 sparks | asks for cradles; purrs when held; the hug for a friend egg takes 1 s, not 3 | hearts float up when cradled |
| 12 | **Show-off** | ~ | M: heart locus, r | rare (2%) | ⚖ a win with his knack's best stance gives +1 spark (the "zone" sparkle becomes real); a loss with it costs 1 | does hop_circles when anyone is near | a bow after a win |
| 13 | **Chatterbox** | ~ | M: voice locus, D | common (20%) | local: taunts on the marquee in the duel | thoughts twice as often | speech bubbles stack |
| 14 | **Echo** | ~ | M: voice locus, r | rare (2%) | none | repeats the last taught line, sometimes wrong | a small echo bubble |

### Sleep and food

| # | Trait | ± | Genes | Inheritance, rarity | Duel | Idle, feeding, sleep | Presentation |
|---|---|---|---|---|---|---|---|
| 15 | **Night Owl** | ~ | P: the melatonin emitter's gain on low light (row `nocturnal`), past his species' value | uncommon | ⚖ a duel opened at his pet night: +1 spark; by day: -1 (frozen into the card when the duel opens) | wide awake at night, naps all afternoon | eyes glow in the dark |
| 16 | **Sleepyhead** | - | P: the sleepiness tonic's fill and the `sleep_gate` receptor's gain, high | uncommon | ⚖ **Dozy**: on a turn the pet picked (the 10 s autopilot), he dozes a third of the time: no stance, but +2 sparks ("power nap"). A kid who picks for him avoids it | naps more; snores; sleep talk doubles | a nightcap appears when sleepy |
| 17 | **Insomniac** | ~ | P: the same, low | uncommon | ⚖ never dozes (Spore Puff and Dozy do nothing); -1 starting spark | barely sleeps; wanders at night | baggy eyes |
| 18 | **Glutton** | ~ | P: the hunger tonic's fill and `MetabolismGene.biteSize`, high | uncommon | ⚖ **Snack Power**: fed in the last 30 pet-minutes, +1 max heart; hungry (0.6 or more), -1 (frozen in the card) | eats every pellet the moment it lands; begs | round belly after a meal |
| 19 | **Picky Eater** | - | P: `MetabolismGene.rotMinutes` short and the `fed_bad` stimulus's discomfort high | uncommon | none | spits out pellets older than 30 min ("BLEH"), so he never eats a rotten one | turns his nose up |
| 20 | **Iron Stomach** | + | P: the toxin-to-injury reaction rate, slow | uncommon | ⚖ a Zap's extra heart misses half the time; Spore Puff makes him doze only half the time | eats rotten pellets with no harm | a tiny burp |

### Seeing

| # | Trait | ± | Genes | Inheritance, rarity | Duel | Idle, feeding, sleep | Presentation |
|---|---|---|---|---|---|---|---|
| 21 | **Seer's Eye** | + | M: sight locus, D; Grungo's pond is rich in it (the dormant brighter seer glow, woken, adds to it) | common (16%) | local: the shake prophecy reads two turns of pattern, as Grungo's does today | foresees more often | a brighter halo |
| 22 | **Third Eye** | + | M: sight locus, r; made only by mutation | legendary | local: once a duel, **"I SEE WHAT YOUR PET WOULD PICK"**: shows the friend's own pet's whisper (`petChoice` for the friend, from public, resolved turns only) | stares at the rim before a friend arrives | a third eye opens in the halo |
| 23 | **Squinty** | ~ | M: eyes locus, r | uncommon (8%) | local: the friend's pet is drawn as a soft blob with eyes until the reveal of each turn | walks up to pellets to see them | squints |
| 24 | **Sepia** | ~ | M: eyes locus, r | rare (4%) | local: his screen is an old photograph (brown tones; the HUD keeps its shapes) | none | a film grain |

### Screen and controls (the RL2 Vertigo family; all local)

| # | Trait | ± | Genes | Inheritance, rarity | Duel | Idle, feeding, sleep | Presentation |
|---|---|---|---|---|---|---|---|
| 25 | **Upside-Down** | ~ | M: balance locus, r | rare (3%) | local: his duel is drawn upside down; the stance icons are drawn where tilting toward them works, so aiming at a picture still aims | he walks on the "ceiling" at home; "THE SKY IS DOWN" every day | the whole dish turned 180° |
| 26 | **Dizzy** | ~ | M: balance locus, r | uncommon (7%) | local: the three stance icons slowly turn round the rim, one slot every 3 s, so the kid aims where the icon **is** now | the dish gently rotates while he wanders | swirl eyes after a spin |
| 27 | **Hiccups** | - | M: rhythm locus, r; Grungo's pond is rich in it | common (20%) | ⚖ on a turn, one time in six (his luck), "HIC!": his stance shifts one step round the wheel; never on a special's landing; a hiccup turn gives +1 spark | hiccups every few minutes; a thought says "HIC" | a little hop and a bubble |

### Colour and pattern (cosmetic)

| # | Trait | ± | Genes | Inheritance, rarity | Duel | Idle, feeding, sleep | Presentation |
|---|---|---|---|---|---|---|---|
| 28 | **Albino** | ~ | M: coat locus, r | rare (2%) | none | prefers shade: sits away from the screen's bright side | pale skin, pink eyes (each region's value up, saturation down) |
| 29 | **Shadow** | ~ | M: coat locus, r | rare (2%) | none | at night only his eyes show | near-black skin |
| 30 | **Mood Ring** | ~ | M: coat locus, D; expressed through a palette gene's `chemBound` and `gain` (a field that exists today) | uncommon (10%) | none (his colour shows his mood, not his move) | his skin shifts with fear, pluck and boredom | colour follows the chemistry |
| 31 | **Shiny** | ~ | S: a wild palette roll into the band's bright edge; inherited as the byte, drifting back | rare (1%) | none | none | a sparkle sweep every few seconds; the egg glitters |
| 32 | **Thrill Seeker** | ~ | S: the starter's **dormant** "loves being shaken" stimulus gene, woken by MutWake | uncommon (estimate: the wake rate) | ⚖ +1 spark whenever he loses a heart (the rush) | a shake makes him happy; he asks to be shaken | grins mid-hop |

### Fighting quirks (the RL2 "this run is different" family)

| # | Trait | ± | Genes | Inheritance, rarity | Duel | Idle, feeding, sleep | Presentation |
|---|---|---|---|---|---|---|---|
| 33 | **Twin Strike** | ~ | M: strike locus, r; Fizz's pond is rich in it | common (13%) | ⚖ **attacks twice but trips**: a winning Pounce takes 2 hearts; a Pounce that head-bonks trips him and he loses 1 | practises pouncing on pellets | a double after-image on Pounce |
| 34 | **Thick Shell** | + | M: hide locus, D; Mossby's pond is rich in it | common (16%) | ⚖ starts the duel with a shell up (the first hit bonks off); his power costs 1 more | shrugs off knocks | a sheen on his back |
| 35 | **Lucky** | + | M: fortune locus, r | rare (4%) | ⚖ once a duel, a hit that would take his last heart misses ("PHEW!"); no element head-start pip | finds a pellet that was not there (the pantry refills one early, once a pet day) | a clover mark |
| 36 | **Glass Cannon** | ~ | M: spirit locus, D | uncommon (11%) | ⚖ a landed special deals +1 more; max hearts -1 | knocks things over when excited | cracks of light on landing |
| 37 | **Copycat** | ~ | M: spirit locus, r | rare (0.7%) | his power is the **friend's** power, at its cost (drawn from the friend's card; deterministic) | copies the friend's idle trick when one is near (#33) | wears the friend's power glyph |
| 38 | **Ghostly** | ~ | M: spirit locus, r; made only by mutation | legendary | ⚖ the first clash he loses to a Pounce passes through him | phases through the rim and comes back | half transparent |

So there are 38 traits: 8 positive, 5 negative, and 25 silly or both ways.
Of them, 23 change a duel's numbers (the 22 marked ⚖, and Copycat), 8 change only
what his own screen shows or what his own pet thinks (local), and the rest
are home and looks only.

### Not a trait

Scars, war paint, vendettas and trophies stay what BATTLES.md made them:
marks of one life and of the book. Rule 10 still holds for every trait's
art: nothing reads as a piercing, tattoo or ornament. The clover, the halo,
the sheen and the third eye are light or pattern on a limb, the back or the
head, never on the chest or the belly.

## 3. Attacks that belong to the creature

Today every pet has the same three stances, its species' one power and a
knack (its taste in stances). After this design, **a pet's moves are:**

```
kit(pet) = species (element, loved stance, power, the stance's art)
         + power flavour (PowerGene's reserved byte, now meaningful)
         + up to 2 active duel traits (riders on stances, start state, the special)
         + knack and mood (the lean and the autopilot, as today)
```

### Species names the moves

Each species names and draws its three stances, so a stance reads as that
creature's move. This is art and a name only; the rule is still the wheel.

| Species | Pounce | Guard | Trick |
|---|---|---|---|
| Grungo | Tongue Lash | Lily Pad | Croak Feint |
| Mossby | Slime Slide | Shell Tuck | Moss Mask |
| Puffkin | Bounce | Puff Up | Spore Cloud |
| Gilly | Gill Slap | Bubble Wall | Wiggle |
| Mothwick | Dive | Wing Wrap | Flicker |
| Mudge | Mud Dash | Burrow | Splash Fake |
| Fizz | Crackle Charge | Carapace | Blink Out |
| Hushwing | Talon Drop | Feather Wall | Silent Turn |
| Tinker | Wrench Whack | Bin Lid | Junk Toss |
| Octopus | Ink Lash | Curl Up | Ink Cloud |

(Names are drafts for the owner and the personality notes to rewrite.)

### Power flavour: one gene, three ways to fire the same power

`PowerGene`'s `reserved` byte becomes **flavour**, expressed as
`byte % 3`. It is Fixed under point mutation, as the power is. It changes
only by allele-style recombination with the pond or a friend (section 4),
so a line keeps its flavour and a friend can bring a new one.

| Flavour | Cost | Quick | Heavy | Feel |
|---|---|---|---|---|
| Classic | the row's | +1 | +2 | today's |
| Swift | the row's - 1 | +1 | no heavy (a hold is a quick) | ⚖ fires often, never big |
| Mighty | the row's + 1 | no quick (a press is a heavy) | +3 | ⚖ rare and devastating; easier to stagger |

### Riders: how a trait changes a move

A duel trait is a **rule row** (`defs/traits.def`), and some rows add a
handler, the way `powers.def` does today. The hook vocabulary is small and
covers every ⚖ trait above:

| Hook | Read at | Examples |
|---|---|---|
| `start` (hearts ±1, spark ±1, shell, no element pip) | before turn 0 | Giant, Tiny, Thick Shell, Jumpy, Lucky |
| `cost` (±1), `windups` (Quick and/or Heavy allowed, extra) | step 1 (the commit) | Clumsy, Giant, Glass Cannon, flavours |
| `winRider(stance)` (+hit, +spark) | step 3 (the clash), the winner | Twin Strike (Pounce +1 hit), Show-off, Stubborn, Grumpy |
| `loseRider` (spark -1, a dodge on luck) | step 3, the loser | Jumpy, Tiny, Ghostly, Stubborn |
| `bonk` (wins on luck, or trips) | step 3, a tie | Wobbly, Twin Strike |
| `turnLuck` (a once-in-n event from his side's luck) | step 1 | Hiccups, Dozy, Clumsy's fumble |
| `afterDamage` (spark on a heart lost, a once-a-duel save) | step 6 | Thrill Seeker, Cuddlebug, Lucky |
| `immune` (no doze, no zap extra, no stagger) | wherever the effect lands | Insomniac, Iron Stomach, Fearless |
| `frozen` (a bit sampled when the duel opens: night, fed, hungry) | the card | Night Owl, Glutton |

Every hook is integer, side-symmetric and reads only the turn's inputs and
`SideState`, as the power handlers do. A trait's luck is
`draw(luck, Purpose::Trait + k)`, a new purpose beside Gadget, Dazzle, Scar
and Mind, so it never disturbs the existing draws.

So two Grungos really do differ. One has Twin Strike and a Swift Foresee:
he pounces hard and foresees every other turn. The other is a Giant with a
Mighty Foresee and Lucky: he is slow to wind up and terrifying when he
lands. Their kids learn to play them differently, and the friend learns to
read them.

### How it shows in the picker and the HUD

The radial HUD (`lib/paint/src/hud.cpp`, `docs/notes/duel-hud.md`) has
spare angle between the heart gauge (22 to 71°) and the stance icons.

- **The VS splash, "tale of the tape":** each pet's active traits fan out
  as glyphs (16 px, ART-style, distinguishable without colour) on the arc
  beside its portrait, rarity on the glyph's ring (1 to 4 dots, plus
  colour). The marquee says one line in the pet's voice: "TWIN STRIKE. HIDE
  YOUR HEARTS."
- **Stance icons carry chips:** a rider sits as a 12 px chip on the icon's
  corner: "x2" on Pounce for Twin Strike, a shell dot on all three for Thick
  Shell, a moon on the power glyph for Night Owl at night. The aimed icon at
  200% shows its chip at 200% too, so the kid sees "this move is special
  for my pet" as they aim.
- **The power glyph shows the flavour:** one pip ring (Swift), two
  (Classic), three (Mighty), which the special-countdown ring already uses
  as its language.
- **A trait firing blooms:** its glyph pops off the pet with one burst word
  (HIC!, TRIP!, x2!, PHEW!, GRUDGE!), using the HUD's bloom (a flash toward
  white, then decay). Both screens show the friend's trait firing too,
  because `TurnReport` gains `traitFired[2]`.
- **Local quirks** draw only on their own screen: the Dizzy ring turns, the
  Upside-Down scene flips, and the Sepia palette swaps the LUT. The HUD's
  shapes (reticle brackets, lime lock ring, segment gaps) are unchanged, so
  nothing becomes colour-only.
- **The lobby:** the friend faces on the rim carry their trait glyphs, so
  "uh oh, a Giant" happens before the match.

## 4. Genetics v2

### What exists today (v1)

A genome is a haploid string of typed genes with stable uids
(`genome.h`). Eggs come from **one parent** by `mutate()`: point mutation
(Nudge, Any, Flag and Fixed rules per byte), duplication, deletion, wake,
sleep, wild rerolls and heirlooms, with a guaranteed visible Look change
and a Mind change per egg, and `viability()` on every child. A **friend egg**
(`friend_egg.cpp`) is the only two-parent path. It copies the friend's
body bytes at shared uids in linkage blocks of about 8 genes, as
`MutPoint` ops, for crossable kinds only (looks, faces, temperament, knack,
species and power). So `apply(parent, diff) == child` replays without the
friend's genome.

### What v2 adds

**1. A diploid trait chromosome on top of the haploid body.** There is one
new gene kind:

```
BLORB_GENE(0x26, allele, AlleleGene, Mind)       // clear of v2's 0x14..0x19, 0x23, 0x33
struct AlleleGene { uint8_t locus, a, b, reserved; };   // two alleles; 0 = wild type
```

- There are 13 loci (rhythm, footing, strike, hide, fortune, sight, temper,
  heart, balance, eyes, coat, spirit, voice). Each sits at a **fixed uid**,
  `0x0F80 + locus`, past the species genes' 0x0F00, 0x0F40 and 0x0F41, so a
  cross lines them up across every badge.
- Rules: `a` and `b` are **Fixed** under point mutation. They change only
  in the recombination step below. That step emits ordinary `MutPoint` ops,
  so the Birth entry and `apply()` replay as today.
- Not Dupable, not Delable. A missing locus reads as wild/wild.
- Expression (`expressTraits`) is pure. A dominant allele shows when either
  slot holds it, and the lower allele id wins if two dominants meet. A
  recessive shows only when both slots hold it. A recessive in one slot
  beside wild is **carried**: not shown, but the egg preview hints at it.

**2. The pond mate: one parent becomes two.** Today's clutch has one
parent, and real Mendelian surprise needs two. For each locus of each egg:

```
allele 1 = one of the parent's two, by the egg's rng
allele 2 = friend egg:  one of the friend's two
           otherwise:   one of the parent's two with p = 0.35 (selfing)
                        else a draw from the species' pond (its wild allele frequencies)
then each allele mutates with p = 1/64 to another allele of that locus, weighted by rarity
(common 8, uncommon 4, rare 1, legendary 0.5)
```

The **pond** is data: `defs/traits.def` gives each locus default pond
frequencies, and species rows lean them. Grungo's pond is rich in Hiccups
and Seer's Eye, Fizz's in Twin Strike, Mossby's in Thick Shell,
Mothwick's in Mood Ring, Hushwing's in Squinty and Sepia, and Tinker's in
Copycat. So species shape which quirks are common, and a line can still
carry anything.

**3. Polygenic bytes regress toward the species.** In v1 a byte only
random-walks, so after enough generations every line drifts to the
extremes. The prototype showed this: polygenic traits climbed past 8% a
trait and still rising at 10 generations. In v2 the egg's byte for each
polygenic gene (`traits.def`'s `BLORB_POLY` rows name them: the SizeGene
base, the startle gain, the melatonin gain, the sleep gate, the hunger
tonic, `biteSize`, `rotMinutes`, the toxin rate and the temperament bytes)
is **the mean of the parent's byte and the pond mate's**. The pond mate's
byte is drawn round the species norm (sd 16). In a friend egg it is the
mean of the parent's and the friend's byte. Then mutation runs as today.
That makes the trait heritable, but not fixed: a Giant's eggs are bigger
than average and smaller than him, and a kid who keeps picking big eggs
breeds a line of giants.

**4. Mutation stays v1's, plus allele mutation.** The point, dup, del,
wake and sleep ops, the forced Look and Mind changes, heirlooms and the
viability retries are unchanged. Sports come from them: a wild palette
roll gives Shiny, a woken dormant gene gives Thrill Seeker or the bright
seer glow, and a duplicated SizeGene gives a giant.

**5. Versioning.**

| What | v1 | v2 |
|---|---|---|
| Genome | haploid genes | + 13 `AlleleGene`s at 0x0F80..0x0F8C; a genome without them is v1 (as `predatesSpecies` reads a genome without a SpeciesGene) |
| `kMutateVersion` | 1 | **2**: the seed-to-child mapping now includes recombination and the regression |
| Lineage Birth entry | diff + seeds + version | unchanged shape; `mutateVersion = 2` |
| Keepsake | format 1 | unchanged: traits are derived at load, as the phenotype is |
| BattleCard | version 1 | **2**: + traits (count, 2 ids), + power flavour, + frozen bits (night, fed, hungry) = 5 bytes |
| Duel wire | version 2 | **3**: v2 badges grey out v3 ones ("another frog") |
| `rosterHash` | species and powers | + the traits table's duel rows and the flavours, so two firmwares duel only if they agree on every rule |

### Recombination at egg laying, concretely

`Creature::layClutch` is unchanged: it draws the seeds. `mutate()` v2,
given the egg's seed, adds two passes before the existing random pass:
`recombine(parent, mate, rng)` and `regress(parent, mate, rng)`, where the
mate is a friend's genome or a `PondMate{species, rng}` that draws
lazily. Both passes emit `MutPoint`s, so the diff, `describeDiff` ("allele
#0F82: wild -> Twin Strike, from the pond") and `apply()` all work as
today. Viability runs last, as today. Traits never touch chemistry, but
polygenic bytes do, and those are exactly what the dry run already checks.

### What the prototype measured

`python3 scripts/test/lineage_sim.py` (measured, 2000 families of 9 badges,
10 generations, 3-egg clutches, a friend egg in 30% of clutches and kept
half the time, kids picking at random):

- **Traits a pet shows:** a mean of 2.4 at founding and 2.8 by generation
  10, and it is stable, not climbing. 4% of pets show none, and 1% show 7
  or more.
- **Duel traits:** 12% of pets have none, 28% one, 31% two, and 30% more
  than two. The rule "at most two active" matters, and it lets a kid choose
  which two on the pre-duel screen (an option, section 9).
- **Lineage is visible:** recessives "come back" (shown by a pet whose
  parent did not show them) 44 to 68% of the time they appear. That is
  Grandpa's hiccups returning. Dominants do so about 37% of the time, and
  polygenic traits about 67%, because of regression.
- **Kids' choices matter** (measured, 600 families each). Picking the egg
  with the most traits gives 3.7 traits a pet at founding and 4.9 by
  generation 10, with 72% of pets over the duel cap. Picking the rarest
  takes Lucky from 4% to 12% and Shiny from 1% to 4%. Picking the fewest
  gives 1.1 traits, and 42% of pets have no duel trait at all. A line is
  what its kids made it.
- **Legendaries are too rare as tuned:** about 0.02 a family by generation
  10 picking at random, 0.04 picking the rarest. Third Eye and Ghostly
  should be a story a family tells once a season, so Courage's golden egg
  (below) is the lever. Tune it in phase 1 so a family of 9 sees about one
  legendary in its first 10 generations.
- **Rarity tiers** are measured, not assigned: the script prints the tier
  each trait's generation-10 share falls in, and the catalogue above uses
  the measured tier. The script's own labels said uncommon for Wobbly, Twin
  Strike, Thick Shell and Seer's Eye, which settle at 13 to 16% (common), and
  rare for Copycat, which sits at 0.7%. Phase 1 picks the pond rates that put
  each trait where the owner wants it.

The full table is in the script's output. Its parameters (selfing 0.35,
allele mutation 1/64, pond sd 16, threshold 18) are the starting values for
`traits.def`.

### Egg previews: choosing an heir (#23)

An egg's genome is fully known at the vigil (`derivePreviewStep` already
derives it), so a preview can show **exactly** what will hatch:

- **Bright glyphs:** traits the hatchling will show, with rarity dots.
- **Dim glyphs with a little arrow:** traits that express only in the
  grown-up (Adult-stage genes, such as the adult SizeGene that makes a
  Giant). "He might grow big."
- **A "?" speckle on the shell:** a carried recessive. It is not shown on
  this egg, but it can come back in its eggs.
- **"?" with a sparkle:** a trait this line has never hatched. It turns
  into its glyph, with NEW TRAIT on the marquee, the first time one hatches
  (the line's discovered set lives in `lineage.log`'s feats or its own
  small file).
- **The friend egg** is speckled and shows both parents' faces in its
  glint.
- **Golden egg** (Courage, below): a gold shell with rarer odds inside.

With the egg-ux picker (#23), eggs sit where the duel's stances sit. The
aimed egg grows to 200% with its trait glyphs fanned around it, and the pet
of the moment (the remains, or the line's spirit) says one line about it:
"THIS ONE HAS HICCUPS. HOW FUN!" Knock to keep, as today.

### Courage (RL2's gold)

Each trait has a courage value: 0 for positive traits, 1 for silly ones, 2
for negative ones and for the screen and control quirks. At death, a life's
courage is the sum over its expressed traits, plus 1 for every duel it
fought carrying an active negative trait (counted in the book, capped at
3). Then:

| Courage | The next clutch gets |
|---|---|
| 2+ | every egg's carried recessives shown plainly (no "?") |
| 3+ | one **golden egg**: allele mutation x4, the legendary weight x10 |
| 5+ | +1 egg, up to `kMaxClutch` (3), earlier than feats would give it |

Courage pays in heirs, never in duel strength, so it cannot snowball a
family's fights.

### The fresh start: wipe and re-found

The owner allows a wipe. Recommended, as a one-time "Season 2" at the first
boot of the v2 firmware on a badge whose pet predates v2 (no `AlleleGene`):

1. **A farewell, not a deletion.** The current pet waves and walks off the
   rim ("MOSSBY I GOES TO THE BIG POND"). The keepsake slots are moved to
   `season1/` on petfs and `lineage.log` to `season1/lineage.log`. Nothing
   on petfs is deleted, which is the keepsake rule. The phone's family tree
   can show Season 1 as the ancestors.
2. **A founding nest of three v2 eggs** of the badge's own species
   (`badges.def` still decides the species): `foundingGenome(species,
   seed)` plus 13 loci drawn from the species' pond, so each egg has its
   own traits. The kid picks with the egg picker. This is the first heir
   choice, and the timeout keeps the first egg as today.
3. **`duels.log` survives:** the book, prowess and ledger are the kid's, not
   the pet's. Vendettas name lines, and Season 1 lines can still be
   avenged.
4. **All 9 badges move together.** Wire v3 cannot duel wire v2, so one
   release goes to every badge on the same day (the owner's phone, or #26
   and #27's Wi-Fi when it lands).

The alternative is **adoption**: append 13 pond-drawn loci to the living
pet, as `adoptSpecies` appended the species genes, and keep him. He keeps
his life, scars and brain, and the first heir choice waits for his death.
This is more code, two genome histories to test, and the kids meet no new
egg on update day. It is the fallback if the owner hears that a kid would
mind the farewell.

## 5. Personality to behaviour (#44)

The personality is a text "bible" that the server writes from the genes
(#37). A kid edits it in the phone app (#43). The server turns it into a
small, bounded set of numbers, and the badge lays those over the genes.

### The modifiers

Six axes, each an integer from **-2 to +2**, plus two picks:

| Axis | What it moves, per step | Plugs in at |
|---|---|---|
| `activity` | wander and rest: ±0.04 drive-units on their brain score (`brain.cpp` decide), the idle walk speed ±15% | a per-action bias vector beside habituation |
| `curiosity` | `Temperament.explore` ±12%; follow_tilt's instinct strength ±12% | `Phenotype.temperament` after `expressStage` |
| `boldness` | the startle receptor's gain ±10%; the curl instinct's strength ±12%; the duel card's `nerve` ±16 | the phenotype, and `cardMood` |
| `playfulness` | hop_circles and follow_tilt bias; the duel card's `pep` ±16 | brain bias, `cardMood` |
| `affection` | cradle's need_touch reward ±12%; call's instinct strength; the duel card's `trust` ±16 | stimulus response, instincts, `cardMood` |
| `chattiness` | thought cooldowns x(1 ∓ 0.2) a step; idle-talk rate | `thoughts.cpp` |
| `idle` (pick) | his favourite idle flourish: `stargaze`, `groom`, `pace_rim`, `nap_spot`, `dance`, `none` | the renderer's idle layer (presentation) |
| `lean` (pick) | `pounce`, `guard`, `trick` or `none`: +1 knack rank of lean in the whisper and autopilot only | `petChoice` |

**Bounds.** The sum of the six axes' sizes is at most 6. The server clamps
and the badge clamps again, so no pet is "maximum everything". Each step is
at most about 15% of the gene's own range, so the genes stay primary: a
Jumpy pet described as brave becomes a less jumpy pet, not a fearless one.

**What they never touch:** life, injury, toxin, hunger, the die and cause
loci, any chemistry rule, the egg, the clutch, viability, mutation, or any
field of the BattleCard that `resolve()` reads (species, power, flavour,
traits, the assist's prowess). In a duel they move only `Mood` (which only
colours the lean and the autopilot, as BATTLES.md already says) and the
lean pick. `petChoice` runs only on the pet's own badge and reaches the
friend only as a locked move, so **duel fairness and determinism are
untouched by construction.**

### Data format

The server (the #37 Worker) adds a third call after the bible: the
description, plus the kid's edits, plus the expressed traits, go in, and
JSON comes out, validated, retried, then clamped:

```json
{"v":1,"mods":{"activity":1,"curiosity":2,"boldness":-1,"playfulness":0,"affection":2,"chattiness":1},
 "idle":"stargaze","lean":"guard",
 "why":{"affection":"loves hugs","curiosity":"pokes at everything"},"bible":"sha1:..."}
```

On the badge it is a 20-byte petfs file `persona.mods` (magic `BLPM`,
version, 6 int8 axes, idle u8, lean u8, the bible's hash u32, crc32). It is
written atomically like `taught.lines`, and a torn or foreign file reads as
all zeros. It comes in two ways: `TWIST persona <hex>` over BLE from the
app (#43), or with the lines pack on a Wi-Fi window (#37, #49). The app
shows the `why` lines: "Because you wrote 'loves hugs', Mossby will ask
for more cuddles."

**Determinism.** The mods are an input, like a twist. The Dish applies
them between ticks (`overlay(Phenotype&, PersonaMods)` re-run after any
`expressStage`). They are part of `Creature::hash()` (their 6 bytes), so a
replay test sets them. A pet without the file replays exactly as today.

**Inheritance.** By default the mods do not inherit as numbers. The
child's bible is written fresh from its genes, its traits and the parent's
bible, so families sound related. An option: the child starts with half the
parent's mods (rounded toward 0) until its own bible arrives.

**Traits feed the personality.** The bible prompt receives the expressed
traits ("has hiccups, is a Giant, strikes twice"). So the text a kid reads
in the app already matches the genes, and the kid's edits steer the
behaviour within the bounds.

## 6. Balance and fairness

The family: 9 badges (8 kids' badges and the gift grungo; `species.def`
also has a 10th species, the octopus), with players aged 6 to 13 and their
parents.

### The rule changes, said plainly

BATTLES.md said "level, age, generation and breeding change no number in
`resolve()`". This design keeps level, age and generation out. It lets
**breeding** change numbers, but only by **sidegrades whose measured edge
is near zero**. The harness, not the designer's guess, is the guarantee.

### Gates (extending `tools/duel_balance.cpp`)

1. **Per trait:** every ⚖ trait, added to each species against the
   unmodified field, wins **0.46 to 0.54** for Random, Queuer and
   autopilot players. A trait outside that band gets its cost column
   moved, the way Lantern's cost moved from 4 to 3.
2. **Per pair:** any two trait-and-species cards, worst pair at most **0.70**
   (today's bound is 0.300 to 0.700).
3. **Combos:** every pair of duel traits (23 x 22 / 2 = 253 combos) on
   every species, with each combo's win rate within **0.40 to 0.60**. Combos
   that break it are made exclusive: the two traits share a locus or a
   rule ("Giant and Tiny never both"), or a combo cost is added.
4. **Envelopes, as `static_assert`s on the table:** max hearts 4 to 6;
   starting spark 0 to 2; a cost never 0 and never above the meter; at most
   2 active duel traits; no trait adds hearts or damage without a cost
   field set.
5. **Unwinnable and unbeatable:** for every card, against the weakest and
   the strongest card in the family's roster, win rates stay **0.25 to 0.75**
   for random players. A Giant Mighty Lucky Thick-Shell is the test case,
   and the cap of 2 active traits is what holds it.
6. **The young kid's levers stay whole:** no trait touches the whisper
   (`petChoice`'s output when followed), the assist or prowess. That is
   why Scatterbrain has no duel effect. Re-run BATTLES.md's fairness table
   (reader against a habit kid, a lean follower, the autopilot) with traits
   on both sides, and it must stay within 0.05 of today's.
7. **Mortal's fair-fight gate** gains the **trait edge**: the harness's
   precomputed win rate of the two cards. Mortal is refused when it is
   above 0.60 for either side, shown as the same balance picture. Spar and
   Scrap are always allowed, as today.

### Determinism and replay

- **Inputs are unchanged in kind:** two cards, two nonces and two move logs.
  Traits are bytes in the cards, and the trait rules are a constexpr table
  in `resolve()`'s compilation unit, so `tools/duel_determinism.sh` can
  still have the S3 compiler evaluate golden battles with traits.
- **The mirror test** (`resolve(B, A)` mirrors `resolve(A, B)` over 10,000
  battles) runs with random trait cards. Trait luck is drawn per side, from
  `fnv(seed, turn, lineage)` then `fnv(luck, purpose)`, never from a shared
  stream.
- **Frozen bits** (night, fed, hungry) are sampled once when the card is
  built and travel in it. The friend never reads my clock.
- **Local quirks** (Upside-Down, Dizzy, Sepia, Squinty, Seer's Eye, Third
  Eye, Curious, Chatterbox) change only what my screen draws and what my
  pet suggests. What reaches the air is still a locked `Move`.
- **Prefix monotonicity:** resolving N+1 turns never changes turns 0 to N.
  It is checked with traits.

### The ESP-NOW protocol: what is exchanged

| Field | Bytes | Where |
|---|---|---|
| `traitCount`, `traits[2]` (active duel trait ids) | 3 | `BattleCard` |
| `flavour` (power flavour) | 1 | `BattleCard` |
| `frozen` (bit 0 night, bit 1 fed, bit 2 hungry) | 1 | `BattleCard` |
| `shownTraits[3]` (looks, for the lobby and the VS) | 3 | `CardLook` |
| `ante` (#46, below) | 5 | `StatePacket` |

A Beacon goes from 78 to 86 B and a State from 111 to 124 B (arithmetic,
from `duel_wire.h`'s byte counts).
Both stay well under the 250 B frame. `kCardVersion` becomes 2 and
`kWireVersion` 3. `rosterHash` includes the traits table, so a badge with
other rules greys out as "they speak another frog" rather than desyncing.
**Both badges agree on effects** because both compute them from the same
card bytes and the same table, as they already do for powers. Nothing about
a trait is negotiated.

## 7. Fights have consequences (#46)

The owner: "I want winning or losing fights to effect the creature, and the
user should get something if they win, or lose it if they lose."

### What a fight does to the creature, visibly

Most of this exists already (BATTLES.md, "What the pets feel"). It is made
**visible** and given a few days' weight:

| After | The creature | Shown |
|---|---|---|
| A win | `mettle` up (exists: a half-life of about 6 pet-days); **swagger** for an hour: a chest-out idle, a victory lap of hop_circles, a boast line | a strut; the rim's trophy shelf gains the spoil |
| A loss | `dread` up (exists); **sulk** for an hour: hides in his hood, needs a cuddle, which clears the sulk and the "three losses" decline | slumped pose; a cradle shows hearts and brings him round |
| A Scrap or Mortal | wounds and scars (exist) | scars on limbs and the head, as today |
| Over many fights | **confidence** = mettle - dread, the trait a kid can read: a confident pet's idle is bolder and his lean (only the lean) steadier | a confidence arc on the home HUD: a thin gauge, as `duel-hud.md` makes them |
| A lineage of wins or losses | the line's crest: "CHAMPION LINE" after its pets win 10 Scraps, "UNDERDOGS" after 10 losses (cosmetic) | a crest on the egg and the hatch marquee |

None of this enters `resolve()` except through `Mood`, which only leans.

### What is at stake: the ante

Before turn 0, on the Staking screen, each badge puts up an **ante** that
fits the tier. Both antes show between the pets, and confirming the tier
confirms the antes. The **winner takes the loser's ante**. A draw, a
fizzle or "wandered off" moves nothing. A flight in a Scrap or Mortal duel
forfeits the ante, as it forfeits the duel.

| Tier | Ante | The winner gains | The loser loses |
|---|---|---|---|
| **Spar** | a **snack**: one pellet from his pantry | a victory snack: a pellet that drops in his dish now and is tastier (boredom -0.2 when eaten) | one pellet from his pantry (it refills on the clock as always) |
| **Scrap** | a **charm**: a keepsake from his shelf (each species has its own: a spark jar, a moss button, a gill frond, a wing scale, a junk gear...) | the charm, on his shelf, and a **trait token** from it (below) | that charm |
| **Mortal** | his **crest**: the line's best charm, plus the life on the Finish (as today) | the crest and a trophy (exists) | the crest; if finished, the life (as today); if spared, a debt (exists) |

**Trait tokens: "a trait nudge" that fits the genetics.** Every charm is
minted with one of its owner's expressed traits. A charm won is a **token
of that trait**: the winner's next clutch draws its pond allele for that
trait's locus from the token's allele once, instead of the pond. So
beating a Twin Striker means one of your next eggs might strike twice. It
is a heritable, visible spoil. It never makes the winner stronger today,
and it gives the kids a reason to fight different friends. Without a
charm, a pet antes a **pebble**: worth nothing to lose and a small trophy to
win. A brand-new pet can always fight and never be stripped.

### How both badges agree

- The ante travels in the `StatePacket` (5 bytes: kind, item id u16, the
  owner's species, the token's allele). It is fixed when the tier is
  confirmed, and both antes are folded into the battle id from turn 0, as
  the stakes are. So the two devices can never fight one battle for two
  different pots.
- The transfer is applied on each badge **from the shared Outcome**, as
  wounds are. The loser's badge removes its own ante, and the winner's
  badge adds the loser's ante from the packet bytes it already holds. Each
  is idempotent by `BattleId` in `duels.log`, so a reboot or a doubled
  packet changes nothing.
- In Mortal, the winner takes the crest only once the loser's
  acknowledgement covers the ending, the same rule as the slaying, so a
  crest is never duplicated by a lost packet.
- Storage: the shelf (charms, crest, pebbles) lives in `duels.log`'s fold or
  a small `shelf.bin` on petfs. It is not the keepsake, and old firmware
  never opens it.

### Balance: no runaway winners

- **Spoils never enter `resolve()`.** A charm is a trophy and a token for
  the next generation, never a heart or a spark. The option of a one-time
  "power charge" (+1 spark next duel) is listed below and not recommended.
- **The existing farming limits hold:** one for-keeps duel (Scrap or Mortal)
  between two lines per pet-day, the head-to-head floor, and Spar offered
  only to a wounded pet. Snack antes are capped at 3 a pair a pet-day.
- **A bounty on the leader:** a pet holding 5 or more charms glows on the
  lobby rim. Beating him wins his ante plus a snack, and losing to him
  costs only a pebble. So the family's champion is the target, and the
  small kid's win against him is the biggest win.
- **Never stripped bare:** the last charm can't be anted (a pebble goes
  instead), and the crest is anted only in Mortal.
- **The underdog keeps theirs:** when the side with assist hearts loses,
  it antes a pebble, whatever the tier. The assist marks the weaker kid, so
  a 6-year-old never loses their favourite charm to a parent.
- **Shelf cap:** 6 charms. A seventh pushes the oldest into the **family
  chest**, which the next generation inherits as its first charm.

### On screen

- **Staking:** both antes sit on a little scale between the pets: the
  charm's 24 px token (ART2-style), a pellet, a pebble or the crest. The
  tier glyph and the skulls are as today.
- **The end:** the loser's ante lifts off his side and flies along the rim
  to the winner's shelf with a bloom. The winner's marquee says a boast in
  his voice. On the loser's screen, the pet waves it goodbye ("BYE, SPARK
  JAR").
- **At home:** the shelf is the rim's trophy arc (the newest three, as
  trophies are today), and a token's trait glyph shows on it. The clutch
  preview marks the egg whose allele came from a token with the token's
  little picture.

### Options for #46

| Option | Gain and loss | Verdict |
|---|---|---|
| **Ante by tier, with trait tokens** (above) | snack, charm, crest; tokens tilt the next clutch | **recommended** |
| Power charge | the winner banks +1 spark for his next duel; the loser loses his | snowballs (each win feeds the next); measure first if wanted |
| Trait theft | the winner's next egg gets the loser's trait for sure; the loser's next egg loses it | too strong a swing for one fight; a token's one-egg pond draw is the gentle form |
| Stat points | wins raise a stat | breaks "no edge on balance"; rejected, as BATTLES.md rejected stats |
| Pantry only | snacks at every tier | fair and simple, but "lose it" feels small at Scrap and Mortal |

## 8. Delight moments

The scenes a kid tells a sibling about:

1. **"Grandpa's hiccups came back!"** Theo's Grungo III hiccups, though his
   dad Grungo II never did. The egg had a "?" speckle, and Theo picked it
   anyway.
2. **The Giant on the VS screen.** Maya's Mudge is so big his head is cut
   off by the rim, and he gets an extra heart. Everyone wants a giant now.
3. **"HIC!" at the worst moment.** A hiccup turns Fizz's Pounce into a Guard
   just as Dad pounces, and it wins by accident. The whole table shouts.
4. **PHEW!** Lucky Gilly at one heart, Dad's Twin Strike lands, the clover
   flashes, and the hit misses.
5. **The upside-down duel.** Theo's Mothwick is Upside-Down. His screen is
   flipped and he still wins, holding the badge the wrong way round for fun.
6. **"I SEE WHAT YOUR PET WOULD PICK."** Third Eye fires once. It shows
   that Mom's pet wants to Guard. Does Mom follow her pet or not? Mind
   games at age 8.
7. **The golden egg.** A Clumsy, Hiccuping, Sleepyhead pet dies after a
   brave life, and its clutch has a gold egg. It hatches a Shiny.
8. **NEW TRAIT.** The first Copycat in the family hatches. The marquee
   says so, and the next duel he fights with his friend's own power.
9. **The friend egg's choice.** Two cousins hug after a duel, and the
   speckled egg shows a Puffkin body with Twin Strike from Fizz's side.
10. **Winning the spark jar.** Maya beats the family's champion, the
    bountied pet with six charms, and takes his spark jar. She keeps it
    on her shelf, and its token gives her next egg a shot at his trait.
11. **The Sleepyhead power nap.** A kid lets the timer run out on purpose,
    and her pet dozes and wakes with two sparks.
12. **Breeding a line of giants.** A 12-year-old picks the biggest egg five
    generations running, and the line ends up Giant every time. He is the
    family's expert.
13. **The personality edit.** A kid writes "he is very brave and loves to
    dance", and that evening the pet does a little dance by the rim and
    leans Pounce.

## 9. Implementation phases

Each phase ends with tests that can be run. Phases 1 to 3 are engine and
presentation and need no other badge. Phase 4 changes the wire, so it ships
to all badges at once.

| # | Phase | Files (C created, E edited) | Tests | After |
|---|---|---|---|---|
| 0 | **This design** and the prototype | C `docs/design/LINEAGE.md`, `scripts/test/lineage_sim.py` | the prototype's tables | now |
| 1 | **Genetics v2 core**: `AlleleGene`, `traits.def` (loci, alleles, ponds, polygenic rows, the trait registry), `expressTraits`, `mutate` v2 (recombine, regress, allele mutation), the friend egg's diploid cross, `kMutateVersion` 2 | C `defs/traits.def`, `traits.h`, `src/traits.cpp`, `test/test_traits/`; E `defs/gene_kinds.def`, `genes.h/.cpp`, `mutate.cpp`, `friend_egg.cpp`, `species_starter.cpp`, `lineage.h` | Mendelian ratios over 10k seeds within binomial bounds (Rr x pond gives the table's rates); `apply(parent, diff) == child` for 10k seeds with and without a friend; regression keeps a 50-generation line's bytes within the norm ± 3 sd; the prototype's frequencies reproduced by the C++ within 2 points; viability unchanged; a v1 genome expresses no traits | grungo-cleanup (#36) and sleep-idle (#20, #21) merged: both move engine hashes |
| 2 | **Home and presentation traits**: size, colour, wobble, hiccup reflex, sleep and feeding quirks, Mood Ring | E `appearance.h/.cpp`, `creature.cpp` (trait hooks over genes), `lib/paint` (scale, LUT effects, flip), thought rows | struct goldens per trait; sim goldens (Giant clipped by the rim, Tiny in the bowl, Upside-Down home); `body_only_full_life` still passes; a 24 h replay hash with a fixed trait set | phase 1; life-stats (#38) merged (it changes the save) |
| 3 | **Egg previews and courage**: trait glyphs, carriers, adult-onset, discovery, the golden egg | E `clutch.cpp` (`EggPreview` gains traits), `lineage.cpp` (courage, discovered set), the egg picker's scene | a preview's traits equal the hatchling's for 1,000 clutches; the golden egg's odds; NEW TRAIT fires once per line | egg-ux (#23) merged |
| 4 | **Duel traits**: card v2, wire v3, `resolve()` hooks, flavours, `traitFired`, the HUD chips and blooms, the balance gates | E `duel.h`, `duel_resolve.cpp`, `duel_powers.cpp`, `duel_card.cpp`, `duel_wire.*`, `species.h` (`rosterHash`), `tools/duel_balance.cpp`, `lib/paint/src/hud.cpp`, `duel_scene.cpp` | section 6's gates 1 to 6; the mirror test and prefix monotonicity with traits; golden battles per trait with the S3 constexpr check; wire round trips at the new sizes; `test_two_dishes` with traited pets | controls (#24, #35) and duel-hud merged |
| 5 | **Local quirks**: Upside-Down, Dizzy, Sepia, Squinty, Third Eye, Seer's Eye | E `duel_scene.cpp`, the session's aim mapping (Dizzy), `duel_mind.cpp` (Third Eye) | goldens; Dizzy's aim follows the drawn icon at every rotation; a Third Eye reveal is computed from resolved turns only (a test that a move logged but unresolved never changes it) | phase 4 |
| 6 | **Spoils (#46)**: antes, the shelf, tokens, swagger and sulk | E `duel_wire.*` (ante), `duel_session.cpp`, `duel_book.*` (shelf, idempotent transfer), `stakes.h`, clutch (token draw) | two-Dish duels at each tier: the ante moves once, from the shared Outcome, on both badges; a reboot mid-transfer converges; the underdog's pebble; the bounty; the cap and the chest | phase 4 (wire v3 carries the ante, so phases 4 and 6 should ship in one release) |
| 7 | **Personality mods (#44)**: the server's third call, `persona.mods`, `TWIST persona`, the overlay | Worker (#37); E `twists.def`, `twists.cpp`, `creature.cpp` (overlay), `brain.cpp` (bias), `thoughts.cpp`, `duel_card.cpp` (`cardMood`) | clamps; the sum bound; an all-zero file replays today's hash; mods never change `resolve()`'s output (fuzz the mods, same Outcome hash); a JSON eval of 20 bibles in the #37 harness | #37 and #43 |
| 8 | **Season 2 release**: the farewell, `season1/` archive, the v2 founding nest, one release to every badge | E `founding.cpp`, `dish.cpp` (the one-time migration), `main.cpp` | a v1 keepsake fixture boots into the farewell and then the nest, and `season1/` holds the old bytes; a second boot never repeats it; all 9 badges on the same build before any duel | everything above; the owner's go-ahead |

Phases 2, 3 and 7 can run in parallel after phase 1. Phases 4 to 6 are one
release, because the wire changes.

## 10. Questions for the owner

Each has a recommended default.

1. **Wipe or adopt?** Default: **wipe with a farewell.** Season 1 is
   archived on the badge, and each badge founds a 3-egg v2 nest of its own
   species. The alternative keeps today's pets and appends traits.
2. **Screen and control quirks (Upside-Down, Dizzy) in duels?** Default:
   yes in Spar and Scrap, and **off in Mortal**, so no pet dies because the
   screen was upside down.
3. **Duel traits active at once:** 2 (default), chosen by rarity, or let the
   kid choose 2 of their pet's traits on the VS screen?
4. **Spoils (#46):** the ante by tier with trait tokens (default), or also a
   power charge for the next duel (not recommended: it snowballs)?
5. **The pond mate** (each egg's second parent is the species' wild pool,
   unless a friend egg brings a real one): OK? Default: yes, with 35%
   selfing.
6. **Personality edits:** may a kid's edit move behaviour by up to 2 steps
   on each axis (sum at most 6), applied at once? Default: yes.
7. **Legendaries:** about one per family per season (default), or rarer?

## Risks

- **Balance work is the cost.** 23 duel traits and 3 flavours on 10 species
  is a big table. The harness makes it mechanical, but phase 4 should ship
  the 10 duel traits that measure cleanest first and add the rest later.
- **Too much on a small screen.** At most 3 shown traits and 2 active is the
  guard. The goldens are looked at before they are blessed.
- **A trait a kid hates** (Hiccups at the wrong moment). Courage pays for
  it, the egg choice avoids it next time, and each such trait is mild by
  design.
- **The fresh start can sting.** The farewell scene and the archive are the
  mitigation, and adoption is the fallback.
- **The prototype is a model.** Its numbers set the starting parameters,
  and phase 1's C++ must re-measure them through the real `mutate()`.
