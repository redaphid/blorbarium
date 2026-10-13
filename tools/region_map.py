#!/usr/bin/env python3
"""A pet's recolour region map: pets/<pet>/art/masks/regions.png, which
tools/sprite_pack.py --pet lets overrule its own labelling of the body.

    python3 tools/region_map.py puffkin            # writes the map and pets/puffkin/regions-map.png
    python3 tools/region_map.py puffkin --smooth 12

Genes recolour by region, so a region that spans two parts of a pet (a puffball's rim
and face, a fish's body and belly) tints both together on every child. The converter
walls regions by the art's linework; where the art has no line between two parts, this
map draws one. Each body pixel of the neutral frame takes its nearest colour among
pack.json's "prototypes" (a region to one colour, or to a list of colours), a box
majority vote over --smooth pixels settles highlights and shading into their part,
and pack.json's "region_paint" shapes, in anchor pixels, have the last word:

    "region_paint": [{"region": "cloak", "polygon": [[x, y], ...]}, {"region": "skin", "ellipse": [x0, y0, x1, y1]}]

Eyes, mouth and ink are left to the converter (255 in the map). Needs scipy.
"""
import argparse
import json
from pathlib import Path

import numpy as np
from PIL import Image, ImageDraw
from scipy import ndimage

ROOT = Path(__file__).resolve().parent.parent
REGION = {"skin": 0, "belly": 1, "cloak": 2}
SHOW = {0: (90, 170, 70), 1: (220, 230, 120), 2: (150, 90, 45)}


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("pet")
    ap.add_argument("--smooth", type=int, default=9, help="half-width of the majority box, in art pixels")
    a = ap.parse_args()
    art = ROOT / "pets" / a.pet / "art"
    cfg = json.loads((art / "pack.json").read_text())
    im = np.asarray(Image.open(art / "frames/neutral.png").convert("RGBA")).astype(float)
    rgb, opaque = im[..., :3], im[..., 3] > 0
    v = rgb.max(-1) / 255
    eyes = np.asarray(Image.open(art / "masks/eyes.png").convert("L")) > 127
    mouth = np.asarray(Image.open(art / "masks/mouth.png").convert("L")) > 127
    body = opaque & (v >= cfg.get("ink", 0.16)) & ~eyes & ~mouth

    protos = []
    for region, colours in cfg["prototypes"].items():
        for c in (colours if isinstance(colours[0], list) else [colours]):
            protos.append((np.array(c, float), REGION[region]))
    d = np.stack([((rgb - c) ** 2).sum(-1) for c, _ in protos], -1)
    nearest = np.array([r for _, r in protos], np.uint8)[d.argmin(-1)]

    size = 2 * a.smooth + 1
    votes = np.stack([ndimage.uniform_filter(((nearest == r) & body).astype(float), size) for r in range(3)], -1)
    label = votes.argmax(-1).astype(np.uint8)

    paint = Image.fromarray(label)
    draw = ImageDraw.Draw(paint)
    for s in cfg.get("region_paint", []):
        if "polygon" in s:
            draw.polygon([tuple(p) for p in s["polygon"]], fill=REGION[s["region"]])
        else:
            draw.ellipse(s["ellipse"], fill=REGION[s["region"]])
    label = np.asarray(paint)
    out = np.where(body, label, 255).astype(np.uint8)
    Image.fromarray(out).save(art / "masks/regions.png")

    show = (rgb * 0.35).astype(np.uint8)
    for r, c in SHOW.items():
        show[out == r] = (np.array(c) * 0.75 + rgb[out == r] * 0.25).astype(np.uint8)
    Image.fromarray(show).save(art.parent / "regions-map.png")
    print(a.pet, {n: int((out == r).sum()) for n, r in REGION.items()}, "->", art / "masks/regions.png")


if __name__ == "__main__":
    main()
