"""The feeding frames (test_feeding's Film.DumpWhenAsked) as PNGs and contact sheets.

    FEEDING_DUMP=outputs/feeding/ppm_after .pio/build/native/program --gtest_filter='Film.*'
    FEEDING_PLAIN=1 FEEDING_DUMP=outputs/feeding/ppm_before .pio/build/native/program --gtest_filter='Film.*'
    python3 scripts/test/feeding_sheet.py outputs/feeding

(after `pio test -e native -f test_feeding`, so the program is test_feeding's.)
Writes outputs/feeding/<name>.png (after, 2x), before/<name>.png, and
sheet_fed.png / sheet_refused.png: before above after, frame by frame.
"""
import glob
import os
import sys

from PIL import Image, ImageDraw

out = sys.argv[1] if len(sys.argv) > 1 else "outputs/feeding"
os.makedirs(os.path.join(out, "before"), exist_ok=True)


def load(kind, name):
    return Image.open(os.path.join(out, "ppm_" + kind, name + ".ppm")).convert("RGB")


names = sorted((os.path.basename(p)[:-4] for p in glob.glob(os.path.join(out, "ppm_after", "*.ppm"))),
               key=lambda n: (n.split("_")[0], int(n.split("_")[1]) if n.split("_")[1].isdigit() else 0, n))
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


sheet(os.path.join(out, "sheet_fed.png"), [n for n in names if n.startswith("fed_")])
sheet(os.path.join(out, "sheet_refused.png"), [n for n in names if not n.startswith("fed_")])
print("wrote %d frames and two sheets to %s" % (len(names), out))
