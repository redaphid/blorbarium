# Expression picks

These are the frames the firmware draws for each face in
`lib/blorb/include/blorb/defs/expressions.def`, plus the egg, for all nine
species. Each was checked against every candidate still on disk on 2026-10-15.
The frames are in `pets/<species>/art/frames/`. `tools/sprite_pack.py --pet
<species>` packs them into `pets/<species>/<species>_pack.h`. On 2026-10-15 the
packer was re-run for all nine pets, and every header came out byte-identical
to the committed one. The build therefore draws exactly these frames:

| Pack | sha256 |
|---|---|
| grungo | `6345fbcf…e4fcb` |
| mossby | `9014442c…877df` |
| puffkin | `8a88cef7…52160` |
| gilly | `db06e850…a07f` |
| mothwick | `53046cd5…fb583` |
| mudge | `f9fc8573…4a59d` |
| fizz | `f9e36c19…1dc57` |
| hushwing | `afebe751…39781` |
| tinker | `7e825b83…49b3` |

No face was swapped. Every slot holds the picked frame, and no candidate on
disk is both clearly better and usable without new renders.

## What is on disk

| Place | What is there | Usable for a face swap? |
|---|---|---|
| `D:\sync\Comfy\blorbarium` (ComfyUI's `--output-directory` is `D:\sync\Comfy`) | 828 PNGs. `fuzz/hurt1/<species>/` holds 72 for each of the nine species: angry 12, fallen 12, hurt 24, sad 12, sick 12. `fuzz/sad1/<species>/sad` holds 12 for each species. `expressions/grungo` holds 24 sad and a sheet, and `expressions/mossby` holds 20 sad. `poses/` holds windup poses: gilly 16, mothwick 9, mossby 1, puffkin 1. | No. These are battle faces and poses, which this firmware does not draw. |
| `D:\sync\Comfy\grungo` | 13 `adjust_*` whole-body neutral re-renders from the art-flow hand-adjust workflow. | No. They would replace the anchor and change the alpha that every face is registered on. |
| `D:\sync\Comfy\grungo-stories` | 52 `scene_*` illustrations. | No. They are not sprites. |
| `D:\sync\THE_SYNC` | Does not exist. | |
| `D:\Projects\_wt\sprexp-roster` (`characters/<species>/raw/`) | One picked frame per face for each species, plus the intermediate chain frames (`happy_eyes`, `annoyed_eyes`, `foresee_glow`, `foresee_eyes`, `egg_jelly`). All 99 shipped frames are byte-identical to these picks. | They are the shipped frames. |
| `D:\Projects\_wt\sprexp-roster\work\rounds` | 17 candidate sheets made on 2026-10-13. Each shows four seeds per face at sheet scale: fizz (happy to blep, foresee_eyes to egg, foresee, sleep re-roll), gilly (happy to foresee_glow, sleepy to egg, foresee and yawn), hushwing (alarmed to foresee, sleepy to egg, yawn), mothwick (happy, annoyed, foresee, yawn; meow to sleepy; sleep, egg, happy_eyes, annoyed_eyes), mudge (annoyed to foresee, sleepy to egg), puffkin (meow to foresee, sleepy to egg, happy and hushwing sleep re-rolls). | No. These are downscaled sheet crops, not 1024 px frames with the anchor's alpha. To use one, its seed would have to be re-rendered through ComfyUI. |
| `D:\Projects\_wt\sprexp-roster\work\pv*`, `work\concepts*` | Pack previews (faces, regions, recolour, remains) and body concepts. | No. These are reviews, not candidates. |
| `D:\Projects\_wt\se-<species>` | Deleted. The numbered full-size candidates for the eight roster species lived here and are gone. | |
| `D:\Projects\sprite-expressions\work\grungo`, `C:\Users\hypnodroid\Worktrees\sprite-expressions-blorbarium-art\work\grungo` | Grungo's numbered candidates: four for each face, plus round-two faces, stages and accessories. | The grungo section below covers them. |
| `D:\Projects\_wt\blorb-art` (`roster-art`) | The same 99 frames as the release, byte for byte. | They are the shipped frames. |

In the tables below, a "seed" is the forge seed recorded in
`characters/<species>/raw/manifest.json`. Candidate *n* on a round sheet is
seed 1001 + *n*, or 1005 + *n* on a re-roll sheet. "<" marks a chain: that face
was painted on the named frame instead of on the anchor. Where no sheet survives,
the only frame left to judge is the pick, and the judgement is made from
`roster/contact-sheet.png`.

## Grungo (frog)

| Expression | Chosen file | Why |
|---|---|---|
| neutral | `frames/neutral.png` (unchanged) | The user's pick. It is the only candidate on disk, and every other face is painted from it. The 13 ComfyUI `adjust_*` bodies are alternative anchors, not faces. |
| happy | `frames/happy.png` (unchanged) | The user's pick. Its wide smile reads well at 240 px. |
| alarmed | `frames/alarmed.png` (unchanged) | The user's pick. Its huge pupils are clear on the small panel. |
| annoyed | `frames/annoyed.png` (unchanged) | Kept over the newer `angry` pick (seed 1004) and its four candidates. Those bare teeth and shout, which is too harsh for an everyday grump. They also need new eye anchors and golden re-blesses. |
| croak | `frames/meow.png` (unchanged) | The user's pick. Its open mouth reads as a croak. |
| blep | `frames/blep.png` (unchanged) | The user's pick. Its tongue shows plainly. |
| foresee | `frames/foresee.png` (unchanged) | The user's open-mouth pick, which is candidate 3 painted wider. It beats candidates 0 to 5 and `foresee_closed`: the glow and the awe read best. Candidate 4's tongue looks like a blep. |
| sleepy | `frames/sleepy.png` (unchanged) | The user's pick. Its half-lids read clearly. |
| asleep | `frames/sleep.png` (unchanged) | The user's pick. Its shut eyes match the "asleep always shows shut eyes" rule. |
| yawn | `frames/yawn.png` (unchanged) | The user's pick. |
| egg | `frames/egg.png` (unchanged) | The user's pick: egg_nest seed 1007 with the nest cut away. Of the 16 egg_nest candidates, it is the only one whose hooded tadpole matches his cloak, and its teal eyes stay visible at hatchling size. |

## Mossby (snail)

No round sheets survive for Mossby, so each pick is the only frame left.

| Expression | Pick | Why kept |
|---|---|---|
| happy | seed 1003 | Open eyes with a smile and blush. It reads as content and is distinct from sleepy. |
| alarmed | seed 1003 | Ringed, staring eyes and a small open mouth. It is the most different face in his row. |
| annoyed | seed 1002 | Flat brows and a flat mouth: a mild grump that fits a slow snail. |
| croak | seed 1003 | An open "o" mouth that reads as a call. |
| blep | seed 1004 | The pink tongue is plain at device scale. |
| foresee | seed 1001, `glow_eyes` | Solid yellow glowing eyes. The halo has a clear eye region to ring. |
| sleepy | seed 1002, `lids_half` | Half-lids on the same smile. |
| asleep | seed 1003, `lids_shut` | Shut arcs. It is also the source of Remains. |
| yawn | seed 1003 < sleep | Shut eyes with an open mouth, so it chains cleanly from asleep. |
| egg | seed 1002, holes filled | A baby snail on a cushion inside the egg, with eyes measured in `eyes.json`. |

## Puffkin (puffball mushroom)

| Expression | Pick | Why kept |
|---|---|---|
| happy | re-roll seed 1007 (candidate 2) | The first roll put a second pair of eyes on his cheeks. On the re-roll sheet, candidate 2's winking arcs and open smile are the warmest. Candidate 0's dot eyes read close to neutral. |
| alarmed | seed 1001, `alarm_face` | No sheet survives. Wide ringed eyes and an open mouth, clear on the panel. |
| annoyed | seed 1003, `frown_face` | No sheet survives. Slanted brows and a frown, the strongest grump in the roster. |
| croak | seed 1002 (candidate 1) | The "o" mouth with a tongue reads as a puff. Candidate 0's mouth is a pink blob that turns to mush at device scale. |
| blep | seed 1001 (candidate 0) | The tongue is the largest and clearest of the four. |
| foresee | seed 1002 (candidate 1) | All four are pink-glow eyes. Candidate 1's rims are the cleanest. |
| sleepy | seed 1003 (candidate 2) | The heaviest lids that still show the eye, which separates it best from asleep. |
| asleep | seed 1001 (candidate 0), `lids_shut` + `shut_mouth` | Clean shut arcs, with no leftover eye shading. |
| yawn | seed 1002 < sleep | No sheet survives. Shut eyes with a wide open mouth. |
| egg | seed 1002 (candidate 1), holes filled | A full puffball face behind glossy jelly. Candidate 2's rim is broken. |

## Gilly (axolotl)

| Expression | Pick | Why kept |
|---|---|---|
| happy | seed 1003 (candidate 2), `happy_eyes` | Squinting arcs and a big smile. Candidates 0 and 1 keep open eyes and read close to neutral. |
| alarmed | seed 1002, the only frame left | Wide eyes and an open mouth. |
| annoyed | seed 1003, the only frame left | Flat lids and a wavy mouth. |
| croak | seed 1002 (candidate 1), `squeak_mouth` | The open squeak with a tongue is clearest. Candidates 2 and 3 are near-copies of happy. |
| blep | seed 1002 (candidate 1), `blep_tongue` | The tongue is centred and red against the pink. |
| foresee | seed 1002 (candidate 1) < foresee_glow seed 1002, `mend_mouth` | Green glowing eyes with an open smile. The glow candidates are almost identical, and the mouth candidates differ only in tooth detail. |
| sleepy | seed 1004 (candidate 3) | Half-lids with lashes, the most readable of four close variants. |
| asleep | seed 1003 (candidate 2), `shut_eyes` | Clean arcs with no ghost of the iris. |
| yawn | seed 1002 (candidate 1) < sleep | The tallest open mouth. Candidate 2 reads as a grin. |
| egg | seed 1004 (candidate 3), holes filled | A whole axolotl baby with gills, centred in the egg. Candidate 0 is a big face pressed against the shell, with no body. |

## Mothwick (glow-moth)

| Expression | Pick | Why kept |
|---|---|---|
| happy | seed 1003 (candidate 2) < happy_eyes seed 1003 (candidate 2) | Clean arcs and an open smile. Candidates 0 and 1 smear dark paint or yellow lid marks across the cheeks. |
| alarmed | seed 1004, the only frame left | Ringed eyes and an open mouth. |
| annoyed | seed 1003 (candidate 2) < annoyed_eyes seed 1003 | A yellow glare under a flat lid with a wavy mouth. Candidates 0 and 1 leak yellow outside the eye. |
| croak | seed 1002 (candidate 1) | Round eyes and an open mouth. Candidates 0 and 2 have fur blobs on the forehead. |
| blep | seed 1003 (candidate 2) | The tongue is long and centred. Candidate 1's mouth blurs at device scale. |
| foresee | seed 1003 (candidate 2) < foresee_glow seed 1002 | Bright yellow lamps with a small smile. Candidate 1 adds buck teeth. |
| sleepy | seed 1003 (candidate 2) | Clean half-lids. Candidates 1 and 3 leave a purple ghost of the iris. |
| asleep | seed 1004 (candidate 3) | Clean arcs. Candidate 2 leaves pale eye discs. |
| yawn | seed 1003 (candidate 2) < sleep | A clean open mouth. Candidate 0 paints a white patch over one eye, and candidate 3 leaves an eye open. |
| egg | seed 1004 (candidate 3), holes filled | A moth chick with wings visible inside. Candidate 1 is black-masked and off-model. |

## Mudge (mudskipper)

| Expression | Pick | Why kept |
|---|---|---|
| happy | seed 1002, `happy_eyes`, the only frame left | Closed happy arcs and a wide grin. |
| alarmed | seed 1004, `alarm_eyes` + `alarm_mouth`, the only frame left | Shrunken pupils, blush and an open mouth. |
| annoyed | seed 1003 (candidate 2) | Heavy lids and a wavy frown. Candidate 1 adds a sweat drop, and candidate 3's lashes read as surprised. |
| croak | seed 1004 (candidate 3), `blub_mouth` | The open mouth with a bubble reads as a fish's blub. |
| blep | seed 1002 (candidate 1) | The tongue is clearest, and the face is clean. |
| foresee | seed 1002 (candidate 1) | Bright cyan eyes with a smile. The four candidates are close, and candidate 1 has the cleanest eye edge. |
| sleepy | seed 1003 (candidate 2) | Half-lids that still show the eye. Candidate 3's lids are lopsided. |
| asleep | seed 1003, `sleep_eyes`, the only frame left | Shut arcs. |
| yawn | seed 1002 (candidate 1) < sleep | A tall open mouth with shut eyes. Candidate 0 reads as a shout. |
| egg | seed 1004 (candidate 3), holes filled | A fin-tailed fry with clear eyes. Candidate 2's chick is off-model. |

## Fizz (firefly beetle)

| Expression | Pick | Why kept |
|---|---|---|
| happy | seed 1001 (candidate 0), `happy_arcs` | Arcs, an open smile and a tongue, the brightest of the four. Candidate 2 keeps brown eye discs. |
| alarmed | seed 1002 (candidate 1), `shock` | White ringed eyes with an "o" mouth. All four are close. |
| annoyed | seed 1002 (candidate 1), `grump` | A plain frown with eyes intact. Candidate 3's eyes are smeared. |
| croak | seed 1002 (candidate 1), `chirp` | A clear open "o". Candidate 2's mouth is too small at device scale. |
| blep | seed 1002 (candidate 1) | The longest, most central tongue. |
| foresee | seed 1003 (candidate 2) < foresee_eyes seed 1004 (candidate 3), `zap_grin` | Green glow eyes with a grin. Candidates 0 and 1 have a smaller mouth. |
| sleepy | seed 1002 (candidate 1) | Half-lids. All four are close. |
| asleep | seed 1010 (third roll, candidate 1) at denoise 0.25, with lash arcs drawn on | The first two rolls left the eye discs visible. This is the only roll with shut eyes. |
| yawn | seed 1004 < sleep | No sheet survives. An oval yawn on the shut-eye frame. |
| egg | seed 1003 (candidate 2) | A beetle baby that looks like Fizz. Candidate 1 has wings that read as a bee, and candidate 3 is tinted green. |

## Hushwing (marsh owl)

| Expression | Pick | Why kept |
|---|---|---|
| happy | seed 1003, `happy_eyes`, the only frame left | Closed happy arcs with blush. |
| alarmed | seed 1002 (candidate 1), `alarm_eyes` + `open_beak` | Big orange eyes with a gaping beak. Candidate 3's beak is flattened. |
| annoyed | seed 1001, `annoyed_eyes`, the only frame left | A heavy-browed glare. |
| croak | seed 1001 (candidate 0), `open_beak` | The beak shows a clear opening. Candidate 2 paints the beak pink. |
| blep | seed 1003 (candidate 2), `blep_tongue` | The tongue hangs below a clean beak. Candidate 0 smears pink across the face. |
| foresee | seed 1002 (candidate 1) | Ice-blue glow eyes with rims. All four are close. |
| sleepy | seed 1003 (candidate 2) | Half-lids. Candidate 0 is lopsided. |
| asleep | re-roll seed 1007 (candidate 2) at denoise 0.62 | The first roll left pale eye discs. The re-roll's candidate 2 has lash arcs and no disc. Candidate 0 still shows orange eyes. |
| yawn | seed 1002 (candidate 1) < sleep | The beak opens with no ghost eye. Candidate 2's beak is rounded and loses its shape. |
| egg | seed 1002 (candidate 1) | A white owlet with blue eyes. Candidates 0 and 2 have black paint smudges. `egg_baby` in `pack.json` marks the chick. |

## Tinker (raccoon)

No round sheets survive for Tinker, so each pick is the only frame left.

| Expression | Pick | Why kept |
|---|---|---|
| happy | seed 1002 < happy_eyes seed 1004 | Closed arcs inside the mask with a smile. |
| alarmed | seed 1003, `o_mouth` | Wide eyes and an "o" mouth. |
| annoyed | seed 1002 | Slanted brows and a frown. |
| croak | seed 1004 | An open chatter mouth. |
| blep | seed 1001 | The tongue is plain. |
| foresee | seed 1004 < foresee_glow seed 1001, `idea_mouth` | Amber glowing eyes under the goggles. |
| sleepy | seed 1002, `lids_half` | Half-lids. |
| asleep | seed 1003 at denoise 0.3, `lids_shut` + `shut_lines` | Shut lines drawn on, so no eye shows. |
| yawn | seed 1004 < sleep | An open mouth on the shut-eye frame. |
| egg | egg_jelly seed 1003 | A raccoon kit in goggles behind jelly. |

## Battle faces

The sad, angry, sick, hurt and fallen renders in `D:\sync\Comfy\blorbarium`
were not compared. This firmware does not draw those faces.
