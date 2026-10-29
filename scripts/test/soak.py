# /// script
# requires-python = ">=3.9"
# dependencies = ["pyserial", "bleak>=0.22"]
# ///
"""Leave a cabled badge alone for a while and say what it did by itself.

    uv run scripts/test/soak.py /dev/ttyACM0 --minutes 120 --log .jobs/soak.log

At the start, `DEBUG wifi due` (the next automatic check is due, as after a
boot); then nothing is sent until the end, when `DEBUG pickup` (a hand lifting
it, through the real HeldDetector) must wake it. Graded:
  - it went dark and napped;
  - its own scheduled Check opened, joined and reported (the site's last_seen moved);
  - every window's sampled low >= 5 KB, and no allocation failure;
  - the pick-up woke it;
  - no crash or reset.
The heap trend (each `[hw] up=` line's heap, minheap, big) is summarised.
"""
import argparse
import os
import re
import sys
import time

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
sys.path.insert(0, os.path.join(ROOT, "tools"))
sys.path.insert(0, os.path.join(ROOT, "scripts", "test"))
from power_e2e import Board  # noqa: E402
from moth_0735 import Masked, laptop_network, online, secrets  # noqa: E402

p = argparse.ArgumentParser()
p.add_argument("port")
p.add_argument("--minutes", type=float, default=120)
p.add_argument("--mac", default="d40592771cb0")
p.add_argument("--untried-away", action="store_true",
               help="first set the .env network by cable: untried, newest and out of reach here, so the Check must fall back")
p.add_argument("--log", required=True)
a = p.parse_args()
m = Masked(open(a.log, "w"), secrets(laptop_network()))
b = Board(a.port, m)


def say(s):
    s = m.mask(s)
    print(s, flush=True)
    b.note(s)


boot = b.wait(r"\[hw\] blorbarium .*fw=(\S+)", 25, since=0)
say("booted %s" % (boot and boot.group(1)))
b.wait(r"\[hw\] ble adv on", 10, since=0)
time.sleep(3)
base, _ = online(a.mac, 0)
start_ms = int(time.time() * 1000)
if a.untried_away:
    env = {}
    for line in open("/home/redaphid/Projects/blorbarium/.env"):
        if "=" in line and not line.lstrip().startswith("#"):
            k, v = line.split("=", 1)
            env[k.strip()] = v.strip().strip("\"'")
    m.hidden = sorted(set(m.hidden) | {env["ssid"], env["password"]}, key=len, reverse=True)
    mark = b.mark()
    b.note('>> DEBUG wifi set "<env ssid>" <env password>')
    b.p.write(b"\r" * 8)
    time.sleep(0.06)
    b.p.write(('DEBUG wifi set "%s" %s\n' % (env["ssid"], env["password"])).encode())
    r = b.wait(r"\[wifi\] network #1: .*", 6, mark)
    say("untried away: %s" % (r.group(0) if r else "(no answer)"))
mark = b.mark()
b.send("DEBUG wifi due")
due = b.wait(r"\[wifi\] due: .*", 6, mark)
say("due: %s" % (due.group(0) if due else "(no answer)"))
end = time.monotonic() + a.minutes * 60
next_note = time.monotonic() + 600
while time.monotonic() < end:
    time.sleep(5)
    if time.monotonic() >= next_note:
        next_note += 600
        ups = [l for _, l in b.lines if l.startswith("[hw] up=")]
        say("t=%d min: %d lines, last: %s" % ((a.minutes * 60 - (end - time.monotonic())) / 60, len(b.lines),
                                             (ups[-1][:110] if ups else "-")))
lines = [l for _, l in b.lines]
mark = b.mark()
b.send("DEBUG pickup")
woke = b.wait(r"\[power\] nap out why=.*|\[power\] power=full.*", 30, mark)
time.sleep(5)
after, _ = online(a.mac, start_ms)

dark = [l for l in lines if re.search(r"\[power\] power=low", l)]
naps = [l for l in lines if l.startswith("[power] nap in")]
napping = [l for l in lines if l.startswith("[power] napping")]
windows = [l for l in lines if re.search(r"\[wifi\] window \w+ (opens|closed)", l)]
checks = [l for l in lines if re.search(r"\[wifi\] window check closed", l)]
reports = [l for l in lines if re.search(r"remote: report home|checked in", l)]
lows = [int(x) for l in lines if "this window's low" in l for x in re.findall(r"(?:tls|io|loop)=(\d+)", l)]
alloc = [l for l in lines if re.search(r"no RAM|alloc(ation)? fail|out of memory|would not come up", l, re.I)]
boots = [l for l in lines if "] blorbarium " in l and " fw=" in l][1:]   # the first is the open's
heaps = [tuple(int(x) for x in re.search(r"heap=(\d+) minheap=(\d+) big=(\d+)", l).groups())
         for l in lines if l.startswith("[hw] up=") and "big=" in l]

say("== soak %.0f min on %s" % (a.minutes, boot and boot.group(1)))
say("dark: %d power=low lines; naps: %d nap-in, %d napping lines" % (len(dark), len(naps), len(napping)))
for l in windows:
    say("  " + l[:230])
say("scheduled checks closed: %d; report lines: %d; site last_seen moved: %s (%s)" % (
    len(checks), len(reports), bool(after.get("joined")), after))
fell = [l for l in lines if "heard in the scan, instead" in l]
say("fallbacks: %d%s" % (len(fell), "".join("\n  " + l[:200] for l in fell[:3])))
say("window lows (sampled): min %s" % (min(lows) if lows else "-"))
say("allocation failures: %d%s" % (len(alloc), "".join("\n  " + l[:160] for l in alloc[:5])))
say("pick-up woke it: %s" % (woke.group(0)[:160] if woke else "NO"))
say("boots during the soak: %d; crashes: %d%s" % (len(boots), len(b.crashes), "".join("\n  " + c for c in b.crashes[:5])))
if heaps:
    n = len(heaps)
    pick = [heaps[0], heaps[n // 4], heaps[n // 2], heaps[3 * n // 4], heaps[-1]]
    say("heap trend (heap/minheap/big) over %d samples: %s" % (n, "  ".join("%d/%d/%d" % h for h in pick)))
    say("heap min over the soak: %d" % min(h[0] for h in heaps))
ok = (dark and (naps or napping) and checks and after.get("joined") and (not lows or min(lows) >= 5000)
      and not alloc and woke and not boots and not b.crashes and (fell or not a.untried_away))
say("RESULT %s" % ("PASS" if ok else "FAIL"))
sys.exit(0 if ok else 1)
