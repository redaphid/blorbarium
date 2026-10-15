# Expression picks

These are the frames the firmware draws for each face in
`lib/blorb/include/blorb/defs/expressions.def`, plus the egg. Each was chosen
from the candidates on disk on 2026-10-15. The frames are in
`pets/grungo/art/frames/`. `tools/sprite_pack.py` packs them into
`pets/grungo/grungo_pack.h`. The packer was re-run on 2026-10-15 and wrote
the same bytes as the committed header (sha256 `6345fbcf…e4fcb`), so the build
draws exactly these frames.

The candidates that were compared came from these places:

- The sprite-expressions repo, branch `blorbarium-art`
  (`C:\Users\hypnodroid\Worktrees\sprite-expressions-blorbarium-art`).
  `characters/grungo/raw/` holds the picked frames, and `work/grungo/`
  holds the numbered candidates that were never committed.
- `D:\Projects\_wt\sprexp-roster`, which holds the same grungo picks and
  the art for the other roster species.
- The ComfyUI queue in `roster-art` (`tools/comfy_queue.py`). It never
  rendered into `D:\comfy\output`, and its faces (sad, angry, sick, hurt,
  fallen) are battle faces that this firmware does not draw.

| Expression | Chosen file | Why |
|---|---|---|
| neutral | `frames/neutral.png` (unchanged) | The user's pick. It is the only candidate on disk, and every other face is painted from it. |
| happy | `frames/happy.png` (unchanged) | The user's pick, and the only candidate. Its wide smile reads well at 240 px. |
| alarmed | `frames/alarmed.png` (unchanged) | The user's pick, and the only candidate. Its huge pupils are clear on the small panel. |
| annoyed | `frames/annoyed.png` (unchanged) | Kept over the newer `angry` pick (seed 1004) and its four candidates. Those bare teeth and shout, which is too harsh for an everyday grump. They also need new eye anchors and golden re-blesses. |
| croak | `frames/meow.png` (unchanged) | The user's pick, and the only candidate. Its open mouth reads as a croak. |
| blep | `frames/blep.png` (unchanged) | The user's pick, and the only candidate. Its tongue shows plainly. |
| foresee | `frames/foresee.png` (unchanged) | The user's open-mouth pick, which is candidate 3 painted wider. It beats candidates 0 to 5 and `foresee_closed`: the glow and the awe read best. Candidate 4's tongue looks like a blep. |
| sleepy | `frames/sleepy.png` (unchanged) | The user's pick, and the only candidate. Its half-lids read clearly. |
| asleep | `frames/sleep.png` (unchanged) | The user's pick, and the only candidate. Its shut eyes match the "asleep always shows shut eyes" rule. |
| yawn | `frames/yawn.png` (unchanged) | The user's pick, and the only candidate. |
| egg (not a face) | `frames/egg.png` (unchanged) | The user's pick: egg_nest seed 1007 with the nest cut away. Of the 16 egg_nest candidates (r0 to r3), it is the only one whose hooded tadpole matches his cloak, and its teal eyes stay visible at hatchling size. |

No face needed a new frame. Every slot already held the best candidate on
disk, and in most slots it is the only one.
