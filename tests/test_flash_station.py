"""scripts/flash_station.py's 2.4 GHz twin: which network a badge is given when the laptop is on 5 GHz.

    python3 -m unittest tests/test_flash_station.py
"""
import sys
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / "scripts"))
import flash_station as fs  # noqa: E402


class Twin(unittest.TestCase):
    def test_the_laptops_own_network_when_it_is_on_2_4_ghz_here(self):
        scan = [("Home", 2437, "WPA2"), ("Home", 5180, "WPA2"), ("Home-2G", 2412, "WPA2")]
        self.assertEqual(fs.twin_24ghz("Home", scan), "Home")

    def test_the_band_swapped_name_not_a_wpa3_only_look_alike(self):
        # The parents' house, 2026-10-28: the longest shared prefix tied between
        # the real twin and another router's WPA3-only network, and picked that one.
        scan = [("Garcia Family-5G", 5785, "WPA2"), ("Garcia Family-2G", 2462, "WPA2"),
                ("Garcia Family-2G Mesh-2.4G", 2437, "WPA3"), ("Garcia Family-2G Mesh-5G", 5180, "WPA3")]
        self.assertEqual(fs.twin_24ghz("Garcia Family-5G", scan), "Garcia Family-2G")

    def test_a_wpa3_only_network_is_never_the_twin_of_a_wpa2_one(self):
        scan = [("Home-5G", 5180, "WPA2"), ("Home-2G", 2437, "WPA3")]
        self.assertIsNone(fs.twin_24ghz("Home-5G", scan))

    def test_wpa3_only_for_a_laptop_on_wpa3_only(self):
        scan = [("Home-5G", 5180, "WPA3"), ("Home-2.4G", 2437, "WPA3")]
        self.assertEqual(fs.twin_24ghz("Home-5G", scan), "Home-2.4G")

    def test_the_name_with_the_band_dropped(self):
        scan = [("Home 5GHz", 5180, "WPA2"), ("Home", 2437, "WPA2"), ("Homestead", 2412, "WPA2")]
        self.assertEqual(fs.twin_24ghz("Home 5GHz", scan), "Home")

    def test_a_band_glued_on(self):
        scan = [("Home5G", 5180, "WPA2"), ("Home2.4G", 2437, "WPA2")]
        self.assertEqual(fs.twin_24ghz("Home5G", scan), "Home2.4G")

    def test_the_same_security_first(self):
        scan = [("Home-5G", 5180, "WPA2"), ("Home-2G", 2437, "WPA1 WPA2"), ("Home_2.4GHz", 2462, "WPA2")]
        self.assertEqual(fs.twin_24ghz("Home-5G", scan), "Home_2.4GHz")

    def test_no_band_in_the_name_no_guess(self):
        scan = [("Home", 5180, "WPA2"), ("Home-2G", 2437, "WPA2"), ("Homer", 2437, "WPA2")]
        self.assertIsNone(fs.twin_24ghz("Home", scan))

    def test_a_bare_digit_glued_on_is_part_of_the_name(self):
        self.assertEqual(fs.band_base("Apt5"), "apt5")
        self.assertEqual(fs.band_base("Apt 5"), "apt")
        self.assertEqual(fs.band_base("Home_2.4GHz"), "home")


class Scan(unittest.TestCase):
    def test_nmcli_terse_escapes(self):
        text = "Cafe\\:Net:5180 MHz:WPA2\n:2437 MHz:WPA2\nHome:2462 MHz:WPA2 WPA3\n"
        self.assertEqual(fs.parse_scan(text), [("Cafe:Net", 5180, "WPA2"), ("Home", 2462, "WPA2 WPA3")])


if __name__ == "__main__":
    unittest.main()
