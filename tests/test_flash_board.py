"""tools/flash_board.sh against a pretend esptool: what it would run, and what it refuses before writing.

A fake HOME holds a fake PlatformIO penv whose `python` answers for both the USB serial lookup and
esptool.py, and writes each call to a log. No port is opened.

    python3 -m unittest tests/test_flash_board.py
"""
import os
import shutil
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / "tools"))
import flash_batch as fb  # noqa: E402

FAKE_PYTHON = r"""#!/usr/bin/env bash
echo "$*" | tr '\n' ' ' >> "$FAKE_LOG"; echo >> "$FAKE_LOG"
if [ "$1" = -c ]; then echo "$FAKE_SERIAL"; exit 0; fi
case " $* " in
  *" version "*) echo "esptool.py v$FAKE_VERSION"; echo "$FAKE_VERSION";;
  *" read_mac "*) echo "MAC: $FAKE_MAC";;
esac
"""
SERIAL, MAC = "5C94095006", "d4:05:92:77:1b:34"


class FlashBoard(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.home = Path(self.tmp.name)
        py = self.home / ".platformio/penv/bin/python"
        py.parent.mkdir(parents=True)
        py.write_text(FAKE_PYTHON, newline="\n")
        py.chmod(0o755)
        script = self.home / ".platformio/packages/tool-esptoolpy/esptool.py"
        script.parent.mkdir(parents=True)
        script.touch()
        self.image = self.home / "merged.hex"
        self.image.write_text(":00000001FF\n")
        self.log = self.home / "calls.log"

    def tearDown(self):
        self.tmp.cleanup()

    def run_board(self, *args, serial=SERIAL, mac=MAC, version="4.11.0", port="COM11", image=None):
        env = dict(os.environ, HOME=self.home.as_posix(), FAKE_LOG=self.log.as_posix(),
                   FAKE_SERIAL=serial, FAKE_MAC=mac, FAKE_VERSION=version)
        r = subprocess.run([fb.find_bash(), (ROOT / "tools/flash_board.sh").as_posix(), *args, port, SERIAL, MAC,
                            (image or self.image).as_posix()], env=env, capture_output=True, text=True)
        calls = [c.strip() for c in self.log.read_text().splitlines()] if self.log.exists() else []
        return r.returncode, r.stdout + r.stderr, calls

    def test_keep_checks_serial_and_mac_then_writes_the_hex_and_erases_nothing(self):
        code, out, calls = self.run_board("--no-reset")
        self.assertEqual(code, 0, out)
        self.assertIn("list_ports", calls[1])
        self.assertTrue(calls[2].endswith("--chip esp32s3 --port COM11 --after no_reset read_mac"), calls[2])
        self.assertTrue(calls[-1].endswith("--after no_reset -b 921600 write_flash 0x0 " + self.image.as_posix()), calls[-1])
        self.assertFalse(any("erase" in c for c in calls))

    def test_wipe_erases_pet_and_petfs_before_writing(self):
        code, out, calls = self.run_board("--wipe-pet")
        self.assertEqual(code, 0, out)
        erases = [c.split("erase_region ")[1] for c in calls if "erase_region" in c]
        self.assertEqual(erases, ["0x810000 0x40000", "0xA50000 0x5A0000"])
        self.assertIn("write_flash", calls[-1])

    def test_another_usb_serial_is_refused_before_anything_is_written(self):
        code, out, calls = self.run_board(serial="5C94094999")
        self.assertEqual(code, 1)
        self.assertIn("is USB serial 5C94094999, not %s" % SERIAL, out)
        self.assertFalse(any("write_flash" in c or "read_mac" in c for c in calls))

    def test_another_mac_is_refused(self):
        code, out, calls = self.run_board(mac="d4:05:92:77:1b:44")
        self.assertEqual(code, 1)
        self.assertIn("answers with MAC d4:05:92:77:1b:44", out)
        self.assertFalse(any("write_flash" in c for c in calls))

    def test_an_esptool_older_than_4_7_is_refused(self):
        code, out, calls = self.run_board(version="4.5.1")
        self.assertEqual(code, 1)
        self.assertIn("need 4.7 or newer", out)
        self.assertFalse(any("write_flash" in c for c in calls))

    def test_a_flat_image_is_refused(self):
        flat = self.home / "firmware.bin"
        flat.touch()
        code, out, _ = self.run_board(image=flat)
        self.assertEqual(code, 1)
        self.assertIn("not a merged .hex", out)

    @unittest.skipIf(shutil.which("esptool") or shutil.which("esptool.py"), "an esptool is on PATH here")
    def test_no_esptool_names_both_fixes(self):
        shutil.rmtree(self.home / ".platformio")
        code, out, _ = self.run_board()
        self.assertEqual(code, 1)
        self.assertIn("pio run -e badge128", out)
        self.assertIn("pipx install esptool", out)

    @unittest.skipIf(os.name == "nt", "a Linux port path")
    def test_a_missing_linux_port_is_named(self):
        code, out, calls = self.run_board(port="/dev/ttyACM99")
        self.assertEqual(code, 1)
        self.assertIn("no /dev/ttyACM99", out)
        self.assertFalse(any("read_mac" in c for c in calls))


if __name__ == "__main__":
    unittest.main()
