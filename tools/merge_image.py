"""One image for whoever writes the badge, at the offsets the build knows.

From claude-notification-screen, where it was proven on this board. Offsets
are PlatformIO's to know, so this runs inside the build, where it knows them:
FLASH_EXTRA_IMAGES for the bootloader, the partition table and boot_app0, and
ESP32_APP_OFFSET, which the platform's own partition parsing sets (and also
leaves in INTEGRATION_EXTRA_DATA, where a core that exposes it in the
metadata gets it from).

Intel HEX rather than a flat .bin: a flat one pads the gaps with 0xFF, and the
gaps are `pet` and `petfs` (tools/partitions_16mb.csv), where the creature
lives. A cable flash writes the code and never him.

The writing is PlatformIO's esptool where that esptool can write hex. The one
espressif32 pins here (tool-esptoolpy 4.6.2) cannot -- merge_bin's --format
takes "raw" and nothing else -- so the records are written below instead. The
two are the same bytes: every flash parameter is `keep`, which leaves merge_bin
nothing to do but put each image at its address.
"""
import os
import re
import subprocess

Import("env")   # noqa: F821  (SCons puts it there)


def _flash_images(env):
    """[(address, file)] the upload would write, in flash order.

    The extra images are the bootloader and the partition table, as the
    framework declared them; the app goes at the offset the platform worked out
    from that partition table.
    """
    out = []
    for addr, path in env.get("FLASH_EXTRA_IMAGES", []):
        out.append((int(str(addr), 0), env.subst(path)))
    out.append((_app_offset(env), env.subst(os.path.join("$BUILD_DIR", "${PROGNAME}.bin"))))
    out.sort()
    end = 0
    for addr, path in out:
        if addr < end:
            raise Exception("merge_image: %s at %#x overlaps the image before it" % (path, addr))
        end = addr + os.path.getsize(path)
    return out


def _app_offset(env):
    """Where the app partition starts, asked of the build and never guessed.

    The platform sets ESP32_APP_OFFSET while it checks the program size, so it
    is there by the time anything depending on firmware.bin runs; a core that
    keeps it in INTEGRATION_EXTRA_DATA instead is read there.
    """
    off = env.get("ESP32_APP_OFFSET") or (env.get("INTEGRATION_EXTRA_DATA") or {}).get("application_offset")
    if not off:
        raise Exception(
            "merge_image: this build has no application offset -- the platform sets one while it "
            "checks the program size, so this wants a real `pio run`, not -t nobuild")
    return int(str(off), 0)


def _esptool_wrote_hex(env, out, images):
    """True if PlatformIO's own esptool merged them; False if it cannot write hex."""
    tool = [env.subst("$PYTHONEXE"), env.subst("$OBJCOPY")]
    help_text = subprocess.run(tool + ["merge_bin", "--help"], capture_output=True, text=True).stdout
    if not re.search(r"--format[^\n]*\bhex\b", help_text):
        return False
    cmd = tool + ["--chip", env.BoardConfig().get("build.mcu", "esp32"), "merge_bin",
                  "--format", "hex", "-o", out,
                  "--flash_mode", "keep", "--flash_freq", "keep", "--flash_size", "keep"]
    for addr, path in images:
        cmd += [hex(addr), path]
    subprocess.run(cmd, check=True, capture_output=True, text=True)
    return True


def _write_hex(out, images):
    """The same merge, written here: data records per segment, gaps left as gaps."""
    def record(kind, addr, data):
        body = bytes([len(data), (addr >> 8) & 0xFF, addr & 0xFF, kind]) + data
        return ":%s%02X\n" % (body.hex().upper(), (-sum(body)) & 0xFF)

    with open(out, "w") as f:
        upper = None
        for addr, path in images:
            with open(path, "rb") as src:
                blob = src.read()
            for i in range(0, len(blob), 32):
                at = addr + i
                if at >> 16 != upper:
                    upper = at >> 16
                    f.write(record(4, 0, upper.to_bytes(2, "big")))
                f.write(record(0, at & 0xFFFF, blob[i:i + 32]))
        f.write(record(1, 0, b""))


def merge(target, source, env):
    images = _flash_images(env)
    out = str(target[0])
    if not _esptool_wrote_hex(env, out, images):
        _write_hex(out, images)
    print("merged.hex: " + "  ".join("%#x %s" % (a, os.path.basename(p)) for a, p in images))


# A target rather than a post-action on firmware.bin, so a build that finds the
# firmware up to date but the hex gone still produces one, and `pio run` alone
# leaves the file to flash.
merged = env.Command(                                          # noqa: F821
    os.path.join("$BUILD_DIR", "merged.hex"),
    os.path.join("$BUILD_DIR", "${PROGNAME}.bin"),
    env.VerboseAction(merge, "Merging $TARGET"),                # noqa: F821
)
env.Depends("buildprog", merged)                               # noqa: F821
