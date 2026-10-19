#!/usr/bin/env python3
"""Read an imu_record.py log: per labelled segment, what the IMU did and what was detected.

    python3 tools/imu_analyze.py rec.log [--from LABEL] [--samples]

A segment runs from one `## <label>` mark to the next. For each: the samples,
the poses (settled rotation, side/inverted levels, the side hold's level),
the tap engine's reports, the jolts (|a| off 1 g), and every stimulus fired,
with times relative to the segment's start.
"""
import argparse
import math
import re
import sys


def load(path):
    segs = [{"label": "(start)", "t": 0.0, "imu": [], "stim": [], "other": []}]
    for raw in open(path):
        m = re.match(r"([\d.]+) (.*)", raw.rstrip("\n"))
        if not m:
            continue
        t, line = float(m.group(1)), m.group(2)
        if line.startswith("## "):
            segs.append({"label": line[3:], "t": t, "imu": [], "stim": [], "other": []})
            continue
        s = segs[-1]
        if g := re.match(r"\[imu\] (\d+) (-?\d+) (-?\d+) (-?\d+) (\d+) (\d+) (\d) (\d) (\d) (\d)(?: pk (-?\d+) (-?\d+) (-?\d+))?", line):
            v = [int(x) if x is not None else None for x in g.groups()]
            s["imu"].append(dict(ms=v[0], ax=v[1], ay=v[2], az=v[3], tap=v[4], rot=v[5], side=v[6], inv=v[7],
                                 held=v[8], btn=v[9], pkx=v[10], pky=v[11], pkz=v[12]))
        elif g := re.match(r"\[stim\] (\d+) (\w+)", line):
            s["stim"].append((int(g.group(1)), g.group(2)))
        elif not line.startswith("[hw] up=") and not line.startswith(">>"):
            s["other"].append((t, line))
    return segs


def runs(samples, key):
    """[(value, start_ms, end_ms)] for consecutive runs of samples[key]."""
    out = []
    for x in samples:
        if out and out[-1][0] == x[key]:
            out[-1][2] = x["ms"]
        else:
            out.append([x[key], x["ms"], x["ms"]])
    return out


def report(s, show_samples):
    imu = s["imu"]
    print("=" * 78)
    print("## %s   (%d samples, %.1f s)" % (s["label"], len(imu), (imu[-1]["ms"] - imu[0]["ms"]) / 1000 if imu else 0))
    if not imu:
        return
    t0 = imu[0]["ms"]
    rel = lambda ms: (ms - t0) / 1000.0
    mags = [math.sqrt(x["ax"] ** 2 + x["ay"] ** 2 + x["az"] ** 2) for x in imu]
    dev = [abs(m - 1000) for m in mags]
    print("  |a| off 1 g: max %d mg, samples over 250 mg: %d, over 800 mg: %d" %
          (max(dev), sum(d > 250 for d in dev), sum(d > 800 for d in dev)))
    for key, name in (("rot", "rotation"), ("side", "side level"), ("inv", "inverted level"), ("held", "SIDE HOLD")):
        r = runs(imu, key)
        if len(r) > 1 or key == "rot":
            print("  %-15s " % name + "  ".join("%s@%.2f-%.2f" % (v, rel(a), rel(b)) for v, a, b in r))
    taps = [(rel(x["ms"]), x["tap"]) for x in imu if x["tap"]]
    print("  tap engine:     " + (" ".join("%s@%.2f" % ("double" if c == 2 else "single", t) for t, c in taps) or "none"))
    print("  stimuli:        " + (" ".join("%s@%.2f" % (n, rel(ms)) for ms, n in s["stim"]) or "none"))
    for t, line in s["other"]:
        if "[diag]" in line or "[power]" in line or "[duel]" in line:
            print("  log: %s" % line)
    if show_samples:
        for x, m in zip(imu, mags):
            print("    %.2f %6d %6d %6d |%5d| tap%d rot%d s%d i%d h%d" % (rel(x["ms"]), x["ax"], x["ay"], x["az"], m,
                                                                     x["tap"], x["rot"], x["side"], x["inv"], x["held"]))


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("log")
    ap.add_argument("--from", dest="start", default=None, help="only segments from the last one with this label on")
    ap.add_argument("--samples", action="store_true")
    a = ap.parse_args()
    segs = load(a.log)
    if a.start:
        idx = max(i for i, s in enumerate(segs) if s["label"].startswith(a.start))
        segs = segs[idx:]
    for s in segs:
        report(s, a.samples)


if __name__ == "__main__":
    sys.exit(main())
