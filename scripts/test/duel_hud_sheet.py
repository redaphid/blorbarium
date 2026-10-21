"""Before and after sheets of the duel HUD: each pairing through choosing with
each action aimed, locked, a hit landing and draining, low HP, a KO, mercy and
the end, drawn by two duel_shot builds (scripts/test/duel_shot_build.sh), and
the two side by side.

    python3 scripts/test/duel_hud_sheet.py <before duel_shot> <after duel_shot> <out dir>

Writes before.png, after.png and side_by_side.png (each frame before over after) in <out dir>.
"""
import os
import subprocess
import sys
import tempfile

from PIL import Image, ImageDraw

PAIRINGS = [("grungo", "fizz"), ("gilly", "mothwick"), ("tinker", "mudge")]
# (label, moment, extra duel_shot args)
SHOTS = [
    ("aim pounce", "choosing", ["--aim", "0"]),
    ("aim guard", "choosing", ["--aim", "1"]),
    ("aim trick", "choosing", ["--aim", "2"]),
    ("locked", "locked", []),
    ("hit lands", "hit", ["--ticks", "13"]),
    ("hit drains", "hit", ["--ticks", "17"]),
    ("low hp", "choosing", ["--hearts", "1", "--their-hearts", "2"]),
    ("ko", "landing", ["--ticks", "16"]),
    ("windup", "windup_heavy", []),
    ("stakes", "staking_mortal", []),
    ("mercy", "mercy", []),
    ("outcome", "outcome", []),
]
SIDE = 240
LABEL = 16
LEFT = 112


def shot(binary, moment, mine, theirs, extra):
    with tempfile.TemporaryDirectory() as tmp:
        bmp = os.path.join(tmp, "f.bmp")
        r = subprocess.run([binary, "--moment", moment, "--mine", mine, "--theirs", theirs, "--out", bmp] + extra,
                           capture_output=True, text=True)
        if r.returncode:
            raise RuntimeError("%s %s: %s" % (binary, moment, r.stderr))
        return Image.open(bmp).convert("RGB")


def sheet(rows, title):
    out = Image.new("RGB", (LEFT + SIDE * len(SHOTS), LABEL + SIDE * len(rows)), (0, 0, 0))
    ink = ImageDraw.Draw(out)
    for col, (label, _, _) in enumerate(SHOTS):
        ink.text((LEFT + col * SIDE + 4, 2), label, fill=(255, 255, 255))
    for row, (name, frames) in enumerate(rows):
        ink.text((2, LABEL + row * SIDE + SIDE // 2 - 8), name, fill=(255, 255, 255))
        ink.text((2, LABEL + row * SIDE + SIDE // 2 + 6), title, fill=(160, 160, 160))
        for col, im in enumerate(frames):
            out.paste(im, (LEFT + col * SIDE, LABEL + row * SIDE))
    return out


def main(before, after, out_dir):
    os.makedirs(out_dir, exist_ok=True)
    rows = {"before": [], "after": []}
    for mine, theirs in PAIRINGS:
        for which, binary in (("before", before), ("after", after)):
            frames = [shot(binary, m, mine, theirs, extra) for _, m, extra in SHOTS]
            rows[which].append(("%s v %s" % (mine, theirs), frames))
    for which in rows:
        sheet(rows[which], which).save(os.path.join(out_dir, which + ".png"))
    both = []
    for b, a in zip(rows["before"], rows["after"]):
        both += [(b[0], b[1]), (a[0], a[1])]
    side = sheet(both, "")
    ink = ImageDraw.Draw(side)
    for i in range(len(both)):
        ink.text((2, LABEL + i * SIDE + SIDE // 2 + 6), "before" if i % 2 == 0 else "after", fill=(160, 160, 160))
    side.save(os.path.join(out_dir, "side_by_side.png"))
    print("wrote", ", ".join(os.path.join(out_dir, n) for n in ("before.png", "after.png", "side_by_side.png")))


if __name__ == "__main__":
    main(*sys.argv[1:4])
