// node --test web/test/debug.test.mjs
// The debug and history store (migrations/0002_debug.sql, src/debug.js): raw
// generation trails, device debug uploads, installs, and keep-everything retention.
import { test } from "node:test";
import assert from "node:assert/strict";
import { readFileSync } from "node:fs";
import worker from "../src/app.js";
import { purgeOld, INLINE_MAX } from "../src/debug.js";
import { fakeEnv } from "./support/fake_env.mjs";

const fixtures = JSON.parse(readFileSync(new URL("./fixtures/genomes.json", import.meta.url)));
const MAC = "7cdfa1b2c3d4";
const bucket = () => {
  const m = new Map();
  return { m, async put(k, v) { m.set(k, v); }, async get(k) { return m.has(k) ? { text: async () => m.get(k) } : null; }, async delete(k) { m.delete(k); } };
};
const call = async (env, path, body, admin = false) => {
  const r = await worker.fetch(new Request(`https://blorb.test${path}`, {
    method: body === undefined ? "GET" : "POST", body: body === undefined ? undefined : JSON.stringify(body),
    headers: { "content-type": "application/json", ...(admin ? { authorization: "Bearer sekrit" } : {}) } }), env, { waitUntil() {} });
  return { status: r.status, body: await r.json() };
};

test("a generation keeps its raw replies, checks and cost; large trails go to R2", async () => {
  const env = { ...fakeEnv(), DEBUG: bucket() };
  const pack = (await call(env, "/api/pet-lines", { mac: MAC, genome: fixtures[0].genome })).body;
  const row = env.DB.raw.prepare("SELECT * FROM gen_debug").get();
  assert.equal(row.ok, 1);
  assert.equal(row.pack_id, env.DB.raw.prepare("SELECT id FROM packs").get().id);
  assert.equal(row.calls, 5);
  // Large trails go to R2, small ones stay inline (the scripted model's replies are short).
  assert.ok(row.size > INLINE_MAX ? row.r2_key && row.body_json === null : !row.r2_key && row.body_json);
  const body = JSON.parse(row.r2_key ? env.DEBUG.m.get(row.r2_key) : row.body_json);
  assert.equal(body.trace.replies.length, 5);
  assert.match(body.trace.replies[0].reply, /bible/);
  assert.ok(body.trace.replies[0].sampling, "sampling recorded per call");
  assert.ok(Array.isArray(body.trace.checks));
  const prov = JSON.parse(env.DB.raw.prepare("SELECT provenance_json FROM packs").get().provenance_json);
  assert.ok(prov.sampling && prov.meter.inTok > 0 && prov.meter.trace === undefined, "provenance: sampling and tokens, no raw trail");
  // Through the dashboard.
  const c = (await call(env, `/api/admin/creatures/${pack.creature_id}`, undefined, true)).body;
  assert.equal(c.genDebug.length, 1);
  const raw = (await call(env, `/api/admin/blob/gen/${c.genDebug[0].id}`, undefined, true)).body;
  assert.equal(raw.trace.replies.length, 5);
});

test("a failed generation keeps its error and whatever replies it got", async () => {
  const env = fakeEnv({ failWith: "HTTP 400: nope" });
  await call(env, "/api/pet-lines", { mac: MAC, genome: fixtures[1].genome });
  const row = env.DB.raw.prepare("SELECT * FROM gen_debug").get();
  assert.equal(row.ok, 0);
  assert.match(row.error, /nope/);
  assert.equal(JSON.parse(row.body_json).trace[0].error, "HTTP 400: nope");
});

test("device debug uploads are kept, small inline and large in R2, and the dashboard filters them", async () => {
  const env = { ...fakeEnv(), DEBUG: bucket() };
  const big = "x".repeat(INLINE_MAX + 10);
  const r = await call(env, "/api/debug", { mac: MAC, fw: "1.5.0", items: [
    { kind: "boot", summary: "boot 1.5.0 reset=brownout", data: { lines: ["BOOT", "reset brownout"] } },
    { kind: "crash", summary: "panic in paint", data: { backtrace: big } },
    { kind: "command", summary: "DEBUG fps", data: { fps: 31 } }] });
  assert.equal(r.status, 200);
  assert.equal(r.body.ids.length, 3);
  const crash = env.DB.raw.prepare("SELECT * FROM device_debug WHERE kind = 'crash'").get();
  assert.ok(crash.r2_key && !crash.body_json);
  const list = (await call(env, `/api/admin/debug?mac=${MAC}&kind=crash`, undefined, true)).body.items;
  assert.deepEqual(list.map((i) => i.kind), ["crash"]);
  const body = (await call(env, `/api/admin/blob/device/${list[0].id}`, undefined, true)).body;
  assert.equal(body.backtrace.length, big.length);
  const b = (await call(env, `/api/admin/badges/${MAC}`, undefined, true)).body;
  assert.deepEqual(b.debugKinds.map((k) => k.kind), ["boot", "command", "crash"]);
  assert.equal((await call(env, "/api/debug", { mac: MAC, kind: "Not A Kind!" })).status, 400);
  assert.equal((await call(env, "/api/admin/debug", undefined, false)).status, 401);
});

test("which pack each badge installed, and when: from the ack and from a line_pack action", async () => {
  const env = fakeEnv();
  const pack = (await call(env, "/api/pet-lines", { mac: MAC, genome: fixtures[2].genome })).body;
  await call(env, "/api/pet-lines/ack", { mac: MAC, genome_hash: pack.genome_hash, version: 1, via: "phone" });
  await call(env, `/api/admin/creatures/${pack.creature_id}/regenerate`, { push: true }, true);
  const action = (await call(env, `/api/badge/actions?mac=${MAC}`)).body.actions[0];
  await call(env, `/api/badge/actions/${action.id}`, { mac: MAC, ok: true });
  const rows = env.DB.raw.prepare("SELECT version, via FROM installs ORDER BY id").all();
  assert.deepEqual(rows.map((r) => [r.version, r.via]), [[1, "phone"], [2, "action"]]);
  const b = (await call(env, `/api/admin/badges/${MAC}`, undefined, true)).body;
  assert.equal(b.installs.length, 2);
  assert.equal(env.DB.raw.prepare("SELECT COUNT(*) AS n FROM packs").get().n, 2, "both packs kept");
});

test("retention keeps everything unless RETENTION_DAYS is set", async () => {
  const env = fakeEnv();
  await call(env, "/api/debug", { mac: MAC, kind: "log", data: { a: 1 } });
  env.DB.raw.prepare("UPDATE device_debug SET received_at = received_at - 400 * 86400000").run();
  assert.equal(await purgeOld(env), 0);
  assert.equal(env.DB.raw.prepare("SELECT COUNT(*) AS n FROM device_debug").get().n, 1);
  assert.equal(await purgeOld({ ...env, RETENTION_DAYS: "365" }), 1);
});
