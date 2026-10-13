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
TOKEN = "Tq3vX9bLk2mWc8RfY1sJpA"
FINE = "Grungo is fine."
CHECKING = f"The new version 0.4.0 is still checking itself. {FINE} Keep this page open, or come back within five minutes."
RENAME_LATER = "You can rename Grungo once the new version has finished checking itself."
NAME_HINT = "Up to 15 letters, numbers, - or _."


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

STATS = "({ ...transport.dish.stats, fw: transport.dish.fw, last: transport.dish.last, trial: transport.dish.trial, sha: mockRelease && mockRelease.manifest.sha256 })"


def wait_text(page, want, timeout=90000):
    try:
        page.wait_for_function("t => document.getElementById('updateText').textContent === t", arg=want, timeout=timeout)
    except Exception:
        seen = page.evaluate("window.__seen || []")[-4:]
        raise AssertionError(f"wanted {want!r}, ended on {page.text_content('#updateText')!r}, saw {seen}")


def open_page(browser, base, query, before=None):
    page = browser.new_page()
    page.errors = []
    page.on("pageerror", lambda e: page.errors.append(str(e)))
    if before:
        page.goto(f"{base}/index.html")
        page.evaluate(before)
    page.goto(f"{base}/index.html?{query}")
    page.evaluate(WATCH)
    return page


def update(page, press):
    go = page.locator("#updateGo")
    go.wait_for(state="visible", timeout=15000)
    assert go.text_content() == "Update to 0.4.0", go.text_content()
    go.click()
    if press:
        page.locator("#mockPress").wait_for(state="visible", timeout=15000)
        page.locator("#mockPress").click()


def rename_locked(page):
    assert page.locator("#nameInput").is_disabled(), "rename input still enabled on trial"
    assert page.locator("#nameForm button").is_disabled(), "rename button still enabled on trial"
    assert page.text_content("#nameHint") == RENAME_LATER, page.text_content("#nameHint")


def reconnect(page):
    page.evaluate("transport.dish.drop()")
    page.locator("#reconnect").wait_for(state="visible", timeout=15000)
    page.locator("#reconnect").click()


# One case per function; each returns the dish's stats for the log line.

def drop_gaps_lost_ack(browser, base):
    page = open_page(browser, base, "mock=1")
    update(page, press=True)
    wait_text(page, f"Updated to 0.4.0. {FINE}")
    s, seen = page.evaluate(STATS), page.evaluate("window.__seen")
    assert s["drops"] == 1, f"drops {s['drops']}"
    assert s["lostWrites"] == 3, f"lostWrites {s['lostWrites']}"
    assert s["gaps"] >= 1, f"gaps {s['gaps']}"
    assert s["lostAcks"] == 1, f"lostAcks {s['lostAcks']}"
    assert s["polls"] == 1, f"STATUS polls mid-transfer {s['polls']}"
    assert s["reboots"] == 1, f"reboots {s['reboots']}"
    assert s["confirms"] == 1, f"CONFIRMs {s['confirms']}"
    assert s["flashed"] == s["sha"], "the dish hashed what it wrote to the release's SHA-256"
    assert s["fw"] == "0.4.0"
    for text in ("Press the button on the dish to say yes.", "Moved out of reach, keep the phone close.", "Restarting the dish…",
                 "Checking that the new version works…"):
        assert text in seen, f"never said {text!r}"
    assert any(t.startswith("Sending the update… ") and t.endswith("%") for t in seen), "no progress shown"
    assert page.locator("#nameInput").is_enabled(), "rename unlocked once the update kept"
    return page, s


def nothing_written(query, press, want, last):
    def case(browser, base):
        page = open_page(browser, base, query)
        update(page, press)
        wait_text(page, want)
        s = page.evaluate(STATS)
        assert s["reboots"] == 0 and s["flashed"] is None and s["fw"] == "0.3.0", s
        assert s["last"] == last, f"dish last={s['last']}"
        return page, s
    return case


def rolls_back(browser, base):
    page = open_page(browser, base, "mock=1&faults=rollback")
    update(page, press=True)
    wait_text(page, f"The update didn't take, so the dish went back to 0.3.0. {FINE}")
    s = page.evaluate(STATS)
    assert s["reboots"] == 2 and s["flashed"] == s["sha"], "into the new image, then back"
    reconnect(page)
    wait_text(page, f"Last time, 0.4.0 didn't work on this dish, so it went back to 0.3.0. {FINE}")
    go = page.locator("#updateGo")
    assert go.is_visible() and go.text_content() == "Try again", go.text_content()
    return page, s


def reload_on_own_trial(browser, base):
    page = open_page(browser, base, f"mock=1&faults=none&fw=0.4.0&trial_from=0.3.0&trial_token={TOKEN}&trial_ms=4000",
                     before=f"localStorage.setItem('blorb-ota-token:blorb-7a3f:0.4.0', '{TOKEN}')")
    wait_text(page, CHECKING, timeout=15000)
    rename_locked(page)
    wait_text(page, f"Updated to 0.4.0. {FINE}", timeout=20000)
    s = page.evaluate(STATS)
    assert s["confirms"] >= 1 and s["refusedConfirms"] == 0, s
    assert s["fw"] == "0.4.0" and s["reboots"] == 0 and not s["trial"], s
    assert page.locator("#nameInput").is_enabled() and page.text_content("#nameHint") == NAME_HINT
    return page, s


def anothers_trial(browser, base):
    page = open_page(browser, base, f"mock=1&faults=none&fw=0.4.0&trial_from=0.3.0&trial_token={TOKEN}&trial_ms=3000")
    wait_text(page, f"The dish is still checking its last update. Try again in a few minutes.", timeout=15000)
    rename_locked(page)
    page.locator("#reconnect").wait_for(state="visible", timeout=20000)
    s = page.evaluate(STATS)
    assert s["confirms"] == 0 and s["refusedConfirms"] >= 1, s
    page.locator("#reconnect").click()
    wait_text(page, f"Last time, 0.4.0 didn't work on this dish, so it went back to 0.3.0. {FINE}", timeout=20000)
    assert page.text_content("#updateGo") == "Try again"
    assert page.locator("#nameInput").is_enabled(), "rename unlocked after the trial"
    return page, page.evaluate(STATS)


def unconfirmed_then_kept(browser, base):
    page = open_page(browser, base, "mock=1&faults=none&trial_ms=6000&trial_wait_ms=800")
    update(page, press=True)
    wait_text(page, CHECKING)
    assert not any(t.startswith("Updated") for t in page.evaluate("window.__seen")), "said Updated while on trial"
    rename_locked(page)
    page.evaluate("""() => { $("nameInput").value = "bob"; $("nameForm").requestSubmit(); }""")
    page.wait_for_timeout(500)
    assert page.evaluate("transport.petName") == "grungo", "renamed on trial"
    rename_locked(page)
    wait_text(page, f"Updated to 0.4.0. {FINE}", timeout=20000)
    assert page.locator("#nameInput").is_enabled() and page.text_content("#nameHint") == NAME_HINT
    page.fill("#nameInput", "bob")
    page.click("#nameForm button")
    page.wait_for_function("() => $('nameHint').textContent === 'He answers to bob now.'", timeout=10000)
    return page, page.evaluate(STATS)


CASES = [
    ("update with a drop, lost writes and a lost at=", drop_gaps_lost_ack),
    ("nobody presses the button", nothing_written("mock=1&faults=none&confirm_ms=1500", False,
     f"Nobody pressed the button on the dish, so nothing changed. {FINE}", "no_confirm")),
    ("a corrupted byte", nothing_written("mock=1&faults=corrupt", True,
     f"The update didn't finish, so nothing changed. {FINE}", "bad_hash")),
    ("the new image rolls back, and the next visit says so", rolls_back),
    ("a reload finds its own update on trial and confirms it", reload_on_own_trial),
    ("another phone's update on trial is not confirmed and goes back", anothers_trial),
    ("a trial outlasting the wait is unconfirmed; rename waits", unconfirmed_then_kept),
]


def main():
    server = serve()
    base = f"http://127.0.0.1:{server.server_address[1]}"
    only = sys.argv[1:]
    failures = 0
    with sync_playwright() as p:
        browser = p.chromium.launch()
        for name, case in CASES:
            if only and not any(o in name for o in only):
                continue
            start = time.time()
            try:
                page, stats = case(browser, base)
                assert not page.errors, page.errors
                page.close()
                print(f"PASS {name} ({time.time() - start:.1f}s) {stats}")
            except Exception as e:
                failures += 1
                print(f"FAIL {name}: {e!r}")
        browser.close()
    server.shutdown()
    print("ok" if not failures else f"{failures} failed")
    sys.exit(1 if failures else 0)


if __name__ == "__main__":
    main()
