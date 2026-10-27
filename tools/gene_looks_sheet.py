"""PNGs and a contact sheet from tools/gene_looks.cpp's PPMs (gene-looks).

    ~/Worktrees/blorbarium/pio run -e genelooks
    python3 tools/gene_looks_sheet.py [outputs/frames] [species,...|all]

Runs .pio/build/genelooks/program for the adult and the child of each species,
writes each pet as <dir>/gene_looks_<species>_<stage>_<who>.png and lays them
out as <dir>/gene_looks_contact.png: a row per species and stage, founders,
their clutch and the friend cross, grandchildren, then one line's 5th, 10th
and 20th generation.
"""
import csv
import os
import subprocess
import sys

from PIL import Image, ImageDraw

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
OUT = sys.argv[1] if len(sys.argv) > 1 else os.path.join(ROOT, "outputs/frames")
WHICH = sys.argv[2] if len(sys.argv) > 2 else "grungo,hushwing,puffkin"
PROGRAM = os.path.join(ROOT, ".pio/build/genelooks/program")
CELL, LABEL, GAP = 160, 16, 12

os.makedirs(OUT, exist_ok=True)
table = os.path.join(OUT, "gene_looks.csv")
if os.path.exists(table):
    os.remove(table)
for stage in ("adult", "child"):
    subprocess.run([PROGRAM, OUT, WHICH, stage], check=True)

rows = {}
with open(table) as f:
    for r in csv.reader(f):
        name, species, stage, who = r[0], r[1], r[2], r[3]
        ppm = os.path.join(OUT, name + ".ppm")
        img = Image.open(ppm).convert("RGB")
        img.save(os.path.join(OUT, name + ".png"))
        os.remove(ppm)
        rows.setdefault((species, stage), []).append((who, img))

width = max(len(v) for v in rows.values()) * (CELL + 4) + 120
sheet = Image.new("RGB", (width, len(rows) * (CELL + LABEL + GAP) + 8), (24, 24, 28))
draw = ImageDraw.Draw(sheet)
y = 4
for (species, stage), pets in rows.items():
    draw.text((6, y + CELL // 2), f"{species}\n{stage}", fill=(230, 230, 230))
    x = 116
    for who, img in pets:
        sheet.paste(img.resize((CELL, CELL), Image.NEAREST), (x, y))
        draw.text((x + 4, y + CELL + 2), who, fill=(200, 200, 200))
        x += CELL + 4
    y += CELL + LABEL + GAP
sheet.save(os.path.join(OUT, "gene_looks_contact.png"))
print(os.path.join(OUT, "gene_looks_contact.png"))
