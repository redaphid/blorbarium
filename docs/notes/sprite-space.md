# Sprite space on the badge, and room for more Grungo

2026-10-03. Read-only investigation of `origin/engine` at `a1cb726` in a scratch clone
(`scratchpad/space-clone`). Nothing was edited or committed in any repo. Every number marked
**measured** comes from a command below. Numbers marked **guess** are labelled where they appear.

## Answer in brief

Space is not the limit on Grungo variety. Grungo's whole sprite pack is **54,168 B** of flash. A
unit-20-sized firmware leaves **2.5 MB** free in today's app slot and **3.35 MB** in the 4 MiB slot
the OTA plan proposes. `petfs` holds another 7.6 MiB. At today's encoding, one complete alternate
"breed" (body, ten faces, own palette) costs **36 KB**. A heritable part such as horns, a tail stub
or a moss tuft costs about **0.5 KB**. The 4 MiB slot holds about 63 breeds or about 4,800 parts,
even with 1 MiB kept for code growth. What actually limits variety is art production (a person picks
every frame), how well a part reads at 120 px, and the 256-colour palette, which is already full.

Recommended plan:
1. Wire up the procedural traits the genome already carries but never draws (zero bytes).
2. Add heritable parts through the `mark` seam that already exists.
3. Add a few rare breeds as dormant genes that wake by mutation.
4. Reserve a raw `art` partition in the first-flash table now, and move art there later.

## 1. Measurements

### Partition table in use

`origin/engine` sets `board_build.flash_size = 16MB`, but PlatformIO does not read that key. The
board manifest's 8 MB default applies, as the OTA survey found for CNS. **Measured**:

```
$ pio run -e badge128_size        # build log line 5
PLATFORM: Espressif 32 (7.1.0) > Espressif ESP32-S3-DevKitC-1-N8 (8 MB QD, No PSRAM)
$ grep partitions ~/.platformio/platforms/espressif32/boards/esp32-s3-devkitc-1.json
      "partitions": "default_8MB.csv",
$ python gen_esp32part.py .pio/build/badge128_size/partitions.bin
nvs,data,nvs,0x9000,20K,
otadata,data,ota,0xe000,8K,
app0,app,ota_0,0x10000,3264K,
app1,app,ota_1,0x340000,3264K,
spiffs,data,spiffs,0x670000,1536K,
coredump,data,coredump,0x7f0000,64K,
$ od -An -tx1 -N4 firmware.bin
 e9 05 02 3f                      # byte 3 high nibble 3 = the image declares 8 MB flash
```

The top 8 MB of the 16 MB chip is invisible to the firmware today. Unit 20 needs
`board_upload.flash_size = 16MB` and `board_build.partitions = partitions.csv`.

### Firmware size

The badge envs stop at `#error "... arrive in build unit 20"` (`src/main.cpp:19`). In the scratch
clone, I replaced that line with a stand-in 1.28 board. The stand-in is a GC9A01 on the Waveshare
pins (`scratch_board/board_128.h`) with `MemStorage`. Then I built env `badge128_size`.

**Second finding: the badge envs do not compile once the guard is gone.** The S3 Arduino core adds
`-std=gnu++11` to CXXFLAGS (`framework-arduinoespressif32/tools/platformio-build-esp32s3.py:50`).
That flag wins over the repo's `-std=gnu++17`, so `fixed.h:25` and `registry.h:182` fail with
"body of constexpr function not a return-statement". The fix is
`build_unflags = -std=gnu++11` in `[esp32]`. Unit 20 will hit this on its first build.

| build | Flash (image) | static RAM (DRAM data+bss) | `.flash.text` | `.flash.rodata` | `.iram0.text` |
|---|---|---|---|---|---|
| `badge128_size`: today's loop, stand-in board | **571,445 B** of 3,342,336 (17.1%) | **146,860 B** of 327,680 (44.8%) | 318,535 | 178,104 | 59,783 |
| `badge128_unit20`: plus NimBLE server and advertising, LittleFS, labelled `Preferences`, `esp_ota_*` | **841,933 B** (25.2%) | **157,804 B** (48.2%) | 524,047 | 218,576 | 79,055 |

The second row is the honest floor for unit 20. It links what unit 20 must add, with no protocol
code yet. NimBLE also takes heap at runtime (guess: tens of KB), and that is not in the static count.

**Where the bytes go**, from the measured `nm --size-sort` of the first build:

- The `grungo_pack` arrays take 54,762 B, and its code takes 307 B.
- The blorb engine takes about 99.6 KB of named code. Its archive is 145.7 KB before section GC.
- The paint archive is 21.1 KB before GC.
- LovyanGFX code takes 28.5 KB.
- The rest is the Arduino core, IDF, and newlib (`_vfprintf_r` alone is 12 KB).
- The largest RAM item is `canvas`, at **115,200 B** of bss. `dish` takes 10,880 B.

### The sprite pack

**Measured** by `scratchpad/pack_bytes.py`:

| frame | w×h px | RLE bytes (today) | raw 8 bpp | PackBits | zlib -9 |
|---|---|---|---|---|---|
| Body | 82×120 | 10,570 | 9,840 | 6,951 | 4,829 |
| 10 face patches | 54×38 each | 24,670 (avg 2,467) | 20,520 | 16,378 | 12,759 |
| 3 eggs | 53×72 | 10,684 | 11,448 | 8,188 | 4,626 |
| Remains | 96×82 | 6,668 | 7,872 | 4,369 | 3,176 |
| 3 dish items | ≤15×13 | 552 | 534 | 439 | 285 |
| palette | 256 × 4 B | 1,024 | | | |
| **total** | | **54,168** | | **37,349** | **26,699** |

**Format**:

- **Palette depth.** Frames use 8-bit indexed colour into one shared 256-entry palette, and every
  slot is used (`BUDGET` in `tools/sprite_pack.py` sums to 255 plus clear). Each entry is RGB888
  plus a region tag: skin, belly, cloak, eye, mouth, glow, shell, or 255 for the invariant
  outline. Genes recolour a region by rebuilding a 256-entry LUT once per draw. Recolouring
  costs no flash.
- **Compression.** Each frame is byte-pair RLE of `(count, index)` per row. On this painted art it
  is **worse than no compression**. The body costs 10,570 B, against 9,840 B for raw 8 bpp
  pixels, and the faces lose too, because the cel-shaded and dithered art has few long runs. A
  literal-run variant such as PackBits keeps the same streaming decoder shape and saves 31%
  overall (54.2 KB to 37.3 KB). zlib saves 51%, but it needs a RAM decode cache.
- **Face-patch scheme.** There is one front-facing 82×120 body for every pose and stage. Each of
  the ten expressions is a 54×38 rectangle cut from its full frame, at body x 14..67, y 7..44.
  This works because sprite-expressions inpaints every face onto one anchor, so the frames differ
  only inside that box. The renderer draws the body, then marks, then the face patch
  (crossfading from the previous face), then the procedural foresee halo.
- **Location.** All of it is `inline constexpr` arrays in `pets/grungo/grungo_pack.h`, compiled
  into the app image's `.flash.rodata`. No data partition is involved.
- **RAM to decode and draw.** Frames are read in place from memory-mapped flash. `RowReader`
  decodes one row at a time into a 320 B stack buffer (`draw.cpp:233`). The colour LUT
  (`Colours`) is 1,280 B on the stack. The 240×240 RGB565 canvas is 115,200 B of static RAM.
  Art adds **zero** RAM per frame or per breed, and that stays true for any art kept
  memory-mappable.

## 2. Budget

**Measured** in `scratchpad/budget.py` against the unit-20 floor of 841,933 B. The two reserves
are guesses.

| place | free for art |
|---|---|
| app slot today (`default_8MB`, 3,264 KiB) | 2,500,403 B |
| app slot in the OTA plan (4 MiB) | 3,352,371 B |
| the same, keeping 1 MiB for code growth (**guess** reserve) | 2,303,795 B |
| `petfs` (7.6 MiB), keeping 1 MiB for journal and backups (**guess** reserve) | 6,946,816 B |
| `pet` (256 KiB NVS) | none. It holds the save, and NVS is a poor fit for frame blobs |

OTA keeps two slots, which halves the usable app space. Art in the image must fit in **one** slot
alongside the code, because the other slot holds the next image. Every byte of art in the image is
also resent on every BLE update. At the 7 to 9 KB/s CNS measured (`explore-ota.md` §6), the
842 KB floor takes about 95 to 120 s over the air. The 54 KB pack is about 7 s of that.

## 3. Variety options

### (a) Procedural only: zero bytes

The genome's Look genes today (`gene_kinds.def`, `genes.h`, `starter_genome.cpp:233-243`):

- **`palette` (0x20)** `{region, hue, sat, val, chemBound, gain}` tints one of 7 regions within
  that region's band. The starter has skin, belly, cloak, eye and glow genes (glow is bound to the
  vision chemical), plus two dormant genes: a bluer cloak and a brighter seer glow.
- **`size` (0x21)** `{base, growth, squash}`. Only `base` is drawn (`appearance.cpp:28`), as the
  adult's uniform scale. **`growth` and `squash` are expressed into `Phenotype::Size` and never
  read.** The renderer scales x and y together for adults (`draw.cpp:574-575`).
- **`mark` (0x22)** `{layer, variant, tint, chemBound}`. Layer 0 is mottling, drawn as seeded
  spots on skin pixels, with density from `variant` and spot hue from `tint`. The starter also
  carries a **layer-1 "longer stalks" mark, gated on the elder feat**, but `grungo_pack`'s
  `mark()` returns an empty frame, so it draws nothing.
- **Bound to chemistry, not genes:** the `hue_shift`, `size` and `glow` act loci shift colour,
  scale and halo from body chemistry.

What is free to add, with no art:

- **Stockiness.** Read `squash` as an x/y aspect between 0.9 and 1.15 in `drawCreature`'s `base`
  transform. The hop already squashes this way.
- **Growth.** Use `growth` for the juvenile's scale curve.
- **Belly pattern.** A second procedural layer (stripes or chevrons on belly-region pixels), like
  mottling.
- **Eye colour at rest.** Already in place, through the eye region.

All of these mutate by `Nudge` and blend from generation to generation. The region bands
(`BANDS` in the converter) keep every result a frog green with a brown cloak. **Art:** none needed.

### (b) Heritable parts as small patches

**How it fits the genome.** Most of the plumbing exists. `MarkGene.layer` is the locus,
`variant` is the allele, and `tint` and `chemBound` colour it. `SpritePack::mark(layer, variant)`
is already the seam, with variant taken modulo the pack's allele count. `drawCreature` already
blits each non-zero layer with the body's transform (`draw.cpp:605-606`), so parts squash, hop and
scale with him for free. `Appearance.marks[8]` allows mottling plus 7 parts. A part gene is 12 B of
the 8 KB genome cap. Three changes are needed:

1. `rules_mark` steps `variant` by `Nudge` (±1..8). That suits ordered allele ladders, such as
   stalk length. Unordered loci, such as accessories, want `Any` at a low `mutWeight`. A per-layer
   rule needs a small `parts.def` registry: layer to name, allele count, ordered or not. That also
   lets the phone's `describe` print "hood: frayed" instead of numbers.
2. The converter learns to emit mark frames.
3. A part **cannot sit inside the face box**. The face patch is drawn after the marks and is
   opaque there, so anything under it is painted over. That box holds the eyes, the mouth and
   most of the hood's opening.

**Where parts can go and what they cost.** These are **measured** body areas outside the face
box: cloak 910 px, skin 1,642 px, belly 1,156 px, and only 83 px above the box. Small painted
patches cost about 1.6 B per opaque px in today's RLE and about 1.3 B in PackBits (measured on the
items). Estimated part sizes, by area:

| locus (layer) | alleles, for example | opaque px (guess) | bytes (RLE) |
|---|---|---|---|
| stalk tips (above the hood) | nub, long, forked, curled | 80 to 200 | 130 to 320 |
| hood crown | plain, horns, ears, toadstool, moss tuft | 150 to 400 | 240 to 640 |
| cloak edge and back | plain, frayed, patched, ragged, leaf-stitched | 300 to 1,000 | 0.5 to 1.6 KB |
| skin warts and nodules (sides, legs) | none, few, many, ridge | 60 to 200 | 100 to 320 |
| tail stub | none, nub, curl | 50 to 120 | 80 to 190 |
| accessory | scarf knot, bone pin, satchel strap, bell | 100 to 500 | 160 to 800 |
| eye shapes | see below | | |

**Eye shapes are breed-class, not part-class.** The eyes live in the face patches, which differ
in all ten expressions, so a new eye shape means re-rendering about 7 open-eyed faces. That costs
7 × 2.5 KB, or about 17 KB per allele. A cheaper half-measure is an over-face layer placed at each
face's `EyeAnchor`s, such as brows, lashes, a scar or slit pupils. The anchors already exist per
frame. That costs about 0.2 KB per allele and one new draw step after the face. It would hide on
the closed-eye faces (`eyeCount` is 0 there).

**What fits.** Six loci with five alleles each is 30 patches, about **15 KB**. That gives
5^6 = 15,625 part combinations before colour, and the 2.3 MB reserved-app budget would hold about
4,800 such patches. **Keeping him grungo:** parts stay outside the face and never touch the
silhouette's defining features (squat body, big head, stalks, cloak and hood). The palette is
full, so every part must be quantised into the existing region ramps. That is a feature, because
a horn drawn in cloak colours recolours with his cloak genes.

**Art.** This is a good fit for the pipeline. Each locus is a registered inpaint on the anchor with
its own mask and paint op, followed by a heal at low denoise. `hood_band` (mask and paint op on
`blorbarium-art`) is a working precedent. The converter already extracts "pixels that differ
from neutral" for the face box (`build()` in `sprite_pack.py`), so the same diff extracts a
part's patch. One caution is a guess: patches under about 40 device px may not read at 120 px, so
judge each one in `roundview.py`. The very small ones may be better drawn with the converter's own
`shapes` and `Draw` helpers, as the flies are, than generated at 1024 and divided by 7.

### (c) A whole alternate breed per rare mutation

**Size**, measured from the components:

| codec | breed: body + 10 faces + own palette | plus its own eggs and remains |
|---|---|---|
| RLE (today) | 36,264 B | 53,616 B |
| PackBits | 24,355 B | 36,912 B |

**How many fit** (`budget.py`, today's RLE and PackBits):

| budget | breeds | full breeds |
|---|---|---|
| app slot today | 68 / 102 | 46 / 67 |
| 4 MiB slot, 1 MiB kept for code | 63 / 94 | 42 / 62 |
| `petfs` less 1 MiB | 191 / 285 | 129 / 188 |
| one 1 MiB bank of an `art` partition | 28 / 43 | 19 / 28 |

**How it fits the genome.** A breed is a separate `SpritePack` instance with its own palette.
`draw()` already takes the pack as a parameter, and a breed with novel colours, such as albino or
crystal, needs its own 256 slots anyway. The selector is one new Look gene, `breed {id}`, or a
dormant `palette`-style gene that wakes, using the existing `kDormant` plus `MutWake` path. That
makes "rare mutation" literal. Only the starter's dormant genes would be the source, and a wake
is already a tracked, inheritable `MutationOp` that lineage records and lists as "wake"
(`lineage.cpp:449`). A friendlier "throwback" line would be new copy.
Breeds draw from the same seam, so palette genes, parts and mottling all still apply on top.

**Keeping him grungo.** Keep the silhouette and change only inside it. The pipeline's registration
property makes that the cheap path: an elder-style anchor inpaint, then the expression set on it.
A breed that changes the silhouette needs blank frames and new eye anchors, which is far more
picking work.

**Art.** This is exactly the shape of the art plan's elder (`art-plan.md` §6): a new
`characters/grungo-<breed>/` folder whose hero is a registered inpaint on the anchor, then the
usual masked expression inpaints. The cost is one anchor plus about 10 human picks per breed. At
the egg's pace of about 2 minutes per 8 SDXL images, generation is cheap, and the picking sets the
pace.

### (d) Generation-to-generation morphing

The art cannot morph pixelwise. It has no rig and no movable pupils (`grungo.md` §5). Morphing
therefore comes in three forms, cheapest first:

- **Continuous traits** (palette, size, stockiness, mottle density, glow) already drift by
  `Nudge` each generation. Zero bytes.
- **Ordered allele ladders** emulate shape morphing. Stalk length 0 to 5, hood horn size 0 to 4,
  and wart count 0 to 4 are examples. With `Nudge` ±1 the trait creeps along the ladder across
  generations, so a lineage visibly grows its horns over five lives. Each rung costs about
  0.2 to 0.6 KB. Five ladders of five rungs is about 10 KB. The pipeline makes the rungs with
  graded paint ops (`shapes` lengths or radii), then heals each rung.
- **A per-generation tint drift** ("cloak dye fades each generation, a new one every N") is also
  zero bytes. It is a rule in `look()` keyed on `Appearance.generation`.

## 4. Should the art move to a data partition?

**Yes, as a planned target, and the partition must be reserved before the first flash.** The
code move can wait.

**For it:**

- Art can grow and update over BLE in seconds rather than minutes. A new part takes under 1 s,
  and a breed takes 3 to 5 s at 7 to 9 KB/s, against about 100 s for a full firmware.
- Art stops competing with code for the one usable OTA slot.
- New breeds can be "discovered" as content drops without a firmware release.

**What it costs:**

1. **It must be raw and memory-mapped, not LittleFS.** A raw data partition can be mapped with
   `esp_partition_mmap`, which is an IDF API. I did not test it on this board. `FrameRef.rle` is
   already a plain pointer, so frames keep streaming in place, with zero RAM and the same cache
   path as `.rodata` today. A LittleFS file cannot be mapped, so every frame would have to be read
   into RAM: 37 to 54 KB per breed, out of about 170 KB of free internal DRAM before NimBLE's
   heap, on a board whose PSRAM is unverified. Proposed change: carve a 2 MiB `art` partition
   (`data`, custom subtype) from the proposed 7.6 MiB `petfs`, and leave `petfs` at about
   5.6 MiB. Split `art` into two 1 MiB banks plus a header (version, schema ids, SHA-256). Write
   the inactive bank over BLE, verify it, then flip the active bank in `pet` NVS. That is the same
   A/B logic as the app OTA, and it is idempotent if interrupted. One bank holds about 28 breeds
   at today's RLE.
2. **It needs a binary pack format and a loader.** The converter writes a blob (offset table,
   palette, bands, frames) next to, or instead of, the header. A `BlobPack : SpritePack` reads it
   from the mapped pointer. The `SpritePack` interface needs no change.
3. **It needs a fallback and version checks.** Keep base Grungo compiled into the image, so a
   blank, torn or mismatched `art` bank still draws him. The blob header must carry the
   expressions, regions and parts registries it was built against, and the firmware refuses a
   blob that does not match (boundary parse, like the keepsake codec).
4. **The sim and the goldens** load the blob from a file, so `tests/feeds` and the goldens keep
   working.
5. **The first flash must write the table.** The table cannot change by OTA (`explore-ota.md`
   §2), so `art` exists only if the first cable flash ships it.

**Recommendation.** Ship the partition table with `art` reserved in unit 20. Keep the art in the
image until the pack passes about 300 to 500 KB, or until the first downloadable breed, whichever
comes first. While the pack is under about 100 KB it costs only a few seconds per OTA, and the
in-image version needs no loader. Switch the codec to PackBits when the pack is next regenerated.
That is a 31% saving, with two decode sites (`RowReader::seek` and `visitOutline`) plus the
converter, and no RAM.

## Recommended variety plan

1. **Now, zero bytes:** draw `squash` as stockiness and `growth` as the juvenile curve. Add a
   procedural belly pattern. Unit 20 also needs `board_upload.flash_size = 16MB`, the
   partition CSV with `art`, and `build_unflags = -std=gnu++11`.
2. **Parts:** 5 or 6 loci outside the face box. Stalk tips, hood crown and tail are ordered
   ladders. Cloak edge, warts and accessory are unordered. Each locus gets 4 or 5 alleles through
   the existing `mark` seam, plus a `parts.def` registry. About 15 KB. This is where generations
   become visibly different and collectable, because each locus is a gene that inherits, mutates
   and can be bred for.
3. **Breeds:** 3 to 5 rare breeds as dormant genes that wake by mutation, registered inside his
   silhouette and made with the elder recipe. 110 to 180 KB at today's RLE. A breed is the
   roguelite "rare drop" that a lineage remembers.
4. **Eye accessories** come later, through an over-face layer at the eye anchors.

## Reproduce

- **Scratch clone.** `scratchpad/space-clone` is `origin/engine` `a1cb726`. The uncommitted
  changes are the `#error` swap in `src/main.cpp`, `scratch_board/`, and envs `badge128_size`
  and `badge128_unit20` in `platformio.ini`.
- **Builds.** `~/.platformio/penv/Scripts/pio.exe run -e badge128_size` and `-e badge128_unit20`.
  Logs are `build.log` and `build20.log`.
- **Pack bytes.** `python scratchpad/pack_bytes.py space-clone/pets/grungo/grungo_pack.h`.
- **Region areas.** `python scratchpad/regions_area.py space-clone/pets/grungo/grungo_pack.h`.
- **Fit counts.** `python scratchpad/budget.py`.
