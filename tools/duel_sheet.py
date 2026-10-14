"""Every moment of a duel for chosen species, on one contact sheet to look at.

    MSYS_NO_PATHCONV=1 wsl -d survivor --exec bash tools/duel_film.sh --sheet gilly:fizz grungo:mothwick
    or, in a mirror where duel_shot is built: python3 tools/duel_sheet.py [mine:theirs ...] [--out sheet.png]

A row per pairing (mine first, the friend second), a column per moment in the
order a duel reaches them, each at the tick test/support/duel_scenes.h names.
With no pairing given: grungo against fizz, gilly against mothwick, tinker
against mudge. Default out: outputs/duel-sheet.png.
"""
import os
import sys

from PIL import Image, ImageDraw

sys.path.insert(0, os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__))), "tests"))
import duel_frames  # noqa: E402
import film  # noqa: E402

LABEL = 16


def main(argv):
    out = os.path.join(film.ROOT, "outputs", "duel-sheet.png")
    pairs = []
    i = 0
    while i < len(argv):
        if argv[i] == "--out":
            out = argv[i + 1]
            i += 2
            continue
        mine, _, theirs = argv[i].partition(":")
        pairs.append((mine, theirs or mine))
        i += 1
    pairs = pairs or duel_frames.PAIRINGS
    side = 240
    moments = duel_frames.MOMENTS
    sheet = Image.new("RGB", (LABEL * 7 + side * len(moments), LABEL + side * len(pairs)), (0, 0, 0))
    ink = ImageDraw.Draw(sheet)
    for col, moment in enumerate(moments):
        ink.text((LABEL * 7 + col * side + 4, 2), moment, fill=(255, 255, 255))
    for row, (mine, theirs) in enumerate(pairs):
        ink.text((2, LABEL + row * side + side // 2 - 8), mine, fill=(255, 255, 255))
        ink.text((2, LABEL + row * side + side // 2 + 4), "vs " + theirs, fill=(200, 200, 200))
        for col, moment in enumerate(moments):
            sheet.paste(duel_frames.frame(moment, mine, theirs), (LABEL * 7 + col * side, LABEL + row * side))
        print(mine, "vs", theirs, "done")
    os.makedirs(os.path.dirname(os.path.abspath(out)), exist_ok=True)
    sheet.save(out)
    print("wrote", out)


if __name__ == "__main__":
    main(sys.argv[1:])
