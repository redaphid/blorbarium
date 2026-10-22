"""The carry film's frames (test/test_carry_film) as film strips.

    python3 scripts/test/carry_sheet.py outputs/carry-react

Reads <out>/ppm/<film>_<species>_<nn>.ppm and writes <out>/<film>.png (one
row per species, up to 10 frames across, evenly picked through the
reaction) and <out>/all.png (every film's first species, one row each).
"""
import collections
import glob
import os
import sys

from PIL import Image, ImageDraw

out = sys.argv[1] if len(sys.argv) > 1 else "outputs/carry-react"
COLS, W, HEAD = 10, 240, 18

rows = collections.OrderedDict()
for path in sorted(glob.glob(os.path.join(out, "ppm", "*.ppm"))):
    name = os.path.basename(path)[:-4]
    film_species, _ = name.rsplit("_", 1)
    rows.setdefault(film_species, []).append(path)


def pick(paths):
    if len(paths) <= COLS:
        return paths
    return [paths[round(i * (len(paths) - 1) / (COLS - 1))] for i in range(COLS)]


def sheet(labelled, path):
    n = max(len(p) for _, p in labelled)
    img = Image.new("RGB", (n * (W + 4) + 4, len(labelled) * (W + HEAD + 4) + 4), (24, 24, 24))
    draw = ImageDraw.Draw(img)
    for r, (label, paths) in enumerate(labelled):
        y = 4 + r * (W + HEAD + 4)
        draw.text((6, y + 2), label, fill=(235, 235, 235))
        for c, p in enumerate(paths):
            img.paste(Image.open(p).convert("RGB"), (4 + c * (W + 4), y + HEAD))
    img.save(path)
    print(path)


films = collections.OrderedDict()
for key, paths in rows.items():
    film = key
    for sp in ("grungo", "mossby", "puffkin", "gilly", "mothwick", "mudge", "fizz", "hushwing", "tinker", "octopus"):
        if key.endswith("_" + sp):
            film = key[: -len(sp) - 1]
            break
    films.setdefault(film, []).append((key, pick(paths)))
for film, labelled in films.items():
    sheet(labelled, os.path.join(out, film + ".png"))
sheet([labelled[0] for labelled in films.values()], os.path.join(out, "all.png"))
