// node --test web/test/diary_page.test.mjs
// The pet's diary on the game page: his own entries only, newest first, as written,
// from the badge's genome or from the page's own ?pet=<genome hash> link.
import { test } from "node:test";
import assert from "node:assert/strict";
import { readFileSync } from "node:fs";
import worker from "../src/app.js";
import { runDiaries } from "../src/diary.js";
import { fakeEnv } from "./support/fake_env.mjs";

const fixtures = JSON.parse(readFileSync(new URL("./fixtures/genomes.json", import.meta.url)));
const req = async (env, path, body) => {
  const r = await worker.fetch(new Request(`https://blorb.test${path}`, body === undefined ? {} :
    { method: "POST", body: JSON.stringify(body), headers: { "content-type": "application/json" } }), env, { waitUntil() {} });
  return { status: r.status, body: await r.json() };
};

test("a pet's page data carries only his own diary, newest first, exactly as stored", async () => {
  const env = fakeEnv();
  const a = (await req(env, "/api/pet-lines", { mac: "7cdfa1b2c3d4", genome: fixtures[0].genome, name: "Gub" })).body;
  const b = (await req(env, "/api/pet-lines", { mac: "3c0f026f2aec", genome: fixtures[1].genome, name: "Mop" })).body;
  await req(env, "/api/stats", { mac: "7cdfa1b2c3d4", genome_hash: a.genome_hash, stats: { feeds: 2 } });
  await req(env, "/api/stats", { mac: "3c0f026f2aec", genome_hash: b.genome_hash, stats: { feeds: 9 } });
  await runDiaries(env, { day: "2099-05-05" });
  await req(env, "/api/stats", { mac: "7cdfa1b2c3d4", genome_hash: a.genome_hash, stats: { feeds: 5 } });
  await runDiaries(env, { day: "2099-05-06" });
  // Whatever the model wrote is what the page gets, odd characters and all.
  const odd = "I ate a <b>moth</b> & then\nI ate its funeral.";
  env.DB.raw.prepare("UPDATE diary_entries SET entry = ? WHERE day = '2099-05-06'").run(odd);

  const p = (await req(env, `/api/pet?genome_hash=${a.genome_hash}`)).body;
  assert.deepEqual(p.diary.map((d) => d.day), ["2099-05-06", "2099-05-05"]);
  assert.equal(p.diary[0].entry, odd);
  assert.match(p.diary[1].entry, /fed 2 times/);
  const q = (await req(env, `/api/pet?genome_hash=${b.genome_hash}`)).body;
  assert.deepEqual(q.diary.map((d) => d.day), ["2099-05-05"]);
  assert.match(q.diary[0].entry, /fed 9 times/);
  assert.ok(!JSON.stringify(q).includes("moth"), "never another pet's entries");
});

test("life stats riding in the badge's pet-lines POST are stored as /api/stats would, and feed the night's diary", async () => {
  const env = fakeEnv();
  const db = env.DB.raw;
  const MAC = "3c0f026f2aec";
  // No stats: as before, nothing stored, and the reply is the same pack.
  const plain = await worker.fetch(new Request("https://blorb.test/api/pet-lines", { method: "POST",
    body: JSON.stringify({ mac: MAC, genome: fixtures[2].genome, format: "pack" }) }), env, { waitUntil() {} });
  assert.equal(plain.status, 200);
  const packText = await plain.text();
  assert.equal(db.prepare("SELECT COUNT(*) n FROM life_stats").get().n, 0);
  assert.equal(await runDiaries(env, { day: "2099-06-01" }), 0);

  // With stats: the same reply, and a life_stats row exactly like /api/stats makes.
  const withStats = await worker.fetch(new Request("https://blorb.test/api/pet-lines", { method: "POST",
    body: JSON.stringify({ mac: MAC, genome: fixtures[2].genome, format: "pack", stats_version: 2,
      stats: { feeds: 4, knocks: 1, fights: { won: 1, lost: 0 } } }) }), env, { waitUntil() {} });
  assert.equal(await withStats.text(), packText);
  const row = db.prepare("SELECT id, mac, creature_id, stats_version, stats_json FROM life_stats").get();
  assert.equal(row.mac, MAC);
  assert.equal(row.stats_version, 2);
  assert.deepEqual(JSON.parse(row.stats_json), { feeds: 4, knocks: 1, fights: { won: 1, lost: 0 } });
  const viaStats = (await req(env, "/api/stats", { mac: MAC, genome: fixtures[2].genome,
    genome_hash: (await req(env, "/api/pet-lines", { mac: MAC, genome: fixtures[2].genome })).body.genome_hash,
    stats_version: 2, stats: { feeds: 4, knocks: 1, fights: { won: 1, lost: 0 } } })).body;
  const twin = db.prepare("SELECT mac, creature_id, stats_version, stats_json, parents_json FROM life_stats WHERE id = ?").get(viaStats.stats_id);
  const ours = db.prepare("SELECT mac, creature_id, stats_version, stats_json, parents_json FROM life_stats WHERE id = ?").get(row.id);
  assert.deepEqual({ ...ours }, { ...twin }, "the same row /api/stats would write");
  db.prepare("DELETE FROM life_stats WHERE id = ?").run(viaStats.stats_id);

  // A stats value that is not an object is ignored; the lines still come back.
  const bad = await req(env, "/api/pet-lines", { mac: MAC, genome: fixtures[2].genome, stats: [1, 2] });
  assert.equal(bad.status, 200);
  assert.equal(db.prepare("SELECT COUNT(*) n FROM life_stats").get().n, 1);

  // The 04:00 cron writes his entry from them, and his page shows it.
  assert.equal(await runDiaries(env, { day: "2099-06-02" }), 1);
  const p = (await req(env, `/api/pet?genome_hash=${bad.body.genome_hash}`)).body;
  assert.equal(p.diary[0].day, "2099-06-02");
  assert.match(p.diary[0].entry, /fed 4 times/);
});

// A tiny DOM: enough of document, elements and fetch for the diary box's script.
function fakePage({ search = "", stored = null, pet }) {
  const els = {};
  const node = (tag) => ({
    tag, hidden: false, textContent: "", value: "", href: "", dateTime: "", readOnly: false, dataset: {}, children: [], listeners: {},
    append(...c) { this.children.push(...c); },
    replaceChildren(...c) { this.children = c; },
    addEventListener(t, f) { this.listeners[t] = f; },
  });
  const ids = ["selfBox", "diaryTitle", "selfHint", "selfBody", "journal", "says", "bible", "bibleHint", "saveBible", "family", "petLink"];
  for (const id of ids) els[id] = node("x");
  els.selfBox.hidden = els.selfBody.hidden = true;
  const fetched = [];
  const store = { "blorb.pet": stored };
  const globals = {
    document: { getElementById: (id) => els[id], createElement: node },
    location: { search },
    localStorage: { getItem: (k) => store[k] ?? null, setItem: (k, v) => { store[k] = v; } },
    fetch: async (url) => { fetched.push(url); return { ok: true, status: 200, json: async () => pet }; },
    addEventListener: () => {},
    crypto: globalThis.crypto,
    URLSearchParams,
  };
  return { els, fetched, globals };
}
function runBox(page) {
  const html = readFileSync(new URL("../public/index.html", import.meta.url), "utf8");
  const box = html.slice(html.indexOf('<section id="selfBox"'), html.indexOf("</section>", html.indexOf('<section id="selfBox"')));
  const script = box.slice(box.indexOf("<script>") + 8, box.indexOf("</script>"));
  const names = Object.keys(page.globals);
  new Function(...names, script)(...names.map((n) => page.globals[n]));
}
const settle = () => new Promise((r) => setTimeout(r, 10));
const PET = {
  species: "grungo", name: "Gub", generation: 1, status: "alive", version: 2, personality_version: 1, bible: "A grumpy frog.",
  lines: { peckish: ["FEED ME, PEASANT", "crumbs?"], moon: ["the moon owes me"] },
  diary: [{ day: "2099-05-06", entry: "I ate a <b>moth</b> & then\nI ate its funeral." }, { day: "2099-05-05", entry: "Dear diary." }],
  family: { parent: null, children: [] },
};
const HASH = "ab".repeat(32);

test("?pet=<genome hash> shows his diary, newest first, as written, without the badge", async () => {
  const page = fakePage({ search: `?pet=${HASH}`, pet: PET });
  runBox(page);
  await settle();
  const { els } = page;
  assert.deepEqual(page.fetched, [`/api/pet?genome_hash=${HASH}`]);
  assert.equal(els.selfBox.hidden, false);
  assert.equal(els.selfBody.hidden, false);
  assert.equal(els.diaryTitle.textContent, "Gub's diary");
  const entries = els.journal.children.map((li) => ({ day: li.children[0].dateTime, text: li.children[1].textContent }));
  assert.deepEqual(entries, PET.diary.map((d) => ({ day: d.day, text: d.entry })), "textContent, so nothing is parsed or changed");
  assert.match(els.journal.children[0].children[0].textContent, /6/);
  assert.deepEqual(els.says.children.map((li) => li.textContent), ["FEED ME, PEASANT", "crumbs?", "the moon owes me"]);
  assert.equal(els.saveBible.hidden, true, "changing him needs the badge's genome");
  assert.equal(els.bible.readOnly, true);
  assert.equal(els.petLink.href, `./?pet=${HASH}`);
});

test("with no link the page shows the last pet this phone met; nothing at all without one", async () => {
  const seen = fakePage({ stored: HASH, pet: PET });
  runBox(seen);
  await settle();
  assert.deepEqual(seen.fetched, [`/api/pet?genome_hash=${HASH}`]);
  assert.equal(seen.els.journal.children.length, 2);

  const fresh = fakePage({ search: "?pet=../admin", pet: PET });
  runBox(fresh);
  await settle();
  assert.deepEqual(fresh.fetched, [], "only a 64-hex genome hash is looked up");
  assert.equal(fresh.els.selfBox.hidden, true);
});

test("a pet with no entries yet gets a plain note, not an empty box", async () => {
  const page = fakePage({ search: `?pet=${HASH}`, pet: { ...PET, diary: [] } });
  runBox(page);
  await settle();
  assert.equal(page.els.journal.children.length, 1);
  assert.match(page.els.journal.children[0].children[1].textContent, /No entries yet/);
});
