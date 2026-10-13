# uv run --with playwright==1.63.0 python web/test/ota_page.py
# The update section of web/public/index.html under ?mock=1, in headless Chromium.
import functools
import http.server
import pathlib
import sys
import threading
import time

from playwright.sync_api import sync_playwright

PUBLIC = pathlib.Path(__file__).resolve().parent.parent / "public"


class Quiet(http.server.SimpleHTTPRequestHandler):
    def log_message(self, *args):
        pass


def serve():
    server = http.server.ThreadingHTTPServer(("127.0.0.1", 0), functools.partial(Quiet, directory=str(PUBLIC)))
    threading.Thread(target=server.serve_forever, daemon=True).start()
    return server


# Every text the update line showed, in order, so passing states can be asserted.
WATCH = """() => {
  window.__seen = [];
  const el = document.getElementById("updateText");
  new MutationObserver(() => window.__seen.push(el.textContent)).observe(el, { childList: true, characterData: true, subtree: true });
}"""


def run_case(browser, base, query, press, want):
    page = browser.new_page()
    errors = []
    page.on("pageerror", lambda e: errors.append(str(e)))
    page.goto(f"{base}/index.html?{query}")
    go = page.locator("#updateGo")
    go.wait_for(state="visible", timeout=15000)
    assert go.text_content() == "Update to 0.4.0", go.text_content()
    page.evaluate(WATCH)
    start = time.time()
    go.click()
    if press:
        page.locator("#mockPress").wait_for(state="visible", timeout=15000)
        page.locator("#mockPress").click()
    try:
        page.wait_for_function("t => document.getElementById('updateText').textContent === t", arg=want, timeout=90000)
    except Exception:
        raise AssertionError(f"ended on {page.text_content('#updateText')!r}, saw {page.evaluate('window.__seen')[-4:]}")
    stats = page.evaluate("({ ...transport.dish.stats, fw: transport.dish.fw, last: transport.dish.last, sha: mockRelease.manifest.sha256 })")
    seen = page.evaluate("window.__seen")
    page.close()
    assert not errors, errors
    return stats, seen, time.time() - start


def main():
    server = serve()
    base = f"http://127.0.0.1:{server.server_address[1]}"
    failures = 0
    with sync_playwright() as p:
        browser = p.chromium.launch()

        def case(name, query, press, want, check):
            nonlocal failures
            try:
                stats, seen, secs = run_case(browser, base, query, press, want)
                check(stats, seen)
                print(f"PASS {name} ({secs:.1f}s) {stats}")
            except Exception as e:
                failures += 1
                print(f"FAIL {name}: {e!r}")

        def faults_survived(s, seen):
            assert s["drops"] == 1, f"drops {s['drops']}"
            assert s["lostWrites"] == 3, f"lostWrites {s['lostWrites']}"
            assert s["gaps"] >= 1, f"gaps {s['gaps']}"
            assert s["lostAcks"] == 1, f"lostAcks {s['lostAcks']}"
            assert s["polls"] == 1, f"STATUS polls mid-transfer {s['polls']}"
            assert s["reboots"] == 1, f"reboots {s['reboots']}"
            assert s["flashed"] == s["sha"], "the dish hashed what it wrote to the release's SHA-256"
            assert s["fw"] == "0.4.0"
            for text in ("Press the button on the dish to say yes.", "Moved out of reach, keep the phone close.", "Restarting the dish…"):
                assert text in seen, f"never said {text!r}"
            assert any(t.startswith("Sending the update… ") and t.endswith("%") for t in seen), "no progress shown"

        def nothing_written(last):
            def check(s, seen):
                assert s["reboots"] == 0 and s["flashed"] is None and s["fw"] == "0.3.0", s
                assert s["last"] == last, f"dish last={s['last']}"
            return check

        def rolled_back(s, seen):
            assert s["reboots"] == 1 and s["flashed"] == s["sha"], s

        case("update with a drop, lost writes and a lost at=", "mock=1", True,
             "Updated to 0.4.0. Grungo is fine.", faults_survived)
        case("nobody presses the button", "mock=1&faults=none&confirm_ms=1500", False,
             "Nobody pressed the button on the dish, so nothing changed. Grungo is fine.", nothing_written("no_confirm"))
        case("a corrupted byte", "mock=1&faults=corrupt", True,
             "The update didn't finish, so nothing changed. Grungo is fine.", nothing_written("bad_hash"))
        case("the new image rolls back", "mock=1&faults=rollback", True,
             "The update didn't take, so the dish went back to 0.3.0. Grungo is fine.", rolled_back)
        browser.close()
    server.shutdown()
    print("ok" if not failures else f"{failures} failed")
    sys.exit(1 if failures else 0)


if __name__ == "__main__":
    main()
