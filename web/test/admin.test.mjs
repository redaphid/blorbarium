// node --test web/test/admin.test.mjs
// The dashboard's API (#41) and the badge's side of the action queue (#49).
import { test } from "node:test";
import assert from "node:assert/strict";
import { readFileSync } from "node:fs";
import worker from "../src/app.js";
import { fakeEnv } from "./support/fake_env.mjs";

const fixtures = JSON.parse(readFileSync(new URL("./fixtures/genomes.json", import.meta.url)));
const A = "7cdfa1b2c3d4", B = "aabbccddeeff";
const call = async (env, path, { body, admin = true, method } = {}) => {
  const r = await worker.fetch(new Request(`https://blorb.test${path}`, {
    method: method ?? (body === undefined ? "GET" : "POST"), body: body === undefined ? undefined : JSON.stringify(body),
    headers: { "content-type": "application/json", ...(admin ? { authorization: "Bearer sekrit" } : {}) } }), env, { waitUntil() {} });
  return { status: r.status, body: await r.json() };
};

async function twoBadges() {
  const env = fakeEnv();
  await call(env, "/api/pet-lines", { body: { mac: A, genome: fixtures[0].genome, lineage: "L1", generation: 2 }, admin: false });
  await call(env, "/api/pet-lines", { body: { mac: B, genome: fixtures[5].genome }, admin: false });
  return env;
}

test("admin routes need the token", async () => {
  const env = await twoBadges();
  assert.equal((await call(env, "/api/admin/overview", { admin: false })).status, 401);
  const o = await call(env, "/api/admin/overview");
  assert.equal(o.status, 200);
  assert.equal(o.body.badges.length, 2);
  assert.equal(o.body.counts.creatures, 2);
  assert.ok(o.body.badges.every((b) => b.current && b.current.pack_version === 1));
});

test("badge and creature detail hold genes, traits, versions and provenance", async () => {
  const env = await twoBadges();
  const b = await call(env, `/api/admin/badges/${A}`);
  assert.equal(b.body.creatures.length, 1);
  const c = await call(env, `/api/admin/creatures/${b.body.creatures[0].id}`);
  assert.equal(c.body.creature.raw.species, fixtures[0].raw.species);
  assert.equal(c.body.personalities.length, 1);
  assert.ok(c.body.packs[0].provenance.model);
});

test("regenerating from an edited bible makes a new version, skips call 1, and can push it", async () => {
  const env = await twoBadges();
  const id = (await call(env, `/api/admin/badges/${A}`)).body.creatures[0].id;
  const calls = env.AI.calls.length;
  const r = await call(env, `/api/admin/creatures/${id}/regenerate`, { body: { bible: "He is a pirate now. Arr.", push: true } });
  assert.equal(r.status, 200);
  assert.equal(r.body.version, 2);
  assert.equal(env.AI.calls.length - calls, 4, "four line batches, no bible call");
  const c = (await call(env, `/api/admin/creatures/${id}`)).body;
  assert.equal(c.personalities[0].bible, "He is a pirate now. Arr.");
  assert.equal(c.packs[0].status, "ready");
  assert.equal(c.packs[1].status, "superseded");
  const got = await call(env, `/api/badge/actions?mac=${A}`, { admin: false });
  assert.equal(got.body.actions.length, 1);
  assert.equal(got.body.actions[0].kind, "line_pack");
  assert.equal(got.body.actions[0].payload.version, 2);
});

test("actions fan out to all badges, are delivered once, and report results", async () => {
  const env = await twoBadges();
  const q = await call(env, "/api/admin/actions", { body: { macs: "all", kind: "settings", payload: { idle_timeout_s: 90 } } });
  assert.equal(q.body.ids.length, 2);
  const first = await call(env, `/api/badge/actions?mac=${A}&via=phone`, { admin: false });
  assert.equal(first.body.actions.length, 1);
  const id = first.body.actions[0].id;
  assert.equal((await call(env, `/api/badge/actions/${id}`, { body: { mac: B, ok: true }, admin: false })).status, 404, "another badge cannot answer it");
  assert.equal((await call(env, `/api/badge/actions/${id}`, { body: { mac: A, ok: true, result: { applied: 1 } }, admin: false })).status, 200);
  assert.equal((await call(env, `/api/badge/actions?mac=${A}`, { admin: false })).body.actions.length, 0);
  const list = await call(env, `/api/admin/actions?mac=${A}`);
  assert.equal(list.body.actions[0].status, "done");
  assert.equal(list.body.actions[0].delivered_via, "phone");
  assert.equal((await call(env, "/api/admin/actions", { body: { macs: "all", kind: "nope", payload: {} } })).status, 400);
});

test("an OTA rollout pauses (held) and kills (cancelled)", async () => {
  const env = await twoBadges();
  const ro = await call(env, "/api/admin/ota", { body: { release: "lcd128/1.5.0", target: [A] } });
  assert.equal(ro.status, 200);
  await call(env, `/api/admin/ota/${ro.body.rollout_id}`, { body: { state: "paused" } });
  assert.equal((await call(env, `/api/badge/actions?mac=${A}`, { admin: false })).body.actions.length, 0, "paused: held");
  await call(env, `/api/admin/ota/${ro.body.rollout_id}`, { body: { state: "active" } });
  assert.equal((await call(env, `/api/badge/actions?mac=${A}`, { admin: false })).body.actions[0].payload.release, "lcd128/1.5.0");
  await call(env, `/api/admin/ota/${ro.body.rollout_id}`, { body: { state: "killed" } });
  assert.equal((await call(env, `/api/badge/actions?mac=${A}`, { admin: false })).body.actions.length, 0, "killed: cancelled");
  const acts = (await call(env, `/api/admin/actions?mac=${A}`)).body.actions;
  assert.equal(acts[0].status, "cancelled");
});

test("the Worker's entry module exports only its handler (workerd takes named exports for entrypoints)", async () => {
  const entry = await import("../src/worker.js");
  assert.deepEqual(Object.keys(entry), ["default"]);
  assert.equal(typeof entry.default.fetch, "function");
  assert.equal(typeof entry.default.scheduled, "function");
});

test("/fw/ still serves the firmware releases from R2 (main's route, kept through the merge)", async () => {
  const objects = { "lcd128/manifest.json": '{"v":1}' };
  const fw = {
    async get(key) { return objects[key] === undefined ? null : { body: objects[key], httpEtag: '"e1"', writeHttpMetadata() {} }; },
    async head(key) { return objects[key] === undefined ? null : { size: objects[key].length, httpEtag: '"e1"', writeHttpMetadata() {} }; },
  };
  const env = { ...fakeEnv(), FW: fw };
  const r = await worker.fetch(new Request("https://blorb.test/fw/lcd128/manifest.json"), env, { waitUntil() {} });
  assert.equal(r.status, 200);
  assert.equal(r.headers.get("content-type"), "application/json");
  assert.equal(await r.text(), '{"v":1}');
  assert.equal((await worker.fetch(new Request("https://blorb.test/fw/lcd128/../x"), env, {})).status, 404);
  assert.equal((await worker.fetch(new Request("https://blorb.test/fw/nope.bin"), env, {})).status, 404);
  assert.equal((await worker.fetch(new Request("https://blorb.test/fw/a.bin", { method: "POST" }), env, {})).status, 405);
});

test("the dashboard page is served, holds no data, and its script parses", async () => {
  const env = fakeEnv();
  const r = await worker.fetch(new Request("https://blorb.test/admin"), env, { waitUntil() {} });
  assert.equal(r.status, 200);
  assert.match(r.headers.get("content-type"), /text\/html/);
  const html = await r.text();
  assert.match(html, /<title>Blorbarium Admin<\/title>/);
  const script = html.slice(html.indexOf("<script>") + 8, html.lastIndexOf("</script>"));
  assert.doesNotThrow(() => new Function(script));
});

test("analytics: the metric list, then one metric per day and per badge", async () => {
  const env = await twoBadges();
  await call(env, "/api/stats", { body: { mac: A, analytics: { duel: { rounds: 10 } } }, admin: false });
  await call(env, "/api/stats", { body: { mac: B, analytics: { duel: { rounds: 4 } } }, admin: false });
  const list = await call(env, "/api/admin/analytics");
  assert.deepEqual(list.body.metrics.map((m) => m.metric), ["duel.rounds"]);
  const one = await call(env, "/api/admin/analytics?metric=duel.rounds&days=7");
  assert.equal(one.body.perDay[0].sum, 14);
  assert.equal(one.body.perBadge.length, 2);
});
