# uv run --with playwright==1.63.0 python scripts/test/live-diary-page.py <genome hash> [base url]
# Opens the live game page's ?pet=<genome hash> link in headless Chromium and
# prints the diary it shows: the title and each entry's date and text.
import sys

from playwright.sync_api import sync_playwright

pet = sys.argv[1]
base = sys.argv[2] if len(sys.argv) > 2 else "https://blorb.hypnodroid.com"
with sync_playwright() as p:
    b = p.chromium.launch()
    page = b.new_page()
    page.goto(f"{base}/?pet={pet}")
    page.wait_for_selector("#selfBody:not([hidden]) #journal li", timeout=20000)
    print("title:", page.inner_text("#diaryTitle"))
    for li in page.query_selector_all("#journal li"):
        print("-", li.query_selector("time").inner_text(), "|", li.query_selector("p").inner_text()[:100])
    print("lines shown:", len(page.query_selector_all("#says li")))
    b.close()
