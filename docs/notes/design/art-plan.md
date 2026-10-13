# Grungo art plan for blorbarium

2026-10-02. This plan covers what art blorbarium needs and how `sprite-expressions` makes each
piece. Grungo's egg was generated through the pipeline as a proof, and the pipeline worked
end to end.

| shorthand | path |
|---|---|
| `SE` | branch `blorbarium-art` of `D:\Projects\sprite-expressions`, worktree `C:\Users\hypnodroid\Worktrees\sprite-expressions-blorbarium-art`, pushed to `origin` (`redaphid/sprite-expressions`) |
| `ART` | `scratchpad\design\art\` next to this file |

## 1. How the pipeline keeps Grungo on-model

Every expression frame starts from one **anchor**. The anchor is the approved hero at a
fixed place on a 1024 canvas, body bbox `(227,80)-(796,920)`. `flows/inpaint.json`
re-noises only a mask region (eyes, mouth or face). It pastes the decode back over the
untouched anchor, so every pixel outside the mask stays identical. Each frame also takes
the anchor's alpha. IP-Adapter Plus (weight 0.5, `ease in-out`, ending at 0.6) on the
anchor holds identity, on SDXL Juggernaut XI, 28 steps, cfg 7. A prompt alone does not
commit to closed eyes or glowing teal, so **paint ops** draw rough lids or teal discs first
and the flow heals them at low denoise (0.4 to 0.45). A person picks each frame into
`raw/<expr>/frame-00.png`, and `work/` holds the candidates. The rules in `SE/CLAUDE.md`
are image models only, a person picks, and push after every commit.

The result is that all ten frames differ only inside the face box `(337,142)-(694,385)`.
That registration is what makes cheap storage and per-region recolouring possible.

## 2. What changed in the pipeline (branch `blorbarium-art`)

Four commits, pushed.

- **Blank frames** (`forge.py`). `"blank": true` on an expression starts from an empty
  canvas instead of the anchor and keys the frame's own alpha. The keyed alpha is then
  eroded 1 px so no white ring shows on a dark screen. This is what lets the same inpaint
  flow draw something that is not the adult body (the egg, and later the tadpole or a
  death pose). The flow itself is unchanged.
- **`shapes` paint op.** This blocks in a new subject with flat ellipses, chords, polygons
  and lines, stored as data in `character.json`.
- **IP-Adapter `style transfer`** per expression (`vars.ipa_weight_type`) with
  `vars.subject` set to `""`. The reference lends Grungo's linework, cel shading and
  palette without pulling his frog body into the frame. Round one confirmed this.
- **`roundview.py`.** It shows any sprite at 64, 96 and 120 px on a 240 px round dish.
  Every new frame should be judged here, not at 1024.
- **`eyes.py` and `characters/grungo/eyes.json`.** These hold the per-frame eye centres
  (section 5).
- **`regions.py`.** A prototype colour-region labeller (section 4).

## 3. The egg (proof generation)

### Final (picked by the user)

The user picked `egg-nest-s1007` and asked for the ground stripped. The final egg is
`ART/egg-final.png`, committed as `raw/egg/frame-00.png` in commit `2cd035a`, with the pick
itself as `raw/egg_nest/frame-00.png`. `cutout.py` fitted a conic to the egg's ink ring
(median error 0.28 px) and cut along it with a soft edge. It painted the outer 3 px in the
ring's ink `(40,17,15)`, so no halo shows on any background. Its eyes are at (469,633) and
(551,632), r13 and r12, in `eyes.json`. `ART/egg-final-round-preview.png` shows it at 64,
96 and 120 px. The candidates below are kept for the record.

### Picks

| file in `ART` | concept | why |
|---|---|---|
| `egg-hood-s1007.png` | jelly egg under a little brown cloth hood with two eye-stalk knobs; inside, a froglet that wears its own tiny hood, eyes glowing teal | **Recommended.** Reads best at 64 px: brown hood, olive egg, teal eyes. The hooded froglet makes it a baby Grungo, not a generic egg. |
| `egg-hood-s1002.png` | same concept, a plainer froglet with bigger teal eyes | Highest-contrast eyes. The safest pick for the 64 px dish view. |
| `egg-nest-s1007.png` | jelly egg resting in a crumpled pile of his brown cloak; the froglet peeks from a hood-shaped hollow | The story pick: the egg left on the cloak of the Grungo who died. Wider, so it reads best at 96 px and up. |

`ART/egg-round-preview.png` shows the three on the round screen at 64, 96 and 120 px. All
three read at 64 px, and the teal eyes are the first thing you see at every size.

### Recipe

The recipe lives in `characters/grungo/expressions.json` (`egg_hood`, `egg_nest`) and
`character.json` (`masks.egg`, `paint.egg_hood`, `paint.egg_nest`). It is blank, denoise
0.6, IP-Adapter `style transfer` at weight 0.7 ending at 0.8, seeds as named. Reproduce
with `python forge.py gen grungo egg_hood --seeds 1007` from the worktree.
`egg-hood-s1002` came from round two, whose paint differs only by sub-pixel rounding from
the committed recipe.

### How it got there

1. **Round one.** The jelly came out lime, not his olive `#749b50`. The small teal glints
   were healed away, and the hood read as an acorn cap.
2. **Round two.** The jelly was painted in his skin olive, and the hood got folds and a
   scalloped hem. The fix that mattered was a **froglet embryo with two teal eyes**. The
   teal is large enough to survive denoise, and it ties the egg to the foresee glow.
3. **Round three.** Four more seeds. Two seeds spontaneously gave the froglet its own hood.

None of the eggs is committed to `raw/`, because a person picks frames. To adopt one,
run `python forge.py pick grungo egg_hood <n>` after a `gen` with that seed.

### On device

The egg is about 668 px tall on the 1024 canvas, where the adult is 840. In the dish, draw
it at about 64 to 80 px against Grungo's 96 to 104. Recolour maps jelly to the skin genes,
the hood or nest to the cloak genes, and the froglet's eyes to the glow genes. The
froglet's eye centres are recorded (section 5), so the procedural foresee halo can pulse
on the unhatched egg too. That is a cheap and legible "this egg has the seeing" cue for a
heritable seer gene.

## 4. Colour-region masks for every frame

Genes recolour skin, cloak, eyes, glow and spots on device. The plan is a palette LUT.
Each frame is palette-indexed, and every palette entry is tagged with a region, so a new
life rebuilds one LUT and no pixel data. That needs one label image per frame.

### What the prototype showed (`regions.py`, `ART/regions-prototype.png`)

- **Clean on the adult frames by hue alone.** Ink, skin, cloak, eye, glow (teal) and
  mouth all separate. The eye region is seeded from `eyes.json`. Glow must be tested
  before eye, or the foresee teal is labelled eye. The open mouth needs a "saturated red
  below y 255, value above 0.25" rule. Unclassified pixels, mostly the dark hood interior,
  take their nearest labelled neighbour's region.
- **Spots do not segment.** "Lighter than its neighbourhood" catches the real head spots
  on alarmed and happy, and also every specular highlight on the muscles.
- **New subjects need their own rules.** On the egg, the lit top of the hood is labelled
  mouth and the pale jelly falls through to fill.

### How the pipeline should make them

1. **Label the anchor once, and have a person correct it once.** Run `regions.py` on the
   anchor, fix the errors in an image editor, and commit `regions/anchor.png` as an 8-bit
   label image. Every registered frame shares the anchor's pixels outside the face box,
   so the body labels never need redoing.
2. **Per frame, re-label only the face box** `(337,142)-(694,385)` (wider for alarmed's
   eyes) with `regions.py`, and composite it into a copy of the anchor labels. Review the
   result on a contact sheet beside the frame. Save it as `raw/<expr>/regions-00.png`
   next to `frame-00.png`.
3. **Spots** should be procedural. Use seeded noise masked to the skin region and tinted
   by the spot genes. Density, size and colour then become genes for free. The drawn
   highlights stay part of skin shading. If drawn spots must be recolourable, a person
   paints `masks/spots.png` once on the anchor, which works because the body is
   registered.
4. **Blank frames** (egg, tadpole, death pose) have no anchor to share, so each needs its
   own rules or segmentation. `SAM3_Detect`, which takes a text prompt, is installed on
   the 8188 ComfyUI. No `sam3` weights appear in its loaders' choices, so it needs a
   checkpoint download first. Until then, label blank frames with per-subject hue rules
   and a person's correction pass. That is cheap, because there are only a few such
   frames.
5. **Fix eye-colour drift before labelling.** The face-mask frames (happy, alarmed,
   annoyed, meow) went to bright red irises, where neutral has dark red-brown. Shift each
   frame's eye-region hue and value to neutral's iris palette with a small script
   (`regions/eye` pixels only). Then the LUT's eye entries recolour every frame alike.
6. **The converter** (the survey's rewrite of `cyber-puck/tools/sprite2header.py`)
   quantises each region separately into a shared palette. It emits a `region` tag per
   palette entry and drops the Puck colour snap. It reserves the glow entries for the
   foresee teal.

## 5. Eye centres per frame (for the procedural foresee glow)

Measured by `eyes.py`, which finds the iris by colour (red-brown or teal). Coordinates are
in anchor pixels on the 1024 canvas, and `(u, v)` is the fraction of the frame's alpha
bbox, which survives any rescale. `r` is the 90th-percentile iris radius. The full data,
with boxes, is in `SE/characters/grungo/eyes.json`, and with the eggs in `ART/eyes.json`.
`ART/eyes-overlay.png` draws every measurement on its frame. All were checked by eye.

| frame | bbox | left eye (x, y) r | right eye (x, y) r | left (u, v) | right (u, v) | note |
|---|---|---|---|---|---|---|
| `neutral` | (227, 80, 796, 920) | (408, 217) r33 | (611, 215) r31 | (0.317, 0.163) | (0.675, 0.161) | |
| `blep` | same | (408, 217) r33 | (611, 215) r31 | (0.317, 0.163) | (0.675, 0.161) | mouth-only edit |
| `foresee` | same | (407, 206) r29 | (612, 206) r29 | (0.316, 0.151) | (0.676, 0.150) | teal disc |
| `sleepy` | same | (412, 231) r28 | (612, 234) r25 | (0.325, 0.180) | (0.677, 0.183) | visible lower half of iris |
| `sleep` | same | (408, 217) r33 | (611, 215) r31 | (0.317, 0.163) | (0.675, 0.161) | lidded, uses neutral |
| `yawn` | same | (408, 217) r33 | (611, 215) r31 | (0.317, 0.163) | (0.675, 0.161) | lidded, uses neutral |
| `happy` | same | (404, 234) r40 | (620, 237) r38 | (0.310, 0.184) | (0.691, 0.187) | eyes moved |
| `meow` | same | (404, 236) r35 | (617, 234) r34 | (0.311, 0.186) | (0.685, 0.184) | eyes moved |
| `annoyed` | same | (412, 254) r30 | (608, 258) r31 | (0.325, 0.207) | (0.670, 0.212) | narrowed, moved down |
| `alarmed` | same | (399, 269) r34 | (626, 262) r34 | (0.303, 0.225) | (0.702, 0.217) | sclera reaches about r60 |
| `egg-hood-s1007` | (288, 234, 737, 901) | (471, 637) r21 | (552, 636) r16 | (0.407, 0.604) | (0.589, 0.602) | froglet |
| `egg-hood-s1002` | (291, 234, 739, 900) | (473, 634) r20 | (554, 632) r22 | (0.406, 0.601) | (0.586, 0.598) | froglet |
| `egg-nest-s1007` | (172, 361, 854, 979) | (469, 633) r13 | (551, 632) r12 | (0.436, 0.440) | (0.556, 0.439) | froglet |

Notes for the renderer:

- **Centre the halo on the eyeball, not the iris.** On the frames whose eyes did not move,
  the iris sits about 10 px low in the eyeball. The eyeball centre is the eye mask's
  `(408,207)` and `(612,207)`, which is also where the foresee teal discs are. Use those
  for neutral, blep, foresee, sleep, sleepy and yawn. Use the measured centres for happy,
  meow, annoyed and alarmed, whose eyes moved.
- **Scale.** Device pixels equal anchor pixels times `display_height / 840`. In a 180 px
  portrait that factor is 0.214, so the eyes are about 44 px apart with a radius of
  about 7 px. In a 96 px dish view it is 0.114, so they are about 23 px apart with a
  radius of about 4 px.
- **Halo size.** Make the halo reach about 2.2 times the eyeball radius (eyeball radius
  about 47 anchor px, the mask ellipse) so it visibly spills over the brow, cheeks and
  hood edge. Draw it additively over the sprite, so it reads on olive skin and brown
  cloak alike. Tint it with the glow genes, and pulse it with sparks if they read at
  96 px. Recolour the eye region's teal entries with the same gene, so the eye and its
  halo match.
- When a new frame is picked, rerun `python eyes.py grungo`. It is cheap and keeps the
  table in sync.

### Is a stronger-glow foresee frame worth generating?

**Yes, as a reference image. Do not ship it as a sprite.** Today's foresee frame has flat
teal discs with a pale rim and no light around them, so it shows the eyes but not the
power. A baked glow would spill teal onto pixels labelled skin and cloak. The genes would
then recolour that spill as skin, fighting the gene-tinted halo. It would also lock the
glow's hue. As a reference it is worth the roughly 4 SDXL images it costs. It gives the
procedural halo a target to tune against (falloff, rim brightness, how far the light
wraps the brow and hood). The recipe is a `glow` paint op (radial teal gradient around
each eye, out to about 2.2 times the eyeball radius), the `eyes` mask grown to about 1.8
times, and a heal at denoise 0.35 to 0.45, `"from": "foresee"`.

## 6. What else blorbarium needs, and how to make each

The structure follows from section 1. A change **inside** the adult silhouette is a
registered inpaint on the anchor. That is cheap, keeps the region labels, and reuses the
body. A change of **silhouette or pose** is a blank frame. A **life stage** with its own
expressions becomes its own character folder, whose hero is a picked blank frame. Then
all of forge's expression machinery (masks, paint, chains) applies to it unchanged.

| asset | how | frames | notes |
|---|---|---|---|
| **Egg** | done (section 3) | 1, plus an optional wobble from the renderer | Recolours by region. The foresee halo can play on it. |
| **Hatch** | renderer: squash and wobble, crack sparks, then a pop to the hatchling | 0 new | A cracked-egg frame is one more blank frame if wanted: the same egg with a split jelly top, `"from"` the picked egg. |
| **Tadpole / hatchling** | blank frame, a `shapes` blocking of a big-headed olive tadpole with a tail, a scrap of brown hood, eye-stalk nubs and dark red-brown eyes. IP-Adapter `style transfer` 0.7. Then `characters/grungo-tadpole/` with that pick as its hero, for neutral, happy, sleep and foresee by the usual masked inpaints. | 1 blank + about 4 expressions | Every egg seed grew a sitting froglet, not a tadpole. If the user wants egg to froglet to adult, skip the tadpole and make a **froglet** stage: a blank frame painted like the eggs' embryo (big head, tiny hood, short legs). The eggs show the model already draws that well. |
| **Juvenile** | no new art. The adult drawn at 75 to 85 percent scale with a head-to-body squash, plus brighter saturation through the LUT | 0 | The survey's fractional nearest-neighbour scaling. |
| **Old age** | `characters/grungo-elder/`. Its hero is a registered inpaint on the anchor: a face mask (drooping lids, white brow tufts, a few wrinkles) and a new `cloak` mask (patched, frayed and faded cloak; do not change the silhouette). Then the expression set on the elder anchor. Also renderer cues: desaturate skin through the LUT, slower bob and blink, a slight hunch (y-squash). | 1 anchor + the expressions that matter (neutral, sleepy, sleep, foresee, happy) | Registered, so the elder shares the body region labels. A cane or a stoop changes the silhouette, which needs keyed alpha on a non-blank frame. That is one small forge option (`"alpha": "key"`), not built yet. |
| **Death** | the user chose plain death (`user-asks.md` 3). Two frames. (a) **Hood pulled down over the face**: registered inpaint, `face` mask grown up to the hood, the hood drawn down over the eyes, mouth soft. (b) **Lying still**: a blank frame, `shapes` blocking of the adult slumped on his side with the cloak draped over, IP-Adapter in its normal identity mode at about 0.5 (not style transfer) and denoise about 0.7. The renderer then desaturates, sinks him into the bog, and fades to the egg on the cloak (`egg_nest` fits this beat). | 2 | Frame (a) doubles as **"hood up" for the startle** (SOUL.md "eep, and the hood goes up"), so it earns its place twice. |
| **derp** | registered, `eyes` mask, with a new paint op drawing **cross-eyed pupils** (white eyeballs, dark pupils pushed toward the nose) and a heal at denoise 0.45 to 0.55, like the lids and the foresee teal. Optionally chain `blep` from it (`"from": "derp"`) for the tongue. | 1 | It failed before because a prompt alone at 0.92 never went cross-eyed. Paint then heal is the pipeline's proven fix for states the model will not commit to. |
| **Blink** | none. Flick to `sleep` for about 120 ms | 0 | Already registered and lidded. |
| **Idle** | renderer: breathing bob (1 to 2 px y-squash), blink, occasional yawn or sleepy | 0 | |
| **Startle hop (shake)** | renderer: `alarmed` face plus procedural squash, leap, stretch and land. Optional one frame: arms out, cloak flared, as a blank frame | 0 or 1 | `user-asks.md` 8. A flared cloak frame would sell it, but a squash already reads at 96 px. |
| **Walk** | reuse `puck-sprites/out/frog/atlas96` (5 directions x 8 frames, mirror the west). Label its regions with the same hue rules, which should hold on that palette but need checking at 96 px. | 0 new | It is pixel art and the expressions are illustration (survey section 6.3). If the mismatch shows, use front-view-only movement: the neutral frame sliding with a hop-bob. |
| **Hood down (sleep)** | registered inpaint on `sleep`. A mask over the hood that pushes the hood back and shows the top of the head and stalks | 1 | Optional character beat from the old genome. |
| **Stronger-glow foresee** | reference only (section 5) | 1 reference | |

Priority for a first playable life: eggs (done), froglet or tadpole, derp, the death pair,
the elder anchor, then region labels for everything shipped.

## 7. Notes for the next run on this box

- 8188 was up with the stock launch line (no `--preview-method`). Both ComfyUI queues
  were empty, but the GPU sat at 80 to 86 percent with 17 GB used by something outside
  ComfyUI, most likely Ollama's elevated `llama-server`. `qwen3:4b-instruct` and `bge-m3`
  were loaded. SDXL with IP-Adapter fit in the 14.9 GB free, so Ollama was left alone
  (the local-ops skill says to ask before stopping it). `wkf` on 8189 was up and idle,
  and was not touched.
- **The commit limit is now 135.9 GB, not the 160 GB in `3d-bear/docs/MACHINE.md`.** The
  memory guard ran at a 130 GB trip, and commit peaked at 104.7 GB. Anything heavier than
  SDXL (Qwen-Image, Z-Image) needs that lower ceiling in mind.
- Each egg round was 8 SDXL images in about 2 minutes, through `forge.py`'s own HTTP
  client (no `comfy run`, so no stray python processes).
