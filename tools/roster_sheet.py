#!/usr/bin/env python3
"""The roster's contact sheet: one row per creature in roster/roster.json, its ten
faces and its egg from pets/<slug>/art/frames/, on the dark screen colour.

    python3 tools/roster_sheet.py              # -> roster/contact-sheet.png

A creature with no art yet gets a labelled empty row, so the sheet says what is missing.
"""
import json
from pathlib import Path

from PIL import Image, ImageDraw

ROOT = Path(__file__).resolve().parent.parent
FACES = [("neutral", "neutral"), ("happy", "happy"), ("alarmed", "alarmed"), ("annoyed", "annoyed"),
         ("croak", "meow"), ("blep", "blep"), ("power", "foresee"), ("sleepy", "sleepy"),
         ("asleep", "sleep"), ("yawn", "yawn"), ("egg", "egg")]
CELL, LABEL, BG = 150, 150, (24, 26, 30)


def tile(path):
    im = Image.open(path).convert("RGBA")
    im = im.crop(im.getchannel("A").getbbox())
    s = (CELL - 10) / max(im.size)
    im = im.resize((max(1, round(im.width * s)), max(1, round(im.height * s))), Image.LANCZOS)
    out = Image.new("RGBA", (CELL, CELL), BG + (255,))
    out.alpha_composite(im, ((CELL - im.width) // 2, CELL - 5 - im.height))
    return out


def main():
    roster = json.loads((ROOT / "roster/roster.json").read_text())["creatures"]
    sheet = Image.new("RGB", (LABEL + CELL * len(FACES), 24 + CELL * len(roster)), BG)
    d = ImageDraw.Draw(sheet)
    for j, (label, _) in enumerate(FACES):
        d.text((LABEL + j * CELL + 6, 6), label, fill=(200, 200, 200))
    for i, c in enumerate(roster):
        y = 24 + i * CELL
        d.text((6, y + 6), f"{c['id']} {c['name']}", fill=(255, 220, 90))
        d.text((6, y + 24), c["species"], fill=(170, 170, 170))
        d.text((6, y + 42), c["power"]["name"], fill=(110, 230, 210))
        frames = ROOT / "pets" / c["slug"] / "art" / "frames"
        for j, (_, art) in enumerate(FACES):
            p = frames / f"{art}.png"
            if p.exists():
                sheet.paste(tile(p).convert("RGB"), (LABEL + j * CELL, y))
            else:
                d.text((LABEL + j * CELL + 40, y + 65), "missing", fill=(220, 80, 80))
    out = ROOT / "roster/contact-sheet.png"
    sheet.save(out)
    print("sheet ->", out, sheet.size)


if __name__ == "__main__":
    main()
