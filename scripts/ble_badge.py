# /// script
# requires-python = ">=3.9"
# dependencies = ["bleak>=0.22", "cryptography"]
# ///
"""Talk to one badge over Bluetooth from this laptop: protocol lines and debug lines.

    uv run scripts/ble_badge.py mudge HELLO STATE                    # lines on RX (docs/ble.md)
    uv run scripts/ble_badge.py mudge HELLO 'DEBUG STATUS' 'DEBUG HELP'   # DEBUG ...: the debug characteristic
    uv run scripts/ble_badge.py mudge STATE --wait 5 STATE           # --wait N: pause N s before the next line
    uv run scripts/ble_badge.py D4:05:92:77:1B:31 'OTA STATUS' --listen 30   # then print events for 30 s

The badge is named by its advertised name or its Bluetooth address. Each
line prints every reply line, continuations included, and events (`! ...`)
print as they arrive on either characteristic. HELLO always goes first, so
the badge does not drop the link as a squatter (docs/ota.md). For updates,
use tools/ble_ota.py: it is the page's state machine (web/public/ota.js).
"""
import argparse
import asyncio
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent.parent / "tools"))
import ble_ota  # noqa: E402
from ble_ota import BlePort, LineReader, ble_scan, pick_targets  # noqa: E402

DEBUG = "6e400005-b5a3-f393-e0a9-e50e24dcca9e"   # docs/debug-ble.md


class BadgeLink(BlePort):
    """BlePort, keeping continuation lines and speaking the debug characteristic too."""

    def __init__(self, address):
        super().__init__(address)
        self.extra = {}
        self.dbg_reader = LineReader()
        self.dbg_pending = {}
        self.dbg_lines = {}
        self.on_event = lambda text: print("  ! " + text, flush=True)

    async def open(self, scan_s=10.0):
        await super().open(scan_s)
        self.reader = TapReader(self)
        if self.client.services.get_characteristic(DEBUG) is not None:
            await self.client.start_notify(DEBUG, self._debug_notify)

    def _debug_notify(self, _sender, data):
        for line in self.dbg_reader.feed(data):
            p = ble_ota.parse_line(line)
            if not p:
                continue
            if p[0] == "event":
                print("  ! " + p[1], flush=True)
                continue
            _, i, kind, body = p
            if kind == "+":
                self.dbg_lines.setdefault(i, []).append(body)
                continue
            fut = self.dbg_pending.get(i)
            if fut and not fut.done():
                fut.set_result((kind, body))

    async def debug(self, cmd, timeout=8.0):
        self.ids = self.ids % 0xFFFFFF + 1
        i = self.ids
        fut = asyncio.get_running_loop().create_future()
        self.dbg_pending[i] = fut
        try:
            await self.client.write_gatt_char(DEBUG, ("#%x %s\n" % (i, cmd)).encode(), response=True)
            kind, body = await asyncio.wait_for(fut, timeout)
        finally:
            self.dbg_pending.pop(i, None)
        return self.dbg_lines.pop(i, []), kind, body

    async def request(self, cmd, timeout=8.0):
        """A protocol line: (continuations, "OK"|"ERR", body)."""
        try:
            body = await self.send(cmd, timeout)
            kind = "OK"
        except ble_ota.Err as e:
            kind, body = "ERR", "%s %s" % (e.code, e.text)
        return self.extra.pop(self.ids, []), kind, body


class TapReader(LineReader):
    """Keeps each reply's `#id + ...` lines, which BlePort itself skips."""

    def __init__(self, link):
        super().__init__()
        self.link = link

    def feed(self, data):
        lines = super().feed(data)
        for line in lines:
            p = ble_ota.parse_line(line)
            if p and p[0] == "reply" and p[2] == "+":
                self.link.extra.setdefault(p[1], []).append(p[3])
        return lines


async def main(args):
    rows = await ble_scan(args.seconds)
    targets, problems = pick_targets([args.badge], rows)
    if problems:
        print("; ".join(problems))
        return 1
    link = BadgeLink(targets[0].address)
    await link.open()
    print("connected %s (%s)" % (link.address, link.name), flush=True)
    try:
        steps = list(args.lines)
        if not steps or steps[0].upper() != "HELLO":
            steps.insert(0, "HELLO")
        for step in steps:
            if step.startswith("--wait="):
                await asyncio.sleep(float(step.split("=", 1)[1]))
                continue
            if step.upper().startswith("DEBUG "):
                more, kind, body = await link.debug(step[6:], args.timeout)
            else:
                more, kind, body = await link.request(step, args.timeout)
            print("> " + step, flush=True)
            for m in more:
                print("  + " + m)
            print("  %s %s" % (kind, body), flush=True)
        if args.listen:
            await asyncio.sleep(args.listen)
    finally:
        await link.close()
    return 0


def cli(argv=None):
    argv = list(sys.argv[1:] if argv is None else argv)
    # --wait N between lines becomes a step of its own, kept in order
    steps, i = [], 0
    while i < len(argv):
        if argv[i] == "--wait" and i + 1 < len(argv):
            steps.append("--wait=" + argv[i + 1])
            i += 2
            continue
        steps.append(argv[i])
        i += 1
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("badge", help="advertised name or Bluetooth address")
    ap.add_argument("lines", nargs="*", help="request lines; DEBUG <verb> goes on the debug characteristic")
    ap.add_argument("--seconds", type=float, default=6.0, help="how long to scan first")
    ap.add_argument("--timeout", type=float, default=8.0, help="seconds to wait for each reply")
    ap.add_argument("--listen", type=float, default=0.0, help="then print events for this many seconds")
    args = ap.parse_args([s for s in steps if not s.startswith("--wait=")])
    args.lines = [s for s in steps[1:] if s.startswith("--wait=") or s in args.lines]
    return asyncio.run(main(args))


if __name__ == "__main__":
    sys.exit(cli())
