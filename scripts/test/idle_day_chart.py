"""Awake and asleep per wall hour, before and after, from test_idle_day's sweep.

    IDLE_DAY_SWEEP=1 IDLE_DAY_HOURS=72 IDLE_DAY_OUT=<dir> pio test -e native -f test_idle_day
    python3 scripts/test/idle_day_chart.py <out.png> before=<dir> after=<dir> [--only <substring>]

One row per run (species, clock, night, founding hour), one cell per wall
hour: dark blue asleep, pale awake, shaded by the minutes asleep in it. The
grey band marks the kid's night (21:00 to 07:00), when nobody touches it.
"""
import csv
import glob
import os
import sys

from PIL import Image, ImageDraw


def runs(d):
    out = []
    for f in sorted(glob.glob(os.path.join(d, "*.csv"))):
        with open(f) as h:
            rows = list(csv.DictReader(h))
        out.append((os.path.basename(f)[:-4], [(int(r["wall_hour"]), float(r["asleep_min"])) for r in rows]))
    return out


def main(argv):
    out, sets, only = argv[0], [], None
    i = 1
    while i < len(argv):
        if argv[i] == "--only": only = argv[i + 1]; i += 2
        else: sets.append(argv[i].split("=", 1)); i += 1
    cell, label, rowh = 7, 230, 12
    data = [(name, [r for r in runs(d) if not only or only in r[0]]) for name, d in sets]
    hours = max(len(h) for _, rs in data for _, h in rs)
    height = sum(18 + len(rs) * rowh + 10 for _, rs in data) + 30
    img = Image.new("RGB", (label + hours * cell + 120, height), (250, 250, 250))
    d = ImageDraw.Draw(img)
    d.text((6, 4), "minutes asleep per wall hour (dark = asleep); grey ticks = 21:00-07:00", fill=(0, 0, 0))
    y = 24
    for name, rs in data:
        d.text((6, y), name, fill=(0, 0, 0))
        y += 16
        for run, hrs in rs:
            d.text((6, y), run, fill=(40, 40, 40))
            day_awake = day_n = 0
            for k, (wall, mins) in enumerate(hrs):
                x = label + k * cell
                if wall >= 21 or wall < 7:
                    d.rectangle([x, y + rowh - 3, x + cell - 1, y + rowh - 2], fill=(150, 150, 150))
                else:
                    day_awake += 60 - mins
                    day_n += 60
                s = mins / 60.0
                c = (int(235 - 205 * s), int(240 - 200 * s), int(250 - 120 * s))
                d.rectangle([x, y, x + cell - 2, y + rowh - 5], fill=c)
            if day_n:
                d.text((label + hours * cell + 6, y), "day awake %d%%" % round(100 * day_awake / day_n), fill=(0, 0, 0))
            y += rowh
        y += 10
    img.save(out)
    print("wrote", out)


if __name__ == "__main__":
    main(sys.argv[1:])
