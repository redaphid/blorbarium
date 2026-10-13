# Creatures, researched for blorbarium

How Steve Grand's Creatures (C1 1996, C2 1998, C3 1999 / Docking Station 2001)
actually worked, then what a single ESP32-S3 pet in a dish should keep, drop,
and leave room for.

## Source key

Every claim has a source tag. Tags with a URL point at the exact material.

| Tag | Source | Trust |
|---|---|---|
| **[P]** | Grand, Cliff, Malhotra, *Creatures: Artificial Life Autonomous Software Agents for Home Entertainment*, Millennium TR 9601 / Sussex CSRP434 (1996). https://www.sussex.ac.uk/informatics/cogslib/reports/csrp/csrp434.pdf . Journal version: Grand & Cliff, *Creatures: Entertainment Software Agents with Artificial Life*, Autonomous Agents and Multi-Agent Systems 1 (1998), https://www.springerprofessional.de/en/creatures-entertainment-software-agents-with-artificial-life/11864520 | Primary, describes **C1** |
| **[O-gen]** | openc2e `genomeFile.h`, https://github.com/openc2e/openc2e/blob/main/src/fileformats/genomeFile.h | Reimplementation. File formats are reliable. |
| **[O-bio]** | openc2e `Biochemistry.cpp`, https://github.com/openc2e/openc2e/blob/main/src/openc2e/creatures/Biochemistry.cpp | Reimplementation. Its own comments mark several details "TODO: correct?" |
| **[O-cr]** | openc2e `Creature.cpp`, https://github.com/openc2e/openc2e/blob/main/src/openc2e/creatures/Creature.cpp | as above |
| **[O-ai]** | openc2e `CreatureAI.cpp`, https://github.com/openc2e/openc2e/blob/main/src/openc2e/creatures/CreatureAI.cpp | as above. The instinct code calls itself "mostly guesswork". |
| **[O-br]** | openc2e `c2eBrain.h` / `c2eBrain.cpp`, https://github.com/openc2e/openc2e/blob/main/src/openc2e/creatures/c2eBrain.h , https://github.com/openc2e/openc2e/blob/main/src/openc2e/creatures/c2eBrain.cpp | as above |
| **[W-x]** | Creatures Wiki pages: Drive https://creatures.wiki/Drive , Brain https://creatures.wiki/Brain , Lobe https://creatures.wiki/Lobe , Combination lobe https://creatures.wiki/Combination_lobe , Life stage https://creatures.wiki/Life_stage , Instinct https://creatures.wiki/Instinct , Biochemistry https://creatures.wiki/Biochemistry , C3 chemical list https://creatures.wiki/C3_Chemical_List , Genetics https://creatures.wiki/Genetics | Community, fact-checked against the engine by fans |
| **[M]** | My own memory or my own arithmetic, not checked against a source in this session | Treat as a lead, verify before relying on it |

---

## Part A: How Creatures worked

### A1. The big idea: an interpreter for a genome

The engine is a generic machine: chemicals, reactions, emitters, receptors,
lobes of neurons, dendrites. The genome says what to build from those parts.
Grand: "the vast majority [of genes] code for structure, not function ...
Genes in our creatures' genomes therefore code for structures such as
chemo-receptors, reactions and brain lobes, rather than outward phenomena such
as disease-resistance, fearlessness or strength." [P]

- Chemicals "are just arbitrary numbers in the range 0 to 255 ... Chemicals
  have no inherent properties--the reactions which each can undergo are
  defined genetically." [P]
- The brain is "a generalized engine for neuron-like computation, whose
  circuitry can be defined genetically." The shipped brain is one model
  "superimposed onto the system." [P]
- C1 needed "approximately 320 interacting genes, each with several
  parameters." [P] By C2 a genome had "around 800 genes", about half
  behaviour/appearance and half biochemistry/nervous system. [W-Genetics]

This is the property that matters most for "modular enough to add ideas
later": new behaviour mostly arrives as data, not code.

### A2. Genome format

- "The genome is a string of bytes, divided into isolated genes by means of
  'punctuation marks'. Genes of particular types are of characteristic lengths
  ... any byte in the genome (other than gene markers) may safely mutate into
  any 8-bit value, without fear of crashing the system." [P]
- One haploid chromosome. [P]

**Gene header** (C1/C2/C3, field names from openc2e) [O-gen]:

| Field | Meaning |
|---|---|
| `flags._mutable` | body bytes may point-mutate |
| `flags.dupable` | gene may be duplicated in crossover |
| `flags.delable` | gene may be deleted in crossover |
| `flags.maleonly` / `flags.femaleonly` | sex-linked expression |
| `flags.notexpressed` | dormant: carried and inherited, never expressed |
| `generation` | clone/generation counter |
| `switchontime` | the life stage at which the gene is expressed |
| `mutweighting` | C2/C3 only: how likely this gene is to be mutated |
| `variant` | C3 only: genome variant |

Why the sex flags exist: "Because the genome is haploid, we have to prevent
useful sex-linked characteristics from being eradicated simply because they
were inherited by a creature of the opposite sex. Therefore, each gene carries
the genetic instructions for both sexes, but only the un-sexed and
appropriately sexed genes get expressed." [P]

**Expression** is a filter run at birth and again at every life-stage change
[P] ("The genome is re-scanned at intervals, and new genes can be expressed
... for example during puberty"). openc2e's filter is exactly four tests: not
`notexpressed`, sex matches, and `switchontime == current stage` [O-cr,
`Creature::shouldProcessGene`, L70-90]. `ageCreature()` bumps the stage and
re-runs `processGenes()`; ageing past the last stage calls `die()` [O-cr,
L141-155]. So a gene is expressed once, at its stage, and what it builds stays
built.

**Gene types** (type/subtype numbers and fields from [O-gen]):

| Group | Gene | Key fields | What it builds |
|---|---|---|---|
| Brain 0/0 | Lobe (C1/C2 `oldBrainLobeGene`) | x, y, w, h, threshold, leakage, rest state, input gain, `staterule[12]`, two dendrite blocks | a lobe and its incoming dendrites |
| Brain 0/0 | Lobe (C3 `c2eBrainLobeGene`) | `id[4]`, `updatetime`, w, h, colour, `WTA`, `tissue`, `initialiserule[48]`, `updaterule[48]` | a lobe whose neurons run SVRules |
| Brain 0/1 | Brain organ | as organ gene | the brain as an organ (ATP, lifeforce) |
| Brain 0/2 | Tract (C3 only) | src/dest lobe ids and neuron ranges, connection counts, `migrates`, `initialiserule[48]`, `updaterule[48]` | a bundle of dendrites between two lobes |
| Bio 1/0 | Receptor | organ, tissue, locus, chemical, threshold, nominal, gain, inverted, digital | chemical level -> writes a locus |
| Bio 1/1 | Emitter | organ, tissue, locus, chemical, threshold, rate, gain, clear, digital, invert | locus value -> emits a chemical |
| Bio 1/2 | Reaction | `reactant[4]`, `quantity[4]`, rate | iA + jB -> kC + lD |
| Bio 1/3 | Half-lives | `halflives[256]` | decay rate for every chemical |
| Bio 1/4 | Initial concentration | chemical, quantity | starting level |
| Bio 1/5 | Neuro-emitter (C3) | `lobes[3]`, `neurons[3]`, rate, `chemical[4]`, `quantity[4]` | firing neurons emit chemicals |
| Creature 2/0 | Stimulus | stim, significance, sensory neuron, intensity, modulate, whenasleep, `silent[4]`, `drives[4]`, `amounts[4]` | what an event does to the body |
| Creature 2/1 | Genus | genus, mum, dad | species and parents |
| Creature 2/2 | Appearance | part, variant, species | which sprite for each body part |
| Creature 2/3 | Pose | poseno, `pose[16]` | a body pose |
| Creature 2/4 | Gait | drive, `pose[8]` | walk cycle chosen by a drive |
| Creature 2/5 | Instinct | `lobes[3]`, `neurons[3]`, action, drive, level | pre-wired learning, applied while dreaming |
| Creature 2/6 | Pigment | colour, amount | tint |
| Creature 2/7 | Pigment bleed | rotation, swap | tint |
| Creature 2/8 | Facial expression (C3) | expressionno, weight, `drives[4]`, `amounts[4]` | which face to show for which drive mix |
| Organ 3/0 | Organ (C2/C3) | clockrate, damagerate, lifeforce, biotickstart, atpdamagecoefficient | a container whose following bio genes belong to it |

### A3. Biochemistry

**Chemicals.** 256 slots, ids 0-255; C1 used 73 of them. [P], [W-Biochemistry]
C1/C2 stored them as bytes, C3 as floats 0.0-1.0 [O-bio, `addChemical` vs
`adjustChemical`, L32-66].

Only a handful have engine-fixed meaning. The rest are meaningful only through
genes. C3 examples [W-C3 Chemical List]: 3 glucose, 4 glycogen, 5 starch,
34 energy, 35 ATP, 36 ADP, 117 adrenaline, 125 Life ("Creature ages as this
depletes"), 127 injury, 129 sleepase, 131-145 drive backups, 148-162 drives,
204 reward, 205 punishment, 212 pre-REM, 213 REM. openc2e hardcodes 35/36
(ATP/ADP), 127 (injury), 148 + n (drive n), 212/213 (dreaming) [O-bio L620,
L649; O-ai L442, L280-284].

**Reactions.** "iA + [jB] -> [kC] + [lD]", every form allowed except
"nothing -> something": fusion, decay to nothing, catalysis (A + B -> A + C),
catalytic breakdown. Rate "is concentration-dependent and therefore
exponential over time." [P] In C3, the reaction runs `ratio =
min(A/i, B/j)`, scaled by `1 - 0.5^(1 / 2.2^((1 - rate) * 32))`, then
subtracts reactants and adds products [O-bio L770-800].

**Half-lives.** One 256-byte gene. C3 decay per biotick:
`chem -= chem * (1 - 0.5^(1 / 2.2^(hl * 32/255)))` [O-bio L156-168]. C1/C2 used
a 32-entry integer multiplier table and a tick mask so slow chemicals are only
touched every 2^n ticks [O-bio L68-84, L124-142]. That is a nice trick for a
microcontroller: integer only, and slow decays cost almost nothing.

**Emitters and receptors: the body-brain bridge.** Both attach to a *locus*, a
named byte (float in C3) somewhere in the creature, addressed by
(organ, tissue, locus). "Changes in the value of a byte to which an emitter
is attached will automatically cause the emitter to adjust its output,
without the code which has caused the change needing to be aware of the
emitter's existence." [P]

- Emitter: every `rate` bioticks, reads its locus. Analog: emits
  `(locus - threshold) * gain` if positive. Digital: emits `gain` if over
  threshold. Optional invert, and `clear` zeroes the locus after reading.
  [O-bio L870-898]
- Receptor: reads a chemical. Analog: `nominal + (chem - threshold) * gain`.
  Digital: `nominal + (chem > threshold ? gain : 0)`. Optional invert,
  clamped 0-1, written to the locus. Several receptors on one locus sum.
  [O-bio L982-1027]

The C3 locus map [O-bio, `c2eCreature::getLocusPointer`, L371-452]:

| Organ.Tissue | Receptor loci (chem -> body) | Emitter loci (body -> chem) |
|---|---|---|
| 0.lobe | any neuron state variable | any neuron state variable |
| 1.0 somatic | 7 life-stage triggers | muscle energy |
| 1.1 circulatory | 32 free "floating" loci | the same 32 |
| 1.2 reproductive | ovulate, receptive, chance of mutation, degree of mutation | fertile, pregnant, and the same |
| 1.3 immune | `dead` (non-zero = die) | |
| 1.4 sensorimotor | 8 involuntary actions, 16 gait loci | 14 senses (always-on, asleep, hot, cold, light, crowded, radiation, time of day, season, air quality, slope up/down, wind) |
| 1.5 drives | 20 drive levels | 20 drive levels |
| organ-local | clock rate, repair rate, injury to apply, any reaction's rate | |

Note what this means: ageing, death, sneezing, fertility and mutation rate
are not code paths. They are receptors on loci. Life stage advances when a
receptor on life-stage locus *n* (fed by a falling Life chemical) goes
non-zero [O-cr L432-436], [W-Life stage].

**Organs** (C2 onward) [W-Biochemistry], [O-bio L605-680]. Each organ owns the
reactions, emitters and receptors that follow it in the genome. Each organ
tick: add `clockrate` to a counter and, when it passes 1, spend `energycost`
ATP (35 -> ADP 36), then run its emitters and reactions. If ATP is short it
takes `atpdamagecoefficient` injury instead. Long-term lifeforce drifts down
toward short-term lifeforce by `damagerate`. Repair moves short-term back up
and emits into chemical 127 (injury). Both decay by 1e-6 per tick (ageing).
An organ whose long-term lifeforce falls to 0.5 is dead. "The brain decreases
to low lifeforce in seconds" without ATP. [W-Biochemistry]

**Metabolism** in C1 was flavour: "starch -> glucose <-> glycogen ->
CO2 + H2O + energy", plus toxins, bacteria with antigens and an "antibody"
response. [P]

**Tick rates.** C1 biochemistry every 5 world ticks (openc2e's guess),
C2 every 2 ticks "(0.2s)", C3 every 4 ticks [O-bio L86-90, L110-114, L145-149].
C3's world tick is 50 ms (20 Hz) [M], so C3 biochemistry runs at about 5 Hz.

### A4. Drives, reward and punishment, stimuli

**Drives are chemicals.** "The higher the concentration of each chemical, the
more pressing that drive." [P]

- C1: pain, need for pleasure, hunger, coldness, hotness, tiredness,
  sleepiness, loneliness, crowdedness, fear, boredom, anger, sex drive.
- C2 added injury, suffocation, thirst, stress, broodiness.
- C3/DS drive neurons 0-19: pain, hunger for protein / carbohydrate / fat,
  cold, hot, tiredness, sleepiness, loneliness, crowdedness, fear, boredom,
  anger, sex drive, comfort/homesickness, and 15-19 navigation drives.
  [W-Drive]

**Reward and punishment come from drive change, not from events.**

```
DriveRaiser          -> Drive + Punishment
DriveReducer + Drive -> Reward
```

"Drive reduction therefore increases the weights of excitatory synapses while
drive increase reinforces inhibitory ones. Of course, reducing a non-present
drive has no effect ... Creatures therefore learn to eat when hungry but not
when full." [P], [W-Drive]. C1 numbered drives 1-12 with raisers at 17-28 and
reducers at 33-44 [W-Biochemistry].

The hand's tickle and slap are the exception: they inject reward and
punishment directly [P] ("stroking it (which generates a positive, 'reward'
reinforcement signal) or slapping it"), [W-C3 Chemical List] (204 "Produced
when the Hand rewards").

**Stimuli.** A world event fires a stimulus number. The creature looks up its
own stimulus gene for that number, so two creatures react differently to the
same event. A C3 stimulus gene names up to four drives and signed amounts; the
engine adds `amount` to chemical `148 + drive`, and unless that slot is
`silent` it also writes the amount into the `resp` (response) lobe so the brain
learns from it. A `whenasleep` flag gates it during sleep. It can also light a
verb neuron (what was done to me). [O-ai L421-505]

### A5. The brain

**C1 brain** [P], [W-Lobe]: about 1,000 neurons in 9 lobes and about 5,000
synapses [P]. The wiki's C1 table: perception 112, drive 16, source 40,
verb 16, noun 40, general sense 32, decision 16, attention 40, concept 640
[W-Lobe].

Each neuron [P, Table 1]: state, threshold (`output = state > threshold ?
state : 0`), relaxation rate (exponential return to rest state), rest state,
input gain, and an SVRule computing the new state from up to two dendrite
classes. Relaxation is the damping: "the further the neuron's state gets from
equilibrium, the faster it relaxes ... the state of the neuron reflects both
the intensity and the frequency of the stimuli." [P]

Each dendrite [P, Table 3]: short-term weight (STW), long-term weight (LTW,
STW's rest state), their relaxation rates, susceptibility and its half-life,
strength (which controls migration), and SVRules for reinforcement,
susceptibility, strength gain and strength loss. "The STW therefore reacts
strongly to individual reinforcement episodes, while the LTW effectively
computes a moving average." [P] The wiki gives C1/C2 as
`stw = ltw + (susceptibility/255) * reinforcement` [W-Brain].

**SVRules** are byte-coded expressions, "designed to be interpreted extremely
rapidly, and also to be non-brittle and fail-safe--genetic mutations can never
cause syntax errors." [P] C1/C2 had 12-byte rules with about 40 opcodes; C3
turned each neuron into a small register machine with 48-byte rules (16
three-byte instructions), 68+ opcodes, conditionals and branches [W-Brain],
[O-gen]. Jumps only go forward, so a rule always terminates [O-br, c2eBrain.cpp
"we must never jump backwards"]. Operands can read the input, the dendrite,
the neuron, a "spare" neuron, a random number, or a chemical (read-only)
[O-br, `c2eSVRule::runRule`].

C3 state per neuron is `float variables[8]` plus an input; per dendrite
`float variables[8]` plus source and destination pointers [O-br, c2eBrain.h].

**Attention.** Each object class has a cell in an input lobe. "These signals are
mapped one-on-one into an output lobe, which sums the intensity and frequency
of those stimuli over time. Simulated lateral inhibition allows these cells to
compete." The winner is "it", the target of whatever the creature does. This
limits creatures to "verb object" thought but means "the net need only
consider one object at a time." [P]

**Concept and decision.** C1: a perception lobe gathers about 128 sensory
inputs; the 640-cell concept lobe holds pattern-matchers, each ANDing one to
four inputs, randomly wired at birth, migrating to new patterns, kept while
reinforced. The decision lobe has 16 cells, one per action ("activate it",
"deactivate it", "walk west" ...), each fed by many migrating dendrites from
concept space. "The strongest-firing Decision cell is taken to be the best
course of action, and whenever the winner changes, the creature invokes the
appropriate action script." [P] Generalisation comes for free: a new
situation ABCD lights stored sub-patterns D, ABD and so on [P].

**Learning.** A dendrite's susceptibility "is raised whenever that dendrite is
conducting a signal to a cell and that cell is firing ... It then decays
exponentially over time" so it can catch "a more-or-less deferred reward or
punishment." Reward raises STW on excitatory synapses, punishment on
inhibitory ones. [P] This is an eligibility trace.

**C3/DS changes** [W-Brain], [W-Combination lobe], [O-br]:
- Tracts replace the two-input limit, so a lobe can take input from many lobes.
- Learning moves into SVRules and extra lobes, so creatures learn "which choices
  affect which drives, rather than just which choices lead to reward and
  punishment." The `resp` lobe carries one value per drive. [W-Brain], [O-ai]
- The combination lobe becomes a grid: "each row of neurons represents an
  action, and each column represents an object class." [W-Combination lobe]
- C3 lobe names, from memory [M]: `driv` (drives, 20), `verb` and `noun`
  (what was done, by what), `visn` (visible objects, 40), `smel`, `situ` and
  `detl` (situation and detail senses), `attn` (attention, winner-take-all),
  `decn` (decision, winner-take-all), `comb` (combination), `resp` (drive
  response), `forf` (friend-or-foe), `stim` (stimulus source). Counts other
  than `driv` are not confirmed here. openc2e's tick does feed `driv` from
  drives 0-19 [O-ai L108-112].

**Brain tick.** openc2e runs C1/C2/C3 brain every 4 world ticks [O-ai L41,
L98]. Lobes and tracts are sorted by their gene's `updatetime` and each runs
its update SVRule over every neuron or dendrite [O-br L39, L227-240,
L367-375].

### A6. Sleep, dreaming and instincts

- Instincts are "predefined 'good' responses to stimuli, contained in their
  genome, that are automatically learnt by creatures while they sleep or in
  the last stages of hatching ... it is as if their mind is detached from their
  body and run through a series of scenarios to train their brain." [W-Instinct]
- An instinct gene says: when up to three input neurons (`lobes[3]`,
  `neurons[3]`) fire, and I do `action`, drive `drive` changes by `level`
  (signed around 128). [O-gen]
- openc2e's C3 version, one queued instinct per brain tick while dreaming:
  wipe all lobes; set pre-REM (212) to 1 and tick; set REM (213) to 1, fire
  the inputs and the verb neuron, tick; write `(level - 128) / 128` into the
  `resp` neuron for that drive, tick; clear REM and wipe the lobes again.
  [O-ai L207-328] The brain's own learning rules then lay down the
  association as if it had happened. Instinct genes are queued when they
  switch on, so new instincts can arrive at later life stages [O-cr L123-126].
- Asleep but not dreaming, the C3 brain does nothing at all [O-ai L89-96].

### A7. Life cycle

- Seven stages: baby, child, adolescent, youth, adult, old, senile ("ancient"
  in C3/DS). A Life chemical is injected at creation and decays; genes switch
  the stage at given levels "and eventually one of them triggers death."
  [W-Life stage]
- Standard C1 norn: child 20 min, adolescent 50 min, adult 1 h 20, pensioner
  10 h, death 15 h. Standard C3: child 9 min ... death 5 h 39 min. "The 'die of
  old age' gene was left out of the original Creatures." [W-Life stage]
- On-screen size grows until maturity, "approximately one third of the way
  through their life." [P]
- Death also comes from organ failure (lifeforce), the immune `dead` locus,
  and starvation of ATP [O-bio], [W-Biochemistry].

### A8. Breeding

- "Parental genes are crossed and spliced at gene boundaries. Occasional
  crossover errors can introduce gene omissions and duplications. A small
  number of random mutations to gene bodies is also applied." Header flags
  say which of omission, duplication, mutation each gene allows. [P]
- "Crossing-over is performed in such a way that gene linkage is
  proportional to separation distance", so neighbouring genes travel
  together. [P]
- Duplicated genes are where novelty comes from; creatures with 36+ lobes have
  been bred this way. [P], [W-Lobe]
- In C3 the mutation chance and degree are themselves loci driven by
  receptors, so biochemistry can modulate mutation rate [O-bio L410-418].

### A9. Compute cost

- Design target: "a world with ten creatures requires the processing of some
  20,000 neurons and 100,000 synaptic connections every second, in addition to
  the load imposed by the display." [P] That is about 2,000 neuron updates and
  10,000 synapse updates per creature per second.
- Hardware: "runs in real-time on Windows95 (486/66Mhz)", "up to ten
  creatures can be active at one time before serious degradation of
  response-time." [P]
- So one C1 creature's whole mind fitted in a few percent of a 66 MHz 486 with
  no FPU work [M: inference from the two quotes above].

---

## Part B: blorbarium

The setting: one pet, no other creatures, no objects, no vision. Its world is
its body (IMU), the clock, and an owner who visits over BLE.

### B1. What carries the "it's alive" feeling

Ranked by how much each buys for a pet you can only poke, shake and feed.

1. **Drives as chemicals with half-lives, ticking whether you look or not.**
   This is the single biggest effect. Hunger, tiredness, boredom and
   loneliness creep up on the clock, so the pet you come back to is not the
   pet you left. It needs no learning to work. [design judgement]
2. **Stimulus genes between the sense and the feeling.** A knock is not
   "punishment"; it is stimulus 3, and *this* blorb's gene says stimulus 3
   raises fear by 40 and boredom down by 10. A sibling's gene might say the
   opposite. This is how two devices with the same firmware get different
   personalities, and it is cheap. [P], [O-ai]
3. **Reward and punishment computed from drive change.** It makes the same
   action good or bad depending on state ("eat when hungry but not when
   full", [P]). For blorbarium, "being shaken" can be fun when bored and awful
   when tired. That reads as mood, not a lookup table.
4. **Learning its own actions against its own drives (C3-style, per drive).**
   The actions are things it can do in a dish: wander, curl up, wobble, sleep,
   chirp/glow to call the owner, follow tilt, flee tilt, groom. It learns
   which ones reduce which drives in which situation. The most visible payoff
   is *anticipation*: if the owner usually connects and feeds it in the
   evening, it can learn to "call" when hungry in the evening. [W-Brain]
5. **Face from drive mix (C3 facial-expression gene).** On a 240 px round
   screen the face is the main output. Expression genes mapping drive mixes to
   faces make internal state legible without a menu, and the mapping itself is
   heritable. [O-gen]
6. **Sleep tied to the clock, dreaming shown on screen, instincts learnt in
   dreams.** A visible night cycle is very "alive". Dreaming as the moment
   instincts are wired is a story worth showing the owner (the phone can list
   tonight's instincts). [W-Instinct], [O-ai]
7. **Life stages from a decaying Life chemical plus switch-on genes.** The pet
   visibly grows up; new genes (new faces, new instincts, new reactions) come
   online at puberty. This rewards keeping it for months. [P], [W-Life stage]
8. **Injury and slow healing.** A hard knock or a fall sets injury, which
   heals over hours. Cause and consequence the owner can see. [O-bio]
9. **Adrenaline-style modulators read by receptors on the brain.** After a
   shake, a fast-decaying chemical can raise neuron gain or relaxation so the
   pet is jumpy for a minute. [P] ("control of arousal" is listed as possible
   but unimplemented in C1.)

A caution on death. Creatures dies of old age (C3 about 5.5 h of play
[W-Life stage]). The handoff calls this pet a keepsake. Options: no
senescence gene in the starting genome (C1 shipped without one,
[W-Life stage]); a very slow Life chemical; or a lineage where an old pet lays
an egg (self-crossed and mutated genome) so the keepsake is the line, not the
individual. This is a product call, not a technical one.

### B2. Dead weight at this scale

| Creatures mechanism | Why it does not pay here | Keep a stub? |
|---|---|---|
| Vision lobe (`visn`, 40 object classes), smell | No world, no objects. | No |
| Attention over many objects | The only "things" are the body, the dish and the owner. Attention collapses to "what just happened" (a 3-6 cell input). | Tiny |
| 640-cell migrating concept lobe | About 20 inputs, not 128. A fixed C3-style grid of situations x actions is enough. | Replace with grid |
| Dendrite migration, strength atrophy | Exists to cover a huge input space with few dendrites. Here the grid can be fully connected. | No |
| Sex, mating, pregnancy, sex drive, pheromones, broodiness, crowdedness | No second creature. | Keep sex flags in the header for format compatibility only |
| Navigation drives, gait genes, 16-part skeletal poses, per-part appearance sprites | No walking world; a blob in a dish. | Replace with a small "body style" gene |
| Verb-object language learning | No other creatures, no keyboard on the device. A learnt name (the phone sends the name, it is rewarded for responding) is the useful slice. | Optional, later |
| Bacteria, antigens, antibodies, toxins | No environment to carry them. "Bad food" from the phone could be one toxin. | One toxin at most |
| Hot/cold drives | No environment, **unless** the QMI8658's on-die temperature sensor is used (warm in a hand) [M]. Board self-heating would bias it. | Maybe, measure first |
| Full multi-organ ATP economy | Many organs each burning ATP is invisible on a single screen. One body organ plus maybe a brain organ keeps "starving makes you weak". | 1-2 organs |
| General SVRule interpreter (C3, 48-byte rules) | Powerful but hard to tune and to show on a phone. C1-style fixed neuron/dendrite equations with genetic parameters cover the shipped brain. | Defer; keep the gene type id reserved |
| Breeding crossover between two parents | No second parent on one device. Still useful for eggs (self-cross with mutation), or two friends' devices meeting over the phone. | Keep the code path small |

### B3. What makes Creatures extensible, and what blorbarium's extension points should be

**Why it is data-driven.**

1. **The genome is the only description of the creature.** The engine has no
   idea what hunger is. It knows "chemical 148+n is drive n" and little else
   [O-ai L442]. Everything else is genes. [P]
2. **Loci decouple producers from consumers.** Code writes a sense value into a
   locus. It does not know who listens. Genes attach emitters to that locus
   ("without the code ... needing to be aware of the emitter's existence",
   [P]). Receptors write loci that code reads (life stage, death, actions).
3. **Chemicals are anonymous integers.** Adding a chemical is choosing an
   unused id and writing genes that make, use and decay it.
4. **Genes are typed, fixed-length, self-delimiting records, and every body
   byte is mutation-safe.** [P] Readers can skip a gene type they do not know.
5. **Expression is a pure function** of (gene header, sex, life stage). [O-cr]
6. **Stimuli are numbered, and the response lives in the genome**, not in the
   code that detected the event. [O-ai]

**Proposed extension points** (my design, [M]). The aim: most later ideas
should be a table entry plus genes, not a change to the engine.

| Later idea | What the code adds | What the genome adds | Engine untouched? |
|---|---|---|---|
| **A new sense** (e.g. "spun", "dark room" via a light sensor, "warm") | A detector that writes a float into a newly registered **sense locus**, and/or fires a **stimulus id** on the edge. One line in a `SENSES[]` table: id, name, locus index. | Emitter genes on the locus (continuous senses) and stimulus genes for the id (events). Optional instinct genes so it has a reflex from birth. | Yes |
| **A new chemical** | A row in a `CHEMICALS[]` metadata table: id, name, colour, "show on phone" flag. No engine change; the table is for the phone UI only. | Half-life entry, initial concentration, reactions that make and use it, receptors that make it matter. | Yes |
| **A new behaviour / action** | A row in `ACTIONS[]`: decision-neuron index, name, an animation, and an `execute()` that may change the body (move the blob, glow) and may fire a **self-stimulus** (e.g. "groomed myself" -> boredom down). | Instinct genes to bootstrap when to do it; stimulus genes for its self-stimulus; optional expression genes. The decision lobe grows by one row. | Yes, if the lobe size comes from a gene |
| **A new gene type** | One record in a `GENE_TYPES[]` registry: (type, subtype), fixed body length, decoder, `express(creature)`, `describe()` for the phone, and per-byte mutation range. Unknown types are kept, inherited and skipped. Bump a genome-format version only if existing types change. | The genes themselves. | Registry change only |
| **A new organ** | Usually nothing: an organ is a container gene with clockrate, lifeforce, damage and repair, and the bio genes that follow it belong to it. New *organ-local loci* (a receptor that drives something new) need a row in the locus map. | An organ gene followed by its reactions, emitters, receptors. | Yes |
| **A new thing the phone can do** | Prefer one of four generic BLE verbs so the phone grows without firmware: `STIM <id> [strength]` (feed, pet, play are just stimuli), `READ chem|drives|loci|genome|brain`, `EDIT gene <index> <bytes>` (rename, recolour = edit a pigment gene; validated against the gene registry), `INJECT <chem> <amount>` (debug or "medicine"). A truly new verb is one handler in a `COMMANDS[]` table. | For feeding etc., the stimulus gene decides what food does. | Usually yes |

Two rules that keep this honest:

- **The locus map is the API.** Publish it as a single table (organ, tissue,
  index, name, direction, range) that firmware, genome tools and the phone all
  read. Creatures' locus map was implicit in engine code [O-bio L371-452]; make
  it explicit.
- **Hard-code as few chemicals as possible.** Fix only what the engine must
  read: reward, punishment, Life, injury, ATP/ADP if organs are kept, REM, and
  the drive range. Everything else stays anonymous.

### B4. Budgets for a faithful-but-small version on the ESP32-S3

All numbers below are my estimates [M] unless tagged.

**Proposed sizes**

| Part | Size | Notes |
|---|---|---|
| Chemicals | 256 x float = 1 KB (or 256 B as uint8, C1/C2 style) | Keep 256 slots even if 40 are used, for genome compatibility with mutation. |
| Half-life table | 256 B | One gene. [O-gen] |
| Reactions | ~60 x 9 B ~ 0.5 KB | C1 shipped the whole metabolism in ~320 genes total [P]. |
| Emitters + receptors | ~120 x 12 B ~ 1.5 KB | |
| Stimulus genes | ~24 x 16 B ~ 0.4 KB | one per sense event, phone action, self-action |
| Brain | inputs: ~20 drives + ~16 sense/situation cells; comb grid ~8 situations x ~12 actions = 96 cells; decision 12; resp 20. About 170 neurons. Dendrites: ~96 x 4 into comb, 96 x 12 into decision, ~1,500 total. | C1 was ~1,000 neurons and ~5,000 synapses [P]; this is a sixth of that. |
| Neuron state | 170 x 8 floats = 5.4 KB | C3 layout [O-br]. |
| Dendrite state | 1,500 x (8 floats + 2 x uint16) = 51 KB, or 1,500 x 8 int16 = 24 KB | Fits internal SRAM. A full C1-size brain (5,000 dendrites) is ~170 KB float, so it would want PSRAM. |
| Genome | ~300 genes x ~15 B avg ~ 5-8 KB | C1: ~320 genes [P]. Store as one file. |
| Whole live creature | under 100 KB | |

**Memory context.** Internal SRAM is 512 KB. A full 240x240 RGB565 frame is
115 KB; the NimBLE host and IDF take a large share too [M]. The 1.28 board's
PSRAM is not stated in the handoff (I believe it is an ESP32-S3R2 with 2 MB,
[M], check); the 1.46 board has 8 MB octal PSRAM (handoff). The creature
above fits in SRAM; a full-size C1/C3 brain belongs in PSRAM.

**Compute per second** (240 MHz LX7 with a single-precision FPU [M]):

| Work | Rate | Cost | CPU of one core |
|---|---|---|---|
| Biochemistry: 256 decays + 60 reactions + 120 emitters/receptors | 5 Hz (C3 cadence, [O-bio] + [M]) | ~500 items x ~60 cycles x 5 = 150k cycles/s | ~0.1% |
| C1-style brain (fixed equations), 170 neurons + 1,500 dendrites | 5 Hz | ~1,700 x ~40 cycles x 5 = 340k cycles/s | ~0.15% |
| Same brain through a C3 SVRule interpreter (16 instr x ~15 cycles) | 5 Hz | ~1,700 x 240 x 5 = 2M cycles/s | ~1% |
| Full C1-size brain, 1,000 neurons + 5,000 dendrites, SVRules | 5 Hz | ~6,000 x 240 x 5 = 7.2M cycles/s | ~3% |
| Dreaming: one instinct = 3 brain ticks [O-ai] | 1 instinct per brain tick while dreaming | same as brain tick x 3 | <1% |
| Display: full-frame push over SPI at 80 MHz | 115,200 B x 8 / 80e6 = 11.5 ms per frame | ~35% of wall time at 30 fps | This dominates, not the creature |

**Persistence.** A snapshot is chemicals (1 KB) + neuron and dendrite state
(~30-60 KB) + organ lifeforce + age + genome (~8 KB): under 80 KB [M]. Write
it to a flash file with write-temp-then-rename for atomicity (the handoff's
keepsake rule). At one save every 5 minutes that is ~105,000 saves a year;
spread by LittleFS's wear levelling over a 1 MB partition that is far inside
NOR flash's ~100k erase cycles per sector [M]. Save LTW, not STW: STW is meant
to be short-lived anyway [P].

**Clock.** Run biochemistry and brain on a fixed simulated tick (C3: 20 Hz
world, biochem and brain every 4th, [O-bio], [O-ai], [M]) and decouple it from
the frame rate. Let half-lives be expressed in real minutes so a day on the
device is a day for the pet.

---

## Open questions worth answering before building

1. Death or no death (B1, last paragraph).
2. Keep the C3 SVRule interpreter for future brain experiments, or ship C1-style
   fixed equations with genetic parameters and reserve the gene type? (B2)
3. Does the QMI8658 temperature read usefully through a 3D-printed case on a
   warm board? (B2) Measure.
4. Should two friends' devices be able to breed via the phone? If yes, keep
   crossover, duplication and deletion from day one (A8).
