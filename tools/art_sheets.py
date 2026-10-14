#!/usr/bin/env python3
"""Contact sheets from whatever ComfyUI has rendered under blorbarium/.

    python tools/art_sheets.py                       # every folder of images -> <folder>/_sheet.png
    python tools/art_sheets.py fuzz/run1/gilly/sad   # one folder (relative to the output root)
    python tools/art_sheets.py --root D:/sync/Comfy/blorbarium --cell 256

A sheet is a grid of the folder's images (recursively, newest last), each labelled with
its file or folder name, so fuzz settings in the folder names stay readable.
"""
import argparse
from pathlib import Path

from PIL import Image, ImageDraw


def sheet(folder, cell, cols):
    files = sorted((p for p in folder.rglob("*.png") if p.name != "_sheet.png"), key=lambda p: p.stat().st_mtime)
    if not files:
        return None
    rows = -(-len(files) // cols)
    out = Image.new("RGB", (cols * cell, rows * (cell + 28)), (28, 30, 34))
    d = ImageDraw.Draw(out)
    for i, f in enumerate(files):
        im = Image.open(f).convert("RGBA")
        bg = Image.new("RGBA", im.size, (28, 30, 34, 255))
        bg.alpha_composite(im)
        x, y = (i % cols) * cell, (i // cols) * (cell + 28)
        out.paste(bg.convert("RGB").resize((cell, cell), Image.LANCZOS), (x, y))
        name = str(f.relative_to(folder).parent) if f.parent != folder else f.stem
        d.text((x + 3, y + cell + 2), name[:42], fill=(230, 230, 120))
        d.text((x + 3, y + cell + 14), name[42:84], fill=(230, 230, 120))
    dst = folder / "_sheet.png"
    out.save(dst)
    return dst, len(files)


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("folders", nargs="*")
    ap.add_argument("--root", type=Path, default=Path("D:/sync/Comfy/blorbarium"))
    ap.add_argument("--cell", type=int, default=200)
    ap.add_argument("--cols", type=int, default=8)
    a = ap.parse_args()
    if a.folders:
        targets = [a.root / f for f in a.folders]
    else:   # the deepest folders that hold images, one sheet each
        targets = sorted({p.parent for p in a.root.rglob("*.png") if p.name != "_sheet.png"})
        if any("fuzz" in str(t) for t in targets):   # fuzz runs: one sheet per species/expression, not per setting
            targets = sorted({t.parent if "fuzz" in str(t) else t for t in targets})
    for t in targets:
        r = sheet(t, a.cell, a.cols)
        if r:
            print(f"{r[0]}  ({r[1]} images)")


if __name__ == "__main__":
    main()
