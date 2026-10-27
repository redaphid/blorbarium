// node --test web/test/diary.test.mjs
// #38's uploads (life stats and analytics) and #39's daily diary cron.
import { test } from "node:test";
import assert from "node:assert/strict";
import { readFileSync } from "node:fs";
import worker, { DIARY_CRON } from "../src/app.js";
import { flatten, statsDelta, runDiaries } from "../src/diary.js";
import { fakeEnv } from "./support/fake_env.mjs";

const fixtures = JSON.parse(readFileSync(new URL("./fixtures/genomes.json", import.meta.url)));
const MAC = "7cdfa1b2c3d4";
const req = (env, path, body) => worker.fetch(new Request(`https://blorb.test${path}`, body === undefined ? {} :
  { method: "POST", body: JSON.stringify(body), headers: { "content-type": "application/json" } }), env, { waitUntil() {} });

async function petWithPack(env, f = fixtures[3]) {
  const pack = await (await req(env, "/api/pet-lines", { mac: MAC, genome: f.genome, name: "Moss" })).json();
  return pack.genome_hash;
}

test("stats and analytics are stored, and analytics flattened for charts", async () => {
  const env = fakeEnv();
  const hash = await petWithPack(env);
  const r = await req(env, "/api/stats", { mac: MAC, genome_hash: hash, stats_version: 1, fw: "1.4.0",
    stats: { feeds: 12, knocks: 40, fights: { won: 2, lost: 1 } }, parents: { mother: { feeds: 300 } },
    analytics: { duel: { rounds: 14, timeouts: 1 }, fps_hist: [0, 3, 50, 12], wifi: { ok: true } } });
  assert.equal(r.status, 200);
  const db = env.DB.raw;
  assert.equal(db.prepare("SELECT COUNT(*) AS n FROM life_stats").get().n, 1);
  const metrics = db.prepare("SELECT metric, dims_json, value FROM analytics_metrics ORDER BY metric, dims_json").all();
  assert.deepEqual(metrics.map((m) => m.metric), ["duel.rounds", "duel.timeouts", "fps_hist", "fps_hist", "fps_hist", "fps_hist", "wifi.ok"]);
  assert.equal(metrics.find((m) => m.dims_json === '{"bucket":2}').value, 50);
});

test("stats for a creature the server never met are refused; analytics alone are fine", async () => {
  const env = fakeEnv();
  assert.equal((await req(env, "/api/stats", { mac: MAC, genome_hash: "ab".repeat(32), stats: { feeds: 1 } })).status, 404);
  assert.equal((await req(env, "/api/stats", { mac: MAC, analytics: { resets: 2 } })).status, 200);
});

test("the daily cron writes one entry per pet with new stats, from what changed", async () => {
  const env = fakeEnv();
  const hash = await petWithPack(env);
  await req(env, "/api/stats", { mac: MAC, genome_hash: hash, stats: { feeds: 3, knocks: 1 } });
  await worker.scheduled({ cron: DIARY_CRON }, env, { waitUntil() {} });
  let d = await (await req(env, `/api/diary?mac=${MAC}&genome_hash=${hash}`)).json();
  assert.equal(d.entries.length, 1);
  assert.match(d.entries[0].entry, /fed 3 times/);

  // No new stats: no new entry. New stats the next day: the delta.
  assert.equal(await runDiaries(env, { day: "2099-01-02" }), 0);
  await req(env, "/api/stats", { mac: MAC, genome_hash: hash, stats: { feeds: 10, knocks: 1 } });
  assert.equal(await runDiaries(env, { day: "2099-01-02" }), 1);
  d = await (await req(env, `/api/diary?mac=${MAC}&genome_hash=${hash}`)).json();
  assert.equal(d.entries.length, 2);
  assert.match(d.entries[0].entry, /fed 7 times/, "the newest entry speaks of the 7 new feeds");
});

test("an egg reported before hatching gets its personality from its genes and its parent's life (#40)", async () => {
  const env = fakeEnv();
  const parentHash = await petWithPack(env, fixtures[2]);
  await req(env, "/api/stats", { mac: MAC, genome_hash: parentHash, stats: { feeds: 30, fights_won: 4 } });
  await runDiaries(env, { day: "2099-03-01" });
  const r = await req(env, "/api/pet-lines", { mac: MAC, genome: fixtures[3].genome, egg: true, wait: false,
    parent_genome_hash: parentHash, generation: 3, parents: { mother: { feeds: 30 } } });
  assert.equal(r.status, 202);
  const db = env.DB.raw;
  const egg = db.prepare("SELECT * FROM creatures ORDER BY id DESC LIMIT 1").get();
  assert.equal(egg.status, "egg");
  assert.equal(egg.parent_id, db.prepare("SELECT id FROM creatures WHERE genome_hash = ?").get(parentHash).id);
  assert.equal(db.prepare("SELECT kind FROM jobs ORDER BY id DESC").get().kind, "hatch");
  const before = env.AI.calls.length;
  const { job } = await r.json();
  await worker.queue({ messages: [{ body: { job }, ack() {}, retry() {} }] }, env);   // the PACKS consumer
  const bibleCall = env.AI.calls.slice(before).find((c) => /character bible/.test(c.input.messages.at(-1).content));
  const prompt = bibleCall.input.messages.at(-1).content;
  assert.match(prompt, /From the parent's diary, 2099-03-01: Dear diary/);
  assert.match(prompt, /fights_won/);
  assert.match(prompt, /What the egg carries/);
  const p = db.prepare("SELECT source FROM personalities WHERE creature_id = ?").get(egg.id);
  assert.equal(p.source, "hatch");
});

test("every night, every living pet gets new lines from the same personality; a pet whose life moved on is rewritten (#47)", async () => {
  const { runNightly, nightStart } = await import("../src/app.js");
  const env = fakeEnv();
  const quiet = await petWithPack(env, fixtures[6]);
  const fighter = await petWithPack(env, fixtures[7]);
  const db = env.DB.raw;
  // Their packs and personalities were written two days ago; since then the fighter won two fights.
  db.prepare("UPDATE packs SET created_at = created_at - 172800000").run();
  db.prepare("UPDATE personalities SET created_at = created_at - 172800000").run();
  for (const [hash, stats] of [[quiet, { feeds: 3 }], [fighter, { feeds: 3, fights_won: 2 }]]) {
    await req(env, "/api/stats", { mac: MAC, genome_hash: hash, stats });
  }
  const before = env.AI.calls.length;
  const done = await runNightly(env);
  assert.equal(done.length, 2, "both pets, not only the one with a trigger");
  const calls = env.AI.calls.slice(before).map((c) => c.input.messages.at(-1).content);
  assert.equal(calls.filter((c) => /Rewrite his bible/.test(c)).length, 1, "only the fighter's bible is rewritten");
  assert.ok(calls.some((c) => /What has happened to him lately/.test(c) && /feeds/.test(c)), "the quiet pet's stats are fresh material");

  const rows = db.prepare(`SELECT c.genome_hash AS hash, (SELECT COUNT(*) FROM personalities q WHERE q.creature_id = c.id) AS personalities,
      (SELECT COUNT(*) FROM packs p WHERE p.creature_id = c.id) AS packs,
      (SELECT q.version FROM packs p JOIN personalities q ON q.id = p.personality_id WHERE p.creature_id = c.id ORDER BY p.version DESC LIMIT 1) AS pv,
      (SELECT pending_device FROM packs p WHERE p.creature_id = c.id ORDER BY p.version DESC LIMIT 1) AS pending
    FROM creatures c ORDER BY c.id`).all();
  const q = rows.find((r) => r.hash === quiet), f = rows.find((r) => r.hash === fighter);
  assert.deepEqual([q.personalities, q.packs, q.pv, q.pending], [1, 2, 1, 1], "quiet: same personality v1, a new pack v2 waiting for the badge");
  assert.deepEqual([f.personalities, f.packs, f.pv, f.pending], [2, 2, 2, 1], "fighter: personality v2 and its pack");
  assert.equal(db.prepare("SELECT COUNT(*) AS n FROM packs WHERE status = 'superseded'").get().n, 2, "old packs kept, superseded");
  assert.equal(db.prepare("SELECT COUNT(*) AS n FROM actions WHERE kind = 'line_pack'").get().n, 2);
  const prov = JSON.parse(db.prepare("SELECT provenance_json FROM packs ORDER BY id DESC LIMIT 1").get().provenance_json);
  assert.ok(prov.nightly || prov.model, "provenance kept");

  assert.equal((await runNightly(env)).length, 0, "done for tonight");
  const tomorrow = nightStart() + 24 * 3600 * 1000 + 60000;
  // Tomorrow night: tonight's packs are then from before it.
  assert.equal((await runNightly(env, { now: tomorrow })).length, 2, "and again the next night");
});

test("the night's packs past one batch are carried on by the PACKS queue, not a 10-minute cron", async () => {
  const { runQueueBatch, nightStart, NIGHTLY_MORE } = await import("../src/app.js");
  const sent = [];
  const env = fakeEnv({ env: { MAX_GENERATIONS_PER_DAY: "50", PACKS: { async send(body) { sent.push(body); } } } });
  for (const f of fixtures.slice(0, NIGHTLY_MORE + 1)) await petWithPack(env, f);
  env.DB.raw.prepare("UPDATE packs SET created_at = created_at - 172800000").run();
  const night = nightStart() + 3600 * 1000;   // 05:00 UTC tonight, inside the night's window
  const nightly = () => ({ messages: [{ body: sent.shift(), ack() {}, retry() {} }] });
  const fresh = () => env.DB.raw.prepare("SELECT COUNT(*) AS n FROM packs WHERE created_at >= ?").get(nightStart()).n;

  sent.push({ nightly: true });   // what the 04:00 run sends when its own batch came back full
  await runQueueBatch(nightly(), env, night);
  assert.equal(fresh(), NIGHTLY_MORE, "one batch per message");
  assert.deepEqual(sent, [{ nightly: true }], "a full batch sends the next");
  await runQueueBatch(nightly(), env, night);
  assert.equal(fresh(), NIGHTLY_MORE + 1, "every pet has tonight's pack");
  assert.deepEqual(sent, [], "a short batch is the last");

  sent.push({ nightly: true });
  env.DB.raw.prepare("UPDATE packs SET created_at = created_at - 172800000").run();
  await runQueueBatch(nightly(), env, nightStart() + 7 * 3600 * 1000);   // 11:00 UTC: the night is over
  assert.equal(fresh(), 0, "nothing written after NIGHT_END_HOUR");
});

test("the only cron is the daily one; any other cron name does nothing", async () => {
  const env = fakeEnv();
  await req(env, "/api/pet-lines", { mac: MAC, genome: fixtures[4].genome, wait: false });
  await worker.scheduled({ cron: "*/10 * * * *" }, env, { waitUntil() {} });
  assert.equal(env.DB.raw.prepare("SELECT status FROM jobs").get().status, "queued");
  await worker.scheduled({ cron: DIARY_CRON }, env, { waitUntil() {} });
  assert.equal(env.DB.raw.prepare("SELECT status FROM jobs").get().status, "done");
});

test("a nightly pack's lines are new: his recent packs' lines are refused like repeats", async () => {
  const { checkLines } = await import("../src/validate.js");
  const r = checkLines({ lines: { peckish: ["OLD JOKE", "NEW JOKE"] } }, ["peckish"], { species: "grungo", avoid: new Set(["OLD JOKE"]) });
  assert.deepEqual(r.accepted.peckish, ["NEW JOKE"]);
  assert.match(r.rejected[0].why, /recent packs/);
});

test("flatten and delta", () => {
  assert.deepEqual(flatten({ a: 1, b: { c: 2, d: [5, 6] }, e: "x" }).map((m) => m.metric), ["a", "b.c", "b.d", "b.d"]);
  assert.deepEqual(statsDelta({ feeds: 9, knocks: 2 }, { feeds: 4, knocks: 2 }), { feeds: 5 });
  assert.deepEqual(statsDelta({ feeds: 9 }, null), { feeds: 9 });
});
