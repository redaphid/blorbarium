"""Film strips of pets left on a desk, from test_idle_day's Film test.

    IDLE_FILM_OUT=<dir> pio test -e native -f test_idle_day   (frames: <species>_<n>_<doing>.ppm)
    python3 scripts/test/idle_strips.py <dir> <out.png> [--cols N] [--scale S] [--title T]

One block per species: every frame in order, N to a row, each labelled with
what STATE said he was doing (action=), so the variety reads at a glance.
"""
import glob
import os
import re
import sys
from collections import OrderedDict

from PIL import Image, ImageDraw


def main(argv):
    src, out, cols, scale, title = argv[0], argv[1], 12, 0.5, ""
    i = 2
    while i < len(argv):
        if argv[i] == "--cols": cols = int(argv[i + 1]); i += 2
        elif argv[i] == "--scale": scale = float(argv[i + 1]); i += 2
        elif argv[i] == "--title": title = argv[i + 1]; i += 2
        else: raise SystemExit("unknown " + argv[i])
    by = OrderedDict()
    for f in sorted(glob.glob(os.path.join(src, "*.ppm"))):
        m = re.match(r"([a-z]+)_(\d+)_(.+)\.ppm$", os.path.basename(f))
        if m: by.setdefault(m.group(1), []).append((int(m.group(2)), m.group(3), f))
    w = int(240 * scale)
    lab, head = 14, 18
    rows = sum((len(v) + cols - 1) // cols for v in by.values())
    sheet = Image.new("RGB", (cols * (w + 4) + 4, 22 + len(by) * head + rows * (w + lab + 4) + 4), (24, 24, 24))
    d = ImageDraw.Draw(sheet)
    d.text((6, 4), title, fill=(255, 255, 255))
    y = 22
    for sp, frames in by.items():
        frames.sort()
        kinds = len(set(k for _, k, _ in frames if k not in ("asleep", "egg")))
        d.text((6, y + 2), "%s: %d frames, %d different things awake" % (sp, len(frames), kinds), fill=(235, 235, 235))
        y += head
        for n, (_, what, f) in enumerate(frames):
            r, c = divmod(n, cols)
            x, yy = 4 + c * (w + 4), y + r * (w + lab + 4)
            sheet.paste(Image.open(f).convert("RGB").resize((w, w), Image.LANCZOS), (x, yy))
            d.text((x + 2, yy + w), what, fill=(255, 220, 120) if what != "asleep" else (120, 140, 255))
        y += ((len(frames) + cols - 1) // cols) * (w + lab + 4)
    sheet.save(out)
    print("wrote", out)


if __name__ == "__main__":
    main(sys.argv[1:])
