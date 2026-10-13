# Grungo: what he is and what blorbarium needs to draw him

Read-only investigation, 2026-10-02. Path shorthands:

| shorthand | path |
|---|---|
| `SE` | `D:\Projects\sprite-expressions` (the art pipeline; `characters/grungo` is the folder the user pointed at) |
| `FW` | `C:\Users\hypnodroid\Worktrees\claude-notification-screen-frog`. This is branch `claude-notification-screen-frog` of claude-notification-screen, commits `7dcb404` and `da84304`. **It is not merged to main.** Grungo's pet pack lives here, under `pets/grungo/` |
| `CP` | `D:\Projects\cyber-puck` (owns the PNG-to-C-header converter) |
| `PS` | `D:\Projects\puck-sprites` (has a walk cycle of the same frog) |

The `SE/characters/grungo` folder holds only the art recipe and the frames. The
character writing, including the overfit engine wiring, is in the pet pack at
`FW/pets/grungo/`. That pack's `SOUL.md` is the character bible.

---

## 1. Who Grungo is

**Source of truth.** `FW/pets/grungo/SOUL.md:3-5`: "Everything else in this pack -- the
voice, the genome, the art prompts -- is cut from this. When they disagree, this
wins and they get fixed." `FW/pets/grungo/CLAUDE.md:5-19` repeats this, and adds
that his **Never** list "is not negotiable, including as a joke."

**Origin.** He is a frog Settler from sporefall-station's sinking swamp colony.
`SOUL.md:9-16`: "a human base that has been going down into the bog for
generations, an inch at a time, and is now more marsh than metal. Teal mist,
olive overgrowth swallowing grey tech, little hot points of glowing spores." He
is the cloaked design: `SE/characters/grungo/character.json:2` says "the cloaked
design (frog-s-white keyframe, puck-sprites/out/frog/kf-final). He sees the
future. For Peter's badge." `SOUL.md:18` says "He belongs to Peter." (Check
whether Peter is the friend blorbarium is for. The HANDOFF says only "a friend".)

**Look**, quoted from `SOUL.md:22-28`:
- "Stocky, round, short and wide; a big frog head on a squat body."
- "Mottled olive-green skin, a pale lumpy belly, big webbed hands and feet."
- "Two big round dark red-brown eyes on top of his head, and two small
  eye-stalk antennae poking out through his hood."
- "A brown hooded cloak hanging from his shoulders and a brown scarf wound
  at his throat. The cloak is part of him; he is never without it."
- "When the seeing comes on, his eyes glow bright swamp-teal."

The art prompt says the same: `character.json:24` ("a stocky green frog villager
in a brown hooded cloak and a brown scarf ... two small eye-stalk antennae,
mottled olive green skin"). The style line is `character.json:25`: "cartoon game
character illustration, bold black outlines, flat cel shading".

**Personality**, quoted from `SOUL.md:32-52`:
- **A seer.** "He sees the future. This is the thing about Grungo. Mostly he
  sees small, damp, obvious futures, and he announces them with total
  gravity ... Whatever happens, he saw it coming -- even when he plainly did
  not". He calls it "the bubbles talking".
- **Skittish.** "Anything sudden ... is 'eep!', and the hood goes up. A moment
  later he explains that he foresaw it." He would rather run and tell the Spore
  Wardens than fight.
- **A hoarder.** "Essence bubbles, shiny scrap, lost bolts, a nice pebble.
  Everything is 'for later' ... He will trade any of it for a fat fly".
- **Warm and a little ridiculous.** "patient, slow, damp and pleased with
  himself. He likes sitting very still on a pipe like a stone, watching the
  mist, and hopping in circles with his cloak flapping when he's happy."

**Voice** (`SOUL.md:56-69`): he speaks in the third person ("Grungo", never "I"),
in lowercase and briefly. A prophecy gets one capitalised word ("it is WRITTEN").
He says "brrrup" while thinking, "croooak" when pleased, and "eep" when startled.
Sample lines: "brrrup. grungo has foreseen it. there will be a snack.",
"eep! hood up. ...grungo knew that would happen.", "this bolt is for later.
everything is for later."

**What he cares about** (`SOUL.md:80-83`): Peter, then flies ("the fatter the
better"), then his hoard, then being right about the future.

**Never** (`SOUL.md:87-93`): never menacing or doom; "Never death, drowning,
illness, or anyone being gone -- not even as a joke. The sinking station is
cosy, like a warm bath"; never mean or sarcastic; never brave in a fight; "Never
without his cloak."

> **Design conflict to raise with the user.** Blorbarium's premise is that each
> life ends, and an egg with a mutated genome starts the next one. SOUL.md
> forbids death "not even as a joke." The rule was written for his voice lines,
> but it marks a tone the user cared about. The end of a life probably needs a
> cosy framing that never reads as death: settling into the bog to sleep,
> metamorphosis, or "grungo has foreseen the egg." The user should decide this
> before any death or fade art is made.

**Palette.** I sampled it from `raw/neutral/frame-00.png` with an 8-colour median cut:

| role | colour(s) |
|---|---|
| skin, olive green | `#749b50`, `#587340`, highlight `#99bd73` |
| cloak and scarf, brown | `#453b2a`, `#535137`, `#313524` |
| outline and darks | `#221410`, `#060503` |

Other colours named by the project:

- **Skin:** RGB `(112,152,72)` (`character.json:75-79`, the paint op) and pin body
  colour `(104,148,68)` (`FW/pets/grungo/pet_pack.h:36`).
- **Cloak:** pin accent `(95,62,40)` (`FW/pets/grungo/pet_pack.h:36`).
- **Foresee eyes:** painted teal `(80,240,220)` with a `(230,255,250)` rim
  (`character.json:120-129`). In the 64px header this became `(62,248,190)`.
- **Tongue and mouth:** pink, `(246,153,157)` in the header.
- **Eyes:** "dark red-brown". Note that the happy, alarmed, annoyed and meow
  frames drifted to bright red irises (see §3).

---

## 2. The art assets

### In `SE/characters/grungo/` (committed, the canonical art)

| file | format | size | notes |
|---|---|---|---|
| `hero.png` | 1280x720 RGB, white background | 255 KB | The approved art. Byte-identical to `PS/out/frog/kf-final/frog-s-white.png` (md5 match). |
| `anchor-raw.png` | 1024x1024 RGB | 481 KB | The hero placed on the canvas, white background. Used as the IP-Adapter identity reference (`character.json:10`). |
| `anchor.png` | 1024x1024 RGBA | 545 KB | Same, with binary alpha. Body bbox `(227,80)-(796,920)`, which is **569x840**. |
| `raw/<expr>/frame-00.png` x10 | 1024x1024 RGBA, binary alpha | 545-591 KB each | **One frame per expression.** All ten share the same alpha and bbox. `raw/neutral` is the anchor itself (`forge.py:111-112`). |
| `raw/manifest.json` | JSON | — | Seed, denoise, mask and paint for each picked frame. |
| `masks/{eyes,face,mouth}.png` | 1024x1024 L | ~2 KB | Inpaint masks drawn from `character.json:28-70`. |
| `character.json`, `expressions.json` | JSON | — | The recipe: models, prompts, masks, paint ops. |

These are individual frames, not sprite sheets. They are full-colour,
anti-aliased illustration with no palette constraint. The frames are
**pixel-registered**: `flows/inpaint.json` re-noises only the masked region and
composites the decode back over the untouched anchor (`flows/inpaint.json` node
`54`; `forge.py:23-28`). Every frame also takes the anchor's alpha
(`forge.py:304-310`). I diffed each frame against neutral, and every expression
changes pixels only inside the box **`(337,142)-(694,385)`, 357x243 px**:

| expressions | box changed vs neutral |
|---|---|
| face-mask ones | 356x239 |
| blep | 336x135 |
| eyes-only ones (foresee, sleepy, sleep) | about 305x105 |

The rest of the body is identical, pixel for pixel.

**Generator.** `SE/forge.py` (anchor → masks → gen → pick) drives ComfyUI: SDXL
Juggernaut with IP-Adapter Plus (`character.json:12-14`). `SE/README.md`
describes it. `SE/CLAUDE.md:7-8` adds two rules: image models only, because "the
video route has crashed this GPU", and a person picks every frame
(`SE/CLAUDE.md:9-10`).

### How it reached the ESP32 last time

A generated C header, not runtime PNG decoding.

- **Converter:** `CP/tools/sprite2header.py`, run with
  `--size 64 --colors 128 --name GRUNGO`. The exact command is in
  `FW/pets/grungo/pet_pack.h:11-13`.
- **Steps** (`sprite2header.py:28-41`):
  1. Mask.
  2. Trim to the union bbox and fit the square, standing on the bottom edge.
  3. Quantise to one shared palette (median cut plus k-means).
  4. Downscale by **majority vote of palette indices**, with no blending.
- **Output:** `SE/work/grungo/badge/grungo_sprite.h`, copied to
  `FW/pets/grungo/grungo_sprite.h`. It is "64x64, 126 colours + clear, 10
  expressions, 10 frames (40960 bytes of flash, no RAM)" (header line 4).
  - Format: an 8bpp palette index per pixel, 0 = clear, plus a `uint8 r,g,b`
    palette.
  - Structs: `PetSprite{w,h,colours,palette,anims,anim}` and
    `PetAnim{name,count,frames}`, defined at `FW/src/pet.h:351-363`.
  - `work/` is gitignored (`SE/.gitignore:1`), so the copy under `work/badge`
    is scratch.
- **Draw:** `FW/src/main.cpp:734` `drawSpriteFrame`. It RLE-walks each row into
  `canvas.fillRect` and allows only whole-number upscales (`main.cpp:858`), to
  stay crisp.
- **Two converter problems** to fix before reuse:
  - The converter snaps palette entries to **Puck's** colours
    (`sprite2header.py:57-58`, `ANCHORS = ((235,140,55),(250,215,190))`,
    applied at `:180-183`). Puck's cream `250,215,190` did land in Grungo's
    palette (`work/grungo/badge/grungo_sprite.h:145`).
  - At Puck's default of 24 colours, Grungo lost his teal eyes and pink tongue,
    which is why the pack used 128 (`FW/pets/grungo/pet_pack.h:6-9`, commit
    `7dcb404` message).

`work/grungo/badge/preview.png` shows the 64px result. The whole body fits in
64px, so the face is about 20px wide and only the colour cues read: teal eyes,
pink tongue. Most of the expressions are mush at that size.

### Other Grungo art outside the folder

| art | where | format | assessment |
|---|---|---|---|
| **Walk cycle, 5 directions x 8 frames** (s, se, e, ne, n) | `PS/out/frog/atlas96/<dir>/atlas.{png,json}` | TexturePacker-style JSON with pivots and per-frame durations, cells about 68-88 x 104-108 px | Same cloaked design, rendered as chunky pixel art. Frame durations differ by direction: s 172 ms, se 148, e 188, ne 211, n 352. The 5 PNGs are 27-42 KB. Raw clips are in `PS/out/frog/clips/` and keyframe picks in `PS/out/frog/picks.json`. Mirror se/e/ne for the west-facing directions. The overview at `PS/out/frog/atlas96-rows.png` shows a few white speckle holes in the cloak and legs. |
| In-game idle and step sprites | `D:\Projects\sporefall-station\public\themes\swampspace{,-hires}\chars\frog-settler-s-{idle,step}.png` | 48x48 and 96x96 RGBA | — |
| `frog-settler_{idle,walk,attack,hurt,death}_made` | `D:\Projects\sporefall-art\renders\` | — | **Untextured mannequin** motion-drive renders (`.../idle_made/00/000/shaded.png`, 432x736). Motion reference only, not frog art. |

---

## 3. The expression set

There are ten picked expressions, one frame each (`SE/characters/grungo/raw/manifest.json`,
`work/grungo/badge/grungo_sprite.h:830-841`). `derp` is defined but was never
picked. Intent comes from the prompts in `SE/characters/grungo/expressions.json`
and the `about` fields:

| id | mask (what changes) | meant to convey (prompt, file:line) | what the picked frame shows | natural use in a Grungo life |
|---|---|---|---|---|
| `neutral` | — (the anchor) | his resting face (`forge.py:112`) | dark eyes, flat closed mouth | idle, "sitting very still on a pipe like a stone" |
| `happy` | face | "happy beaming expression ... eyes squeezed shut as happy upturned arcs ... wide closed-mouth frog smile" (`expressions.json:5`) | wide smile, but the **eyes are open**, bright red irises | pleased, fed, owner visiting |
| `alarmed` | face | "terrified startled shocked ... huge bulging wide eyes, tiny pinprick pupils ... mouth open in a small round o" (`:11`) | huge white-rimmed eyes, spotted brow, small o-mouth | "eep!", a startle from a shake or knock |
| `annoyed` | face | "grumpy annoyed ... eyes narrowed to a flat glare under heavy lids ... frown" (`:17`) | red glare, down-turned mouth | hungry, pestered, handled too much |
| `meow` | face | `"about": "the croak"`: "croaking, mouth wide open showing a pink mouth, puffed out round throat" (`:20-24`) | open pink smiling mouth | croaking, "croooak", the talking mouth |
| `blep` | mouth | "a long pink sticky frog tongue sticking straight out" (`:27-31`) | tongue hanging out | **catching a fly / eating**, silliness |
| `derp` | face | cross-eyed goofy face (`:33-38`) | **no frame.** `FW/pets/grungo/README.md:24-25`: "no seed went cross-eyed", falls back to `blep` (`FW/pets/grungo/genome.json:51`) | — |
| `foresee` | eyes | `"about": "he sees the future"`: "eyes glowing bright bioluminescent swamp teal ... a seer in a trance" (`:39-45`) | teal glowing eyes, otherwise neutral | **his signature.** Scrying, prophecy, and the trait most worth making heritable |
| `sleepy` | eyes | "heavy drooping half-closed eyelids" (`:47-53`) | half lids | drowsy, also usable as a slow blink |
| `sleep` | eyes | "eyes fully closed, peacefully asleep" (`:54-60`) | closed lids | sleeping, also usable as the blink frame |
| `yawn` | mouth, built **from** `sleep` (`:61-63`) | "big wide sleepy yawn ... eyes closed" (`:64`) | eyes shut, mouth wide | falling asleep or waking |

**Consistency issue.** The face-mask frames (happy, alarmed, annoyed, meow) went
to bright red irises and some head spotting. Neutral and the eyes-only frames
keep the darker red-brown eyes. See the contact sheet I built at
`scratchpad/faces.png`. At small sizes this reads as the eye colour flickering
between expressions. A genetic recolour of the eyes also needs every frame's
eyes in one palette region.

**Behaviours in SOUL.md with no art yet:**

| behaviour | source | art status |
|---|---|---|
| hood going **up** on a startle | `SOUL.md:40` | Every frame has the hood already up. |
| hood **down** to sleep | genome `sleep.doing`, `FW/pets/grungo/genome.json:35` | No such frame. |
| hopping in circles with the cloak flapping | `SOUL.md:51-52` | — |
| hoarding props: bubbles, bolts, a crate hut | — | — |
| an egg, hatchling or juvenile, or a life-end pose | — | Required by blorbarium's run structure. |

**Frames per expression.** Every expression is a single frame. sprite-expressions
can hold more (`frame-NN`, `forge.py:321-325`), and the converter treats frames
1.. as blinks or loops (`sprite2header.py:17-19`). None have been made.

---

## 4. Overfit, to discard, versus the character, to keep

### Discard: engine and badge specifics

**Claude events as stimuli.** All in `FW/pets/grungo/genome.json`:

| line | stimulus | what it was |
|---|---|---|
| :19 | `ball` | git pushes, counted as essence bubbles |
| :30 | `scary` | a failed session ("a clang in the hull") |
| :31 | `summon` | a session waiting on Peter |
| :24-29 | `treat`, `laser`, `brush`, `catnip`, `box` | the badge's generic toy and gift slots |

**Engine chemistry and drive wiring:**
- The `scry` action's `effects` (boredom −3, joy +1) and its `instinct`
  weights (bias, bored, content, night, sleepy, hyper) (`genome.json:45-47`).
- The gene names and values, which are the old engine's knobs: appetite 1.2,
  sociability 1.1, boldness 0.6, grumpiness 0.8, zoominess 0.7, curiosity 1.4,
  chronotype 2.0 (`genome.json:7-15`). Keep what they **mean**, not the values.
- Fixed meal times 08:00 and 19:00 (`genome.json:4`).
- The action slot list: sleep, loaf, groom, pester, beg, zoomies, watch, hunt,
  sniff, sulk (`genome.json:34-44`).
- `PET_DRIVE_LABELS "HSBLZG"` (`pet_pack.h:26`).
- `speech.templates` and `fallback_nouns` (`genome.json:54-57`).
- The `brags` and `stash` text, which count git stats (`genome.json:17-21`).

**Lexicon tuning.** `alife.json` word strengths and chemical tags
(`FW/pets/grungo/alife.json:12-21`) and its `secret_words` (`:7-10`) are
old-engine data. The **word list itself** is character: peter, flies, bubbles,
bog, wardens, rootcult, mist, hood.

**LLM voice plumbing.**
- `voice.md` is a `claude -p` prompt filled by `host/badged.py`
  (`FW/pets/grungo/CLAUDE.md:21-23`; the template slots and `State:` are at
  `voice.md:1, 30-35`).
- `pack.json` `max_chars` and `status_tags` (`FW/pets/grungo/pack.json:4-14`).

Blorbarium has no host, so lines must be canned or generated on the device. The
seven sample lines in SOUL.md are the seed corpus.

**Firmware presentation wiring:**
- The `Mood` → expression switch (`FW/src/main.cpp:778-800`). For example,
  MOOD_SLEEP shows `sleepy` and MOOD_ALERT shows `alarmed`, and a 23-second
  timer randomly shows `derp`.
- The "a new line out of the host: the mouth goes" `meow` flap
  (`main.cpp:884-889`).
- The cyan and magenta glitch on every expression change
  (`main.cpp:872-897`, `PET_GLITCH_MS = 320` at `:718`). That is the badge's
  cyber look, not Grungo's.
- The rarity halo (`main.cpp:866-869`).

**Pack and build specifics:**
- `PET_PACK_ALWAYS` and the fake MAC pin row (`pet_pack.h:28-37`).
- The cyberpunk boot splash `"> scry --bog"` (`pet_pack.h:21-23`). The words
  are cute; the terminal framing is the badge's.
- The 64px size, a 16x16 footprint constraint (`main.cpp:855-861`).
- The Puck colour snap in the converter (`CP/tools/sprite2header.py:57-58`).

### Keep: the character

**Character text**
- **`SOUL.md` whole**: look, temperament, voice rules, world, cares, and the
  Never list.
- **Temperament as traits to re-express** in the new genome
  (`SOUL.md:101`, "skittish (low boldness), curious, a night frog, slow to
  zoom"): low boldness, high curiosity, nocturnal, calm.
- **Habits as behaviours**: scrying, squatting still on a pipe, gleaning or
  hoarding scrap, startle-then-claim-he-foresaw-it, hopping in circles when
  happy, fly-eating (meal word "flies", `genome.json:5`).

**Art**
- **All the art.** The hero and anchor, the ten registered frames, the masks,
  the forge recipe (`character.json`, `expressions.json`), and the PS walk
  atlas.
- **The registration property.** One body plus face patches is what makes
  cheap storage and genetic recolouring possible (§5).

---

## 5. Implications for the presentation seam

**What the art can do.** The art is a **fixed set of discrete face states on a
fixed front-facing body**, plus a separate walk cycle in five directions. It
cannot be morphed continuously (no rig, no pupils that move). The engine should
therefore emit symbolic state, and a renderer should own timing, transitions
and procedural motion.

### What the engine should output each tick

Suggested shape only, not code:

- **`body`**: the activity, as one enum.
  - Members: `idle/loaf`, `walk(dir8)`, `hop`, `eat`, `sleep`, `scry`,
    `startle`, `egg`, `hatch`, `end-of-life`.
  - Extra fields: position in the dish (x, y), facing, and hop height z.
  - The renderer maps `walk(dir8)` onto the PS atlas (mirroring the west
    directions) and the rest onto front-view poses.
- **`face`**: an expression id from the fixed set, plus an **intensity of 0..1**.
  - Intensity cannot blend the art itself. The renderer can spend it on:
    - **crossfading** the face patch from the previous expression (cheap, see
      the budget below);
    - **hold time** and the bob amplitude, as the old code did
      (`main.cpp:850`);
    - a threshold: a weak "annoyed" stays neutral.
  - Crossfading replaces the badge's glitch transition.
- **`reflex`**: a transient override with a timeout, for example a knock gives
  `alarmed` plus a hood or flinch pose for about a second. The old firmware
  already kept this idea separate (`petReflex`, `main.cpp:780-783`).
- **`speak`**: an optional canned line id. The renderer flaps `meow` with it.
- **Renderer-owned idle life.** Without these, a single-frame sprite looks dead:
  - blink, by flicking to `sleep` or `sleepy` for about 120 ms; the old badge
    blinked every 4 s for 0.12 s (`main.cpp:851`);
  - breathing bob;
  - occasional yawns.

### Phenotype, computed once per life from the genome and cached

A genome-driven **palette LUT**. The drawing is already palette-indexed, so a
recolour costs zero extra flash and one 256-entry LUT rebuild per life. This
needs one new converter output: each palette entry tagged with a **region**.

| region | how to label it |
|---|---|
| skin | by hue on the anchor (green) |
| belly | by hue on the anchor (pale) |
| cloak | by hue on the anchor (brown) |
| eye | from `masks/eyes.png` |
| mouth | from `masks/mouth.png` |
| outline | by hue on the anchor (dark) |
| glow | the foresee frame's teal |

Genes then shift hue, saturation and value per region within bounded ranges.

### Genetic variation that keeps him recognisably Grungo

**Invariant across generations.** These define him:
- the silhouette: squat and wide, a big head, eye-stalks through the hood;
- **the cloak and hood are always present** (`SOUL.md:93`);
- big eyes on top of the head;
- teal glow when scrying;
- the outline weight and the cel style.

**Safe to vary.** Each should stay in a band around the original:

| trait | how to vary it |
|---|---|
| skin hue and saturation | olive to bluer or yellower, within frog greens |
| belly lightness | — |
| mottling | spot density as a seeded procedural overlay on skin pixels only |
| cloak tint and value | browns, rust, moss; it stays a cloak colour |
| scarf | can split from the cloak if the region map separates them |
| eye colour at rest | red-brown, amber or gold |
| **foresee glow hue and strength** | the seer trait, made heritable: a "seeing" gene that drives both how often he scries and how bright the glow is |
| body size | uniform scale of about ±15% (needs fractional nearest-neighbour scaling; whole-number scaling only gives 1x or 2x) |
| stockiness | x-scale of about 0.9-1.15 |
| antenna length | a stretch of the stalk pixels above the hood; needs a stalk mask, none exists yet |

**Temperament genes** map onto presentation as well:
- **Boldness**: how often and how long the `alarmed` reflex fires.
- **Calm or zoominess**: hop frequency and walk speed. The PS frame durations
  can be scaled.
- **Chronotype**: when `sleep` happens.

**Generational cues** without new art: per-life cloak dye or a tint shift.
"Egg" and "hatchling" need new art. A hatchling could be the same sprite scaled
down with a head-to-body ratio squash. That is cheaper than new frames, but it
will look like a small adult, not a baby.

### Storage budget at 240x240, 8bpp indexed

- **Whole frames:** the full body at about 190 px tall is about 128x190, or
  **24 KB per frame**. Ten expressions as whole frames come to about 243 KB.
- **Body plus face patches:** the changed region scales to about **80x54**,
  or about 4.3 KB per expression. One body plus ten patches is about 68 KB,
  roughly 3.5x smaller.
- **Walk atlas:** 40 frames at 8bpp is about 40 x 9 KB = 360 KB at native size.
  Run-length encoding the rows, as the old draw loop already walks them
  (`main.cpp:747-752`), shrinks all of this a lot.

---

## 6. Blockers and risks for the 240x240 round screen

None of these is a hard technical blocker. They are decisions and pipeline work.

1. **A new converter is needed.** `CP/tools/sprite2header.py` is Puck-shaped:
   it snaps to Puck's colours, defaults to 64px, emits whole frames, and has no
   region tags.
   - To fix: drop the snap, export at the screen size, emit body plus face
     patches with an offset, and add the region-tagged palette.
   - It is plain Python with Pillow and numpy, so it runs on this Windows box.
2. **Fit inside the circle.**
   - The body bbox is 0.68 wide per unit of height (569x840).
   - A rectangle that size fits a 240px circle only up to about 198 px tall.
   - In my mockup at 200 px, the feet and antenna tips touch the rim
     (`scratchpad/round_200.png`). Plan on 170-190 px for a close-up "portrait".
   - In a "petri dish" scene where he walks around, he is about 60-100 px
     tall, which matches the PS walk atlas at about 104 px. At that size the
     face patch is only about 40x28 px, and the expressions read mostly through
     colour.
   - **Decide on two zoom levels.** In dish view, he walks and expressions are
     subtle. In portrait view, faces are legible.
3. **Two art styles at two scales.** The expression set is a front-only
   1024px illustration. The walk cycle is 96-104 px pixel art in five
   directions.
   - There are **no expressions for the side or back views**, so when he walks
     sideways his face cannot change.
   - Downscaling the illustration to about 100 px will not exactly match the
     hand-picked pixel-art walk frames.
   - Pick one of: front view only, with procedural motion; or re-cut the
     expression set onto the pixel-art south-facing walk frame.
4. **Single frames everywhere.** Neither set has idle or emotion animation.
   Motion must come from the renderer (bob, blink by swapping frames, squash,
   hop, crossfade) unless more `frame-NN` art is generated. sprite-expressions
   can do that with chained inpaints, but not with video models
   (`SE/CLAUDE.md:7-8`).
5. **Missing art the run structure needs.** There is no egg, hatchling,
   life-end, hood-down or hood-pulled-tight pose, and `derp` is still empty.
   Life-end art collides with SOUL.md's "never death" (§1). This needs a
   product call first.
6. **Eye colour drift between frames** (§3). It breaks a clean "eye" palette
   region for genetic recolouring. Fix it by re-picking or by repainting the
   irises before quantising.
7. **The 412x412 board.** The HANDOFF says the 1.46 board scales the 240 canvas
   up by 1.72x on output. Indexed art then gets uneven pixel widths. Exporting a
   second set at native 412 is easy from the 1024px source, because nothing is
   lost upstream.
8. **No offline voice.** The old pack spoke through `claude -p` on a host
   (`FW/pets/grungo/CLAUDE.md:21-23, 49-51`). On the device, lines must be a
   canned corpus written from SOUL.md and filtered by its Never list.
9. **Not blockers:**
   - Flash and RAM: a 240x240 RGB565 canvas is about 115 KB, and the old
     firmware already does it.
   - Palette size: 126 colours kept the teal and pink.
   - Licensing: the hero is the user's own sporefall keyframe.
