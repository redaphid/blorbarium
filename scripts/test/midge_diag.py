# /// script
# requires-python = ">=3.9"
# dependencies = ["pyserial", "bleak>=0.22"]
# ///
"""Open a badge's port (it resets), keep its boot log, print its Wi-Fi state,
force a check and keep everything until the window closes. Secrets masked.

  uv run scripts/test/midge_diag.py /dev/ttyACM1 --mac d40592771b30 --log .jobs/midge.log
"""
import argparse
import os
import sys
import time

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
sys.path.insert(0, os.path.join(ROOT, "tools"))
sys.path.insert(0, os.path.join(ROOT, "scripts", "test"))
from power_e2e import Board  # noqa: E402
from moth_0735 import Masked, laptop_network, online, secrets  # noqa: E402

p = argparse.ArgumentParser()
p.add_argument("port")
p.add_argument("--mac", required=True)
p.add_argument("--log", required=True)
p.add_argument("--no-check", action="store_true")
p.add_argument("--window", type=int, default=180)
a = p.parse_args()
m = Masked(open(a.log, "w"), secrets(laptop_network()))
b = Board(a.port, m)


def say(s):
    s = m.mask(s)
    print(s, flush=True)
    b.note(s)


before, _ = online(a.mac, 0)
say("site before: %s" % (before,))
boot = b.wait(r"\[hw\] blorbarium .*fw=(\S+)", 25, since=0)
say("booted %s" % (boot and boot.group(1)))
b.wait(r"\[hw\] ble adv on", 10, since=0)
time.sleep(3)
mark = b.mark()
b.send("DEBUG wifi")
time.sleep(3)
for _, l in b.lines[mark:]:
    if l.startswith("[wifi]"):
        say("  " + l)
start_ms = int(time.time() * 1000)
if not a.no_check:
    mark = b.mark()
    b.send("DEBUG ota check")
    closed = b.wait(r"\[wifi\] window \S+ closed.*", a.window, mark)
    say("closed: %s" % (closed.group(0) if closed else "(still open after %d s)" % a.window))
    time.sleep(8)
    for _, l in b.lines[mark:]:
        if l.startswith("[wifi]") or "rst:" in l or "abort" in l or "Guru" in l:
            say("  " + l)
    after, _ = online(a.mac, start_ms)
    say("site after: %s" % (after,))
