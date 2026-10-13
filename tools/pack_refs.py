#!/usr/bin/env python3
"""Draft pets/<pet>/art/pack.json for tools/sprite_pack.py --pet from the pet's art.

    python3 tools/pack_refs.py mossby            # writes pack.json and pack-refs.png beside it
    python3 tools/pack_refs.py mossby --skin "#F2E3C4" --cloak "#E8892B"

Framing comes from the neutral frame's alpha bbox, the eyeballs from the two blobs of
masks/eyes.png. Reference colours are k-means clusters: of the body outside the eye and
mouth masks (each the region of the nearest prototype: --skin, --cloak, --belly), of the
eyes, of the open mouths that differ from the body, and of the power face's eyes that
differ from neutral's (the glow). roster/roster.json gives --skin and --cloak by
default. The prototypes go into pack.json too: sprite_pack.py gives each ink-bounded
patch of the body the region of the prototype nearest its mean colour. pack-refs.png shows every swatch with its region, to check and hand-correct.
"""
import argparse
import json
from pathlib import Path

import numpy as np
from PIL import Image, ImageDraw

ROOT = Path(__file__).resolve().parent.parent
INK = 0.16


def hexrgb(h):
    return np.array([int(h[i:i + 2], 16) for i in (1, 3, 5)], float)


def kmeans(px, k, seed=0):
    """Deterministic k-means over pixel colours: centres start at evenly spaced
    brightness quantiles, so two runs pick the same refs."""
    px = px.astype(float)
    if len(px) <= k:
        return px
    order = np.argsort(px.sum(1), kind="stable")
    c = px[order[np.linspace(0, len(px) - 1, k).astype(int)]]
    for _ in range(12):
        lab = ((px[:, None] - c[None]) ** 2).sum(-1).argmin(1)
        for j in range(k):
            if (lab == j).any():
                c[j] = px[lab == j].mean(0)
    return np.unique(np.round(c), axis=0)


def value(c):
    return c.max(-1) / 255


def sat(c):
    mx = c.max(-1)
    return np.where(mx > 0, (mx - c.min(-1)) / np.maximum(mx, 1), 0)


def far(cs, refs, d):
    """The colours in cs further than d from every colour in refs."""
    if not len(refs):
        return cs
    dist = np.sqrt(((cs[:, None] - np.array(refs)[None]) ** 2).sum(-1)).min(1)
    return cs[dist > d]


def blobs(mask):
    """The two largest blobs of a mask, as (cx, cy, r) by area."""
    from collections import deque
    h, w = mask.shape
    seen = np.zeros_like(mask)
    found = []
    for y, x in zip(*np.nonzero(mask)):
        if seen[y, x]:
            continue
        q, pts = deque([(y, x)]), []
        seen[y, x] = True
        while q:
            cy, cx = q.popleft()
            pts.append((cy, cx))
            for ny, nx in ((cy + 1, cx), (cy - 1, cx), (cy, cx + 1), (cy, cx - 1)):
                if 0 <= ny < h and 0 <= nx < w and mask[ny, nx] and not seen[ny, nx]:
                    seen[ny, nx] = True
                    q.append((ny, nx))
        p = np.array(pts)
        found.append((len(p), p[:, 1].mean(), p[:, 0].mean()))
    found.sort(reverse=True)
    eyes = sorted(found[:2], key=lambda f: f[1])
    return [(round(x), round(y), round(np.sqrt(n / np.pi))) for n, x, y in eyes]


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("pet")
    ap.add_argument("--skin")
    ap.add_argument("--cloak")
    ap.add_argument("--glow")
    ap.add_argument("--belly", default="#F4EEE2", help="the pale underside's prototype")
    a = ap.parse_args()
    art = ROOT / "pets" / a.pet / "art"
    roster = {c["slug"]: c for c in json.loads((ROOT / "roster/roster.json").read_text())["creatures"]}
    col = roster.get(a.pet, {}).get("colours", {})
    skin, cloak = hexrgb(a.skin or col["dominant"]), hexrgb(a.cloak or col["secondary"])
    glow = hexrgb(a.glow or col["glow"])

    def frame(n):
        im = np.asarray(Image.open(art / "frames" / f"{n}.png").convert("RGBA"))
        return im[..., :3].astype(float), im[..., 3] > 0

    neutral, alpha = frame("neutral")
    eye_mask = np.asarray(Image.open(art / "masks/eyes.png").convert("L")) > 127
    mouth_mask = np.asarray(Image.open(art / "masks/mouth.png").convert("L")) > 127
    ys, xs = np.nonzero(alpha)
    eyes = blobs(eye_mask)

    body = neutral[alpha & ~eye_mask & ~mouth_mask & (value(neutral) >= INK)]
    body_c = kmeans(body, 14)

    protos = {"skin": skin, "cloak": cloak, "belly": hexrgb(a.belly)}

    def body_region(c):
        return min(protos, key=lambda r: ((c - protos[r]) ** 2).sum())

    refs = [(c, body_region(c)) for c in body_c]
    eye_c = far(kmeans(neutral[alpha & eye_mask], 6), body_c, 40)
    refs += [(c, "eye") for c in eye_c]
    mouths = np.concatenate([frame(n)[0][alpha & mouth_mask] for n in ("meow", "blep", "yawn")])
    refs += [(c, "mouth") for c in far(kmeans(mouths, 8), list(body_c) + list(eye_c), 45)]
    power, _ = frame("foresee")
    lit = power[alpha & eye_mask & (value(power) > 0.55)]
    glow_c = far(kmeans(lit, 5), list(body_c) + list(eye_c), 40) if len(lit) else np.zeros((0, 3))
    refs += [(c, "glow") for c in (glow_c if len(glow_c) else [glow])]

    egg, egg_alpha = frame("egg")
    egg_c = kmeans(egg[egg_alpha & (value(egg) >= INK)], 10)
    egg_refs = []
    for c in egg_c:
        d_glow = np.abs(c - glow).sum()
        d_body = min(np.abs(c - b).sum() for b, r in refs if r in ("skin", "cloak"))
        egg_refs.append((c, "glow" if d_glow < 90 and value(c) > 0.6 else "skin" if d_body < 60 else "shell"))

    out = {
        "grid": [int(xs.min()), int(ys.min())],
        "body": [int(xs.max()) + 1 - int(xs.min()), int(ys.max()) + 1 - int(ys.min())],
        "eyeballs": [[x, y] for x, y, _ in eyes],
        "eyeball_r": round(sum(r for *_, r in eyes) / len(eyes)),
        "prototypes": {r: [int(v) for v in c] for r, c in protos.items()},
        "refs": [{"rgb": [int(v) for v in c], "region": r} for c, r in refs],
        "egg_refs": [{"rgb": [int(v) for v in c], "region": r} for c, r in egg_refs],
    }
    (art / "pack.json").write_text(json.dumps(out, indent=1) + "\n", newline="\n")

    rows = [("refs", out["refs"]), ("egg", out["egg_refs"])]
    sheet = Image.new("RGB", (40 * max(len(r) for _, r in rows) + 60, 70 * len(rows)), (30, 30, 30))
    d = ImageDraw.Draw(sheet)
    for i, (name, rs) in enumerate(rows):
        d.text((4, i * 70 + 4), name, fill=(255, 255, 0))
        for j, r in enumerate(rs):
            x = 60 + 40 * j
            d.rectangle([x, i * 70 + 4, x + 34, i * 70 + 38], fill=tuple(r["rgb"]))
            d.text((x, i * 70 + 44), r["region"][:5], fill=(230, 230, 230))
    sheet.save(art.parent / "pack-refs.png")
    print(a.pet, "grid", out["grid"], "body", out["body"], "eyes", out["eyeballs"], out["eyeball_r"],
          "refs", len(refs), "egg refs", len(egg_refs), "->", art / "pack.json")


if __name__ == "__main__":
    main()
