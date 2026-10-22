# uv run --with playwright==1.63.0 python scripts/test/ai_lines/shot.py <page.html> <out-prefix>
# Screenshots a local HTML page at desktop and phone width, light and dark, and
# reports any horizontal page overflow. For eyeballing the eval's sample sheet.
import pathlib
import sys

from playwright.sync_api import sync_playwright

page_path = pathlib.Path(sys.argv[1]).resolve()
out = sys.argv[2]
with sync_playwright() as p:
    browser = p.chromium.launch()
    for name, width in (("desktop", 1280), ("phone", 400)):
        for scheme in ("light", "dark"):
            ctx = browser.new_context(viewport={"width": width, "height": 900}, color_scheme=scheme)
            page = ctx.new_page()
            page.goto(page_path.as_uri())
            page.wait_for_timeout(800)
            over = page.evaluate("document.documentElement.scrollWidth - document.documentElement.clientWidth")
            page.screenshot(path=f"{out}-{name}-{scheme}.png")
            if len(sys.argv) > 3 and name == "desktop" and scheme == "light":
                page.locator(sys.argv[3]).first.screenshot(path=f"{out}-element.png")
            print(name, scheme, "horizontal overflow px:", over)
            ctx.close()
    browser.close()
