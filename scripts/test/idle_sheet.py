"""One idle's props on every stage and a few species, on one sheet to look at.

    python3 scripts/test/idle_sheet.py <program> <out.png> <idle> [idle ...]

<program> is a built sim (.pio/build/sim/program, or the mirror sim_film.sh
builds in). Each idle is pinned with the sim's `DEBUG idle` (paint only), the
pet resting at home; a row per species and stage, a column per moment.
"""
import os
import subprocess
import sys
import tempfile

from PIL import Image, ImageDraw

SPECIES = os.environ.get("SPECIES", "tinker grungo mossby gilly hushwing").split()
STAGES = os.environ.get("STAGES", "hatchling child adult").split()
MOMENTS = [17600, 17900, 18300, 18800]
LABEL = 14


def feed(stage, idle):
    lines = ["@0 DEBUG time 1790000000", "@100 DEBUG warp 18000", "@200 DEBUG inject life 0.5", "@300 DEBUG warp 600"]
    if stage != "hatchling":
        lines.append("@350 DEBUG stage %s" % stage)
    lines.append("@400 DEBUG inject vision 0")
    lines += ["@%d DEBUG force rest" % t for t in range(400, 17600, 2000)]
    lines.append("@17000 DEBUG idle %s" % idle)
    return "\n".join(lines) + "\n"


def shot(program, script, species, after, bmp):
    r = subprocess.run([program, "--headless", "--clock", "fixed", "--script", script, "--shot", bmp, "--after",
                        str(after), "--species", species], stdin=subprocess.DEVNULL, capture_output=True, text=True)
    if r.returncode or not os.path.exists(bmp):
        raise RuntimeError(r.stderr[-600:])
    return Image.open(bmp).convert("RGB")


def main(program, out, idles):
    side = 240
    rows = [(sp, st, idle) for idle in idles for sp in SPECIES for st in STAGES]
    sheet = Image.new("RGB", (120 + side * len(MOMENTS), LABEL + side * len(rows)), (0, 0, 0))
    ink = ImageDraw.Draw(sheet)
    with tempfile.TemporaryDirectory() as tmp:
        for r, (sp, st, idle) in enumerate(rows):
            script = os.path.join(tmp, "%s_%s.txt" % (st, idle))
            with open(script, "w") as f:
                f.write(feed(st, idle))
            ink.text((2, LABEL + r * side + side // 2), "%s\n%s\n%s" % (idle, sp, st), fill=(255, 255, 255))
            for c, at in enumerate(MOMENTS):
                img = shot(program, script, sp, at, os.path.join(tmp, "f.bmp"))
                sheet.paste(img, (120 + c * side, LABEL + r * side))
                # The disc's centre column, to see what sits off it.
                ink.line([(120 + c * side + side // 2, LABEL + r * side), (120 + c * side + side // 2, LABEL + r * side + 6)],
                         fill=(255, 0, 0))
    os.makedirs(os.path.dirname(os.path.abspath(out)), exist_ok=True)
    sheet.save(out)
    print(out)


if __name__ == "__main__":
    main(sys.argv[1], sys.argv[2], sys.argv[3:])
