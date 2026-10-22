"""tools/ble_ota.py without a radio: the framing, the manifest and its signature, choosing badges, the
tokens, and whole updates against pretend badges (a drop, lost writes, nobody answering, a rollback).

    python3 -m unittest discover -s tests -p "test_*.py"
"""
import asyncio
import hashlib
import json
import os
import sys
import tempfile
import time
import unittest
from pathlib import Path

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(ROOT, "tools"))
import ble_ota as bo  # noqa: E402

try:
    from cryptography.hazmat.primitives import hashes, serialization
    from cryptography.hazmat.primitives.asymmetric import ec
    from cryptography.hazmat.primitives.asymmetric.utils import decode_dss_signature
    HAVE_CRYPTO = True
except ImportError:
    HAVE_CRYPTO = False

COMMITTED = json.loads(Path(ROOT, "web/public/fw/lcd128/manifest.json").read_text())


def throwaway_release(version="0.7.13", size=20000, flags=0, key=None):
    """(manifest, image, public point): a release signed as tools/ota_release.py signs, by a throwaway key."""
    key = key or ec.generate_private_key(ec.SECP256R1())
    image = bytes([0xE9]) + bytes((i * 37 + 11) & 255 for i in range(size - 1))
    m = {"board": "lcd128", "version": version, "flags": flags, "fmt": 1, "size": size,
         "sha256": bo.b64url(hashlib.sha256(image).digest()), "sig": "", "image": "blorbarium-lcd128-%s.bin" % version}
    r, s = decode_dss_signature(key.sign(bo.signed_text(m), ec.ECDSA(hashes.SHA256())))
    m["sig"] = bo.b64url(r.to_bytes(32, "big") + s.to_bytes(32, "big"))
    point = key.public_key().public_bytes(serialization.Encoding.X962, serialization.PublicFormat.UncompressedPoint)
    return m, image, point


class Words(unittest.TestCase):
    def test_kv_reads_replies_and_events(self):
        self.assertEqual(bo.kv("state=idle board=lcd128 fw=0.7.11 chunk=240 trial=0 last=none rolledback=none"),
                         {"state": "idle", "board": "lcd128", "fw": "0.7.11", "chunk": "240", "trial": "0",
                          "last": "none", "rolledback": "none"})
        self.assertEqual(bo.kv("OTA at=4096 gap"), {"at": "4096"})

    def test_versions_compare_as_numbers(self):
        self.assertTrue(bo.is_newer("0.7.12", "0.7.9"))
        self.assertTrue(bo.is_newer("0.10.0", "0.9.9"))
        self.assertFalse(bo.is_newer("0.7.12", "0.7.12"))
        self.assertFalse(bo.is_newer("0.7", "0.6.0"))

    def test_the_image_id_is_the_hash_head_in_hex(self):
        self.assertEqual(bo.image_id(COMMITTED["sha256"]), bo.unb64url(COMMITTED["sha256"])[:4].hex())
        self.assertEqual(bo.image_id("glVjE8siM8qtBpgYfZQl7w8Ih5pXiQjnBa88lIVuKJw"), "82556313")

    def test_a_token_is_22_base64url_characters(self):
        t = bo.new_token()
        self.assertRegex(t, r"^[A-Za-z0-9_-]{22}$")
        self.assertNotEqual(t, bo.new_token())


class Framing(unittest.TestCase):
    def test_notifications_are_buffered_to_newlines(self):
        r = bo.LineReader()
        self.assertEqual(r.feed(b"#1 OK state=idle board=lc"), [])
        self.assertEqual(r.feed(b"d128 fw=0.7.11"), [])
        self.assertEqual(r.feed(b"\n"), ["#1 OK state=idle board=lcd128 fw=0.7.11"])
        self.assertEqual(r.feed(b"! OTA at=4096\r\n! OTA state=ready\n#2 O"), ["! OTA at=4096", "! OTA state=ready"])
        self.assertEqual(r.feed(b"K\n\n"), ["#2 OK"])

    def test_lines_are_replies_continuations_or_events(self):
        self.assertEqual(bo.parse_line("#1a OK fw=0.7.11"), ("reply", 0x1a, "OK", "fw=0.7.11"))
        self.assertEqual(bo.parse_line("#5 ERR 409 NOT_NEWER"), ("reply", 5, "ERR", "409 NOT_NEWER"))
        self.assertEqual(bo.parse_line("#2b + phase=creature"), ("reply", 0x2b, "+", "phase=creature"))
        self.assertEqual(bo.parse_line("#2b OK"), ("reply", 0x2b, "OK", ""))
        self.assertEqual(bo.parse_line("! OTA at=4320 gap"), ("event", "OTA at=4320 gap"))
        self.assertIsNone(bo.parse_line("[hw] up=12"))

    def test_an_error_has_its_code_and_text(self):
        e = bo.Err.parse("403 BAD_SIGNATURE")
        self.assertEqual((e.code, e.text), (403, "BAD_SIGNATURE"))

    def test_a_data_write_is_the_offset_little_endian_then_the_bytes(self):
        image = bytes(range(256)) * 4
        p = bo.packet(image, 0x0102, 240)
        self.assertEqual(p[:4], bytes([0x02, 0x01, 0, 0]))
        self.assertEqual(p[4:], image[0x102:0x102 + 240])
        self.assertEqual(len(bo.packet(image, 1000, 240)), 4 + 24)   # the last write is short

    def test_begin_is_docs_ota_md_and_fits_the_200_byte_cap(self):
        line = "#ffffff " + bo.begin_line(COMMITTED, "A" * 22)
        self.assertLess(len(line), 200)
        self.assertEqual(bo.begin_line(COMMITTED, "T" * 22).split()[:6],
                         ["OTA", "BEGIN", "lcd128", "0.7.12", "0", "1"])
        self.assertTrue(bo.begin_line(COMMITTED, "T" * 22).endswith(" " + COMMITTED["sig"] + " " + "T" * 22))


class Manifests(unittest.TestCase):
    def test_the_committed_manifest_parses(self):
        m = bo.parse_manifest(COMMITTED)
        self.assertEqual((m["version"], m["size"], m["fmt"]), (COMMITTED["version"], COMMITTED["size"], 1))

    def test_a_bad_manifest_says_what_is_wrong(self):
        for change, word in [({"board": "lcd146"}, "board"), ({"version": "0.7"}, "version"),
                             ({"sig": "abc"}, "sig"), ({"size": 0}, "size"), ({"image": ""}, "image"),
                             ({"flags": -1}, "flags")]:
            with self.assertRaisesRegex(ValueError, word):
                bo.parse_manifest({**COMMITTED, **change})

    def test_the_image_must_match_size_and_hash(self):
        image = b"\xe9" + b"x" * 99
        m = {**COMMITTED, "size": 100, "sha256": bo.b64url(hashlib.sha256(image).digest())}
        bo.check_image(m, image)
        with self.assertRaisesRegex(ValueError, "bytes"):
            bo.check_image(m, image[:-1])
        with self.assertRaisesRegex(ValueError, "hash"):
            bo.check_image(m, image[:-1] + b"y")

    def test_the_signed_text_is_ota_release_pys(self):
        self.assertEqual(bo.signed_text(COMMITTED), ("blorbota2 lcd128 %s 0 1 %d %s" % (
            COMMITTED["version"], COMMITTED["size"], COMMITTED["sha256"])).encode())

    def test_the_public_key_is_read_from_the_header(self):
        point = bo.embedded_point(bo.PUBKEY_H.read_text())
        self.assertEqual((len(point), point[0]), (65, 4))

    @unittest.skipUnless(HAVE_CRYPTO, "needs the cryptography package")
    def test_the_released_manifest_verifies_against_the_key_in_the_tree_and_a_changed_one_does_not(self):
        point = bo.embedded_point(bo.PUBKEY_H.read_text())
        self.assertIs(bo.verify_signature(COMMITTED, point), True)
        self.assertIs(bo.verify_signature({**COMMITTED, "flags": 1}, point), False)   # no downgrade without the key
        self.assertIs(bo.verify_signature({**COMMITTED, "version": "0.7.99"}, point), False)
        sig = COMMITTED["sig"]
        flipped = sig[:10] + ("A" if sig[10] != "A" else "B") + sig[11:]
        self.assertIs(bo.verify_signature({**COMMITTED, "sig": flipped}, point), False)


@unittest.skipUnless(HAVE_CRYPTO, "needs the cryptography package")
class Releases(unittest.TestCase):
    def setUp(self):
        self.dir = Path(tempfile.mkdtemp())
        self.m, self.image, self.point = throwaway_release()
        self.pub = self.dir / "ota_pubkey.h"
        self.pub.write_text("inline constexpr uint8_t kOtaPublicKey[65] = {%s};" % ", ".join(
            "0x%02x" % b for b in self.point))

    def test_a_local_image_with_its_manifest_beside_it(self):
        (self.dir / self.m["image"]).write_bytes(self.image)
        (self.dir / "manifest.json").write_text(json.dumps(self.m))
        rel = bo.load_release(image_path=self.dir / self.m["image"], pubkey_h=self.pub)
        self.assertEqual((rel.manifest["version"], rel.signature, rel.image), ("0.7.13", True, self.image))
        rel = bo.load_release(str(self.dir / "manifest.json"), pubkey_h=self.pub)   # the manifest finds its image
        self.assertEqual(rel.image, self.image)

    def test_an_image_without_a_manifest_is_refused_with_why(self):
        (self.dir / "firmware.bin").write_bytes(self.image)
        with self.assertRaisesRegex(ValueError, "no signature.*ota_release"):
            bo.load_release(image_path=self.dir / "firmware.bin", pubkey_h=self.pub)

    def test_a_manifest_for_another_image_is_refused(self):
        (self.dir / self.m["image"]).write_bytes(self.image[:-1] + b"\0")
        (self.dir / "manifest.json").write_text(json.dumps(self.m))
        with self.assertRaisesRegex(ValueError, "hash"):
            bo.load_release(str(self.dir / "manifest.json"), pubkey_h=self.pub)

    def test_a_release_by_another_key_is_marked(self):
        (self.dir / self.m["image"]).write_bytes(self.image)
        (self.dir / "manifest.json").write_text(json.dumps(self.m))
        rel = bo.load_release(str(self.dir / "manifest.json"))   # the tree's own key: not the throwaway's
        self.assertIs(rel.signature, False)

    def test_the_site_image_is_fetched_beside_its_manifest(self):
        got = {}

        def fetch(url):
            got.setdefault("urls", []).append(url)
            return json.dumps(self.m).encode() if url.endswith("manifest.json") else self.image
        rel = bo.load_release(fetch=fetch, pubkey_h=self.pub)
        self.assertEqual(got["urls"], [bo.SITE, "https://blorb.hypnodroid.com/fw/lcd128/" + self.m["image"]])
        self.assertTrue(rel.signature)


class Choosing(unittest.TestCase):
    SEEN = [bo.Seen("D4:05:92:77:1B:31", "mudge", -40), bo.Seen("D4:05:92:77:1B:3D", "mossby", -60),
            bo.Seen("D4:05:92:77:22:41", "grungo", -70), bo.Seen("D4:05:92:77:23:7D", "grungo", -50)]

    def test_by_name_or_address_in_the_order_given(self):
        got, problems = bo.pick_targets(["mossby", "d4:05:92:77:1b:31"], self.SEEN)
        self.assertEqual(([s.name for s in got], problems), (["mossby", "mudge"], []))

    def test_a_shared_name_or_a_missing_badge_is_a_problem_not_a_guess(self):
        got, problems = bo.pick_targets(["grungo", "gilly", "mudge", "D4:05:92:77:1B:31"], self.SEEN)
        self.assertEqual([s.name for s in got], ["mudge"])
        self.assertEqual(len(problems), 3)
        self.assertIn("2 badges", problems[0])
        self.assertIn("not seen", problems[1])
        self.assertIn("twice", problems[2])

    def test_a_badge_is_the_uart_service_or_its_name_never_the_whole_advertisement(self):
        self.assertTrue(bo.is_badge("mossby", [bo.NUS.upper()]))
        self.assertTrue(bo.is_badge("blorb-1b3c", [bo.NUS]))
        self.assertTrue(bo.is_badge("mossby", []))                         # the UUID pushed out by more ad data
        self.assertTrue(bo.is_badge("", [bo.NUS, "00467768-6228-2272-4663-277478268000"]))   # Improv beside it
        self.assertFalse(bo.is_badge("Pixel 8", ["0000fe9f-0000-1000-8000-00805f9b34fb"]))
        self.assertFalse(bo.is_badge(None, []))

    def test_the_bluetooth_address_is_the_table_mac_plus_one(self):
        self.assertEqual(bo.bt_to_badge("D4:05:92:77:1B:31"), ("mudge", "d4:05:92:77:1b:30"))   # seen on 2026-10-20
        self.assertEqual(bo.bt_to_badge("D4:05:92:77:1B:3D"), ("mossby", "d4:05:92:77:1b:3c"))
        self.assertEqual(bo.bt_to_badge("D4:05:92:77:00:01")[0], None)

    def test_the_question_says_turn_from_0_7_11_and_press_before(self):
        self.assertIn("Turn me around", bo.confirm_hint("0.7.11"))
        self.assertIn("Turn me around", bo.confirm_hint("0.8.0"))
        self.assertIn("Press BOOT", bo.confirm_hint("0.7.10"))
        self.assertIn("Press BOOT", bo.confirm_hint(None))

    def test_tokens_are_kept_per_badge_and_version_across_runs(self):
        path = Path(tempfile.mkdtemp()) / "t.json"
        a = bo.TokenStore(path)
        t = a("d4:05:92:77:1b:31", "0.7.12")
        self.assertEqual(bo.TokenStore(path)("D4:05:92:77:1B:31", "0.7.12"), t)
        self.assertNotEqual(a("D4:05:92:77:1B:31", "0.7.13"), t)
        self.assertIsNone(a.for_badge("D4:05:92:77:1B:3D")("0.7.12", False))   # CONFIRM never invents one


class Quiet:
    def say(self, s):
        self.lines = getattr(self, "lines", []) + [s]

    def progress(self, at, size):
        pass

    def reset(self):
        pass


FAST = bo.Timing(stall=0.05, confirm_poll=0.01, reboot_wait=1.0, trial_wait=2.0, trial_poll=0.01, too_soon=0.03)


@unittest.skipUnless(HAVE_CRYPTO, "needs the cryptography package")
class Updates(unittest.TestCase):
    """Whole updates against FakeBadge, lib/ota's Session in Python, with its clocks at 1/100."""

    @classmethod
    def setUpClass(cls):
        cls.key = ec.generate_private_key(ec.SECP256R1())
        cls.m, cls.image, cls.point = throwaway_release(key=cls.key)

    def badge(self, fw="0.7.11", **kw):
        kw.setdefault("hands", 2.0)
        return bo.FakeBadge("mossby", "D4:05:92:77:1B:3D", fw, self.point, scale=0.01, **kw)

    def push(self, badge, m=None, store=None):
        rel = bo.Release(m or self.m, self.image, "test", True)
        store = store or bo.TokenStore(None)
        port = bo.FakePort(badge)

        async def go():
            await port.open()
            return await bo.update_one(port, rel, store.for_badge(badge.address), Quiet(), FAST)
        out = asyncio.run(asyncio.wait_for(go(), 10))
        return out, port

    def sent(self, badge, word):
        return [line for line in badge.log if word in line]

    def test_an_update_is_kept_after_the_restart_and_confirm(self):
        b = self.badge()
        out, port = self.push(b)
        self.assertEqual((out["ok"], out["kind"], out["fw"], out["before"]), (True, "updated", "0.7.13", "0.7.11"))
        self.assertEqual(len(self.sent(b, "OTA BEGIN")), 1)
        self.assertEqual(len(self.sent(b, "OTA REBOOT")), 1)
        self.assertEqual(len(self.sent(b, "OTA CONFIRM")), 1)
        self.assertEqual((b.fw, b.trial, bytes(b.got)), ("0.7.13", False, self.image))

    def test_a_drop_at_40_percent_and_lost_writes_resume_with_the_same_begin(self):
        b = self.badge(drop_at=0.4, lose=(10, 11, 50))
        out, port = self.push(b)
        self.assertEqual(out["kind"], "updated")
        begins = self.sent(b, "OTA BEGIN")
        self.assertEqual(len(begins), 2)
        self.assertEqual(begins[0].split(" ", 1)[1], begins[1].split(" ", 1)[1])   # the same image and token
        self.assertEqual(port.dropped, 2)   # the drop, then the restart
        self.assertEqual(bytes(b.got), self.image)

    def test_nobody_answering_the_question_changes_nothing(self):
        b = self.badge(hands=None)
        out, _ = self.push(b)
        self.assertEqual((out["ok"], out["reason"]), (False, "no_confirm"))
        self.assertEqual(b.fw, "0.7.11")
        self.assertIn("30 s", bo.why(out["reason"]))

    def test_a_badge_already_on_the_release_is_left_alone(self):
        b = self.badge(fw="0.7.13")
        out, _ = self.push(b)
        self.assertEqual((out["ok"], out["kind"]), (True, "up_to_date"))
        self.assertEqual(self.sent(b, "OTA BEGIN"), [])

    def test_an_image_that_fails_its_trial_is_reported_rolled_back(self):
        b = self.badge(fail_trial=True)
        out, _ = self.push(b)
        self.assertEqual((out["ok"], out["kind"], out["fw"]), (True, "rolled_back", "0.7.11"))
        self.assertEqual(b.rolledback, "0.7.13")

    def test_another_keys_image_is_refused_by_the_badge(self):
        _, _, other = throwaway_release()
        b = bo.FakeBadge("mossby", "D4:05:92:77:1B:3D", "0.7.11", other, scale=0.01)
        out, _ = self.push(b)
        self.assertEqual((out["ok"], out["reason"]), (False, "BAD_SIGNATURE"))

    def test_a_badge_with_no_updater_needs_a_cable(self):
        out, _ = self.push(self.badge(fw="0.3.0", updater=False))
        self.assertEqual((out["ok"], out["reason"]), (False, "UNKNOWN_VERB"))
        self.assertIn("cable", bo.why(out["reason"]))

    def test_a_busy_pet_refuses(self):
        out, _ = self.push(self.badge(busy=True))
        self.assertEqual(out["reason"], "BUSY")

    def test_a_signature_checked_just_now_is_waited_out_once(self):
        b = self.badge()
        b.last_verify = time.monotonic()
        out, _ = self.push(b)
        self.assertEqual(out["kind"], "updated")
        self.assertEqual(len(self.sent(b, "OTA BEGIN")), 2)

    def test_another_phones_or_a_wifi_install_in_progress_is_reported_and_never_aborted(self):
        m2, _, _ = throwaway_release(version="0.7.14", key=self.key)
        b = self.badge(hands=None)
        self.assertIn("OK state=confirm", b.handle("#9 " + bo.begin_line(m2, "P" * 22)))   # a phone's, waiting for its yes
        out, _ = self.push(b)
        self.assertEqual((out["ok"], out["reason"]), (False, "OTA_ACTIVE"))
        self.assertEqual(self.sent(b, "OTA ABORT"), [])
        self.assertEqual(len(self.sent(b, "OTA BEGIN")), 2)   # the phone's and ours, once: no retrying
        self.assertEqual(b.state, "confirm")
        self.assertIn("Wi-Fi", bo.why(out["reason"]))

    def test_the_same_image_under_another_token_is_not_yours(self):
        b = self.badge(hands=None)
        b.handle("#9 " + bo.begin_line(self.m, "P" * 22))
        out, _ = self.push(b)
        self.assertEqual((out["ok"], out["reason"]), (False, "NOT_YOURS"))
        self.assertEqual(self.sent(b, "OTA ABORT"), [])

    def test_an_older_session_this_laptop_began_is_aborted_for_the_new_release(self):
        store = bo.TokenStore(None)
        m2, _, _ = throwaway_release(version="0.7.12", key=self.key)
        b = self.badge(hands=None)
        b.handle("#9 " + bo.begin_line(m2, store(b.address, "0.7.12")))   # this laptop's, from an earlier run
        store.note(b.address, bo.image_id(m2["sha256"]), "0.7.12")
        b.hands = 2.0
        out, _ = self.push(b, store=store)
        self.assertEqual((out["ok"], out["kind"], out["fw"]), (True, "updated", "0.7.13"))
        self.assertEqual(len(self.sent(b, "OTA ABORT")), 1)

    def test_this_laptops_own_trial_is_confirmed_on_connect(self):
        store = bo.TokenStore(None)
        b = self.badge()
        b.trial, b.fw, b.old, b.name_before = True, "0.7.13", "0.7.11", "mossby"
        b.handover = store(b.address, "0.7.13")
        out, _ = self.push(b, store=store)
        self.assertEqual((out["ok"], out["kind"]), (True, "updated"))
        self.assertEqual(self.sent(b, "OTA BEGIN"), [])

    def test_an_older_update_of_ours_on_trial_is_confirmed_and_waited_out_then_the_new_one_goes(self):
        store = bo.TokenStore(None)
        b = self.badge()
        b.trial, b.fw, b.old, b.name_before = True, "0.7.12", "0.7.11", "mossby"
        b.handover = store(b.address, "0.7.12")
        out, _ = self.push(b, store=store)
        self.assertEqual((out["ok"], out["kind"], out["fw"]), (True, "updated", "0.7.13"))
        self.assertGreaterEqual(len(self.sent(b, "OTA CONFIRM")), 2)   # 0.7.12's (safe to repeat), then 0.7.13's

    def test_a_gap_sends_the_writes_back_to_its_at(self):
        class Port:
            pass
        u = bo.Updater(Port(), COMMITTED, b"", lambda v, make=True: "T" * 22)
        u.dish["state"] = "receiving"
        u.event("OTA at=4096 gap")
        self.assertEqual((u.rewind, u.rewinds, u.dish["at"]), (4096, 1, 4096))
        u.event("OTA at=8192")
        self.assertEqual((u.rewind, u.rewinds, u.dish["at"]), (4096, 1, 8192))
        u.event("OTA state=ready")
        self.assertEqual(u.next(), "finishing")

    def test_someone_elses_trial_is_left_to_itself(self):
        b = self.badge()
        b.trial, b.fw, b.handover = True, "0.7.13", "P" * 22
        out, _ = self.push(b)
        self.assertEqual((out["ok"], out["reason"]), (False, "TRIAL"))
        self.assertEqual(self.sent(b, "OTA CONFIRM"), [])


class DryRun(unittest.TestCase):
    def test_the_fleet_covers_a_turn_a_press_and_one_up_to_date(self):
        fleet = bo.fake_fleet(None, {"version": "0.7.12"}, None)
        self.assertEqual([(b.name, b.fw) for b in fleet], [("mossby", "0.7.11"), ("puffkin", "0.7.10"),
                                                           ("gilly", "0.7.12")])
        self.assertEqual(fleet[0].address, "D4:05:92:77:1B:3D")


if __name__ == "__main__":
    unittest.main()
