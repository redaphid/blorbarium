# uv run --with playwright==1.63.0 python web/test/badges_page.py
# At phone width, against the page's own pretend radio (?mock=1&nearby=):
#   1. Connect one badge, let it go, connect another: the page shows only the
#      second, after a reload too (when the first is the remembered pet).
#   2. A badge the server has never met still shows what its Blorb holds, and
#      the server's parts say they appear after its first Wi-Fi check-in.
#   3. Nothing covers the Wi-Fi fields while they are in use: no other input
#      offers a suggestion list, and the fields are what a tap there hits.
import functools
import http.server
import json
import pathlib
import threading

from playwright.sync_api import sync_playwright

PUBLIC = pathlib.Path(__file__).resolve().parent.parent / "public"
PHONE = {"viewport": {"width": 390, "height": 760}, "is_mobile": True, "has_touch": True, "device_scale_factor": 2}

# The server knows mudge (Midge) and not mothwick.
MIDGE = {"species": "mudge", "name": "Midge", "generation": 4, "stage": "adult", "status": "alive", "checked_in_at": None,
         "bible": "Midge is a muddy little philosopher who hums to worms.", "catchphrases": [], "version": 3,
         "personality_version": 2, "lines": {"peckish": ["MIDGE WANTS MUD PIE"]},
         "diary": [{"day": "2026-10-27", "entry": "Midge dug a hole and was proud of it."}],
         "family": {"parent": {"species": "mudge", "name": "Old Mo", "generation": 3, "status": "dead"}, "children": []}}
MOTH_LINE = "THE LAMP IS MY MOON"


class Quiet(http.server.SimpleHTTPRequestHandler):
    def log_message(self, *args):
        pass


def serve():
    server = http.server.ThreadingHTTPServer(("127.0.0.1", 0), functools.partial(Quiet, directory=str(PUBLIC)))
    threading.Thread(target=server.serve_forever, daemon=True).start()
    return server


def routes(page, known):
    def pet(route):
        h = route.request.url.split("genome_hash=")[1]
        if h in known:
            route.fulfill(status=200, content_type="application/json", body=json.dumps(known[h]))
        else:
            route.fulfill(status=404, content_type="application/json", body='{"error":"this pet has not been online yet"}')
    page.route("**/api/pet?*", pet)
    page.route("**/api/species?name=mothwick", lambda r: r.fulfill(
        status=200, content_type="application/json", body=json.dumps({"species": "mothwick", "lines": {"moon": [MOTH_LINE]}})))


def choose(page, name, another=False):
    page.click("#another" if another else "#connect")
    page.locator("#mockChoices").get_by_role("button", name=name, exact=True).click()
    page.wait_for_function("document.getElementById('status').dataset.s === 'connected'", timeout=30000)


def genome_hash(page):
    page.wait_for_function("/^[0-9a-f]{64}$/.test(localStorage.getItem('blorb.pet') || '')")
    return page.evaluate("localStorage.getItem('blorb.pet')")


def text(page):
    return page.evaluate("document.querySelector('main').innerText")


def only_moth(page, why):
    page.wait_for_function(f"document.getElementById('says').innerText.includes({json.dumps(MOTH_LINE)})", timeout=15000)
    t = text(page)
    for gone in ["Midge", "MIDGE WANTS MUD PIE", "muddy little philosopher", "Old Mo", "dug a hole"]:
        assert gone not in t, f"{why}: {gone!r} still shows"
    assert page.text_content("#name") == "mothwick", page.text_content("#name")
    assert page.text_content("#diaryTitle") == "Mothwick's diary", page.text_content("#diaryTitle")
    assert "appears after Mothwick's first Wi-Fi check-in" in page.text_content("#journal"), page.text_content("#journal")
    assert "appears after Mothwick's first Wi-Fi check-in" in page.text_content("#bibleHint")
    assert page.is_hidden("#bible") and page.is_hidden("#saveBible")
    # What the Blorb holds is all still there.
    for box in ["drivesBox", "genomeBox", "nameBox", "sayBox", "selfBox"]:
        assert page.is_visible(f"#{box}"), f"{why}: {box} hidden"


def test_switch(browser, base):
    ctx = browser.new_context(**PHONE)
    page = ctx.new_page()
    page.on("pageerror", lambda e: print("pageerror:", e))
    known = {}
    routes(page, known)
    page.goto(f"{base}/index.html?mock=1&nearby=mudge,mothwick")
    # Learn mudge's genome hash, then let the server know him.
    choose(page, "mudge")
    known[genome_hash(page)] = MIDGE
    page.click("#another")
    page.locator("#mockChoices").get_by_role("button", name="mudge", exact=True).click()
    page.wait_for_function("document.getElementById('diaryTitle').textContent === \"Midge's diary\"", timeout=45000)
    assert "MIDGE WANTS MUD PIE" in page.text_content("#says")
    page.click("#another")
    page.locator("#mockChoices").get_by_role("button", name="mothwick", exact=True).click()
    page.wait_for_function("document.getElementById('status').dataset.s === 'connected'", timeout=30000)
    only_moth(page, "after Connect another")
    print("PASS connect mudge, then mothwick: only mothwick shows")

    # A reload, with mudge the remembered pet: his diary shows until a badge connects.
    known_hash = next(iter(known))
    page.evaluate(f"localStorage.setItem('blorb.pet', {json.dumps(known_hash)})")
    page.goto(f"{base}/index.html?mock=1&nearby=mudge,mothwick")
    page.wait_for_function("document.getElementById('diaryTitle').textContent === \"Midge's diary\"", timeout=45000)
    choose(page, "mothwick")
    only_moth(page, "after a reload")
    print("PASS after a reload, the remembered mudge gives way to mothwick")
    ctx.close()


def test_wifi_uncovered(browser, base):
    ctx = browser.new_context(**PHONE)
    page = ctx.new_page()
    page.on("pageerror", lambda e: print("pageerror:", e))
    routes(page, {})
    page.goto(f"{base}/index.html?mock=1&debug=1")
    page.wait_for_function("document.getElementById('status').dataset.s === 'connected'", timeout=30000)
    page.wait_for_selector("#dbgThought")
    assert page.evaluate("document.querySelectorAll('input[list]').length") > 0, "the debug panel has its lists"
    # A suggestion menu is open on the debug panel's thought field...
    page.locator("#dbgThought").scroll_into_view_if_needed()
    page.tap("#dbgThought")
    # ...and the owner goes to the Wi-Fi form.
    page.locator("#wifiSsid").scroll_into_view_if_needed()
    page.tap("#wifiSsid")
    state = page.evaluate("""() => {
      const hit = (id) => { const b = document.getElementById(id).getBoundingClientRect();
        return [[0.5, 0.5], [0.1, 0.5], [0.9, 0.5]].every(([x, y]) =>
          document.elementFromPoint(b.left + b.width * x, b.top + b.height * y) === document.getElementById(id)); };
      return { focused: document.activeElement.id, lists: document.querySelectorAll('input[list]').length,
               ssid: hit('wifiSsid'), password: hit('wifiPassword'), open: document.getElementById('wifiFold').open };
    }""")
    assert state == {"focused": "wifiSsid", "lists": 0, "ssid": True, "password": True, "open": True}, state
    page.keyboard.type("x")
    assert page.input_value("#wifiSsid") == "x", "the network name takes typing"
    page.tap("#wifiPassword")
    assert page.evaluate("document.querySelectorAll('input[list]').length") == 0
    # Leaving the form gives the other fields their lists back.
    page.locator("#sayInput").scroll_into_view_if_needed()
    page.tap("#sayInput")
    assert page.evaluate("document.getElementById('dbgThought').getAttribute('list')") == "dbgThoughts"
    print("PASS nothing covers the Wi-Fi fields at phone width")
    ctx.close()


def main():
    server = serve()
    base = f"http://127.0.0.1:{server.server_port}"
    failed = 0
    with sync_playwright() as p:
        browser = p.chromium.launch()
        for t in [test_switch, test_wifi_uncovered]:
            try:
                t(browser, base)
            except Exception as e:  # noqa: BLE001
                failed += 1
                print(f"FAIL {t.__name__}: {e!r}")
                import traceback
                traceback.print_exc()
                for pg in [pg for c in browser.contexts for pg in c.pages]:
                    print(pg.evaluate("""() => ({ status: document.getElementById('status').textContent,
                      title: document.getElementById('diaryTitle').textContent, hint: document.getElementById('selfHint').textContent,
                      says: document.getElementById('says').innerText, name: document.getElementById('name').textContent })"""))
        browser.close()
    server.shutdown()
    if failed:
        raise SystemExit(f"{failed} failed")
    print("all passed")


if __name__ == "__main__":
    main()
