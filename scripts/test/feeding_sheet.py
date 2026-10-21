"""The feeding film's frames (test/test_feeding_film) as PNGs and contact sheets.

    python3 scripts/test/feeding_sheet.py outputs/feeding

scripts/test/feeding_film.sh runs it after filming both ppm_before/ and
ppm_after/. Writes outputs/feeding/<name>.png (after, 2x), before/<name>.png,
and sheet_<film>.png (grungo, puffkin, mudge_hatchling, refused): before
above after, frame by frame.
"""
import glob
import os
import sys

from PIL import Image, ImageDraw

out = sys.argv[1] if len(sys.argv) > 1 else "outputs/feeding"
os.makedirs(os.path.join(out, "before"), exist_ok=True)


def load(kind, name):
    return Image.open(os.path.join(out, "ppm_" + kind, name + ".ppm")).convert("RGB")


def split(name):
    """('puffkin', 3) for puffkin_3_landed; (name, 0) for a frame of no film."""
    parts = name.split("_")
    for i, p in enumerate(parts):
        if p.isdigit():
            return "_".join(parts[:i]), int(p)
    return "refused", 0


names = sorted((os.path.basename(p)[:-4] for p in glob.glob(os.path.join(out, "ppm_after", "*.ppm"))),
               key=lambda n: (split(n), n))
for n in names:
    load("after", n).resize((480, 480), Image.NEAREST).save(os.path.join(out, n + ".png"))
    if os.path.exists(os.path.join(out, "ppm_before", n + ".ppm")):
        load("before", n).resize((480, 480), Image.NEAREST).save(os.path.join(out, "before", n + ".png"))


def sheet(path, frames):
    w, label = 240, 16
    img = Image.new("RGB", (w * len(frames), 2 * (w + label)), (24, 24, 24))
    d = ImageDraw.Draw(img)
    for i, n in enumerate(frames):
        for row, kind in enumerate(("before", "after")):
            y = row * (w + label)
            d.text((i * w + 4, y + 2), "%s: %s" % (kind, n), fill=(220, 220, 220))
            img.paste(load(kind, n), (i * w, y + label))
    img.save(path)


for film in sorted({split(n)[0] for n in names}):
    sheet(os.path.join(out, "sheet_%s.png" % film), [n for n in names if split(n)[0] == film])
print("wrote %d frames and their sheets to %s" % (len(names), out))
