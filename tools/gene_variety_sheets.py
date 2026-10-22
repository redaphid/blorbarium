"""Sheets, strips and tables from tools/gene_variety.cpp's output (#60).

    .pio/build/genevariety/program looks     outputs/gene-variety
    .pio/build/genevariety/program behaviour outputs/gene-variety 24 all 4
    .pio/build/genevariety/program lineage   outputs/gene-variety 40 3 24 4
    python3 tools/gene_variety_sheets.py [outputs/gene-variety]

Writes <dir>/looks-<species>.png (each look byte swept), <dir>/founders-<species>.png
(random boards), <dir>/lineage-<species>.png (founder -> gen 5 -> 10 -> 20 -> 40
for each line, picked at random and picked most different) and prints the
markdown tables docs/notes/gene-variety.md quotes.
"""
import csv
import math
import os
import statistics
import sys
from collections import defaultdict

from PIL import Image, ImageDraw

CELL = 150
LABEL_W = 210
HEAD = 22


def cell(path):
    return Image.open(path).convert("RGB").resize((CELL, CELL), Image.LANCZOS)


def looks_sheets(d):
    rows = list(csv.DictReader(open(os.path.join(d, "looks.csv"))))
    by_sheet = defaultdict(list)
    for r in rows:
        by_sheet[r["sheet"]].append(r)
    for sheet, cells in by_sheet.items():
        species, kind = sheet.rsplit("_", 1)
        nrow = max(int(c["row"]) for c in cells) + 1
        ncol = max(int(c["col"]) for c in cells) + 1
        img = Image.new("RGB", (LABEL_W + ncol * CELL, HEAD + nrow * (CELL + 14)), (24, 24, 28))
        ink = ImageDraw.Draw(img)
        title = (f"{species}: each look byte swept on one founder (adult)" if kind == "sweep"
                 else f"{species}: 24 boards' founders (adult)")
        ink.text((6, 4), title, fill=(255, 255, 255))
        for c in cells:
            r, k = int(c["row"]), int(c["col"])
            x, y = LABEL_W + k * CELL, HEAD + r * (CELL + 14)
            img.paste(cell(os.path.join(d, c["file"])), (x, y))
            if kind == "sweep":
                if k == 0:
                    ink.text((6, y + CELL // 2 - 6), c["label"], fill=(255, 255, 255))
                ink.text((x + 4, y + CELL), f"{c['value']}  d={float(c['dist_from_founder']):.1f}", fill=(200, 200, 200))
        name = "looks" if kind == "sweep" else "founders"
        img.save(os.path.join(d, f"{name}-{species}.png"))


def lineage_strips(d):
    rows = list(csv.DictReader(open(os.path.join(d, "strip.csv"))))
    by_species = defaultdict(list)
    for r in rows:
        by_species[r["species"]].append(r)
    for species, cells in by_species.items():
        gens = sorted({int(c["gen"]) for c in cells})
        lines = sorted({(c["pick"], int(c["line"])) for c in cells}, key=lambda t: (t[0] != "random", t[1]))
        img = Image.new("RGB", (LABEL_W + len(gens) * CELL, HEAD + len(lines) * CELL), (24, 24, 28))
        ink = ImageDraw.Draw(img)
        ink.text((6, 4), f"{species}: one line per row, founder -> generation " + " -> ".join(map(str, gens[1:])),
                 fill=(255, 255, 255))
        for i, g in enumerate(gens):
            ink.text((LABEL_W + i * CELL + 4, HEAD - 12), f"gen {g}", fill=(220, 220, 220))
        for row, (pick, line) in enumerate(lines):
            y = HEAD + row * CELL
            ink.text((6, y + CELL // 2 - 6), f"line {line}, eggs picked {'at random' if pick == 'random' else 'most different'}",
                     fill=(255, 255, 255))
            for c in cells:
                if c["pick"] == pick and int(c["line"]) == line:
                    img.paste(cell(os.path.join(d, c["file"])), (LABEL_W + gens.index(int(c["gen"])) * CELL, y))
        img.save(os.path.join(d, f"lineage-{species}.png"))


def fnum(x):
    return float(x) if x not in ("", None) else 0.0


def val(row, metric):
    """A metric, or its negative for a name ending _neg (the gene should lower it)."""
    return -fnum(row[metric[:-4]]) if metric.endswith("_neg") else fnum(row[metric])


AXES = None


def behaviour_tables(d):
    path = os.path.join(d, "behaviour.csv")
    if not os.path.exists(path):
        return
    rows = list(csv.DictReader(open(path)))
    implied = {}
    for a in csv.DictReader(open(os.path.join(d, "axes.csv"))):
        implied[a["axis"]] = (a["gene"], a["metric"])
    species = sorted({r["species"] for r in rows})
    base = defaultdict(list)
    for r in rows:
        if r["kind"] == "base":
            base[r["species"]].append(r)
    axis_rows = defaultdict(dict)
    for r in rows:
        if r["kind"] == "axis":
            axis_rows[(r["species"], r["axis"])][int(r["level"])] = r
    metrics = [k for k in rows[0].keys() if k not in ("species", "kind", "axis", "level", "byte", "kid", "top_idles",
                                                     "hunger_curve", "sleep_curve")]
    # the noise floor: the same founder lived with 4 kids
    def sd(sp, m):
        v = [val(r, m) for r in base[sp]]
        return statistics.pstdev(v) if len(v) > 1 else 0.0

    out = []
    out.append("| gene (byte) | implied metric | " + " | ".join(species) + " | verdict |")
    out.append("|---|---|" + "---|" * len(species) + "---|")
    summary = {}
    for name, (gene, metric) in implied.items():
        cells, ok, inv, weak = [], 0, 0, 0
        for sp in species:
            pair = axis_rows.get((sp, name), {})
            if 0 not in pair or 1 not in pair:
                cells.append("-")
                continue
            lo, hi = val(pair[0], metric), val(pair[1], metric)
            delta = hi - lo
            noise = max(sd(sp, metric), 1e-9)
            z = delta / noise if noise > 1e-6 else (math.inf if delta > 0 else -math.inf if delta < 0 else 0)
            big = abs(delta) >= max(0.1 * max(abs(lo), abs(hi)), 0.005)
            mark = "ok" if z >= 2 and big else "INV" if z <= -2 and big else "~"
            ok += mark == "ok"
            inv += mark == "INV"
            weak += mark == "~"
            cells.append(f"{lo:.2f}->{hi:.2f} {mark}")
        verdict = "moves as named" if ok >= len(species) * 0.7 else "INVERTED" if inv > ok else \
            "weak or none" if weak >= len(species) * 0.7 else "mixed"
        summary[name] = verdict
        out.append(f"| {name} ({gene}) | {metric} | " + " | ".join(cells) + f" | {verdict} |")
    print("\n### Behaviour: each gene at its low and high end (one founder per species, 24 h)\n")
    print("\n".join(out))
    # side effects: which metrics move most for each axis (z over all species)
    print("\n### What else each gene moves (largest effects, mean z over species)\n")
    print("| gene | biggest movers (z = (high - low) / noise sd) |")
    print("|---|---|")
    for name in implied:
        zs = defaultdict(list)
        for sp in species:
            pair = axis_rows.get((sp, name), {})
            if 0 not in pair or 1 not in pair:
                continue
            for m in metrics:
                noise = sd(sp, m)
                delta = fnum(pair[1][m]) - fnum(pair[0][m])
                if noise > 1e-6:
                    zs[m].append(max(-50, min(50, delta / noise)))
                elif abs(delta) > 1e-6:
                    zs[m].append(50 if delta > 0 else -50)
        top = sorted(((statistics.mean(v), m) for m, v in zs.items() if v), key=lambda t: -abs(t[0]))[:5]
        print(f"| {name} | " + ", ".join(f"{m} {z:+.1f}" for z, m in top if abs(z) >= 2) + " |")
    # species and random genomes side by side
    print("\n### The founders, species by species (base, mean of 4 kids)\n")
    keys = ["awake_day_pct", "asleep_night_pct", "bedtime_h", "wake_h", "naps", "hunger_mean", "feeds", "fear_mean",
            "hops", "boredom_mean", "play_pct", "kinds", "thoughts", "duel_pounce", "duel_guard", "duel_trick", "duel_heavy"]
    print("| species | " + " | ".join(keys) + " | top idles |")
    print("|---|" + "---|" * (len(keys) + 1))
    for sp in species:
        b = base[sp]
        print(f"| {sp} | " + " | ".join(f"{statistics.mean(fnum(r[k]) for r in b):.2f}" for k in keys)
              + f" | {b[0]['top_idles']} |")
    print("\n### Random genomes (every behaviour axis rolled), spread within a species\n")
    print("| species | " + " | ".join(keys) + " |")
    print("|---|" + "---|" * len(keys))
    for sp in species:
        rs = [r for r in rows if r["species"] == sp and r["kind"] == "random"]
        if not rs:
            continue
        print(f"| {sp} | " + " | ".join(f"{min(fnum(r[k]) for r in rs):.1f}..{max(fnum(r[k]) for r in rs):.1f}"
                                        for k in keys) + " |")


def looks_table(d):
    rows = [r for r in csv.DictReader(open(os.path.join(d, "looks.csv"))) if r["sheet"].endswith("_sweep")]
    species = sorted({r["sheet"].rsplit("_", 1)[0] for r in rows})
    axes = []
    for r in rows:
        if r["label"] not in axes:
            axes.append(r["label"])
    print("\n### Looks: the most a look byte moves the founder (look distance 0..100; under ~2 a kid cannot see it)\n")
    print("| look byte | " + " | ".join(species) + " |")
    print("|---|" + "---|" * len(species))
    for ax in axes:
        cells = []
        for sp in species:
            v = [fnum(r["dist_from_founder"]) for r in rows if r["label"] == ax and r["sheet"].startswith(sp + "_")]
            cells.append(f"{max(v):.1f}" if v else "-")
        print(f"| {ax} | " + " | ".join(cells) + " |")


def lineage_tables(d):
    path = os.path.join(d, "lineage.csv")
    if not os.path.exists(path):
        return
    rows = list(csv.DictReader(open(path)))
    print("\n### Drift from the founder by generation (mean over lines; look distance 0..100)\n")
    gens = sorted({int(r["gen"]) for r in rows})
    show = [g for g in gens if g in (1, 2, 3, 5, 10, 20, 30, 40)]
    print("| species | pick | " + " | ".join(f"gen {g}" for g in show) + " |")
    print("|---|---|" + "---|" * len(show))
    for sp in sorted({r["species"] for r in rows}):
        for pick in ("random", "different"):
            cells = []
            for g in show:
                v = [fnum(r["look_from_founder"]) for r in rows if r["species"] == sp and r["pick"] == pick and int(r["gen"]) == g]
                cells.append(f"{statistics.mean(v):.1f}" if v else "-")
            print(f"| {sp} | {pick} | " + " | ".join(cells) + " |")
    print("\n### Mutation per egg (mean over every egg kept, all species)\n")
    keys = ["point", "wild", "dup", "del", "wake", "sleep", "heir", "look_ops", "mind_ops", "look_from_parent", "genes",
            "bytes_from_founder"]
    print("| pick | " + " | ".join(keys) + " |")
    print("|---|" + "---|" * len(keys))
    for pick in ("random", "different"):
        rs = [r for r in rows if r["pick"] == pick]
        print(f"| {pick} | " + " | ".join(f"{statistics.mean(fnum(r[k]) for r in rs):.2f}" for k in keys) + " |")
    kin = list(csv.DictReader(open(os.path.join(d, "kinship.csv"))))
    print("\n### Kinship: look distance between pairs (0..100)\n")
    pairs = ["parent_child", "siblings", "strangers", "lines_at_end_random", "lines_at_end_different"]
    print("| species | " + " | ".join(pairs) + " |")
    print("|---|" + "---|" * len(pairs))
    for sp in sorted({r["species"] for r in kin}):
        cells = []
        for p in pairs:
            v = [fnum(r["look"]) for r in kin if r["species"] == sp and r["pair"] == p]
            cells.append(f"{statistics.mean(v):.1f}" if v else "-")
        print(f"| {sp} | " + " | ".join(cells) + " |")
    cells = []
    for p in pairs:
        v = [fnum(r["look"]) for r in kin if r["pair"] == p]
        cells.append(f"{statistics.mean(v):.1f}" if v else "-")
    print("| **all** | " + " | ".join(cells) + " |")
    lb = os.path.join(d, "lineage_behaviour.csv")
    if os.path.exists(lb):
        b = list(csv.DictReader(open(lb)))
        keys = ["awake_day_pct", "asleep_night_pct", "hunger_mean", "feeds", "fear_mean", "hops", "boredom_mean",
                "play_pct", "duel_pounce", "duel_guard", "duel_trick"]
        print("\n### Behaviour along a line (line 0, eggs picked at random)\n")
        print("| species | gen | " + " | ".join(keys) + " | top idles |")
        print("|---|---|" + "---|" * (len(keys) + 1))
        for r in sorted(b, key=lambda r: (r["species"], int(r["line"]), int(r["gen"]))):
            if r["line"] != "0":
                continue
            print(f"| {r['species']} | {r['gen']} | " + " | ".join(f"{fnum(r[k]):.2f}" for k in keys)
                  + f" | {r['top_idles']} |")


def main(d):
    if os.path.exists(os.path.join(d, "looks.csv")):
        looks_sheets(d)
    if os.path.exists(os.path.join(d, "strip.csv")):
        lineage_strips(d)
    if os.path.exists(os.path.join(d, "looks.csv")):
        looks_table(d)
    behaviour_tables(d)
    lineage_tables(d)


if __name__ == "__main__":
    main(sys.argv[1] if len(sys.argv) > 1 else os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__))),
                                                            "outputs", "gene-variety"))
