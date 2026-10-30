# /// script
# requires-python = ">=3.9"
# dependencies = ["pyserial", "bleak>=0.22"]
# ///
"""Does one image read the networks another one saved? Flash an image app-only
(NVS untouched), boot it, and print what it says of its networks (masked).

    uv run scripts/test/nvs_handover.py /dev/ttyACM0 <image.bin> --log .jobs/h.log
"""
import argparse
import os
import sys
import time

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
sys.path.insert(0, os.path.join(ROOT, "tools"))
sys.path.insert(0, os.path.join(ROOT, "scripts", "test"))
from power_e2e import Board  # noqa: E402
from moth_0735 import Masked, laptop_network, secrets  # noqa: E402
from upgrade_keeps import DUMMY, flash  # noqa: E402

p = argparse.ArgumentParser()
p.add_argument("port")
p.add_argument("image")
p.add_argument("--log", required=True)
a = p.parse_args()
m = Masked(open(a.log, "w"), secrets(laptop_network()) + list(DUMMY))
ok, out = flash(a.port, a.image, False)
print("flash %s: %s" % (os.path.basename(a.image), "ok" if ok else "FAILED"), flush=True)
b = Board(a.port, m)
boot = b.wait(r"\[hw\] blorbarium .*fw=(\S+)", 25, since=0)
print("booted", boot and boot.group(1))
b.wait(r"\[hw\] ble adv on", 10, since=0)
time.sleep(3)
mark = b.mark()
b.send("DEBUG wifi")
time.sleep(2)
for _, l in b.lines:
    if l.startswith("[wifi] ") and ("network" in l or "trial" in l) or "nvs" in l.lower() or "wifi store" in l.lower():
        print("  " + m.mask(l)[:220])
print("crashes", len(b.crashes))
