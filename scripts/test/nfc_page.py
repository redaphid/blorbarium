# uv run --with playwright==1.63.0 python scripts/test/nfc_page.py
# The page from an NFC link (docs/nfc.md): it names the pet it looks for, and
# Connect asks Chrome's chooser for that one badge. Web Bluetooth is a stub that
# records the filters; /api/ble-name is answered here.
import functools
import http.server
import json
import pathlib
import threading

from playwright.sync_api import sync_playwright

PUBLIC = pathlib.Path(__file__).resolve().parents[2] / "web" / "public"


class Quiet(http.server.SimpleHTTPRequestHandler):
    def log_message(self, *args):
        pass


STUB = """
window.__filters = null;
Object.defineProperty(navigator, "bluetooth", { value: { requestDevice: async (o) => {
  window.__filters = JSON.parse(JSON.stringify(o.filters, (k, v) => v instanceof Uint8Array ? [...v] : v));
  throw Object.assign(new Error("cancelled"), { name: "NotFoundError" });
} } });
"""


def main():
    server = http.server.ThreadingHTTPServer(("127.0.0.1", 0), functools.partial(Quiet, directory=str(PUBLIC)))
    threading.Thread(target=server.serve_forever, daemon=True).start()
    base = f"http://127.0.0.1:{server.server_port}"
    with sync_playwright() as p:
        browser = p.chromium.launch()
        page = browser.new_page()
        page.add_init_script(STUB)
        asked = []
        def ble_name(route):
            asked.append(route.request.url)
            route.fulfill(status=200, content_type="application/json", body=json.dumps({"name": "hushwing", "pet": "Midge_V"}))
        page.route("**/api/ble-name*", ble_name)
        page.goto(f"{base}/index.html?b=D4:05:92:77:1B:30")
        page.wait_for_function("document.getElementById('meta').textContent.includes('Midge V')")
        meta = page.text_content("#meta")
        assert meta.startswith("Looking for Midge V's Blorb"), meta
        assert asked and asked[0].endswith("/api/ble-name?mac=d40592771b30"), asked
        page.click("#connect")
        page.wait_for_function("window.__filters !== null")
        filters = page.evaluate("window.__filters")
        print(json.dumps(filters))
        assert filters[0]["manufacturerData"] == [{"companyIdentifier": 0xFFFF, "dataPrefix": [0xD4, 0x05, 0x92, 0x77, 0x1B, 0x30]}]
        assert [f.get("name") for f in filters[1:]] == ["blorb-1b30", "mudge", "hushwing"], filters
        page.wait_for_function("!document.getElementById('wider').hidden")

        # Without ?b= nothing changes: no lookup, the old meta line, the old filters.
        asked.clear()
        page.goto(f"{base}/index.html?d=mossby")
        assert page.text_content("#meta").startswith("Bring your phone close to the"), page.text_content("#meta")
        page.click("#connect")
        page.wait_for_function("window.__filters !== null")
        assert page.evaluate("window.__filters") == [{"name": "mossby"}]
        assert not asked
        browser.close()
    server.shutdown()
    print("nfc page: ok")


if __name__ == "__main__":
    main()
