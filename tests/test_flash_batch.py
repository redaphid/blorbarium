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
    "[hw] blorbarium 045ad8a fw=0.7.0 fmt=1 badge128",
    "[hw] storage pet: formatted: it was blank",
    "[hw] boot=Fresh occupant=egg gen=0 genome=5c1e0a9b age=0",
    "[hw] ble name=gilly heap=136244 big=90100",
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
        self.assertEqual(got["fw"], "0.7.0 045ad8a")
        self.assertEqual((got["boot"], got["occupant"], got["gen"], got["genome"]), ("Fresh", "egg", 0, "5c1e0a9b"))
        self.assertEqual(got["ble_name"], "gilly")
        self.assertNotIn("alarms", got)

    def test_a_sealed_board_is_an_alarm(self):
        got = fb.parse_boot(BOOT + ["[hw] !!! SEALED: a pet is on the flash and would not open; ..."])
        self.assertEqual(len(got["alarms"]), 1)

    def test_a_log_cut_short_lacks_the_name(self):
        self.assertNotIn("ble_name", fb.parse_boot(BOOT[:3]))


class Clashes(unittest.TestCase):
    rows = [{"badge": "3", "mac": "d4:05:92:77:1b:34", "genome": "5c1e0a9b", "ble_name": "gilly", "result": "ok"},
            {"badge": "2", "mac": "d4:05:92:77:1b:44", "genome": "deadbeef", "ble_name": "puffkin", "result": "failed"},
            {"badge": "1", "mac": "d4:05:92:77:1b:3c", "genome": "2ea98ea9", "ble_name": "blorb-1b3c", "result": "ok"}]

    def test_another_macs_species_is_flagged(self):
        notes = fb.clashes(self.rows, "d4:05:92:00:1b:34", "0badf00d", "gilly")
        self.assertEqual(notes, ["DUPLICATE SPECIES gilly (badge 3, d4:05:92:77:1b:34)"])

    def test_another_macs_genome_is_flagged_too(self):
        notes = fb.clashes(self.rows, "d4:05:92:00:1b:34", "5c1e0a9b", "mudge")
        self.assertEqual(len(notes), 1)
        self.assertIn("DUPLICATE GENOME", notes[0])

    def test_the_same_board_again_failed_rows_and_old_names_are_not(self):
        self.assertEqual(fb.clashes(self.rows, "d4:05:92:77:1b:34", "5c1e0a9b", "gilly"), [])
        self.assertEqual(fb.clashes(self.rows, "d4:05:92:00:00:01", "deadbeef", "puffkin"), [])
        self.assertEqual(fb.clashes(self.rows, "d4:05:92:77:1b:3c", "11111111", "mossby"), [],
                         "badge 1 reflashed: its 0.4.0 row named it blorb-1b3c")


class Species(unittest.TestCase):
    """The table and the roster come from the firmware's own .def files."""

    def test_the_table_is_the_eight_badges_one_species_each(self):
        table = fb.badge_table()
        self.assertEqual(table, {
            "d4:05:92:77:1b:3c": "mossby", "d4:05:92:77:1b:44": "puffkin", "d4:05:92:77:1b:34": "gilly",
            "d4:05:92:77:1c:b0": "mothwick", "d4:05:92:77:1b:30": "mudge", "d4:05:92:77:1b:40": "fizz",
            "d4:05:92:77:22:58": "hushwing", "d4:05:92:77:22:40": "tinker"})
        self.assertEqual(len(set(table.values())), 8)
        self.assertNotIn("grungo", table.values())

    def test_the_roster_is_the_nine_in_id_order(self):
        self.assertEqual(fb.species_rows(), ["grungo", "mossby", "puffkin", "gilly", "mothwick", "mudge", "fizz",
                                             "hushwing", "tinker"])

    def test_a_table_mac_advertises_its_species_and_any_other_its_hash(self):
        self.assertEqual(fb.expected_name("D4:05:92:77:1C:B0"), "mothwick")
        # The same MAC test_founding pins: badgeSpecies(d4:05:92:77:23:7c) is fizz.
        self.assertEqual(fb.expected_name("d4:05:92:77:23:7c"), "fizz")
        names = {fb.expected_name("02:00:00:00:%02x:%02x" % (n >> 8, n & 255)) for n in range(2048)}
        self.assertEqual(names, set(fb.species_rows()))


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
        self.assertTrue(all(r["ble_name"] == fb.expected_name(r["mac"]) for r in rows))
        for n in (4, 5, 6):
            self.assertIn("Plug in badge %d, press Enter (q to quit)" % n, text)
            self.assertIn("Unplug badge %d" % n, text)
        self.assertIn("Plug in badge 7", text)
        self.assertFalse(log.exists())


class WrongSpecies(unittest.TestCase):
    def test_a_board_advertising_another_name_fails(self):
        bench = fb.DryBench()
        bench.plug()
        port = next(iter(bench.badges()))
        bench.mac = lambda port: "d4:05:92:77:1b:34"
        bench.last = "d4:05:92:77:1b:34"
        bench.boot = lambda port: [line.replace("ble name=gilly", "ble name=blorb-1b34") for line in BOOT]
        with redirect_stdout(io.StringIO()), self.assertRaises(fb.Failed) as e:
            fb.one_badge(bench, 3, port, [])
        self.assertIn("advertises blorb-1b34, but this MAC's species is gilly", str(e.exception))

    def test_the_right_name_passes(self):
        bench = fb.DryBench()
        bench.plug()
        port = next(iter(bench.badges()))
        bench.mac = lambda port: "d4:05:92:77:1b:34"
        bench.boot = lambda port: BOOT
        with redirect_stdout(io.StringIO()):
            row = fb.one_badge(bench, 3, port, [])
        self.assertEqual((row["ble_name"], row["result"], row["note"]), ("gilly", "ok", ""))


class NotFresh(unittest.TestCase):
    def test_a_resumed_boot_fails_the_badge(self):
        bench = fb.DryBench()
        bench.plug()
        port = next(iter(bench.badges()))
        bench.boot = lambda port: [line.replace("boot=Fresh", "boot=Resumed") for line in BOOT]
        with redirect_stdout(io.StringIO()), self.assertRaises(fb.Failed) as e:
            fb.one_badge(bench, 1, port, [])
        self.assertIn("Resumed", str(e.exception))


if __name__ == "__main__":
    unittest.main()
