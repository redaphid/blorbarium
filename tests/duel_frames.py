"""Frame goldens for the duel screen: each moment of test/support/duel_scenes.h,
for three pairings, drawn by tools/duel_shot.cpp.

    python3 tests/duel_frames.py              check (tools/duel_film.sh builds duel_shot first)
    UPDATE_GOLDEN=1 python3 tests/duel_frames.py   bless: write test/golden/duelscene_*.png

Held to the goldens with the same tolerance as the sim's frames (tests/film.py).
Every frame is drawn twice and the two must be the same bytes. A miss leaves
the frame and a diff in outputs/misses/, as film.py does.
"""
import hashlib
import os
import subprocess
import sys
import tempfile

from PIL import Image

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import film  # noqa: E402

PROGRAM = os.path.join(film.ROOT, ".pio", "build", "duelshot", "program")
MOMENTS = ["meeting", "staking", "staking_mortal", "choosing", "locked", "clash", "hit", "bonk", "mercy", "fallen",
           "spared", "outcome", "windup_quick", "windup_heavy", "landing", "riposte", "stagger", "whiff"]
PAIRINGS = [("grungo", "fizz"), ("gilly", "mothwick"), ("tinker", "mudge")]
# Frames between ticks, as the badge draws them (paint/tween.h): the moment at
# `ticks` and `sub`/256 of a tick, so the act's motion is caught mid-move.
BETWEEN = [("meeting", 3, 128), ("clash", 9, 128), ("hit", 13, 77), ("outcome", 4, 154)]
BETWEEN_PAIRING = ("gilly", "fizz")


def shot(moment, mine, theirs, bmp, ticks=None, sub=None):
    """Draws the moment to `bmp`; returns its sha256."""
    args = [PROGRAM, "--moment", moment, "--mine", mine, "--theirs", theirs, "--out", bmp]
    if ticks is not None:
        args += ["--ticks", str(ticks)]
    if sub is not None:
        args += ["--sub", str(sub)]
    r = subprocess.run(args, capture_output=True, text=True)
    if r.returncode != 0 or not os.path.exists(bmp):
        raise RuntimeError("duel_shot %s %s %s exited %d: %s" % (moment, mine, theirs, r.returncode, r.stderr[-400:]))
    with open(bmp, "rb") as f:
        return hashlib.sha256(f.read()).hexdigest()


def frame(moment, mine, theirs, ticks=None, sub=None):
    with tempfile.TemporaryDirectory() as tmp:
        a, b = os.path.join(tmp, "a.bmp"), os.path.join(tmp, "b.bmp")
        if shot(moment, mine, theirs, a, ticks, sub) != shot(moment, mine, theirs, b, ticks, sub):
            raise RuntimeError("%s %s-%s: two draws of one scene differ" % (moment, mine, theirs))
        return Image.open(a).convert("RGB")


def main(cases):
    misses = []
    todo = [(m, mine, theirs, None, None, "duelscene_%s_%s_%s" % (m, mine, theirs))
            for mine, theirs in PAIRINGS for m in MOMENTS]
    todo += [(m, BETWEEN_PAIRING[0], BETWEEN_PAIRING[1], t, sub,
              "duelscene_between_%s_t%d_%03d_%s_%s" % ((m, t, sub) + BETWEEN_PAIRING)) for m, t, sub in BETWEEN]
    for moment, mine, theirs, ticks, sub, case in todo:
        if cases and case not in cases and moment not in cases:
            continue
        got = frame(moment, mine, theirs, ticks, sub)
        os.makedirs(film.FRAMES, exist_ok=True)
        got.resize((480, 480), Image.NEAREST).save(os.path.join(film.FRAMES, case + ".png"))
        why = film.check(case, got)
        print("%-48s %s" % (case, why or "ok"))
        if why:
            misses.append(case)
    if misses:
        print("FAILED: %d duel frame(s): %s" % (len(misses), " ".join(misses)))
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
