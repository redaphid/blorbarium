// node --test web/test/wifi_box.test.mjs
// The Wi-Fi box folds away at the bottom once the dish's Wi-Fi works, and stays
// open where it is while it is not set up or the last try failed.
import { test } from "node:test";
import assert from "node:assert/strict";
import { readFileSync } from "node:fs";
import worker from "../src/app.js";
import { fakeEnv } from "./support/fake_env.mjs";

const html = readFileSync(new URL("../public/index.html", import.meta.url), "utf8");
const fixtures = JSON.parse(readFileSync(new URL("./fixtures/genomes.json", import.meta.url)));

// A page as an ordered list of nodes in <main>: enough for before()/after().
function fakePage() {
  const order = [];
  const remove = (x) => { if (order.includes(x)) order.splice(order.indexOf(x), 1); };
  const node = (id) => {
    const n = {
      id, open: true, textContent: "", dataset: {},
      before(x) { remove(x); order.splice(order.indexOf(n), 0, x); },
      after(x) { remove(x); order.splice(order.indexOf(n) + 1, 0, x); },
      addEventListener() {}, contains: () => false,
    };
    return n;
  };
  const els = Object.fromEntries(["updateBox", "wifiBox", "drivesBox", "selfBox", "debugBox"].map((id) => [id, node(id)]));
  order.push(...Object.values(els));
  els.wifiFold = node("wifiFold");
  els.wifiSummary = node("wifiSummary");
  els.wifiForm = node("wifiForm");
  const listeners = {};
  const globals = {
    document: { getElementById: (id) => els[id], createComment: () => node("#home") },
    addEventListener: (t, f) => { listeners[t] = f; },
    window: {},
  };
  return { els, order: () => order.map((n) => n.id), listeners, globals };
}
function runWifiScript(page) {
  const box = html.slice(html.indexOf('<section id="wifiBox"'), html.indexOf("</section>", html.indexOf('<section id="wifiBox"')));
  const script = box.slice(box.indexOf("<script>") + 8, box.indexOf("</script>"));
  const names = Object.keys(page.globals);
  new Function(...names, script)(...names.map((n) => page.globals[n]));
  return page.globals.window.BlorbWifi;
}
const pet = (checkedAgoMs, fromBadge = true) => ({ detail: { fromBadge, pet: { checked_in_at: Date.now() - checkedAgoMs } } });

test("a dish whose badge checked in over Wi-Fi this week: the box folds away at the bottom", () => {
  const page = fakePage();
  runWifiScript(page);
  page.listeners["blorb:pet"](pet(3600e3));
  assert.deepEqual(page.order(), ["updateBox", "#home", "drivesBox", "selfBox", "wifiBox", "debugBox"]);
  assert.equal(page.els.wifiFold.open, false);
  assert.match(page.els.wifiSummary.textContent, /^Wi-Fi: connected/);
});

test("not set up (never or long ago checked in): the box stays open where it is", () => {
  for (const ago of [Infinity, 30 * 86400e3]) {
    const page = fakePage();
    runWifiScript(page);
    page.listeners["blorb:pet"](pet(ago));
    assert.deepEqual(page.order(), ["updateBox", "#home", "wifiBox", "drivesBox", "selfBox", "debugBox"]);
    assert.equal(page.els.wifiFold.open, true);
    assert.equal(page.els.wifiSummary.textContent, "Wi-Fi");
  }
});

test("a pet from a ?pet= link is not the dish in hand: it moves nothing", () => {
  const page = fakePage();
  runWifiScript(page);
  page.listeners["blorb:pet"](pet(60e3, false));
  assert.deepEqual(page.order(), ["updateBox", "#home", "wifiBox", "drivesBox", "selfBox", "debugBox"]);
  assert.equal(page.els.wifiFold.open, true);
});

test("joining from the page folds it; a failed try brings it back open; another dish starts over", () => {
  const page = fakePage();
  const wifi = runWifiScript(page);
  wifi.joined();
  assert.equal(page.order().at(-2), "wifiBox");
  assert.equal(page.els.wifiFold.open, false);
  wifi.failed();
  assert.deepEqual(page.order(), ["updateBox", "#home", "wifiBox", "drivesBox", "selfBox", "debugBox"]);
  assert.equal(page.els.wifiFold.open, true);
  page.listeners["blorb:pet"](pet(60e3));
  assert.equal(page.els.wifiFold.open, true, "the last try failed: open, even though the badge checked in");
  wifi.joined();
  assert.equal(page.els.wifiFold.open, false);
  wifi.reset();
  assert.equal(page.order()[2], "wifiBox");
  assert.equal(page.els.wifiFold.open, true);
});

test("the page's Wi-Fi code reports joins, failures and a new Blorb to the box", () => {
  assert.match(html, /wifiTell\("ok", `The Blorb joined[^\n]*\n\s*BlorbWifi\.joined\(\);/);
  assert.match(html, /\} catch \(err\) \{\n\s*BlorbWifi\.failed\(\);/);
  assert.match(html, /BlorbWifi\.reset\(\);/);
  // 0.7.35: the site's check-in is the join; no check-in is a failure.
  assert.match(html, /wifiTell\("ok", `\$\{pet\} is online\.`\);\n\s*BlorbWifi\.joined\(\);/);
  assert.match(html, /\n\s*BlorbWifi\.failed\(\);\n\s*const lead = /);
  assert.match(html, /tip it on its side \(or press its button\) while it asks/);
  assert.doesNotMatch(html, /Say yes on the Blorb: turn it around/);
  assert.match(html, /dispatchEvent\(new CustomEvent\("blorb:pet", \{ detail: \{ pet: p, fromBadge: !!genome \} \}\)\)/);
});

test("/api/pet says when the pet's badge last checked in (only the badge's pet-lines POST sets it)", async () => {
  const env = fakeEnv();
  const before = Date.now();
  const r = await worker.fetch(new Request("https://blorb.test/api/pet-lines", { method: "POST",
    body: JSON.stringify({ mac: "7cdfa1b2c3d4", genome: fixtures[0].genome }) }), env, { waitUntil() {} });
  const { genome_hash } = await r.json();
  const p = await (await worker.fetch(new Request(`https://blorb.test/api/pet?genome_hash=${genome_hash}`), env, { waitUntil() {} })).json();
  assert.ok(p.checked_in_at >= before && p.checked_in_at <= Date.now());
});
