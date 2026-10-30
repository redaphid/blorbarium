# Puck's art sources

Puck is the owner's own cat, and his art is cyber-puck's: the 64x64 pixel art in that
repository's `pets/puck/puck_sprite.h` (written by its `tools/sprite2header.py`),
decoded back to PNG unchanged, one frame per picked expression. cyber-puck has
nineteen expressions; these are the ten the engine wears:

| engine face | frame | cyber-puck expression |
|---|---|---|
| neutral | `neutral.png` | `neutral[0]` |
| happy | `happy.png` | `happy[0]` |
| alarmed | `alarmed.png` | `alarmed[0]` |
| annoyed | `annoyed.png` | `annoyed[0]` |
| croak | `meow.png` | `meow[0]` |
| blep | `blep.png` | `blep[0]` |
| foresee | `judgy.png` | `judgy[0]`: the hunter's squint, robot eye lit |
| sleepy | `sleepy.png` | `sleepy[0]` |
| asleep | `sleep.png` | `sleep[0]`: the face uses the head only, the remains all of it |
| yawn | `yawn.png` | `yawn[0]` |

Every frame but `sleep.png` has neutral's exact silhouette, so each face is a patch
over the one body like any roster pet's. The egg is drawn by the converter: a pale egg
with his head inside.

`python3 tools/puck_pack.py` writes `pets/puck/puck_pack.h`: it scales him three device
pixels an art pixel (nearest neighbour, so the pixel art stays crisp), labels regions
from his palette (his own eye is `eye`, the robot eye's red is `glow`, the metal of the
eye ring and his robot paw is `cloak`, his cream is `belly`, his orange is `skin`), and
uses `tools/sprite_pack.py` for the palette, egg cracks, remains, items and header. His
skin band is narrow, so genes nudge his orange but never turn him green. To change a
frame, change it in cyber-puck, decode it here and rerun the converter.
