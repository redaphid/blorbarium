"""Frame goldens: each feed in tests/feeds, played on the simulator's fixed clock.

    python3 tests/film.py [case ...]        check (tools/sim_film.sh builds the sim first)
    UPDATE_GOLDEN=1 python3 tests/film.py   bless: write test/golden/<case>.png instead

Every case is shot twice and the two BMPs must hash the same: the fixed clock,
the seeded engine, integer arithmetic and the pure renderer make a feed the
same bytes every run, and this is where that stays true. The frame is then
held to its golden within a tolerance. A miss leaves <case>.actual.png and
<case>.diff.png in outputs/misses/; every frame shot, 2x nearest-neighbour,
is in outputs/frames/ for a person to look at.
"""
import hashlib
import os
import subprocess
import sys
import tempfile

from PIL import Image, ImageChops, ImageDraw

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
PROGRAM = os.path.join(ROOT, ".pio", "build", "sim", "program")
FEEDS = os.path.join(ROOT, "tests", "feeds")
GOLDEN = os.path.join(ROOT, "test", "golden")
FRAMES = os.path.join(ROOT, "outputs", "frames")
MISSES = os.path.join(ROOT, "outputs", "misses")

# Each case's feed is tests/feeds/<case>.txt; the number is when, on the
# script's clock, the frame is shot.
CASES = {
    "idle": 17600,          # the resting cases rest home first, to 16400
    "eating": 7600,         # the pellet is at his mouth from about 7200 to 8000
    "foresee_glow": 18400,
    "shake_hop": 17600,     # the shake fires near 17200; at 17600 he is in the air
    "sleep": 19000,
    "egg": 3000,
    "hatch": 2000,
    "clutch": 3000,
    "remains": 3000,
    "time_unknown": 17600,
    "elder": 17600,
    "thought": 9000,        # a thought, partway through its one pass
    "prophecy": 9000,       # a prophecy after a shake: foresee face, no hop
}

# A case that must not look like another: a hop that drew as idle once passed
# its golden because the golden was idle too. Share of the disc that must differ.
UNLIKE = {"shake_hop": ("idle", 0.02), "thought": ("idle", 0.01), "prophecy": ("thought", 0.01)}

# A pixel has moved when a channel is further off than this; the panel is
# RGB565, so neighbouring colours are 4 to 8 counts apart.
CHANNEL_TOL = 24
# ...and a frame has moved when more of the disc than this has.
SHARE_TOL = 0.002


def shoot(case, after_ms):
    """The frame `after_ms` into the case's feed, and its sha256. Runs the sim twice."""
    feed = os.path.join(FEEDS, case + ".txt")
    digests = []
    with tempfile.TemporaryDirectory() as tmp:
        for run in range(2):
            bmp = os.path.join(tmp, "%d.bmp" % run)
            r = subprocess.run([PROGRAM, "--headless", "--clock", "fixed", "--script", feed,
                                "--shot", bmp, "--after", str(after_ms)],
                               stdin=subprocess.DEVNULL, capture_output=True, text=True,
                               env={k: v for k, v in os.environ.items() if k != "BLORB_SIM_MAC"})
            if r.returncode != 0 or not os.path.exists(bmp):
                raise RuntimeError("%s: the simulator exited %d: %s" % (case, r.returncode, r.stderr[-800:]))
            with open(bmp, "rb") as f:
                digests.append(hashlib.sha256(f.read()).hexdigest())
        if digests[0] != digests[1]:
            raise RuntimeError("%s: two runs of one feed differ: %s %s" % (case, digests[0], digests[1]))
        img = Image.open(os.path.join(tmp, "0.bmp")).convert("RGB")
    return img, digests[0]


def moved(got, want):
    """The disc's pixels that differ past CHANNEL_TOL, as a mask, and their share of the disc."""
    delta = ImageChops.difference(got, want).split()
    worst = ImageChops.lighter(ImageChops.lighter(delta[0], delta[1]), delta[2])
    disc = Image.new("L", got.size, 0)
    ImageDraw.Draw(disc).ellipse((0, 0, got.size[0] - 1, got.size[1] - 1), fill=255)
    off = ImageChops.multiply(worst.point(lambda v: 255 if v > CHANNEL_TOL else 0), disc)
    return off, off.histogram()[255] / disc.histogram()[255]


def check(case, got):
    """None when the frame matches its golden (or was blessed), else why not."""
    want_path = os.path.join(GOLDEN, case + ".png")
    if os.environ.get("UPDATE_GOLDEN"):
        os.makedirs(GOLDEN, exist_ok=True)
        got.save(want_path)
        return None
    actual = os.path.join(MISSES, case + ".actual.png")
    diff = os.path.join(MISSES, case + ".diff.png")
    if not os.path.exists(want_path):
        got.save(actual)
        return "no golden (UPDATE_GOLDEN=1 writes one)"
    want = Image.open(want_path).convert("RGB")
    if got.size != want.size:
        got.save(actual)
        return "size %s, golden %s" % (got.size, want.size)
    off, share = moved(got, want)
    if share <= SHARE_TOL:
        return None
    got.save(actual)
    # The frame dimmed, and what moved in red on top of it.
    Image.composite(Image.new("RGB", got.size, (255, 0, 0)), got.point(lambda v: v // 3), off).save(diff)
    return "%.2f%% of the disc is off by more than %d (allowed %.2f%%)" % (share * 100, CHANNEL_TOL, SHARE_TOL * 100)


def unlike(case, got, shots):
    """None unless the case looks too much like the one it must not be (UNLIKE)."""
    if case not in UNLIKE:
        return None
    other, least = UNLIKE[case]
    if other not in shots:
        shots[other], _ = shoot(other, CASES[other])
    _, share = moved(got, shots[other])
    if share >= least:
        return None
    return "only %.2f%% of the disc differs from %s (needs %.0f%%)" % (share * 100, other, least * 100)


def main(names):
    unknown = [n for n in names if n not in CASES]
    if unknown:
        print("no such case: %s (cases: %s)" % (" ".join(unknown), " ".join(CASES)))
        return 2
    os.makedirs(FRAMES, exist_ok=True)
    os.makedirs(MISSES, exist_ok=True)
    failed = 0
    shots = {}
    for case in names or CASES:
        for stale in (case + ".actual.png", case + ".diff.png"):
            if os.path.exists(os.path.join(MISSES, stale)):
                os.unlink(os.path.join(MISSES, stale))
        try:
            got, digest = shoot(case, CASES[case])
        except RuntimeError as e:
            print("FAIL %-13s %s" % (case, e))
            failed += 1
            continue
        shots[case] = got
        got.resize((got.width * 2, got.height * 2), Image.NEAREST).save(os.path.join(FRAMES, case + ".png"))
        why = unlike(case, got, shots) or check(case, got)
        verdict = ("FAIL " + why) if why else ("blessed" if os.environ.get("UPDATE_GOLDEN") else "ok")
        print("%-13s sha256 %s x2  %s" % (case, digest, verdict))
        failed += why is not None
    print("%d of %d frames %s" % (len(names or CASES) - failed, len(names or CASES),
                                  "blessed" if os.environ.get("UPDATE_GOLDEN") else "match their goldens"))
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
