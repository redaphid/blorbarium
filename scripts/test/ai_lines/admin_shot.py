# uv run --with playwright==1.63.0 python scripts/test/ai_lines/admin_shot.py <base url> <token> <out-prefix>
# Signs in to the dashboard of a local `wrangler dev` and screenshots the
# badges view and the first creature, to eyeball the page. Nothing is deployed.
import sys

from playwright.sync_api import sync_playwright

base, token, out = sys.argv[1], sys.argv[2], sys.argv[3]
with sync_playwright() as p:
    browser = p.chromium.launch()
    page = browser.new_page(viewport={"width": 1280, "height": 1000})
    errors = []
    page.on("pageerror", lambda e: errors.append(str(e)))
    page.goto(f"{base}/admin")
    page.fill("#tok", token)
    page.click("#login button")
    page.wait_for_selector("tr[data-mac]", timeout=15000)
    page.screenshot(path=f"{out}-badges.png", full_page=True)
    page.click("tr[data-mac]")
    page.wait_for_selector("tr[data-c]", timeout=15000)
    page.click("tr[data-c]")
    page.wait_for_selector("#bible", timeout=15000)
    page.screenshot(path=f"{out}-creature.png", full_page=True)
    print("page errors:", errors or "none")
    browser.close()
