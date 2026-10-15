"""tools/flash_batch.py without a board: port detection, the boot log, keep or wipe, the clash check,
finding esptool and bash, and the whole loop on pretend boards.

    python3 -m unittest discover -s tests -p "test_*.py"
"""
import builtins
import csv
import io
import os
import re
import sys
import tempfile
import unittest
from contextlib import redirect_stdout
from pathlib import Path
from types import SimpleNamespace
from unittest import mock

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(ROOT, "tools"))
import flash_batch as fb  # noqa: E402

# What 0.4.0 prints (src/main.cpp reportBoot, ble.begin), as captured from a board.
BOOT = [
    "[hw] blorbarium 045ad8a fw=0.4.0 fmt=1 badge128",
    "[hw] storage pet: formatted: it was blank",
    "[hw] boot=Fresh occupant=egg gen=0 genome=5c1e0a9b age=0",
    "[hw] ble name=blorb-1b34 heap=136244 big=90100",
]
# 0.7.0 on a badge that kept its pet: the BLE name is the species.
RESUMED = [
    "[hw] blorbarium 1234abc fw=0.7.0 fmt=1 badge128",
    "[hw] storage pet: ok",
    "[hw] boot=Resumed occupant=creature gen=0 genome=8348e859 age=86400 species=gilly",
    "[hw] ble name=gilly heap=136244 big=90100",
]


def port(device, vid=None, pid=None, hwid="", serial="5C94095006"):
    return SimpleNamespace(device=device, vid=vid, pid=pid, hwid=hwid, serial_number=serial)


class Ports(unittest.TestCase):
    def test_the_ch343_is_matched_by_vid_and_pid_on_linux_and_windows(self):
        ports = [port("/dev/ttyACM0", 0x1A86, 0x55D3, "USB VID:PID=1A86:55D3 SER=5C94095006 LOCATION=1-1:1.0"),
                 port("/dev/ttyUSB0", 0x10C4, 0xEA60, "USB VID:PID=10C4:EA60 SER=0001", "0001"),
                 port("/dev/ttyS0", hwid="PNP0501", serial=None),
                 port("COM11", 0x1A86, 0x55D3, "USB VID:PID=1A86:55D3 SER=5C94094999", "5C94094999"),
                 port("/dev/ttyUSB1", 0x1A86, 0x7523, "USB VID:PID=1A86:7523", "")]   # a CH340, not a badge
        self.assertEqual(fb.badge_ports(ports), {"/dev/ttyACM0": "5C94095006", "COM11": "5C94094999"})

    def test_a_record_without_vid_falls_back_to_the_hwid_text(self):
        self.assertTrue(fb.is_badge(port("/dev/ttyACM3", hwid="USB VID:PID=1a86:55d3 SER=X")))
        self.assertFalse(fb.is_badge(port("/dev/ttyACM3", hwid="n/a")))

    def test_the_port_that_appeared_is_chosen(self):
        self.assertEqual(fb.choose_port({}, {"/dev/ttyACM0": "A"}), ("/dev/ttyACM0", "new"))
        self.assertEqual(fb.choose_port({"COM3": "X"}, {"COM3": "X", "COM14": "A"}), ("COM14", "new"))

    def test_one_badge_already_attached_is_used(self):
        self.assertEqual(fb.choose_port({"COM14": "A"}, {"COM14": "A"}), ("COM14", "already attached"))

    def test_nothing_or_several_old_ones_is_no_choice(self):
        self.assertEqual(fb.choose_port({}, {}), (None, "none"))
        self.assertEqual(fb.choose_port({"COM3": "X", "COM4": "Y"}, {"COM3": "X", "COM4": "Y"})[0], None)

    def test_two_new_at_once_is_refused(self):
        with self.assertRaises(fb.Failed):
            fb.choose_port({}, {"/dev/ttyACM0": "A", "/dev/ttyACM1": "B"})

    def test_no_badge_attached_says_none_found(self):
        with self.assertRaises(fb.Failed) as e:
            fb.wait_for_port(fb.DryBench(), {}, 0)
        self.assertIn("no badge found (none)", str(e.exception))


class Access(unittest.TestCase):
    def test_a_port_this_user_may_not_open_says_the_dialout_and_udev_fix(self):
        with self.assertRaises(fb.Failed) as e:
            fb.check_access("/dev/ttyACM0", access=lambda path, mode: False)
        self.assertIn("usermod -aG dialout", str(e.exception))
        self.assertIn('ATTRS{idVendor}=="1a86", ATTRS{idProduct}=="55d3"', str(e.exception))

    def test_an_open_port_or_a_com_port_passes(self):
        fb.check_access("/dev/ttyACM0", access=lambda path, mode: True)
        fb.check_access("COM11", access=lambda path, mode: False)

    def test_esptool_and_pyserial_permission_errors_are_recognised(self):
        self.assertTrue(fb.denied("could not open port /dev/ttyACM0: [Errno 13] Permission denied: '/dev/ttyACM0'"))
        self.assertFalse(fb.denied("Failed to connect to ESP32-S3: No serial data received."))


class Tools(unittest.TestCase):
    def test_platformio_s_esptool_comes_first_on_linux_and_windows(self):
        for py in ("penv/bin/python", "penv/Scripts/python.exe"):
            with tempfile.TemporaryDirectory() as home:
                home = Path(home)
                for f in (".platformio/" + py, ".platformio/packages/tool-esptoolpy/esptool.py"):
                    (home / f).parent.mkdir(parents=True, exist_ok=True)
                    (home / f).touch()
                got = fb.find_esptool(home, which=lambda name: "/usr/bin/esptool")
                self.assertEqual(got, [str(home / ".platformio" / py),
                                       str(home / ".platformio/packages/tool-esptoolpy/esptool.py")])

    def test_else_esptool_on_path(self):
        with tempfile.TemporaryDirectory() as home:
            paths = {"esptool.py": "/home/u/.local/bin/esptool.py"}
            self.assertEqual(fb.find_esptool(Path(home), which=paths.get, windows=False),
                             ["/home/u/.local/bin/esptool.py"])
            self.assertEqual(fb.find_esptool(Path(home), which=paths.get, windows=True)[1:],
                             ["/home/u/.local/bin/esptool.py"])

    def test_no_esptool_names_both_fixes(self):
        with tempfile.TemporaryDirectory() as home, self.assertRaises(fb.Failed) as e:
            fb.find_esptool(Path(home), which=lambda name: None)
        self.assertIn("pio run -e badge128", str(e.exception))
        self.assertIn("pipx install esptool", str(e.exception))

    def test_esptool_versions(self):
        self.assertEqual(fb.esptool_version("esptool.py v4.11.0\n4.11.0\n"), (4, 11))
        self.assertEqual(fb.esptool_version("esptool v5.6.0\n5.6.0"), (5, 6))
        self.assertLess(fb.esptool_version("esptool.py v4.5.1"), fb.MIN_ESPTOOL)
        self.assertIsNone(fb.esptool_version(""))

    def test_bash_is_the_system_s_on_linux_and_git_s_on_windows(self):
        self.assertEqual(fb.find_bash(which=lambda n: "/usr/bin/bash", windows=False), "/usr/bin/bash")
        self.assertEqual(fb.find_bash(which=lambda n: None, windows=False), "/bin/bash")
        with tempfile.TemporaryDirectory() as pf:
            git = Path(pf) / "Git/bin/bash.exe"
            git.parent.mkdir(parents=True)
            git.touch()
            wsl = "C:/Windows/System32/bash.exe"
            self.assertEqual(fb.find_bash(which=lambda n: wsl, windows=True, program_files=pf), str(git))


class BootLog(unittest.TestCase):
    def test_reads_0_4_0_s_fw_boot_genome_and_ble_name(self):
        got = fb.parse_boot(BOOT)
        self.assertEqual(got["fw"], "0.4.0 045ad8a")
        self.assertEqual((got["boot"], got["occupant"], got["gen"], got["genome"]), ("Fresh", "egg", 0, "5c1e0a9b"))
        self.assertEqual(got["ble_name"], "blorb-1b34")
        self.assertEqual(got["pet_storage"], "formatted: it was blank")
        self.assertNotIn("species", got)
        self.assertNotIn("alarms", got)

    def test_reads_0_7_0_s_species_from_the_ble_name(self):
        got = fb.parse_boot(RESUMED)
        self.assertEqual((got["ble_name"], got["species"], got["boot"]), ("gilly", "gilly", "Resumed"))

    def test_the_boot_line_s_species_wins_over_the_ble_name(self):
        got = fb.parse_boot(RESUMED[:3] + ["[hw] ble name=blorb-1b34 heap=1 big=1"])   # an image on OTA trial
        self.assertEqual((got["species"], got["boot_species"], got["ble_name"]), ("gilly", "gilly", "blorb-1b34"))

    def test_a_sealed_board_s_species_none_is_no_species(self):
        got = fb.parse_boot([RESUMED[2].replace("Resumed", "Sealed").replace("gilly", "none")])
        self.assertNotIn("species", got)

    def test_a_0_4_0_pet_adopting_his_badge_s_species_is_read(self):
        lines = [RESUMED[0], RESUMED[1], RESUMED[2].replace("species=gilly", "species=grungo"), RESUMED[3]]
        got = fb.parse_boot(lines + ["[hw] adopted species=gilly gen=0 genome=1a2b3c4d name=Pip "
                                     "(the Bluetooth name follows at the next boot)"])
        self.assertEqual((got["boot_species"], got["adopted"], got["species"], got["genome"]),
                         ("grungo", "gilly", "gilly", "8348e859"))

    def test_a_0_4_0_pet_with_no_adoption_seen_is_still_grungo(self):
        got = fb.parse_boot([RESUMED[2].replace("species=gilly", "species=grungo"), RESUMED[3]])
        self.assertEqual(got["species"], "grungo")
        self.assertIn("is grungo, should be gilly", fb.table_note("d4:05:92:77:1b:34", got["species"]))

    def test_a_sealed_board_is_an_alarm(self):
        got = fb.parse_boot(BOOT + ["[hw] !!! SEALED: a pet is on the flash and would not open; ..."])
        self.assertEqual(len(got["alarms"]), 1)

    def test_a_log_cut_short_lacks_the_name(self):
        self.assertNotIn("ble_name", fb.parse_boot(BOOT[:3]))


class KeepOrWipe(unittest.TestCase):
    def judge(self, wipe, boot, storage="ok", alarms=()):
        got = {"boot": boot, "pet_storage": storage}
        if alarms:
            got["alarms"] = list(alarms)
        return fb.judge_boot(wipe, got)

    def test_keep_wants_resumed(self):
        self.assertEqual(self.judge(False, "Resumed"), (True, ""))
        self.assertTrue(self.judge(False, "FellBack")[0])

    def test_keep_passes_fresh_only_on_a_board_that_never_had_a_pet(self):
        self.assertTrue(self.judge(False, "Fresh", "formatted: it was blank")[0])
        ok, why = self.judge(False, "Fresh", "ok")
        self.assertFalse(ok)
        self.assertIn("PET NOT FOUND", why)

    def test_keep_fails_a_pet_that_did_not_come_back(self):
        for boot in ("FromLineage", "Sealed", "ReadOnlyNewer"):
            self.assertFalse(self.judge(False, boot)[0], boot)

    def test_wipe_wants_fresh_only(self):
        self.assertEqual(self.judge(True, "Fresh", "formatted: it was blank"), (True, ""))
        self.assertFalse(self.judge(True, "Resumed")[0])

    def test_an_alarm_fails_either_mode(self):
        alarm = ["[hw] !!! the pet is not being saved: a reset loses this boot's pet"]
        self.assertFalse(self.judge(False, "Resumed", alarms=alarm)[0])
        self.assertFalse(self.judge(True, "Fresh", alarms=alarm)[0])


class Clashes(unittest.TestCase):
    rows = [{"badge": "1", "mac": "d4:05:92:77:1b:3c", "ble_name": "mossby", "species": "mossby", "result": "ok"},
            {"badge": "2", "mac": "d4:05:92:77:1b:44", "ble_name": "puffkin", "species": "puffkin", "result": "failed"},
            {"badge": "3", "mac": "d4:05:92:77:1b:34", "ble_name": "blorb-1b34", "genome": "8348e859", "result": "ok"}]

    def test_another_macs_species_is_flagged(self):
        notes = fb.clashes(self.rows, "d4:05:92:00:00:01", "mossby", "mossby")
        self.assertEqual(notes, ["DUPLICATE SPECIES mossby (badge 1, d4:05:92:77:1b:3c)"])

    def test_an_old_blorb_name_still_clashes_but_an_old_genome_does_not(self):
        self.assertEqual(len(fb.clashes(self.rows, "d4:05:92:00:00:01", "", "blorb-1b34")), 1)
        self.assertEqual(fb.clashes(self.rows, "d4:05:92:00:00:01", "gilly", "gilly"), [])

    def test_the_same_board_again_and_failed_rows_are_not(self):
        self.assertEqual(fb.clashes(self.rows, "d4:05:92:77:1b:3c", "mossby", "mossby"), [])
        self.assertEqual(fb.clashes(self.rows, "d4:05:92:00:00:01", "puffkin", "puffkin"), [])

    def test_a_known_badge_as_another_species_is_noted(self):
        self.assertIsNone(fb.table_note("d4:05:92:77:1b:34", "gilly"))
        self.assertIn("should be gilly", fb.table_note("d4:05:92:77:1b:34", "fizz"))
        self.assertIsNone(fb.table_note("d4:05:92:00:00:01", "fizz"))


class MacTable(unittest.TestCase):
    def test_it_is_the_decisions_d3_table(self):
        text = (Path(ROOT) / "docs/notes/decisions-2026-10-15.md").read_text()
        rows = re.findall(r"^\s*\| \d \| ([0-9a-f:]{17}) \| blorb-\w+ \| (\w+) \|", text, re.M)
        self.assertEqual(dict(rows), fb.BADGE_SPECIES)
        self.assertEqual(len(set(fb.BADGE_SPECIES.values())), 8)
        self.assertNotIn("grungo", fb.BADGE_SPECIES.values())
        self.assertTrue(set(fb.BADGE_SPECIES.values()) < set(fb.SPECIES))


class Log(unittest.TestCase):
    def test_a_log_with_the_old_header_gains_species_and_pet_and_keeps_its_rows(self):
        old = ["time", "badge", "port", "usb_serial", "mac", "ble_name", "boot", "genome", "fw", "result", "note"]
        with tempfile.TemporaryDirectory() as tmp:
            log = Path(tmp) / "flash_log.csv"
            with log.open("w", newline="") as f:
                w = csv.writer(f)
                w.writerow(old)
                w.writerow(["2026-10-15T05:52:22", "1", "COM14", "5C94095000", "d4:05:92:77:1b:3c", "blorb-1b3c",
                            "Fresh", "2ea98ea9", "0.4.0 aee2f23", "ok", ""])
            fb.append_log(log, {"badge": 1, "mac": "d4:05:92:77:1b:3c", "species": "mossby", "pet": "keep",
                                "result": "ok"})
            with log.open(newline="") as f:
                self.assertEqual(next(csv.reader(f)), fb.FIELDS)
            rows = fb.read_log(log)
        self.assertEqual([(r["ble_name"], r["genome"], r["species"], r["pet"]) for r in rows],
                         [("blorb-1b3c", "2ea98ea9", "", ""), ("", "", "mossby", "keep")])


class DryRun(unittest.TestCase):
    def run_loop(self, *flags):
        with tempfile.TemporaryDirectory() as tmp:
            log = Path(tmp) / "flash_log.csv"
            answers = iter(["", "", "", "q"])
            out = io.StringIO()
            with mock.patch.object(builtins, "input", lambda prompt="": (out.write(prompt), next(answers))[1]), \
                    redirect_stdout(out):
                self.assertEqual(fb.main(["--dry-run", "--log", str(log), "--start", "4", *flags]), 0)
            rows = fb.read_log(log.with_suffix(".dry-run.csv"))
            self.assertFalse(log.exists())
        return rows, out.getvalue()

    def test_three_badges_keep_their_pets_by_default(self):
        rows, text = self.run_loop()
        self.assertEqual([r["badge"] for r in rows], ["4", "5", "6"])
        self.assertTrue(all(r["result"] == "ok" and r["boot"] == "Resumed" and r["pet"] == "keep" for r in rows))
        self.assertEqual([r["species"] for r in rows], ["mossby", "puffkin", "gilly"])
        self.assertEqual([r["note"] for r in rows], ["the pet adopted species %s" % s for s in ("mossby", "puffkin", "gilly")])
        self.assertNotIn("!!", text)
        self.assertEqual(len({r["port"] for r in rows}), 3)
        self.assertIn("KEEPING each badge's pet", text)
        for n in (4, 5, 6):
            self.assertIn("Plug in badge %d (keep pet), press Enter (q to quit)" % n, text)
            self.assertIn("Unplug badge %d" % n, text)
        self.assertIn("Plug in badge 7", text)
        self.assertIn("would run: tools/flash_board.sh --no-reset", text)
        self.assertNotIn("flash_board.sh --wipe-pet", text)

    def test_wipe_pet_resets_each_one_to_fresh(self):
        rows, text = self.run_loop("--wipe-pet")
        self.assertTrue(all(r["result"] == "ok" and r["boot"] == "Fresh" and r["pet"] == "wipe" for r in rows))
        self.assertIn("WIPING each badge's pet", text)
        self.assertIn("Plug in badge 4 (wipe pet)", text)
        self.assertIn("flash_board.sh --wipe-pet --no-reset", text)


class OneBadge(unittest.TestCase):
    def bench(self, lines, wipe=False):
        bench = fb.DryBench(wipe)
        bench.plug()
        bench.boot = lambda port: lines
        return bench, next(iter(bench.badges()))

    def test_a_resumed_boot_fails_a_wipe(self):
        bench, p = self.bench(RESUMED, wipe=True)
        with redirect_stdout(io.StringIO()), self.assertRaises(fb.Failed) as e:
            fb.one_badge(bench, 1, p, [], wipe=True)
        self.assertIn("Resumed", str(e.exception))

    def test_a_fresh_boot_over_a_pet_fails_a_keep(self):
        bench, p = self.bench([line.replace("boot=Resumed", "boot=Fresh") for line in RESUMED])
        with redirect_stdout(io.StringIO()), self.assertRaises(fb.Failed) as e:
            fb.one_badge(bench, 1, p, [])
        self.assertIn("PET NOT FOUND", str(e.exception))

    def test_a_duplicate_species_is_noted(self):
        bench, p = self.bench([line.replace("gilly", "mossby") for line in RESUMED])   # badge 1, mossby
        rows = [{"badge": "9", "mac": "d4:05:92:00:00:09", "species": "mossby", "ble_name": "mossby", "result": "ok"}]
        with redirect_stdout(io.StringIO()):
            row = fb.one_badge(bench, 1, p, rows)
        self.assertIn("DUPLICATE SPECIES mossby", row["note"])
        self.assertEqual((row["mac"], row["species"], row["pet"]), ("d4:05:92:77:1b:3c", "mossby", "keep"))


if __name__ == "__main__":
    unittest.main()
