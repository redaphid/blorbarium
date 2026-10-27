// node --test web/test/pet_lines.test.mjs
// The Worker's pet-lines API (web/src/worker.js) end to end, against the real
// D1 schema (node's SQLite) and a scripted Workers AI.
import { test } from "node:test";
import assert from "node:assert/strict";
import { readFileSync } from "node:fs";
import worker, { runQueuedJobs, normaliseMac } from "../src/app.js";
import { fakeEnv } from "./support/fake_env.mjs";
import { checkLines, lineProblem, normalise, MAX_TEXT, SITUATIONS } from "../src/validate.js";
import { packText, signPack, verifyPack } from "../src/pack.js";
import { PROMPTS } from "../src/prompts.js";

const fixtures = JSON.parse(readFileSync(new URL("./fixtures/genomes.json", import.meta.url)));
const MAC = "7c:df:a1:b2:c3:d4";
const post = (env, body, path = "/api/pet-lines") =>
  worker.fetch(new Request(`https://blorb.test${path}`, { method: "POST", body: JSON.stringify(body),
    headers: { "content-type": "application/json" } }), env, { waitUntil() {} });
const get = (env, path, headers = {}) => worker.fetch(new Request(`https://blorb.test${path}`, { headers }), env, { waitUntil() {} });

test("a first request writes the pet's bible and lines, stores them, and a second is the cache", async () => {
  const env = fakeEnv();
  const f = fixtures.find((x) => x.id === "mudge-b");
  const r = await post(env, { mac: MAC, genome: f.genome, stage: "child", generation: f.generation, fw: "1.2.3" });
  assert.equal(r.status, 200);
  const pack = await r.json();
  assert.equal(pack.cached, false);
  assert.equal(pack.version, 1);
  assert.match(pack.bible, /grumpy test pet/);
  assert.equal(Object.keys(pack.lines).length, SITUATIONS.length);
  assert.deepEqual(pack.fallback, []);
  assert.ok(pack.text.startsWith(`blorbpack1 ${pack.genome_hash.slice(0, 16)} 1 `));
  const calls = env.AI.calls.length;
  assert.equal(calls, 1 + 4, "one bible call and four line batches");

  const again = await (await post(env, { mac: MAC, genome: f.genome, stage: "adult" })).json();
  assert.equal(again.cached, true);
  assert.deepEqual(again.lines, pack.lines);
  assert.equal(env.AI.calls.length, calls, "the cache costs no model call");

  const c = env.DB.raw.prepare("SELECT * FROM creatures").all();
  assert.equal(c.length, 1);
  assert.equal(c[0].mac, "7cdfa1b2c3d4");
  assert.equal(c[0].species, "mudge");
  assert.equal(c[0].stage, "adult", "the newest stage is kept");
  assert.equal(env.DB.raw.prepare("SELECT fw FROM badges").get().fw, "1.2.3");
});

test("lines that break the screen's rules are re-asked for, and none reach the pack", async () => {
  const env = fakeEnv({ mischief: true });
  const f = fixtures.find((x) => x.id === "fizz-a");
  const pack = await (await post(env, { mac: MAC, genome: f.genome })).json();
  for (const lines of Object.values(pack.lines)) {
    for (const l of lines) assert.ok(l.length <= MAX_TEXT && !/[{}]/.test(l), l);
  }
  const row = env.DB.raw.prepare("SELECT provenance_json FROM packs").get();
  const prov = JSON.parse(row.provenance_json);
  assert.ok(prov.retries > 0, "the failures were re-asked");
  assert.ok(prov.rejected.some((x) => /characters/.test(x.why)));
  assert.ok(env.AI.calls.length > 5);
});

test("a new generation on the same badge is a new creature; history is kept", async () => {
  const env = fakeEnv();
  await post(env, { mac: MAC, genome: fixtures[0].genome, generation: 2, lineage: "L1" });
  await post(env, { mac: MAC, genome: fixtures[1].genome, generation: 7, lineage: "L1" });
  const rows = env.DB.raw.prepare("SELECT id, generation, parent_id FROM creatures ORDER BY id").all();
  assert.equal(rows.length, 2);
  assert.equal(rows[1].parent_id, rows[0].id);
  assert.equal(env.DB.raw.prepare("SELECT COUNT(*) AS n FROM packs").get().n, 2);
});

test("the same genome on another badge reuses its pack without a model call", async () => {
  const env = fakeEnv();
  await post(env, { mac: MAC, genome: fixtures[2].genome });
  const calls = env.AI.calls.length;
  const r = await (await post(env, { mac: "aabbccddeeff", genome: fixtures[2].genome })).json();
  assert.equal(r.cached, true);
  assert.equal(env.AI.calls.length, calls);
  assert.equal(env.DB.raw.prepare("SELECT COUNT(*) AS n FROM creatures").get().n, 2);
});

test("wait:false queues the work, the cron writes it, and GET then returns it", async () => {
  const env = fakeEnv();
  const f = fixtures[4];
  const r = await post(env, { mac: MAC, genome: f.genome, wait: false });
  assert.equal(r.status, 202);
  const hash = (await r.json()) && env.DB.raw.prepare("SELECT genome_hash FROM creatures").get().genome_hash;
  assert.equal((await get(env, `/api/pet-lines?mac=${normaliseMac(MAC)}&genome_hash=${hash}`)).status, 202);
  assert.equal(await runQueuedJobs(env), 1);
  const g = await get(env, `/api/pet-lines?mac=${normaliseMac(MAC)}&genome_hash=${hash}`);
  assert.equal(g.status, 200);
  const pack = await g.json();
  const ack = await (await post(env, { mac: MAC, genome_hash: hash, version: pack.version }, "/api/pet-lines/ack")).json();
  assert.equal(ack.acknowledged, 1);
  assert.equal(env.DB.raw.prepare("SELECT pending_device FROM packs").get().pending_device, 0);
});

test("a model outage leaves the job queued for the cron, and the badge keeps its built-in lines", async () => {
  const env = fakeEnv({ failWith: "HTTP 400: model says no" });
  const r = await post(env, { mac: MAC, genome: fixtures[6].genome });
  assert.equal(r.status, 202);
  assert.equal(env.DB.raw.prepare("SELECT status FROM jobs").get().status, "queued");
});

test("the daily cap per badge holds", async () => {
  const env = fakeEnv({ env: { MAX_GENERATIONS_PER_DAY: "2" } });
  assert.equal((await post(env, { mac: MAC, genome: fixtures[8].genome })).status, 200);
  assert.equal((await post(env, { mac: MAC, genome: fixtures[9].genome })).status, 200);
  assert.equal((await post(env, { mac: MAC, genome: fixtures[10].genome })).status, 429);
});

test("bad input is refused", async () => {
  const env = fakeEnv();
  assert.equal((await post(env, { mac: "nope", genome: fixtures[0].genome })).status, 400);
  assert.equal((await post(env, { mac: MAC, genome: "abc" })).status, 400);
  assert.equal((await post(env, { mac: MAC, genome: fixtures[0].genome.slice(0, 40) })).status, 400);
  assert.equal((await get(env, "/api/nothing")).status, 404);
  assert.equal(await (await get(env, "/index.html")).text(), "asset");
});

test("the export is the owner's only, and holds everything", async () => {
  const env = fakeEnv();
  await post(env, { mac: MAC, genome: fixtures[12].genome, name: "Sparky" });
  assert.equal((await get(env, "/api/creatures")).status, 401);
  assert.equal((await get(env, "/api/creatures", { authorization: "Bearer wrong!" })).status, 401);
  const all = await (await get(env, "/api/creatures", { authorization: "Bearer sekrit" })).json();
  assert.equal(all.creatures.length, 1);
  const c = all.creatures[0];
  assert.equal(c.name, "Sparky");
  assert.equal(c.personalities.length, 1);
  assert.equal(c.packs.length, 1);
  assert.ok(c.raw.species === "fizz" && typeof c.packs[0].lines === "object");
});

test("the mechanical checks: length, font, marks, duplicates, keys", () => {
  const sit = (name) => SITUATIONS.find((s) => s.name === name);
  assert.equal(lineProblem("A".repeat(MAX_TEXT), sit("peckish")), null);
  assert.match(lineProblem("A".repeat(MAX_TEXT + 1), sit("peckish")), /31 characters/);
  assert.match(lineProblem("HI {THERE}", sit("peckish")), /cannot draw/);
  assert.match(lineProblem("GEN # IS HUNGRY", sit("peckish")), /does not give/);
  assert.equal(lineProblem("GEN # SAYS HI", sit("inherited")), null);
  assert.match(lineProblem("# AND #", sit("inherited")), /more than one/);
  assert.equal(normalise("it’s  fine…"), "IT'S FINE...");
  const r = checkLines({ lines: { "peckish [#]": ["snack time", "SNACK TIME", "SNACK? SNACK. THE GOOD BITS!"], bogus: ["X"] } },
                       ["peckish", "starving"], { species: "grungo" });
  assert.deepEqual(r.accepted.peckish, ["SNACK TIME"]);
  assert.ok(r.rejected.some((x) => /another of his lines/.test(x.why)));
  assert.ok(r.rejected.some((x) => /built-in/.test(x.why)));
  assert.ok(r.structural.some((x) => /bogus/.test(x)));
  assert.deepEqual(r.missing, ["peckish", "starving"]);
});

// The owner (2026-10-27): no voice frames, and never DISH (the kids do not know the word).
test("a framed line or one saying DISH is thrown away, and every prompt forbids both", () => {
  const sit = SITUATIONS.find((s) => s.name === "peckish");
  for (const bad of ["THE REEDS SAY: YUM.", "SO IT IS... CRUNCH.", "CRUNCH... SO IT IS.", "HEED: SNACK", "I WOULD EAT THE DISH"])
    assert.ok(lineProblem(bad, sit), bad);
  assert.equal(lineProblem("I WOULD EAT MY HOME", sit), null);
  for (const [v, p] of Object.entries(PROMPTS)) {
    const text = p.lines({ bible: "B", traits: { text: "T" }, species: "grungo" }, [sit]).map((m) => m.content).join("\n");
    assert.match(text, /THE REEDS SAY/, `${v} forbids the frames`);
    assert.match(text, /Never the word DISH/, `${v} forbids DISH`);
  }
});

test("a signed pack verifies with the public key, and a changed line does not", async () => {
  const kp = await crypto.subtle.generateKey({ name: "ECDSA", namedCurve: "P-256" }, true, ["sign", "verify"]);
  const b64 = (buf) => Buffer.from(buf).toString("base64");
  const priv = b64(await crypto.subtle.exportKey("pkcs8", kp.privateKey));
  const pub = b64(await crypto.subtle.exportKey("spki", kp.publicKey));
  const text = packText({ genomeHash: "ab".repeat(32), version: 3, lines: { peckish: ["SNACK NOW"], moon: ["MOON CRUMB"] } });
  const sig = await signPack(text, priv);
  assert.equal(Buffer.from(sig.replace(/-/g, "+").replace(/_/g, "/"), "base64").length, 64, "raw r || s");
  assert.ok(await verifyPack(text, sig, pub));
  assert.ok(!(await verifyPack(text.replace("SNACK NOW", "SNACK LATER"), sig, pub)));

  const env = fakeEnv({ env: { PACK_SIGNING_KEY: priv } });
  const pack = await (await post(env, { mac: MAC, genome: fixtures[14].genome })).json();
  assert.ok(await verifyPack(pack.text, pack.sig, pub));
});
