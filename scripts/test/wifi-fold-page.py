# uv run --with playwright==1.63.0 python scripts/test/wifi-fold-page.py [base url]
# The page under ?mock=1 in headless Chromium: the Wi-Fi box starts open in its
# place (the mock connects by itself), folds to the bottom once the dish joins, and comes back open when a try fails.
import sys

from playwright.sync_api import sync_playwright

base = sys.argv[1] if len(sys.argv) > 1 else "https://blorb.hypnodroid.com"


def where(page):
    return page.evaluate("""() => ({
      index: [...document.querySelectorAll('main > section, main > details')].map((e) => e.id).indexOf('wifiBox'),
      count: document.querySelectorAll('main > section, main > details').length,
      open: document.getElementById('wifiFold').open,
      summary: document.getElementById('wifiSummary').textContent })""")


with sync_playwright() as p:
    b = p.chromium.launch()
    page = b.new_page()
    errors = []
    page.on("pageerror", lambda e: errors.append(str(e)))
    page.goto(f"{base}/?mock=1")
    page.locator("#wifiBox").wait_for(state="visible", timeout=15000)
    print("connected:", where(page))
    page.fill("#wifiSsid", "Home")
    page.fill("#wifiPassword", "good-password")
    page.click("#wifiForm button")
    page.wait_for_function("document.getElementById('wifiFold').open === false", timeout=15000)
    print("joined:   ", where(page))
    page.click("#wifiSummary")
    page.fill("#wifiSsid", "Home")
    page.fill("#wifiPassword", "wrong")
    page.click("#wifiForm button")
    page.wait_for_function("document.getElementById('wifiHint').dataset.tone === 'err'", timeout=15000)
    print("failed:   ", where(page))
    print("page errors:", errors)
    b.close()
