// node --test web/test/diary.test.mjs
// #38's uploads (life stats and analytics) and #39's daily diary cron.
import { test } from "node:test";
import assert from "node:assert/strict";
import { readFileSync } from "node:fs";
import worker, { DIARY_CRON } from "../src/worker.js";
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
  await worker.scheduled({ cron: "*/10 * * * *" }, env, { waitUntil() {} });
  const bibleCall = env.AI.calls.slice(before).find((c) => /character bible/.test(c.input.messages.at(-1).content));
  const prompt = bibleCall.input.messages.at(-1).content;
  assert.match(prompt, /From the parent's diary, 2099-03-01: Dear diary/);
  assert.match(prompt, /fights_won/);
  assert.match(prompt, /What the egg carries/);
  const p = db.prepare("SELECT source FROM personalities WHERE creature_id = ?").get(egg.id);
  assert.equal(p.source, "hatch");
});

test("the nightly revision rewrites a pet whose life moved on, and only him (#47)", async () => {
  const { runNightlyRevisions } = await import("../src/worker.js");
  const env = fakeEnv();
  const quiet = await petWithPack(env, fixtures[6]);
  const fighter = await petWithPack(env, fixtures[7]);
  const db = env.DB.raw;
  // Their personalities were written a minute ago; since then the fighter won two fights.
  db.prepare("UPDATE personalities SET created_at = created_at - 60000").run();
  for (const [hash, stats] of [[quiet, { feeds: 3 }], [fighter, { feeds: 3, fights_won: 2 }]]) {
    await req(env, "/api/stats", { mac: MAC, genome_hash: hash, stats });
  }
  const before = env.AI.calls.length;
  assert.equal(await runNightlyRevisions(env), 1);
  const revise = env.AI.calls.slice(before).find((c) => /Rewrite his bible/.test(c.input.messages.at(-1).content));
  assert.match(revise.input.messages.at(-1).content, /fights_won \+2/);
  const versions = db.prepare(`SELECT c.genome_hash, MAX(q.version) AS v, (SELECT source FROM personalities WHERE creature_id = c.id
    ORDER BY version DESC LIMIT 1) AS src FROM creatures c JOIN personalities q ON q.creature_id = c.id GROUP BY c.id`).all();
  assert.deepEqual(versions.map((v) => [v.genome_hash === fighter, v.v, v.src]), [[false, 1, "genes"], [true, 2, "nightly"]]);
  assert.equal(db.prepare("SELECT kind FROM actions").get().kind, "line_pack");
  assert.equal(await runNightlyRevisions(env), 0, "nothing new since: no second revision");
});

test("flatten and delta", () => {
  assert.deepEqual(flatten({ a: 1, b: { c: 2, d: [5, 6] }, e: "x" }).map((m) => m.metric), ["a", "b.c", "b.d", "b.d"]);
  assert.deepEqual(statsDelta({ feeds: 9, knocks: 2 }, { feeds: 4, knocks: 2 }), { feeds: 5 });
  assert.deepEqual(statsDelta({ feeds: 9 }, null), { feeds: 9 });
});
