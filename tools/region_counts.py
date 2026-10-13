#!/usr/bin/env python3
"""How many device pixels each recolour region covers in a pet's pack, summed over
every frame (body, faces, eggs, remains), decoded from the generated header. A region
with a few pixels is one whose genes recolour nothing anyone can see.

    python3 tools/region_counts.py mossby fizz       # or no names for every pack
"""
import sys
from pathlib import Path

import numpy as np

import sprite_pack as sp

NAMES = {i: n for n, i in sp.REGION.items()}


def counts(pet):
    pal, frames, _ = sp.parse_header((sp.ROOT / f"pets/{pet}/{pet}_pack.h").read_text())
    regs = np.array([e[3] for e in pal])
    out = {n: 0 for n in sp.REGION}
    for f in frames.values():
        r = regs[f["idx"][f["idx"] > 0]]
        for i, n in NAMES.items():
            out[n] += int((r == i).sum())
    return out


if __name__ == "__main__":
    pets = sys.argv[1:] or sorted(p.name for p in (sp.ROOT / "pets").iterdir() if (p / f"{p.name}_pack.h").exists())
    for pet in pets:
        c = counts(pet)
        print(f"{pet:9}", "  ".join(f"{n} {v:6}" for n, v in c.items()), " sparse:", [n for n, v in c.items() if v < 200])
