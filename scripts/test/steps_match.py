"""The sim's --steps frames are the frames before frames between ticks existed.

    python3 scripts/test/steps_match.py [base-sha-file]

Shoots every tests/film.py case with --steps and prints its sha256, so a run
on the commit before paint/tween.h (without --steps) and a run here can be
compared line by line: the same feed, the same engine, the same bytes. With a
file of `case sha` lines (a run's own output), says which differ.
"""
import os
import sys
import tempfile

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..", "tests"))
import film  # noqa: E402


def main():
    want = {}
    if len(sys.argv) > 1:
        for line in open(sys.argv[1]):
            parts = line.split()
            if len(parts) >= 2:
                want[parts[0]] = parts[1]
    flag = [] if os.environ.get("NO_STEPS") else ["--steps"]
    differ = 0
    with tempfile.TemporaryDirectory() as tmp:
        for case in sorted(film.STEPS):
            after = film.CASES[case]
            bmp = os.path.join(tmp, case + ".bmp")
            flags = [f for f in film.FLAGS.get(case, []) if f != "--steps"] + flag
            sha = film.frame(film.FEED_OF.get(case, case), after, flags, bmp)
            mark = ""
            if want:
                mark = "same" if want.get(case) == sha else "DIFFERS"
                differ += mark != "same"
            print(case, sha, mark)
    return 1 if differ else 0


if __name__ == "__main__":
    sys.exit(main())
