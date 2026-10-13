"""tools/shot.py against the encoder's own output, test/fixtures/shot_v1.txt
(test/test_shot holds the encoder to that file byte for byte).

    python3 -m unittest discover -s tests -p "test_*.py"
"""
import os
import sys
import unittest

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(ROOT, "tools"))
import shot  # noqa: E402

FIXTURE = os.path.join(ROOT, "test", "fixtures", "shot_v1.txt")


def fixture():
    with open(FIXTURE) as f:
        return f.read().splitlines()


class Decode(unittest.TestCase):
    def test_the_encoders_lines_decode_to_the_sampler_canvas(self):
        px = shot.decode(fixture())
        self.assertEqual(len(px), 240 * 240)
        self.assertEqual(px[0], 0x0841)
        self.assertEqual(px[240 * 10 : 240 * 10 + 4], [0xF801, 0xF802, 0xF802, 0xF801])
        self.assertEqual(px[240 * 40 + 3], 21)
        self.assertEqual(px[-1], 0xFFFF)

    def test_log_lines_between_shot_lines_are_skipped(self):
        lines = fixture()
        lines.insert(3, "[hw] up=12 heap=136000 occupant=creature")
        lines.insert(1, "#4 OK")
        self.assertEqual(shot.decode(lines), shot.decode(fixture()))

    def test_a_damaged_line_fails_the_crc_rather_than_writing_a_wrong_png(self):
        lines = fixture()
        body = lines[2]
        lines[2] = body[:20] + ("A" if body[20] != "A" else "B") + body[21:]
        with self.assertRaises(shot.ShotError):
            shot.decode(lines)

    def test_a_lost_line_is_caught(self):
        lines = fixture()
        del lines[2]
        with self.assertRaises(shot.ShotError):
            shot.decode(lines)


if __name__ == "__main__":
    unittest.main()
