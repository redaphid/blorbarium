"""Every species in every scene the film feeds reach, on one sheet to look at.

    MSYS_NO_PATHCONV=1 wsl -d survivor --exec bash tools/sim_film.sh idle   # builds the sim
    then, in that run's mirror: python3 tools/species_sheet.py [out.png]

A row per species (species.def order), a column per feed: the faces and
stages the art contract names, the power face's glow, sleep, the egg, the
clutch, the remains and the sealed screen. Shot once each on the fixed clock,
so the sheet is the same every run. Default out: outputs/species-sheet.png.
"""
import os
import sys
import tempfile

from PIL import Image, ImageDraw

sys.path.insert(0, os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__))), "tests"))
import film  # noqa: E402

SPECIES = ["grungo"] + film.ROSTER
SCENES = ["idle", "eating", "shake_hop", "foresee_glow", "prophecy", "sleep", "hatch", "egg", "clutch", "elder",
          "remains", "sealed"]
LABEL = 14


def main(out):
    side = 240
    sheet = Image.new("RGB", (LABEL * 6 + side * len(SCENES), LABEL + side * len(SPECIES)), (0, 0, 0))
    ink = ImageDraw.Draw(sheet)
    for col, scene in enumerate(SCENES):
        ink.text((LABEL * 6 + col * side + 4, 1), scene, fill=(255, 255, 255))
    with tempfile.TemporaryDirectory() as tmp:
        for row, slug in enumerate(SPECIES):
            ink.text((2, LABEL + row * side + side // 2), slug, fill=(255, 255, 255))
            for col, scene in enumerate(SCENES):
                bmp = os.path.join(tmp, "f.bmp")
                film.frame(scene, film.CASES[scene], film.FLAGS.get(scene, []) + ["--species", slug], bmp)
                sheet.paste(Image.open(bmp).convert("RGB"), (LABEL * 6 + col * side, LABEL + row * side))
            print(slug, "done")
    os.makedirs(os.path.dirname(os.path.abspath(out)), exist_ok=True)
    sheet.save(out)
    print("wrote", out)


if __name__ == "__main__":
    main(sys.argv[1] if len(sys.argv) > 1 else os.path.join(film.ROOT, "outputs", "species-sheet.png"))
