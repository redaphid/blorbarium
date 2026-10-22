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

test("flatten and delta", () => {
  assert.deepEqual(flatten({ a: 1, b: { c: 2, d: [5, 6] }, e: "x" }).map((m) => m.metric), ["a", "b.c", "b.d", "b.d"]);
  assert.deepEqual(statsDelta({ feeds: 9, knocks: 2 }, { feeds: 4, knocks: 2 }), { feeds: 5 });
  assert.deepEqual(statsDelta({ feeds: 9 }, null), { feeds: 9 });
});
