# uv run --with playwright==1.63.0 python web/test/wifi_page.py
# The Wi-Fi card (web/public/index.html, improv.js) in headless Chromium over
# the fake Web Bluetooth (fake_bluetooth.js), with /api/badge/online scripted:
# a 0.7.35 badge that drops Bluetooth to join and checks in, one that never
# checks in (the page reconnects and shows the badge's own masked WIFI lines),
# and an older badge that answers "provisioned" with a URL as before.
import functools
import http.server
import json
import pathlib
import sys
import threading
import time
from urllib.parse import parse_qs, urlparse

from playwright.sync_api import sync_playwright

HERE = pathlib.Path(__file__).resolve().parent
PUBLIC = HERE.parent / "public"
FAKE_BT = HERE / "fake_bluetooth.js"
MAC = "7cdfa1b2c3d4"
SSID, PASSWORD = "Grandma Net", "hunter2-secret"


class Site(http.server.SimpleHTTPRequestHandler):
    # joins_after: the ask on which the badge has checked in (None: never). asks: what came.
    joins_after = None
    asks = []

    def log_message(self, *args):
        pass

    def do_GET(self):
        url = urlparse(self.path)
        if url.path == "/api/badge/online":
            q = parse_qs(url.query)
            Site.asks.append({k: v[0] for k, v in q.items()})
            n = len(Site.asks)
            at = int(time.time() * 1000) if Site.joins_after is not None and n >= Site.joins_after else None
            since = int(q["since"][0])
            body = json.dumps({"joined": at is not None and at >= since, "at": at}).encode()
            self.send_response(200)
            self.send_header("content-type", "application/json")
            self.send_header("cache-control", "no-store")
            self.end_headers()
            self.wfile.write(body)
            return
        if url.path.startswith("/api/"):
            self.send_error(404)
            return
        super().do_GET()


def serve():
    server = http.server.ThreadingHTTPServer(("127.0.0.1", 0), functools.partial(Site, directory=str(PUBLIC)))
    threading.Thread(target=server.serve_forever, daemon=True).start()
    return server


def wifi_page(browser, base, setup, query="fw=0.7.35&wifi_wait_ms=6000"):
    page = browser.new_context(service_workers="block").new_page()
    page.errors = []
    page.on("pageerror", lambda e: page.errors.append(str(e)))
    page.add_init_script(path=str(FAKE_BT))
    page.add_init_script("window.__fakeBt.nearby = [['mossby']];" + setup)
    page.goto(f"{base}/index.html?{query}")
    page.click("#connect")
    page.locator("#wifiForm button").wait_for(state="visible", timeout=15000)
    page.fill("#wifiSsid", SSID)
    page.fill("#wifiPassword", PASSWORD)
    page.click("#wifiForm button")
    return page


def hint(page):
    return page.locator("#wifiHint").text_content()


def hint_has(page, text, timeout=20000):
    page.wait_for_function("t => document.getElementById('wifiHint').textContent.includes(t)", arg=text, timeout=timeout)


def never_says_what_was_typed(page):
    for text in [hint(page), page.locator("#status").text_content()]:
        assert SSID not in text and PASSWORD not in text, text


def drops_bluetooth_and_checks_in(browser, base):
    Site.asks, Site.joins_after = [], 3
    page = wifi_page(browser, base, f"window.__fakeBt.improv = {{ drops: true, awayMs: 2500 }}; window.__fakeBt.mac = '7c:df:a1:b2:c3:d4';")
    hint_has(page, "Sent. Mossby is dropping Bluetooth to join the network; this takes up to a minute.")
    page.wait_for_function("() => /joining Wi-Fi/.test(document.getElementById('status').textContent)", timeout=5000)
    assert not page.locator("#reconnect").is_visible(), "the drop is expected: no Reconnect button"
    hint_has(page, "Mossby is online.")
    assert page.locator("#wifiHint").get_attribute("data-tone") == "ok"
    assert all(a["mac"] == MAC for a in Site.asks), Site.asks
    assert Site.asks[0]["since"] == "0", "the baseline: what the site had already"
    assert int(Site.asks[1]["since"]) > 0
    # Bluetooth comes back by itself once the badge brings it back.
    page.wait_for_function("() => document.getElementById('status').dataset.s === 'connected'", timeout=20000)
    never_says_what_was_typed(page)
    assert not page.errors, page.errors
    page.context.close()


def never_checks_in_and_says_why(browser, base):
    Site.asks, Site.joins_after = [], None
    lines = ["[wifi] window off, phase idle, auto on",
             "[wifi] network #1: ssid 11 B, password 14 B, wpa2 ch 6 rssi -80 in the last scan; last join: could not join in 20 s: AUTH_EXPIRE",
             "[wifi] networks 1 of 3; last outcome: FAILED could not join in 20 s"]
    page = wifi_page(browser, base, f"window.__fakeBt.improv = {{ drops: true, awayMs: 1500 }}; window.__fakeBt.mac = '{MAC}'; window.__fakeBt.wifiLines = {json.dumps(lines)};")
    hint_has(page, "Sent. Mossby is dropping Bluetooth")
    hint_has(page, "Mossby did not check in with the site", timeout=20000)
    hint_has(page, "Mossby says: networks 1 of 3; last outcome: FAILED could not join in 20 s", timeout=30000)
    assert "network #1: ssid 11 B, password 14 B" in hint(page)
    assert "window off" not in hint(page)
    assert page.locator("#wifiHint").get_attribute("data-tone") == "err"
    assert page.evaluate("__fakeBt.debugHeard.some(l => / WIFI$/.test(l))")
    never_says_what_was_typed(page)
    assert not page.errors, page.errors
    page.context.close()


def an_older_badge_answers_as_before(browser, base):
    Site.asks, Site.joins_after = [], None
    page = wifi_page(browser, base, "window.__fakeBt.improv = { drops: false };", query="fw=0.7.20")
    hint_has(page, f'The Blorb joined "{SSID}" and remembers it.')
    assert page.locator("#status").get_attribute("data-s") == "connected"
    assert Site.asks == [], "no MAC: the site is never asked"
    assert not page.errors, page.errors
    page.context.close()


def asks_with_the_new_words(browser, base):
    page = browser.new_context(service_workers="block").new_page()
    page.goto(f"{base}/index.html")
    html = page.content()
    assert "tip it on its side (or press its button)" in html
    assert "turn it around (or press" not in html
    page.context.close()


def main():
    server = serve()
    base = f"http://127.0.0.1:{server.server_address[1]}"
    cases = [drops_bluetooth_and_checks_in, never_checks_in_and_says_why, an_older_badge_answers_as_before, asks_with_the_new_words]
    failed = 0
    with sync_playwright() as p:
        browser = p.chromium.launch()
        for case in cases:
            try:
                case(browser, base)
                print(f"ok   {case.__name__}")
            except Exception as e:
                failed += 1
                print(f"FAIL {case.__name__}: {e}")
        browser.close()
    server.shutdown()
    print(f"{len(cases) - failed}/{len(cases)} passed")
    sys.exit(1 if failed else 0)


if __name__ == "__main__":
    main()
