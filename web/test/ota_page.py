# uv run --with playwright==1.63.0 python web/test/ota_page.py
# The update section of web/public/index.html under ?mock=1, in headless Chromium.
import base64
import functools
import hashlib
import http.server
import json
import pathlib
import random
import sys
import threading
import time

from playwright.sync_api import sync_playwright

PUBLIC = pathlib.Path(__file__).resolve().parent.parent / "public"
FAKE_BT = pathlib.Path(__file__).resolve().parent / "fake_bluetooth.js"
NUS = "6e400001-b5a3-f393-e0a9-e50e24dcca9e"
PRESS = "Press the button on the dish to say yes."
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
                     before=f"localStorage.setItem('blorb-ota-token:mock:blorb-7a3f:0.4.0', '{TOKEN}')")
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
    wait_text(page, "The dish is still checking its last update. Try again in a few minutes.", timeout=15000)
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


def chooser(page):
    page.locator("#mockChooser").wait_for(state="visible", timeout=10000)
    return page.locator("#mockChoices button").all_text_contents()


def choose(page, name):
    page.locator("#mockChoices").get_by_role("button", name=name, exact=True).click()


NEARBY = "blorb-1b3c>mossby,mothwick,uart-thing:uart,fizzbox:plain,blorb-9z9z:plain"


def chooser_lists_dishes(browser, base):
    page = open_page(browser, base, f"mock=1&faults=none&nearby={NEARBY}")
    page.click("#connect")
    listed = chooser(page)
    assert listed == ["blorb-1b3c", "mothwick", "Cancel"], f"chooser listed {listed}"
    choose(page, "Cancel")
    page.locator("#wider").wait_for(state="visible", timeout=5000)
    assert page.text_content("#status") == "No dish chosen"
    page.click("#wider")
    listed = chooser(page)
    assert listed == ["blorb-1b3c", "mothwick", "uart-thing", "Cancel"], f"wider listed {listed}"
    choose(page, "mothwick")
    page.locator("#updateGo").wait_for(state="visible", timeout=15000)
    assert page.text_content("#status") == "Connected to mothwick", page.text_content("#status")
    assert page.text_content("#updateFw") == "mothwick runs version 0.3.0.", page.text_content("#updateFw")
    assert page.locator("#another").is_visible(), "no way on to the next dish"
    for d in ("mothwick", "blorb-1b3c"):
        named = open_page(browser, base, f"mock=1&nearby={NEARBY}&d={d}")
        named.click("#connect")
        listed = chooser(named)
        assert listed == [d, "Cancel"], f"?d={d} listed {listed}"
        named.close()
    return page, {"opens": page.evaluate("mockChooserOpens")}


def time_refused(browser, base):
    page = open_page(browser, base, "mock=1&faults=none&time_refused=1")
    update(page, press=True)
    wait_text(page, f"Updated to 0.4.0. {FINE}")
    s = page.evaluate(STATS)
    assert s["fw"] == "0.4.0" and not page.evaluate("transport.dish.timeKnown"), s
    return page, s


def stakes_box(browser, base):
    old = open_page(browser, base, "mock=1&faults=none&fw=0.7.0")
    old.locator("#sayBox").wait_for(state="visible", timeout=15000)
    assert old.locator("#stakesBox").is_hidden(), "firmware without stakes= shows no Fights box"
    old.close()
    page = open_page(browser, base, "mock=1&faults=none&fw=0.7.11")
    page.locator("#stakesBox").wait_for(state="visible", timeout=15000)
    assert page.input_value("#stakesInput") == "mortal", page.input_value("#stakesInput")
    page.select_option("#stakesInput", "spar")
    page.click("#stakesForm button")
    page.locator("#stakesHint", has_text="This badge now fights Spar only.").wait_for(timeout=10000)
    assert page.evaluate("transport.stakes") == "spar"
    return page, {"stakes": page.evaluate("transport.stakes")}


# The page's real BleTransport, with fake_bluetooth.js as the radio and the release served here.
def ble_page(browser, base, query, nearby, version="0.7.0", manifest_fails=0):
    # page.route serves the release; a service worker's fetches would go around it.
    page = browser.new_context(service_workers="block").new_page()
    page.errors = []
    page.on("pageerror", lambda e: page.errors.append(str(e)))
    page.add_init_script(path=str(FAKE_BT))
    page.add_init_script(f"window.__fakeBt.nearby = {json.dumps(nearby)}")
    image = random.Random(7).randbytes(256 * 1024)
    name = f"blorbarium-lcd128-{version}.bin"
    manifest = {"board": "lcd128", "version": version, "flags": 0, "fmt": 1, "size": len(image), "image": name,
                "sha256": base64.urlsafe_b64encode(hashlib.sha256(image).digest()).rstrip(b"=").decode(), "sig": "A" * 86}
    fails = [manifest_fails]

    def serve_manifest(route):
        if fails[0]:
            fails[0] -= 1
            return route.fulfill(status=503, body="busy")
        route.fulfill(json=manifest)
    page.route("**/fw/lcd128/manifest.json", serve_manifest)
    page.route(f"**/fw/lcd128/{name}", lambda r: r.fulfill(body=image, content_type="application/octet-stream"))
    page.goto(f"{base}/index.html?{query}")
    page.evaluate(WATCH)
    return page


BT_STATS = """i => { const d = __fakeBt.radio[i].dish;
  return { ...d.stats, fw: d.fw, name: d.name, trial: d.trial, connected: d.connected, timeKnown: d.timeKnown,
           opens: __fakeBt.opens, connects: __fakeBt.connects }; }"""


def update_over_ble(page, dish, version):
    go = page.locator("#updateGo")
    go.wait_for(state="visible", timeout=15000)
    assert go.text_content() == f"Update to {version}", go.text_content()
    go.click()
    wait_text(page, PRESS, timeout=15000)
    page.evaluate("i => __fakeBt.radio[i].dish.press()", dish)


def old_name_to_species(browser, base):
    page = ble_page(browser, base, "fw=0.4.0&faults=drop", [["blorb-1b3c", "mossby"]])
    page.click("#connect")
    page.locator("#updateGo").wait_for(state="visible", timeout=15000)
    filters = page.evaluate("__fakeBt.filters[0]")
    species = page.evaluate("BlorbOta.SPECIES")
    assert len(filters) == 1 + len(species) and all(f["services"] == [NUS] for f in filters), filters
    assert page.text_content("#updateFw") == "blorb-1b3c runs version 0.4.0.", page.text_content("#updateFw")
    update_over_ble(page, 0, "0.7.0")
    wait_text(page, f"Updated to 0.7.0. {FINE}")
    s, seen = page.evaluate(BT_STATS, 0), page.evaluate("window.__seen")
    assert s["name"] == "mossby", f"the dish advertises {s['name']}"
    assert s["opens"] == 1, f"the chooser opened {s['opens']} times"
    assert s["connects"] >= 3, f"gatt.connect {s['connects']} times: first, after the drop, after the restart"
    assert s["drops"] == 1 and s["reboots"] == 1 and s["confirms"] >= 1, s
    assert s["fw"] == "0.7.0" and not s["trial"], s
    assert s["timeKnown"], "the restart lost the clock and the page set it again"
    for text in (PRESS, "Moved out of reach, keep the phone close.", "Restarting the dish…", "Checking that the new version works…"):
        assert text in seen, f"never said {text!r}"
    assert page.evaluate("[...$('updateSteps').children].map((li) => li.dataset.s)") == ["done"] * 5
    assert "ghost" not in page.get_attribute("#another", "class"), "the next dish is not the button to press"
    return page, s


def check_again(browser, base):
    page = ble_page(browser, base, "fw=0.4.0&faults=none", [["blorb-1b3c", "mossby"]], manifest_fails=1)
    page.click("#connect")
    wait_text(page, "Couldn't check for updates right now. Check the phone's internet.", timeout=15000)
    go = page.locator("#updateGo")
    assert go.is_visible() and go.text_content() == "Check again", go.text_content()
    go.click()
    update_over_ble(page, 0, "0.7.0")
    wait_text(page, f"Updated to 0.7.0. {FINE}")
    return page, page.evaluate(BT_STATS, 0)


def second_dish(browser, base):
    page = ble_page(browser, base, "fw=0.4.0&faults=none", [["blorb-1b3c", "mossby"], ["blorb-1b44", "puffkin"]])
    page.evaluate("__fakeBt.choose = 'blorb-1b3c'")
    page.click("#connect")
    update_over_ble(page, 0, "0.7.0")
    wait_text(page, f"Updated to 0.7.0. {FINE}")
    page.evaluate("__fakeBt.choose = 'blorb-1b44'")
    page.click("#another")
    page.locator("#updateGo").wait_for(state="visible", timeout=15000)
    offered = page.evaluate("__fakeBt.offered[1]")
    assert offered == ["mossby", "blorb-1b44"], f"the second chooser offered {offered}"
    assert not page.evaluate(BT_STATS, 0)["connected"], "the first dish was not let go"
    assert page.text_content("#status") == "Connected to blorb-1b44", page.text_content("#status")
    assert page.text_content("#updateFw") == "blorb-1b44 runs version 0.4.0.", page.text_content("#updateFw")
    update_over_ble(page, 1, "0.7.0")
    wait_text(page, f"Updated to 0.7.0. {FINE}")
    s, first = page.evaluate(BT_STATS, 1), page.evaluate(BT_STATS, 0)
    assert s["name"] == "puffkin" and s["fw"] == "0.7.0" and not s["trial"] and s["confirms"] >= 1, s
    assert s["opens"] == 2, f"the chooser opened {s['opens']} times for two dishes"
    assert first["reboots"] == 1 and first["name"] == "mossby" and not first["connected"], first
    keys = page.evaluate("Object.keys(localStorage).filter((k) => k.startsWith('blorb-ota-token:')).sort()")
    assert keys == ["blorb-ota-token:fake-blorb-1b3c:0.7.0", "blorb-ota-token:fake-blorb-1b44:0.7.0"], keys
    return page, s


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
    ("with no ?d= the chooser lists dishes by either name and nothing else", chooser_lists_dishes),
    ("a dish that refuses TIME still updates", time_refused),
    ("an old blorb- dish updates over Bluetooth and comes back as mossby, no second chooser", old_name_to_species),
    ("connect another dish after the first, and update it too", second_dish),
    ("a failed update check offers Check again", check_again),
    ("the Fights box sets the badge's ceiling, and old firmware shows none", stakes_box),
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
