// node --test web/test/nfc.test.mjs
// An NFC sticker's link, ?b=<mac> (docs/nfc.md): Chrome's chooser lists that
// one badge, by the id it advertises (docs/ble.md "Advertisement") or, on older
// firmware, by the names it may advertise; and the site says which name that is.
import { test } from "node:test";
import assert from "node:assert/strict";
import { readFileSync } from "node:fs";
import { createRequire } from "node:module";
import worker from "../src/app.js";
import { fakeEnv } from "./support/fake_env.mjs";

const require = createRequire(import.meta.url);
const { chooserFilters, parseBadge, BADGES, COMPANY, NUS } = require("../public/ota.js");
const { chooserMatches } = require("../public/ota-mock.js");

const hex = (h) => Uint8Array.from(h.match(/../g), (x) => parseInt(x, 16));
// A dish as Chrome sees it: its name, its services, and (from 0.7.33) its id.
const dish = (name, mac) => ({ name, services: [NUS], manufacturerData: mac ? { [COMPANY]: hex(mac) } : {} });
const MUDGE = "d40592771b30", TINKER = "d40592772240";

test("?b= takes the MAC with or without colons or dashes, in any case, or blorb-xxxx; nothing else", () => {
  for (const text of [MUDGE, "D4:05:92:77:1B:30", "d4-05-92-77-1b-30", ` ${MUDGE} `,"D40592771B30"]) {
    assert.deepEqual(parseBadge(text), { mac: MUDGE, tail: "1b30" }, text);
  }
  assert.deepEqual(parseBadge("blorb-1B30"), { mac: null, tail: "1b30" });
  for (const text of [null, "", "mudge", "d40592771b3", "d40592771b300", "d4:05:92:77:1b:3g", "d4::05:92:77:1b:30", "1b30", "blorb-1b3"]) {
    assert.equal(parseBadge(text), null, String(text));
  }
});

test("a ?b= link lists only its badge: by its id, by blorb-xxxx, or by the name the site says", () => {
  const filters = chooserFilters(null, false, parseBadge(MUDGE), ["mudge"]);
  assert.ok(filters.every((f) => f.services && f.services[0] === NUS), "every filter needs the UART service");
  assert.deepEqual(filters[0].manufacturerData, [{ companyIdentifier: 0xffff, dataPrefix: hex(MUDGE) }]);
  assert.ok(chooserMatches(filters, dish("mudge", MUDGE)));
  assert.ok(chooserMatches(filters, dish("hushwing", MUDGE)), "a new species on the same badge");
  assert.ok(chooserMatches(filters, dish("blorb-1b30", MUDGE)), "on an OTA trial");
  assert.ok(chooserMatches(filters, dish("blorb-1b30")), "older firmware on a trial");
  assert.ok(chooserMatches(filters, dish("mudge")), "older firmware, by the site's name");
  assert.ok(!chooserMatches(filters, dish("tinker", TINKER)), "another badge");
  assert.ok(!chooserMatches(filters, dish("mossby", "d40592771b3c")), "another badge");
  assert.ok(!chooserMatches(filters, { name: "mudge", services: [], manufacturerData: {} }), "no UART service");
  assert.ok(!chooserMatches(filters, { name: "x", services: [NUS], manufacturerData: { 0x004c: hex(MUDGE) } }), "another company's data");
});

test("a blorb-xxxx link finds its badge by the last two bytes of the id, or by that name", () => {
  const filters = chooserFilters(null, false, parseBadge("blorb-1b30"));
  assert.ok(chooserMatches(filters, dish("mudge", MUDGE)));
  assert.ok(chooserMatches(filters, dish("blorb-1b30")));
  assert.ok(!chooserMatches(filters, dish("tinker", TINKER)));
  assert.ok(!chooserMatches(filters, dish("mudge")), "an old badge without its id is not guessed");
});

test("the names the site gives are only ever exact names, and ?d= still adds its own", () => {
  const filters = chooserFilters("mossby", false, parseBadge(MUDGE), ["mudge", "", null, "Bad Name!", "x".repeat(40)]);
  assert.deepEqual(filters.filter((f) => f.name).map((f) => f.name), ["blorb-1b30", "mudge", "mossby"]);
  assert.ok(filters.every((f) => !f.namePrefix));
});

test("searching wider from a ?b= link lists any UART device, as without it", () => {
  assert.deepEqual(chooserFilters(null, true, parseBadge(MUDGE), ["mudge"]), [{ services: [NUS] }]);
});

test("without ?b= the chooser is exactly as before", () => {
  assert.deepEqual(chooserFilters(null), chooserFilters(null, false, null, []));
  assert.deepEqual(chooserFilters("mossby"), [{ name: "mossby" }]);
});

test("the page's badge table is defs/badges.def", () => {
  const def = readFileSync(new URL("../../lib/blorb/include/blorb/defs/badges.def", import.meta.url), "utf8");
  const rows = [...def.matchAll(/^BLORB_BADGE\(\d+,((?:\s*0x[0-9a-f]{2},){6})\s*(\w+)\)/gm)]
    .map((m) => [m[1].replace(/0x|,|\s/g, ""), m[2]]);
  assert.ok(rows.length >= 9, `read ${rows.length} badges`);
  assert.deepEqual(BADGES, Object.fromEntries(rows));
});

test("the docs' NFC links all parse to a badge the page knows", () => {
  let doc;
  try { doc = readFileSync(new URL("../../docs/nfc.md", import.meta.url), "utf8"); } catch { return; }
  const links = [...doc.matchAll(/https:\/\/blorb\.hypnodroid\.com\/\?b=([0-9a-f]+)/g)].map((m) => m[1]);
  assert.ok(links.length >= 9);
  for (const mac of links) assert.equal(parseBadge(mac)?.mac, mac);
});

const req = async (env, path, body) => {
  const r = await worker.fetch(new Request(`https://blorb.test${path}`, body === undefined ? {} :
    { method: "POST", body: JSON.stringify(body), headers: { "content-type": "application/json" } }), env, { waitUntil() {} });
  return { status: r.status, body: await r.json(), cache: r.headers.get("cache-control") };
};

test("GET /api/ble-name says the name a badge advertises and its pet's name, nothing else", async () => {
  const env = fakeEnv();
  const fixtures = JSON.parse(readFileSync(new URL("./fixtures/genomes.json", import.meta.url)));
  assert.equal((await req(env, `/api/ble-name?mac=${MUDGE}`)).status, 404);
  assert.equal((await req(env, "/api/ble-name?mac=nope")).status, 400);
  const made = await req(env, "/api/pet-lines", { mac: MUDGE, genome: fixtures[1].genome, name: "Midge" });
  assert.equal(made.status, 200);
  const r = await req(env, `/api/ble-name?mac=D4:05:92:77:1B:30`);
  assert.equal(r.status, 200);
  assert.deepEqual(Object.keys(r.body).sort(), ["name", "pet"]);
  assert.equal(r.body.pet, "Midge");
  assert.equal(r.body.name, made.body.species ?? r.body.name);
  assert.match(r.body.name, /^[a-z]+$/);
  assert.match(r.cache, /max-age=60/);
});
