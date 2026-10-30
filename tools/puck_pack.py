#!/usr/bin/env python3
"""Puck's sprite pack: cyber-puck's 64 px pixel-art cat becomes pets/puck/puck_pack.h.

    python3 tools/puck_pack.py                 # write the header, print its sha256
    python3 tools/puck_pack.py --preview DIR   # also decode it into preview PNGs

Puck is not picked 1024 px art like the roster: he is cyber-puck's finished pixel art
(pets/puck/puck_sprite.h there, cut by its tools/sprite2header.py), decoded to the
64 x 64 frames in pets/puck/art/frames/. So this skips sprite_pack.py's downscaler and
labeller and scales him by a whole number (SCALE device pixels per art pixel, nearest
neighbour, so every pixel stays crisp), labels each pixel's region from its colour and
place, and hands the rest to sprite_pack.py: the palette, the egg's cracks, the
remains, the shared items, the anchor checks and the header are the roster's own.

Deterministic, like sprite_pack.py: two runs write the same bytes.
"""
import argparse
import hashlib
import sys
from pathlib import Path

import numpy as np
from PIL import Image

sys.path.insert(0, str(Path(__file__).resolve().parent))
import sprite_pack as sp  # noqa: E402

ROOT = sp.ROOT
SCALE = 3                     # 55 art rows -> 165 device rows: grungo's 168, near enough
ART_NAME = {"croak": "meow", "asleep": "sleep", "foresee": "judgy"}   # engine face -> frame

# Where things are on the 64 x 64 art (pets/puck/art/frames/neutral.png).
AMBER_EYE = (22.5, 23.0, 6.5)       # his own eye: centre x, y and radius
CYBER_EYE = (41.0, 23.0, 9.0)       # the red robot eye in its ring
MOUTH_BOX = (26, 30, 40, 39)        # x0, y0, x1, y1 inclusive: nose, mouth and tongue
HEAD_ROWS = 41                      # the asleep face keeps only the head (a blink is asleep)

# Colours by role (cyber-puck's palette).
BLACK = (14, 9, 10)
INK = {(30, 22, 26)}                                                   # the robot eye's dark rim
GREYS = {(59, 61, 64), (104, 115, 131), (147, 161, 174), (199, 211, 223)}   # metal: the eye ring, the paw
REDS = {(196, 19, 25), (250, 73, 58)}
MOUTHY = {BLACK, (37, 17, 10), (75, 33, 12), (105, 50, 18)} | REDS
IRIS = {BLACK, (235, 140, 55), (252, 250, 248)}
BELLY = {(180, 142, 105), (199, 168, 136), (227, 191, 152), (228, 203, 179), (250, 215, 190),
         (246, 236, 227), (252, 250, 248)}

# Puck is a real orange tabby: genes may nudge him, never turn him green. The rest are
# sprite_pack.py's defaults.
BANDS = {"skin": (-8, 10, 104, 152, 104, 152)}

R = sp.REGION
ART = ROOT / "pets/puck/art"
OUT = ROOT / "pets/puck/puck_pack.h"


def load(name):
    a = np.asarray(Image.open(ART / "frames" / f"{name}.png").convert("RGBA"))
    return a[..., :3].copy(), a[..., 3] > 0


def label(rgb, alpha):
    """A region per art pixel, by colour and place."""
    h, w = alpha.shape
    yy, xx = np.mgrid[0:h, 0:w]

    def disc(x, y, r):
        return (xx - x) ** 2 + (yy - y) ** 2 <= r * r

    def has(colours):
        m = np.zeros(alpha.shape, bool)
        for c in colours:
            m |= (rgb == c).all(-1)
        return m

    amber, cyber = disc(*AMBER_EYE), disc(*CYBER_EYE)
    x0, y0, x1, y1 = MOUTH_BOX
    mouth_zone = (xx >= x0) & (xx <= x1) & (yy >= y0) & (yy <= y1) & ~cyber
    rules = [
        (amber & has(IRIS), R["eye"]),
        (cyber & has(REDS), R["glow"]),
        (mouth_zone & has(MOUTHY), R["mouth"]),
        (has(REDS), R["glow"]),
        (has(INK) | has({BLACK}), sp.INVARIANT),
        (has(GREYS), R["cloak"]),
        (has(BELLY), R["belly"]),
    ]
    lab = np.select([c for c, _ in rules], [k for _, k in rules], R["skin"]).astype(np.uint8)
    lab[~alpha] = sp.CLEAR
    return lab


def up(a, k=SCALE):
    return np.repeat(np.repeat(a, k, 0), k, 1)


def egg_sprite(head_rgb, head_lab, head_eyes):
    """A pale egg with Puck's head inside, wide-eyed: the baby shows his genes, the
    jelly is shell, and egg() inks the cracks across the dome."""
    cols, rows = 108, sp.EGG_ROWS
    k = sp.Sketch(cols, rows)
    shell = k.mask("ellipse", 1, 1, cols - 1, rows - 1)
    k.fill(shell, (226, 236, 214), R["shell"])
    k.fill(shell & ~k.mask("ellipse", 10, 6, cols - 4, rows - 12), (196, 214, 186), R["shell"])
    k.fill(k.mask("ellipse", 22, 14, 36, 34), (246, 250, 240), R["shell"])   # a shine on the dome
    k.outline(shell, 1.5)
    s = k.sprite((cols // 2, rows - 1))
    hh, hw = head_lab.shape
    ox, oy = (cols - hw) // 2, rows - hh - 18
    m = head_lab != sp.CLEAR
    s.rgb[oy:oy + hh, ox:ox + hw][m] = head_rgb[m]
    s.region[oy:oy + hh, ox:ox + hw][m] = head_lab[m]
    s.eyes = [(x + ox, y + oy, r) for x, y, r in head_eyes]
    return s


def build():
    names = sorted({ART_NAME.get(e, e) for _, e in sp.EXPRESSIONS})
    art = {n: load(n) for n in names}
    alpha = art["neutral"][1]
    ys, xs = np.nonzero(alpha)
    bx0, by0, bx1, by1 = int(xs.min()), int(ys.min()), int(xs.max()) + 1, int(ys.max()) + 1
    bottom = xs[ys == ys.max()]
    feet_art = ((int(bottom.min()) + int(bottom.max())) / 2, int(ys.max()))

    def device(name):
        rgb, a = art[name]
        lab = label(rgb, a)
        return up(rgb[by0:by1, bx0:bx1]), up(lab[by0:by1, bx0:bx1])

    def at(x, y):   # an art point -> the device pixel at its centre, body-relative
        return int((x - bx0) * SCALE + SCALE // 2), int((y - by0) * SCALE + SCALE // 2)

    origin = (int((feet_art[0] - bx0) * SCALE + SCALE // 2), (by1 - by0) * SCALE - 1)
    eyes = [(*at(AMBER_EYE[0], AMBER_EYE[1]), round(AMBER_EYE[2] * SCALE * 0.6)),
            (*at(CYBER_EYE[0], CYBER_EYE[1]), round(CYBER_EYE[2] * SCALE * 0.5))]

    full = {n: device(n) for n in names}
    # The asleep face is the head only: a blink wears it, and the sleeping frame's
    # curled tail must not flick in and out with every blink. The remains get it all.
    sleep_rgb, sleep_lab = full["sleep"]
    head = (HEAD_ROWS - by0) * SCALE
    n_rgb, n_lab = full["neutral"]
    asleep_rgb, asleep_lab = n_rgb.copy(), n_lab.copy()
    asleep_rgb[:head], asleep_lab[:head] = sleep_rgb[:head], sleep_lab[:head]
    full["asleep_head"] = (asleep_rgb, asleep_lab)
    face_src = {e: ("asleep_head" if e == "asleep" else ART_NAME.get(e, e)) for _, e in sp.EXPRESSIONS}

    body = sp.Sprite(n_rgb, n_lab, origin, eyes)
    changed = np.zeros(n_lab.shape, bool)
    for e, src in face_src.items():
        rgb, lab = full[src]
        changed |= (lab != n_lab) | (rgb != n_rgb).any(-1)
    cy, cx = np.nonzero(changed)
    x0, y0 = max(0, int(cx.min()) - 1), max(0, int(cy.min()) - 1)
    x1, y1 = min(n_lab.shape[1], int(cx.max()) + 2), min(n_lab.shape[0], int(cy.max()) + 2)
    faces = {}
    for _, e in sp.EXPRESSIONS:
        rgb, lab = full[face_src[e]]
        faces[e] = sp.Sprite(rgb[y0:y1, x0:x1].copy(), lab[y0:y1, x0:x1].copy(),
                             (origin[0] - x0, origin[1] - y0), [])

    frames = {"Body": body}
    frames.update({f"Face_{e}": faces[e] for _, e in sp.EXPRESSIONS})

    # The egg: his head (down to the chin) at two device pixels an art pixel.
    hy1 = 34
    h_rgb, h_a = art["neutral"]
    h_lab = label(h_rgb, h_a)[by0:hy1, bx0:bx1]
    h_rgb = h_rgb[by0:hy1, bx0:bx1]
    k = 2
    head_eyes = [(int((AMBER_EYE[0] - bx0) * k + 1), int((AMBER_EYE[1] - by0) * k + 1), 4),
                 (int((CYBER_EYE[0] - bx0) * k + 1), int((CYBER_EYE[1] - by0) * k + 1), 4)]
    whole = egg_sprite(up(h_rgb, k), up(h_lab, k), head_eyes)
    frames.update({f"Egg{i}": sp.egg(whole, cracks) for i, cracks in enumerate((0, 1, 3))})

    # The remains: sprite_pack.py's pool under the sleeping frame, fed at its art scale
    # (CELL art pixels a device pixel), with the body box it expects in pack.json.
    C = sp.CELL
    sp.CFG = {"body": [(bx1 - bx0) * SCALE * C, (by1 - by0) * SCALE * C]}
    pad = 200
    sp.GRID = (pad, pad)
    big_rgb = np.zeros((pad * 2 + sleep_lab.shape[0] * C, pad * 2 + sleep_lab.shape[1] * C, 3), np.uint8)
    big_lab = np.full(big_rgb.shape[:2], sp.CLEAR, np.uint8)
    big_rgb[pad:pad + sleep_lab.shape[0] * C, pad:pad + sleep_lab.shape[1] * C] = up(sleep_rgb, C)
    big_lab[pad:pad + sleep_lab.shape[0] * C, pad:pad + sleep_lab.shape[1] * C] = up(sleep_lab, C)
    feet = (pad + origin[0] * C + C // 2, pad + origin[1] * C + C - 1)
    frames["Remains"] = sp.remains(big_rgb, big_lab, feet)

    frames["ItemPellet"] = sp.fly((46, 48, 56), (84, 86, 98), (206, 226, 236), (170, 40, 32))
    frames["ItemRotten"] = sp.fly((112, 126, 96), (90, 102, 78), (172, 184, 164), (120, 128, 92))
    frames["ItemMarble"] = sp.bubble()

    entries, by_region = sp.build_palette(list(frames.values()))
    idx = {n: sp.index(s, by_region) for n, s in frames.items()}
    # The body's anchors are on his eyes through every face with his own eye open (happy,
    # annoyed, foresee, sleepy and yawn shut or lid it; the robot eye never shuts).
    sp.check_anchors(frames, idx, entries, body, faces, ("neutral", "alarmed", "croak", "blep"))
    return dict(entries=entries, frames=frames, idx=idx)


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--preview", type=Path, help="decode the written header into preview PNGs here")
    args = ap.parse_args()
    sp.PET, sp.ART, sp.OUT = "puck", ART, OUT
    sp.COMMAND = "python3 tools/puck_pack.py"
    sp.BANDS = {**sp.BANDS, **BANDS}
    text = sp.emit(build())
    OUT.write_text(text)
    print(f"{OUT.relative_to(ROOT)} sha256 {hashlib.sha256(text.encode()).hexdigest()}")
    if args.preview:
        sp.preview(text, args.preview)


if __name__ == "__main__":
    main()
