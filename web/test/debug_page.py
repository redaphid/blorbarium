# uv run --with playwright==1.63.0 python web/test/debug_page.py
# The debug link's panel (web/public/debug.js) against the mock dish, in
# headless Chromium: absent without ?debug=true, and with it, a locked harm
# command refused, the unlock's press, and the command reaching the dish.
import functools
import http.server
import pathlib
import sys
import threading

from playwright.sync_api import sync_playwright

PUBLIC = pathlib.Path(__file__).resolve().parent.parent / "public"


class Quiet(http.server.SimpleHTTPRequestHandler):
    def log_message(self, *args):
        pass


def serve():
    server = http.server.ThreadingHTTPServer(("127.0.0.1", 0), functools.partial(Quiet, directory=str(PUBLIC)))
    threading.Thread(target=server.serve_forever, daemon=True).start()
    return server


def open_page(browser, base, query):
    page = browser.new_page()
    page.errors = []
    page.on("pageerror", lambda e: page.errors.append(str(e)))
    page.on("dialog", lambda d: d.accept())
    page.goto(f"{base}/index.html?{query}")
    page.wait_for_function("() => typeof transport !== 'undefined' && transport && transport.dish", timeout=15000)
    return page


def heard(page):
    return page.evaluate("(transport.dish.dbg && transport.dish.dbg.heard) || []")


def log_has(page, text, timeout=10000):
    page.wait_for_function("t => [...document.querySelectorAll('#dbgLog li')].some(li => li.textContent.includes(t))",
                           arg=text, timeout=timeout)


def click(page, text):
    page.locator("#debugPanel button", has_text=text).first.click()


def hidden_without_the_param(browser, base):
    for query in ("mock=1", "mock=1&debug=0", "mock=1&debug=false"):
        page = open_page(browser, base, query)
        page.wait_for_timeout(500)
        assert page.locator("#debugPanel").count() == 0, f"{query}: the panel is drawn"
        assert page.evaluate("BlorbDebug.enabled") is False, query
        assert heard(page) == [], f"{query}: debug lines were sent: {heard(page)}"
        assert not page.errors, page.errors
        page.close()


def sends_commands_with_it(browser, base):
    for query in ("mock=1&debug=true", "mock=1&debug=1"):
        page = open_page(browser, base, query)
        page.locator("#debugPanel").wait_for(state="visible", timeout=10000)
        page.wait_for_function("() => document.getElementById('dbgStatus').textContent.includes('unlock=locked')", timeout=10000)
        assert any(l.endswith(" STATUS") for l in heard(page)), heard(page)

        click(page, "Hatch now")
        log_has(page, "ERR 423 LOCKED")
        assert page.evaluate("transport.dish.dbg.phase") == "egg"

        click(page, "Unlock")
        page.locator("#dbgMockPress").wait_for(state="visible", timeout=10000)
        page.locator("#dbgMockPress").click()
        page.wait_for_function("() => document.getElementById('dbgStatus').textContent.includes('unlock=open')", timeout=10000)

        click(page, "Hatch now")
        log_has(page, "OK phase=creature stage=baby")
        click(page, "60x")
        log_has(page, "OK scale=60")
        page.select_option("#dbgStage", "adult")
        click(page, "Grow")
        log_has(page, "OK phase=creature stage=adult")
        click(page, "Fortune")
        log_has(page, "OK fortune=")
        sent = [l.split(" ", 1)[1] for l in heard(page)]
        for want in ("UNLOCK", "HATCH", "SCALE 60", "STAGE adult", "FORTUNE"):
            assert want in sent, f"{query}: {want!r} never reached the dish: {sent}"
        assert not page.errors, page.errors
        page.close()


FAKE_BT = pathlib.Path(__file__).resolve().parent / "fake_bluetooth.js"


def ble_debug_page(browser, base, setup):
    page = browser.new_context(service_workers="block").new_page()
    page.errors = []
    page.on("pageerror", lambda e: page.errors.append(str(e)))
    page.on("dialog", lambda d: d.accept())
    page.add_init_script(path=str(FAKE_BT))
    page.add_init_script("window.__fakeBt.nearby = [['mossby']];" + setup)
    page.goto(f"{base}/index.html?debug=true")
    page.click("#connect")
    return page


def say_has(page, text, timeout=15000):
    page.wait_for_function("t => document.getElementById('dbgSay').textContent.includes(t)", arg=text, timeout=timeout)


# Android Chrome refuses a GATT operation that meets another in flight: the
# panel must ride those refusals out instead of calling the firmware debug-less.
def opens_over_a_busy_android_link(browser, base):
    page = ble_debug_page(browser, base, "window.__fakeBt.busy = 4;")
    page.wait_for_function("() => document.getElementById('dbgStatus').textContent.includes('unlock=locked')", timeout=20000)
    assert page.evaluate("__fakeBt.busy") == 0, "the busy refusals were never met"
    click(page, "Fortune")
    page.wait_for_function("() => __fakeBt.debugHeard.some(l => l.endsWith(' FORTUNE'))", timeout=10000)
    assert "no debug link" not in page.locator("#dbgSay").text_content()
    assert not page.errors, page.errors
    page.context.close()


def says_why_when_the_link_is_missing(browser, base):
    page = ble_debug_page(browser, base, "window.__fakeBt.noDebug = true;")
    say_has(page, "shows no debug link")
    say_has(page, "0.7.0")
    assert page.locator("#dbgRetry").is_visible(), "no Retry debug link button"
    # The page's own panel still works: the update section came up.
    page.locator("#updateGo, #upToDate, #updateStatus").first.wait_for(state="attached", timeout=10000)
    page.context.close()


def main():
    server = serve()
    base = f"http://127.0.0.1:{server.server_address[1]}"
    cases = [hidden_without_the_param, sends_commands_with_it, opens_over_a_busy_android_link, says_why_when_the_link_is_missing]
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
