"""tools/flash_batch.py without a board: port detection, the boot log, the clash check and the
whole loop on pretend boards.

    python3 -m unittest discover -s tests -p "test_*.py"
"""
import builtins
import io
import os
import sys
import tempfile
import unittest
from contextlib import redirect_stdout
from pathlib import Path
from unittest import mock

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(ROOT, "tools"))
import flash_batch as fb  # noqa: E402

# What the firmware prints (src/main.cpp reportBoot, ble.begin), as captured from a board.
BOOT = [
    "[hw] blorbarium 045ad8a fw=0.4.0 fmt=1 badge128",
    "[hw] storage pet: formatted: it was blank",
    "[hw] boot=Fresh occupant=egg gen=0 genome=5c1e0a9b age=0",
    "[hw] ble name=blorb-1b34 heap=136244 big=90100",
]


class Ports(unittest.TestCase):
    def test_the_port_that_appeared_is_chosen(self):
        self.assertEqual(fb.choose_port({}, {"COM14": "A"}), ("COM14", "new"))
        self.assertEqual(fb.choose_port({"COM3": "X"}, {"COM3": "X", "COM14": "A"}), ("COM14", "new"))

    def test_one_badge_already_attached_is_used(self):
        self.assertEqual(fb.choose_port({"COM14": "A"}, {"COM14": "A"}), ("COM14", "already attached"))

    def test_nothing_or_several_old_ones_is_no_choice(self):
        self.assertEqual(fb.choose_port({}, {}), (None, "none"))
        self.assertEqual(fb.choose_port({"COM3": "X", "COM4": "Y"}, {"COM3": "X", "COM4": "Y"})[0], None)

    def test_two_new_at_once_is_refused(self):
        with self.assertRaises(fb.Failed):
            fb.choose_port({}, {"COM14": "A", "COM15": "B"})


class BootLog(unittest.TestCase):
    def test_reads_fw_boot_genome_and_ble_name(self):
        got = fb.parse_boot(BOOT)
        self.assertEqual(got["fw"], "0.4.0 045ad8a")
        self.assertEqual((got["boot"], got["occupant"], got["gen"], got["genome"]), ("Fresh", "egg", 0, "5c1e0a9b"))
        self.assertEqual(got["ble_name"], "blorb-1b34")
        self.assertNotIn("alarms", got)

    def test_a_sealed_board_is_an_alarm(self):
        got = fb.parse_boot(BOOT + ["[hw] !!! SEALED: a pet is on the flash and would not open; ..."])
        self.assertEqual(len(got["alarms"]), 1)

    def test_a_log_cut_short_lacks_the_name(self):
        self.assertNotIn("ble_name", fb.parse_boot(BOOT[:3]))


class Clashes(unittest.TestCase):
    rows = [{"badge": "1", "mac": "d4:05:92:77:1b:34", "genome": "5c1e0a9b", "ble_name": "blorb-1b34", "result": "ok"},
            {"badge": "2", "mac": "d4:05:92:77:1b:44", "genome": "deadbeef", "ble_name": "blorb-1b44", "result": "failed"}]

    def test_another_macs_genome_or_name_is_flagged(self):
        notes = fb.clashes(self.rows, "d4:05:92:00:1b:34", "5c1e0a9b", "blorb-1b34")
        self.assertEqual(len(notes), 2)
        self.assertIn("DUPLICATE GENOME", notes[0])

    def test_the_same_board_again_and_failed_rows_are_not(self):
        self.assertEqual(fb.clashes(self.rows, "d4:05:92:77:1b:34", "5c1e0a9b", "blorb-1b34"), [])
        self.assertEqual(fb.clashes(self.rows, "d4:05:92:00:00:01", "deadbeef", "blorb-1b44"), [])


class Seeds(unittest.TestCase):
    def test_fnv1a_is_blorbs(self):
        self.assertEqual(fb.fnv1a(b""), 0x811C9DC5)
        self.assertEqual(fb.fnv1a(b"a"), 0xE40C292C)

    def test_a_reel_of_macs_gives_distinct_seeds(self):
        seeds = {fb.fnv1a(bytes([0xD4, 0x05, 0x92, 0x77, n >> 8, n & 255])) for n in range(4096)}
        self.assertEqual(len(seeds), 4096)


class DryRun(unittest.TestCase):
    def test_three_badges_then_quit(self):
        with tempfile.TemporaryDirectory() as tmp:
            log = Path(tmp) / "flash_log.csv"
            answers = iter(["", "", "", "q"])
            out = io.StringIO()
            with mock.patch.object(builtins, "input", lambda prompt="": (out.write(prompt), next(answers))[1]), \
                    redirect_stdout(out):
                self.assertEqual(fb.main(["--dry-run", "--log", str(log), "--start", "4"]), 0)
            rows = fb.read_log(log.with_suffix(".dry-run.csv"))
            text = out.getvalue()
        self.assertEqual([r["badge"] for r in rows], ["4", "5", "6"])
        self.assertTrue(all(r["result"] == "ok" and r["boot"] == "Fresh" for r in rows))
        self.assertEqual(len({r["genome"] for r in rows}), 3)
        self.assertEqual(len({r["port"] for r in rows}), 3)
        for n in (4, 5, 6):
            self.assertIn("Plug in badge %d, press Enter (q to quit)" % n, text)
            self.assertIn("Unplug badge %d" % n, text)
        self.assertIn("Plug in badge 7", text)
        self.assertFalse(log.exists())


if __name__ == "__main__":
    unittest.main()
