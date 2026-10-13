"""Signing and releasing firmware for updates over Bluetooth (docs/ota.md).

    uv run --with cryptography python tools/ota_release.py keygen
    uv run --with cryptography python tools/ota_release.py release [--allow-older] [--no-build]
    uv run --with cryptography python tools/ota_release.py check web/public/fw/lcd128/manifest.json
    uv run --with cryptography python tools/ota_release.py fixture

keygen makes the signing key, outside the repo, and writes its public half
into the firmware (src/hw/ota_pubkey.h). release builds the 1.28's firmware,
signs it, and places the manifest and image under web/public/fw/ for the site
to publish. Every dish built with that public key accepts only what this key
signs, so losing the key means the next update goes over a cable.
"""
import argparse
import base64
import hashlib
import json
import os
import re
import subprocess
import sys
from pathlib import Path

from cryptography.hazmat.primitives import hashes, serialization
from cryptography.hazmat.primitives.asymmetric import ec
from cryptography.hazmat.primitives.asymmetric.utils import decode_dss_signature, encode_dss_signature

ROOT = Path(__file__).resolve().parent.parent
DEFAULT_KEY = Path.home() / ".config" / "blorbarium" / "ota-signing-key.pem"
PUBKEY_H = ROOT / "src" / "hw" / "ota_pubkey.h"
VERSION_H = ROOT / "lib" / "blorb" / "include" / "blorb" / "protocol.h"
FIXTURE_H = ROOT / "test" / "support" / "ota_fixture.h"
BOARDS = {"lcd128": "badge128"}   # board name in images -> PlatformIO env


def b64url(b: bytes) -> str:
    return base64.urlsafe_b64encode(b).rstrip(b"=").decode()


def unb64url(s: str) -> bytes:
    return base64.urlsafe_b64decode(s + "=" * (-len(s) % 4))


def signed_text(board, version, flags, size, sha256: bytes) -> bytes:
    return f"blorbota1 {board} {version} {flags} {size} {b64url(sha256)}".encode()


def sign(key, text: bytes) -> bytes:
    r, s = decode_dss_signature(key.sign(text, ec.ECDSA(hashes.SHA256())))
    return r.to_bytes(32, "big") + s.to_bytes(32, "big")


def point(public_key) -> bytes:
    return public_key.public_bytes(serialization.Encoding.X962, serialization.PublicFormat.UncompressedPoint)


def c_bytes(b: bytes) -> str:
    rows = [", ".join(f"0x{x:02x}" for x in b[i:i + 13]) for i in range(0, len(b), 13)]
    return "\n    " + ",\n    ".join(rows) + ",\n"


def load_key(path: Path):
    if not path.exists():
        sys.exit(f"no signing key at {path}. Make one with `keygen`, or pass --key.")
    return serialization.load_pem_private_key(path.read_bytes(), password=None)


def embedded_point() -> bytes:
    text = PUBKEY_H.read_text()
    return bytes(int(x, 16) for x in re.findall(r"0x([0-9a-f]{2})", text.split("{", 1)[1]))


def firmware_version() -> str:
    m = re.search(r'kFirmwareVersion = "(\d+\.\d+\.\d+)"', VERSION_H.read_text())
    if not m:
        sys.exit(f"no kFirmwareVersion in {VERSION_H}")
    return m.group(1)


def write_pubkey_header(pub: bytes):
    PUBKEY_H.write_text(
        "#pragma once\n"
        "// The public half of the owner's update signing key, written by\n"
        "// `tools/ota_release.py keygen`. The dish accepts only images this key's\n"
        "// private half signed (docs/ota.md \"Signing\"). The private half is never in the repo.\n"
        "#include <cstdint>\n\n"
        "namespace hw {\n"
        f"inline constexpr uint8_t kOtaPublicKey[65] = {{{c_bytes(pub)}}};\n"
        "}  // namespace hw\n", newline="\n")


def cmd_keygen(args):
    path = Path(args.key).expanduser()
    if path.exists():
        key = load_key(path)
        print(f"{path} exists; keeping it")
    else:
        path.parent.mkdir(parents=True, exist_ok=True)
        key = ec.generate_private_key(ec.SECP256R1())
        pem = key.private_bytes(serialization.Encoding.PEM, serialization.PrivateFormat.PKCS8,
                                serialization.NoEncryption())
        fd = os.open(path, os.O_WRONLY | os.O_CREAT | os.O_EXCL, 0o600)
        with os.fdopen(fd, "wb") as f:
            f.write(pem)
        print(f"wrote {path}. Back it up: without it the next update needs a cable.")
    write_pubkey_header(point(key.public_key()))
    print(f"wrote {PUBKEY_H.relative_to(ROOT)}")


def cmd_release(args):
    key = load_key(Path(args.key).expanduser())
    if point(key.public_key()) != embedded_point():
        sys.exit(f"{PUBKEY_H.relative_to(ROOT)} holds another key's public half: a dish built from this tree "
                 "would refuse what this key signs. Run `keygen` with this key first.")
    board, env = args.board, BOARDS[args.board]
    if not args.no_build:
        pio = Path.home() / ".platformio" / "penv" / ("Scripts/pio.exe" if os.name == "nt" else "bin/pio")
        subprocess.run([str(pio), "run", "-e", env], cwd=ROOT, check=True)
    image = (ROOT / ".pio" / "build" / env / "firmware.bin").read_bytes()
    if image[0] != 0xE9:
        sys.exit("firmware.bin does not start with an ESP image header")
    version, flags = firmware_version(), 1 if args.allow_older else 0
    sha = hashlib.sha256(image).digest()
    sig = sign(key, signed_text(board, version, flags, len(image), sha))
    out = ROOT / "web" / "public" / "fw" / board
    out.mkdir(parents=True, exist_ok=True)
    name = f"blorbarium-{board}-{version}.bin"
    (out / name).write_bytes(image)
    manifest = {"board": board, "version": version, "flags": flags, "size": len(image),
                "sha256": b64url(sha), "sig": b64url(sig), "image": name}
    (out / "manifest.json").write_text(json.dumps(manifest, indent=1) + "\n", newline="\n")
    print(f"released {board} {version} ({len(image)} bytes) to {out.relative_to(ROOT)}")
    check(out / "manifest.json", key.public_key())


def check(manifest_path: Path, public_key=None):
    m = json.loads(manifest_path.read_text())
    image = (manifest_path.parent / m["image"]).read_bytes()
    sha = hashlib.sha256(image).digest()
    if len(image) != m["size"] or b64url(sha) != m["sha256"]:
        sys.exit("the image does not match its manifest")
    if public_key is None:
        public_key = ec.EllipticCurvePublicKey.from_encoded_point(ec.SECP256R1(), embedded_point())
    raw = unb64url(m["sig"])
    der = encode_dss_signature(int.from_bytes(raw[:32], "big"), int.from_bytes(raw[32:], "big"))
    public_key.verify(der, signed_text(m["board"], m["version"], m["flags"], m["size"], sha),
                      ec.ECDSA(hashes.SHA256()))
    print(f"ok: {m['board']} {m['version']} verifies against {PUBKEY_H.relative_to(ROOT)}")


def cmd_check(args):
    check(Path(args.manifest))


def cmd_fixture(args):
    """A signed release from a throwaway key, so the C++ verifier is held to this tool's output."""
    key = ec.generate_private_key(ec.SECP256R1())
    image = bytes((i * 37 + 11) & 255 for i in range(1000))
    sha = hashlib.sha256(image).digest()
    sig = sign(key, signed_text("lcd128", "0.5.0", 0, len(image), sha))
    begin = f"OTA BEGIN lcd128 0.5.0 0 {len(image)} {b64url(sha)} {b64url(sig)}"
    FIXTURE_H.write_text(
        "#pragma once\n"
        "// Written by `tools/ota_release.py fixture`: a release signed by a throwaway\n"
        "// key, so test_ota holds the dish's verifier to the release tool's output.\n"
        "#include <cstdint>\n\n"
        "namespace otafixture {\n"
        f"inline constexpr uint8_t kPublicKey[65] = {{{c_bytes(point(key.public_key()))}}};\n"
        f"inline constexpr const char* kBegin = \"{begin}\";\n"
        f"inline constexpr uint32_t kImageSize = {len(image)};\n"
        "inline uint8_t imageByte(uint32_t i) { return uint8_t((i * 37 + 11) & 255); }\n"
        "}  // namespace otafixture\n", newline="\n")
    print(f"wrote {FIXTURE_H.relative_to(ROOT)}")


def main():
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = p.add_subparsers(dest="cmd", required=True)
    k = sub.add_parser("keygen")
    k.add_argument("--key", default=str(DEFAULT_KEY))
    r = sub.add_parser("release")
    r.add_argument("--key", default=str(DEFAULT_KEY))
    r.add_argument("--board", default="lcd128", choices=sorted(BOARDS))
    r.add_argument("--allow-older", action="store_true", help="sign flag 1: the dish takes it even if not newer")
    r.add_argument("--no-build", action="store_true", help="sign the firmware.bin already built")
    c = sub.add_parser("check")
    c.add_argument("manifest")
    sub.add_parser("fixture")
    args = p.parse_args()
    {"keygen": cmd_keygen, "release": cmd_release, "check": cmd_check, "fixture": cmd_fixture}[args.cmd](args)


if __name__ == "__main__":
    main()
