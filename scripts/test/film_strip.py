"""Film strips and a side-by-side GIF from the sim's recorded frames.

    python3 scripts/test/film_strip.py out.png label=dir [label=dir ...] [--cols N] [--every K] [--scale S] [--gif out.gif] [--fps F]

Each `label=dir` is one row: the BMPs in dir (sim --record), every K-th, N to
a row, at S x. With --gif, the rows play side by side at F frames a second.
"""
import glob
import os
import sys

from PIL import Image, ImageDraw


def frames(d, every):
    return [Image.open(f).convert("RGB") for f in sorted(glob.glob(os.path.join(d, "*.bmp")))[::every]]


def main(argv):
    out, rows, cols, every, scale, gif, fps = argv[0], [], 10, 1, 1.0, None, 50
    i = 1
    while i < len(argv):
        a = argv[i]
        if a == "--cols": cols = int(argv[i + 1]); i += 2
        elif a == "--every": every = int(argv[i + 1]); i += 2
        elif a == "--scale": scale = float(argv[i + 1]); i += 2
        elif a == "--gif": gif = argv[i + 1]; i += 2
        elif a == "--fps": fps = int(argv[i + 1]); i += 2
        else: rows.append(a.split("=", 1)); i += 1
    shot = [(label, frames(d, every)) for label, d in rows]
    w = int(240 * scale)
    head = 18
    n = min(cols, max(len(f) for _, f in shot))
    sheet = Image.new("RGB", (n * (w + 4) + 4, len(shot) * (w + head + 4) + 4), (24, 24, 24))
    draw = ImageDraw.Draw(sheet)
    for r, (label, fs) in enumerate(shot):
        y = 4 + r * (w + head + 4)
        draw.text((6, y + 2), label, fill=(235, 235, 235))
        for c, f in enumerate(fs[:n]):
            sheet.paste(f.resize((w, w), Image.LANCZOS if scale < 1 else Image.NEAREST), (4 + c * (w + 4), y + head))
    sheet.save(out)
    print("wrote", out)
    if gif:
        length = max(len(f) for _, f in shot)
        clip = []
        for k in range(length):
            pane = Image.new("RGB", (len(shot) * (w + 4) + 4, w + head + 8), (24, 24, 24))
            d = ImageDraw.Draw(pane)
            for r, (label, fs) in enumerate(shot):
                d.text((6 + r * (w + 4), 4), label, fill=(235, 235, 235))
                pane.paste(fs[min(k, len(fs) - 1)].resize((w, w), Image.NEAREST), (4 + r * (w + 4), head + 4))
            clip.append(pane)
        clip[0].save(gif, save_all=True, append_images=clip[1:], duration=int(1000 / fps), loop=0)
        print("wrote", gif)


if __name__ == "__main__":
    main(sys.argv[1:])
