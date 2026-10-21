"""How often a recorded clip changes from frame to frame, and by how much.

    python3 scripts/test/frame_motion.py dir [dir ...]

For each sim --record directory: the frames that differ from the one before
(a 50 fps clip whose pet moves only on 100 ms ticks changes on one frame in
five), and the pixels each change moves. The rim's idle bar is masked out, so
this is the pet and the dish, not the clock.
"""
import glob
import os
import sys

from PIL import Image, ImageChops


def main(dirs):
    for d in dirs:
        fs = sorted(glob.glob(os.path.join(d, "*.bmp")))
        prev, moved, px, runs, run = None, 0, [], [], 0
        for f in fs:
            im = Image.open(f).convert("RGB").crop((8, 8, 232, 232))   # inside the idle bar's ring
            if prev is not None:
                diff = ImageChops.difference(im, prev).convert("L").point(lambda v: 255 if v > 8 else 0)
                n = diff.histogram()[255]
                if n:
                    moved += 1
                    px.append(n)
                    runs.append(run)
                    run = 0
                else:
                    run += 1
            prev = im
        pairs = max(1, len(fs) - 1)
        still = max(runs) if runs else pairs
        print("%-28s frames=%3d changed=%3d (%3.0f%%) longest_still=%2d frames (%3d ms) mean_px_per_change=%6.0f" %
              (os.path.basename(d.rstrip("/")), len(fs), moved, 100.0 * moved / pairs, still, still * 20,
               sum(px) / len(px) if px else 0))


if __name__ == "__main__":
    main(sys.argv[1:])
