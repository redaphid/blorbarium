# The art contract

What the firmware on `flash3` draws for a creature, read from the code rather than
guessed. Every roster creature ships exactly what Grungo ships.

## Sources read

- `lib/blorb/include/blorb/defs/expressions.def`: the faces.
- `lib/blorb/include/blorb/defs/poses.def`: the poses.
- `lib/blorb/include/blorb/defs/regions.def`: the recolour regions.
- `lib/paint/include/paint/sprite_pack.h`: the `SpritePack` interface.
- `lib/paint/src/draw.cpp`: how stages, faces, the halo and recolour are drawn.
- `tools/sprite_pack.py`: the converter from picked art to `pets/<pet>/<pet>_pack.h`.
- `pets/grungo/art/`: Grungo's picked sources, the reference set.
- sprite-expressions, branch `blorbarium-art` (`forge.py`, `eyes.py`, `cutout.py`,
  `characters/grungo/`): how those sources were made.
- `origin/art-flow`: `art/comfy/` (a hand-adjust workflow for one frame) and a
  converter variant with `CELL = 7` (120 px tall body). `flash3` keeps `CELL = 5`
  (168 px). The roster is packed at `flash3`'s scale.

## What a creature is on the device

| Thing | Count | Source art | Notes |
|---|---|---|---|
| Body | 1 | `frames/neutral.png` | The same body for every pose and every stage. `pack.body()` ignores both. |
| Faces | 10 | `frames/<face>.png` | One per `BLORB_EXPR`, cut as a patch where any face differs from neutral. |
| Egg | 3 | `frames/egg.png` | Whole, cracking, hatching. The converter inks the cracks; the art is one egg with the baby visible inside. |
| Remains | 1 | derived from `asleep` | The converter settles the sleeping frame into a pool. No art needed. |
| Items | 3 | drawn by the converter | Pellet, rotten pellet, marble. Shared by every creature. |

### Faces (`expressions.def`)

| id | engine name | art file | what it is |
|---|---|---|---|
| 0 | neutral | `neutral.png` | the anchor itself |
| 1 | happy | `happy.png` | |
| 2 | alarmed | `alarmed.png` | also forced during the shake hop |
| 3 | annoyed | `annoyed.png` | |
| 4 | croak | `meow.png` | the creature's call (Grungo croaks) |
| 5 | blep | `blep.png` | tongue out |
| 6 | foresee | `foresee.png` | **the power face.** Worn whenever the Foresee action runs. Eyes glow; the glow pixels are the `glow` region, and `draw` rings each eye anchor with a pulsing halo tinted by that region. |
| 7 | sleepy | `sleepy.png` | half-lidded |
| 8 | asleep | `sleep.png` | eyes shut; also the source of Remains |
| 9 | yawn | `yawn.png` | made from `sleep`, mouth only |

A missing face falls back to neutral. A missing pose falls back to idle, and no pack
has pose art: motion (bob, hop, squash) is the renderer's.

### Stages

`Stage { Baby, Child, Adult, Elder }` in `ids.h`. The renderer makes them from the one
body: Baby and Child draw it 116 percent wide and 90 percent tall
(`kHatchlingWide`, `kHatchlingTall`), Elder draws it at 96/128 saturation
(`kElderSat`). Grungo ships no stage art, and his pack returns the adult body for
every stage. The egg is the only stage with its own art. Stage art (a hatchling, an
elder) is optional; `sprite-expressions` has unpicked Grungo candidates for both.

### Frames

- 1024 x 1024 RGBA. Every face frame has the **identical alpha** of the anchor:
  `sprite_pack.py` asserts it ("not registered on the anchor's alpha").
- The body fills a bbox 840 px tall with its top at y = 80, centred horizontally
  (`forge.py anchor`, `hero.height = 840`, `hero.top = 80`). Grungo's bbox is
  (227, 80, 796, 920). At `CELL = 5` that is 168 device pixels tall.
- Faces are inpainted on the anchor inside a mask and pasted back, so every pixel
  outside the mask is identical to neutral. That is what makes the face a small patch.

### Masks and anchors

- `masks/eyes.png`, `masks/mouth.png`: the inpaint masks, white = the area a face
  may change. The converter uses the eye mask to label the eye region and the mouth
  mask's box to find the mouth.
- `eyes.json`: per frame, the measured eye centres and radii (`eyes.py`), plus
  `bbox`. `sprite_pack.py` turns them into each frame's `EyeAnchor`s, and asserts
  every anchor lands on an `eye` or `glow` pixel. The foresee halo is drawn around
  them. The egg entry gives the baby's eyes inside the egg.

### Recolour regions (`regions.def`)

`skin, belly, cloak, eye, mouth, glow, shell`; the outline is invariant. Each
palette entry is tagged with one region; palette genes tint a region within a
`RegionBand` so the creature stays recognisable. One 256-entry palette per pack.

For roster creatures the region names are slots, not anatomy:

| region | meaning for a roster creature |
|---|---|
| skin | the main body colour, the dominant colour |
| belly | the pale underside or face |
| cloak | the secondary feature: a shell, a cap, wings, gills, a mask |
| eye | eyes |
| mouth | mouth and tongue |
| glow | the power's glow (the foresee face's eyes) |
| shell | the egg's jelly |

## Per creature, the deliverable

In blorbarium, `pets/<slug>/art/`:

```
frames/neutral.png happy.png alarmed.png annoyed.png meow.png blep.png
       foresee.png sleepy.png sleep.png yawn.png egg.png
masks/eyes.png mouth.png
eyes.json
README.md        which sprite-expressions commit each file came from
```

and the packed `pets/<slug>/<slug>_pack.h`. In sprite-expressions, branch `roster`,
`characters/<slug>/`: `hero.png`, `character.json`, `expressions.json`, `anchor.png`,
`masks/`, `raw/<expr>/frame-00.png`, `eyes.json`.

Flash cost: Grungo's pack is 107 KB (256 palette entries, 18 frames). All nine packs
together are 838 KB (`art` in roster.json lists each), inside a 4 MB app slot.

## How a roster pet differs from Grungo when packed

`tools/sprite_pack.py --pet <slug>` packs a pet other than Grungo; Grungo's own run is
unchanged and his header byte-identical. The differences:

- `eyes.json` holds only the egg's eyes (`egg_eyes.py` in sprite-expressions). The
  converter measures every face's eye anchors itself, from the eye pixels its
  labelling finds, so no per-frame iris detector is needed.
- `pack.json` (drafted by `tools/pack_refs.py`, reviewable in its `pack-refs.png`)
  gives the body bbox, the eyeballs, three body prototypes (`skin`, `cloak`, `belly`)
  and reference colours per region. Each patch of the body walled by linework takes
  the prototype nearest its mean colour; eyes, mouth and glow keep per-pixel labels.
  Where the art has no outline between two parts, `masks/regions.png` draws one: it is
  drafted by `tools/region_map.py` (nearest prototype per pixel, a majority vote, then
  the `region_paint` shapes in pack.json) and overrules the converter on the body.
  Puffkin (rim and face), Mudge (body, belly, and his fins and head tuft as cloak: a fish
  has no cloak), Mothwick (wings and body), Fizz (wings and body) and Gilly (body, cream
  belly, and her gill crown as cloak) have one. Mossby's belly is his pale foot,
  Fizz's belly is her head (a beetle has no belly) and Puffkin's cloak is his feet (he
  has no second part). Hushwing's egg is a white owl in a white egg, so pack.json's
  `egg_baby` ellipse says where the chick is and the rest is shell.
  `tools/region_counts.py` prints each pack's pixels per region over every frame. Inside the eye mask, a colour far from every body
  prototype is always eye, and nothing outside the mask is, so a blush or the fur round
  Tinker's eyes stays body. Mouth references are what the open-mouthed faces repaint.
- Remains are derived from the asleep face exactly as Grungo's are, with the pool
  scaled to the pet's body. That is the contract's only death art.

Nothing in `src/` loads a roster pack yet: `main.cpp` draws `grungoPack()`. Choosing a
pack per device is engine work, left for whoever wires battles and species.

## Battle art (BATTLES.md ART1 to ART3)

`python3 tools/sprite_pack.py --duel` draws `tools/duel_art.py` and writes
`pets/duel/duel_art.h`: one palette, 46 frames, about 19 KB, the same bytes every run.
Everything is drawn in code at device scale, not generated. Indexes follow the battle
sketch's enums:

| array | index | frames |
|---|---|---|
| `kScars[5][3]` | `Scar::Kind` (claw, scorch, notch, ripple, ring) x small, medium, large (12, 18, 26 px) | ART1 |
| `kTrophies[9]` | `SpeciesId`: hood, shell, spore cap, gill frond, wing scale, fin, lamp, feather, gear, each on a token disc so it never reads as a power | ART2, 24 px |
| `kStances[3]` | `Stance`: paw, leaf shield, swirl | ART3, 24 px |
| `kPowers[9]` | `PowerGlyph`: eye, shell dome, spore cloud, leaf with a plus, lantern, water lines, bolt, an owl with her eyes shut (Hush; the lone feather is Spar's), cog with a question mark | ART3, 24 px |
| `kTiers[3]` | spar (feather), scrap (plaster), mortal (full skull) | ART3, 24 px |
| `kElements[3]` | `Element`: splash (a drop), sprout (a seedling), spark (a sparkle), for showing who beats whom; optional | ART3, 24 px |
| `kSkullEmpty`, `kDebt`, `kVendetta`, `kWounded` | the skull to fill, a heart tied with string, a glaring eye under a heavy brow, a small plaster | ART3 |

Every frame's origin is its centre. A scar has no colour: its palette entries are tagged
skin and hold a grey g, meaning "multiply the body pixel underneath by g / 128" (a dark
crease inside a lighter ridge), so it is the pet's own skin whatever its genes did. Fade
moves g toward 128. A scar placed by the hit's luck can land on clear pixels or a pale
wing (Fizz); snapping it to the nearest skin-region pixel keeps it visible. The mortal
tier glyph and `kSkullEmpty` share every pixel's place: fill by drawing the full skull
over the empty one from the bottom row up. The owner's sheets are `roster/battle-art/`:
every scar on all nine bodies, the trophies on a rim at 1:1, the glyphs, each in colour
and in grey.
