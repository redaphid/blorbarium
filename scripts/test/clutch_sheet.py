"""The clutch film's frames (test/test_clutch_film) as PNGs and film strips.

    python3 scripts/test/clutch_sheet.py outputs/egg-ux

scripts/test/clutch_film.sh runs it after filming ppm_before/ (the commit
before the egg picker, driven the old way) and ppm_after/. Writes
<out>/after/<frame>.png and <out>/before/<frame>.png (2x), and for each film
(grungo, puffkin) strip_<film>_before.png and strip_<film>_after.png: every
frame of it in order, labelled, wrapped at eight a row. The two films are the
same death and clutch; only the choosing differs, as the gestures did.
"""
import glob
import os
import sys

from PIL import Image, ImageDraw

out = sys.argv[1] if len(sys.argv) > 1 else "outputs/egg-ux"
PER_ROW = 8


def frames(kind):
    return sorted(os.path.basename(p)[:-4] for p in glob.glob(os.path.join(out, "ppm_" + kind, "*.ppm")))


def load(kind, name):
    return Image.open(os.path.join(out, "ppm_" + kind, name + ".ppm")).convert("RGB")


def strip(kind, film, names):
    w, label = 240, 16
    rows = (len(names) + PER_ROW - 1) // PER_ROW
    img = Image.new("RGB", (w * min(PER_ROW, len(names)), rows * (w + label)), (24, 24, 24))
    d = ImageDraw.Draw(img)
    for i, n in enumerate(names):
        x, y = (i % PER_ROW) * w, (i // PER_ROW) * (w + label)
        d.text((x + 4, y + 2), "%s %s" % (kind, n[len(film) + 1:]), fill=(220, 220, 220))
        img.paste(load(kind, n), (x, y + label))
    img.save(os.path.join(out, "strip_%s_%s.png" % (film, kind)))


total = 0
for kind in ("before", "after"):
    names = frames(kind)
    if not names:
        continue
    os.makedirs(os.path.join(out, kind), exist_ok=True)
    for n in names:
        load(kind, n).resize((480, 480), Image.NEAREST).save(os.path.join(out, kind, n + ".png"))
    for film in sorted({n.split("_")[0] for n in names}):
        strip(kind, film, [n for n in names if n.split("_")[0] == film])
    total += len(names)
print("wrote %d frames and their strips to %s" % (total, out))
