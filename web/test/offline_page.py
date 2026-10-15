# uv run --with playwright==1.63.0 python web/test/offline_page.py
# The page after one visit with the network cut: the context offline and the
# server that served it shut down, so sw.js's caches are all there is.
import base64
import functools
import hashlib
import http.server
import json
import pathlib
import random
import shutil
import sys
import tempfile
import threading
import time

from playwright.sync_api import sync_playwright

from ota_page import BT_STATS, FAKE_BT, FINE, PUBLIC, WATCH, update, update_over_ble, wait_text


# No Last-Modified and no-store: a file rewritten within the same second must
# never come back as a 304 of the old one.
class Quiet(http.server.SimpleHTTPRequestHandler):
    def log_message(self, *args):
        pass

    def send_header(self, keyword, value):
        if keyword != "Last-Modified":
            super().send_header(keyword, value)

    def end_headers(self):
        super().send_header("Cache-Control", "no-store")
        super().end_headers()


# A copy of web/public to serve and change, as a deploy would.
def site():
    root = pathlib.Path(tempfile.mkdtemp(prefix="blorb-site-"))
    shutil.copytree(PUBLIC, root, dirs_exist_ok=True, ignore=shutil.ignore_patterns("*.bin"))
    return root


def serve(root):
    server = http.server.ThreadingHTTPServer(("127.0.0.1", 0), functools.partial(Quiet, directory=str(root)))
    threading.Thread(target=server.serve_forever, daemon=True).start()
    return server, f"http://127.0.0.1:{server.server_address[1]}"


# What tools/ota_release.py leaves in fw/lcd128: the manifest and the image it names.
def write_release(root, version, seed, image=True):
    data = random.Random(seed).randbytes(256 * 1024)
    name = f"blorbarium-lcd128-{version}.bin"
    manifest = {"board": "lcd128", "version": version, "flags": 0, "fmt": 1, "size": len(data), "image": name,
                "sha256": base64.urlsafe_b64encode(hashlib.sha256(data).digest()).rstrip(b"=").decode(), "sig": "A" * 86}
    fw = root / "fw" / "lcd128"
    if image:
        (fw / name).write_bytes(data)
    (fw / "manifest.json").write_text(json.dumps(manifest))
    return manifest


def visit(page, url):
    page.errors = []
    page.on("pageerror", lambda e: page.errors.append(str(e)))
    page.goto(url)
    page.evaluate("navigator.serviceWorker.ready.then(() => true)")
    page.wait_for_function("navigator.serviceWorker.controller !== null", timeout=30000)


def cut(context, server):
    context.set_offline(True)
    server.shutdown()
    server.server_close()


def offline_reload(page):
    page.reload()
    page.evaluate(WATCH)


KEPT = """async () => {
  const out = [];
  for (const name of await caches.keys()) {
    if (!name.startsWith("blorb-fw-")) continue;
    const cache = await caches.open(name);
    const m = await cache.match(new URL("fw/lcd128/manifest.json", location.href).href);
    out.push({ files: (await cache.keys()).map((r) => new URL(r.url).pathname.split("/").pop()).sort(),
               version: m ? (await m.json()).version : null });
  }
  return out;
}"""

# What the page would get: the manifest, and whether the image beside it matches it.
SERVED = """async () => {
  const m = await (await fetch("fw/lcd128/manifest.json", { cache: "no-cache" })).json();
  const bytes = new Uint8Array(await (await fetch(new URL(m.image, new URL("fw/lcd128/", location.href)))).arrayBuffer());
  return { version: m.version, matches: bytes.length === m.size && await BlorbOta.sha256(bytes) === m.sha256 };
}"""


def mock_update_offline(browser):
    server, base = serve(site())
    context = browser.new_context()
    page = context.new_page()
    visit(page, f"{base}/index.html?mock=1&faults=none")
    cut(context, server)
    offline_reload(page)
    update(page, press=True)
    wait_text(page, f"Updated to 0.4.0. {FINE}")
    return page, page.evaluate("({ fw: transport.dish.fw, reboots: transport.dish.stats.reboots })")


def release_update_offline(browser):
    root = site()
    write_release(root, "0.7.0", 7)
    server, base = serve(root)
    context = browser.new_context()
    context.add_init_script(path=str(FAKE_BT))
    context.add_init_script("window.__fakeBt.nearby = [['blorb-1b3c', 'mossby']]")
    page = context.new_page()
    visit(page, f"{base}/index.html?fw=0.4.0&faults=none")
    kept = page.evaluate(KEPT)
    assert kept == [{"files": ["blorbarium-lcd128-0.7.0.bin", "manifest.json"], "version": "0.7.0"}], kept
    cut(context, server)
    offline_reload(page)
    page.click("#connect")
    update_over_ble(page, 0, "0.7.0")
    wait_text(page, f"Updated to 0.7.0. {FINE}")
    s = page.evaluate(BT_STATS, 0)
    assert s["fw"] == "0.7.0" and s["name"] == "mossby" and not s["trial"], s
    return page, s


def release_replaced_whole(browser):
    root = site()
    write_release(root, "0.7.0", 7)
    server, base = serve(root)
    context = browser.new_context()
    page = context.new_page()
    visit(page, f"{base}/index.html")
    assert page.evaluate(SERVED) == {"version": "0.7.0", "matches": True}
    # A deploy caught halfway: the new manifest is up, its image is not.
    write_release(root, "0.7.1", 8, image=False)
    served = page.evaluate(SERVED)
    assert served == {"version": "0.7.0", "matches": True}, f"a manifest without its image was served: {served}"
    kept = page.evaluate(KEPT)
    assert kept == [{"files": ["blorbarium-lcd128-0.7.0.bin", "manifest.json"], "version": "0.7.0"}], kept
    # The image arrives: the new pair replaces the old one whole.
    write_release(root, "0.7.1", 8)
    served = page.evaluate(SERVED)
    assert served == {"version": "0.7.1", "matches": True}, served
    kept = page.evaluate(KEPT)
    assert kept == [{"files": ["blorbarium-lcd128-0.7.1.bin", "manifest.json"], "version": "0.7.1"}], kept
    cut(context, server)
    offline_reload(page)
    served = page.evaluate(SERVED)
    assert served == {"version": "0.7.1", "matches": True}, served
    return page, {"kept": kept, "offline": served}


CASES = [
    ("the mock update runs with the network cut", mock_update_offline),
    ("a release kept on the first visit updates a dish with the network cut", release_update_offline),
    ("a new release replaces the kept one whole, never a manifest without its image", release_replaced_whole),
]


def main():
    only = sys.argv[1:]
    failures = 0
    with sync_playwright() as p:
        browser = p.chromium.launch()
        for name, case in CASES:
            if only and not any(o in name for o in only):
                continue
            start = time.time()
            try:
                page, stats = case(browser)
                assert not page.errors, page.errors
                page.context.close()
                print(f"PASS {name} ({time.time() - start:.1f}s) {stats}")
            except Exception as e:
                failures += 1
                print(f"FAIL {name}: {e!r}")
        browser.close()
    print("ok" if not failures else f"{failures} failed")
    sys.exit(1 if failures else 0)


if __name__ == "__main__":
    main()
